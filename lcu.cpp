#include <SpecialK/dxgi_backend.h>
#include <SpecialK/config.h>
#include <SpecialK/ini.h>
#include <SpecialK/parameter.h>
#include <SpecialK/utility.h>
#include <SpecialK/log.h>

#include <SpecialK/hooks.h>
#include <SpecialK/core.h>

#include <string>
#include <process.h>

#define LCU_VERSION_NUM L"0.1.0"
#define LCU_VERSION_STR L"LCU v " LCU_VERSION_NUM

extern void
__stdcall
SK_SetPluginName(std::wstring name);

// Explanation: shadowmap for lego city undercover = 1024x4096, split into 4 1024x1024 shadowmaps.
// This code simply scales that up.
#define LCU_SHADOWMAP_W 1024
#define LCU_SHADOWMAP_H 4096
#define LCU_SHADOWMAP_SCALE 4

// Concept:
// Hook ID3D11Device::CreateTexture2D: Scale the texture dimensions up, remember the pointers to those textures
// Hook ID3D11Device::CreateDepthStencilView, ID3D11Device::CreateRenderTargetView, remember the pointers to the resulting views if they used our hooked textures
// Hook ID3D11DeviceContext::OMSetRenderTargets, set a static flag if we've set the rendertargets to the hooked views
// If the "rendering to the shadowmap" flag is set
//    Hook ID3D11DeviceContext::RSSetViewports, ID3D11DeviceContext::RSSetScissorRects and multiply all coordinates by LCU_SHADOWMAP_SCALE.
//
// The shadowmap is actually used by one shader only: the final deferred combine pass.
// Unfortunately this pass hardcodes the 1024x4096 values
// ID3D11Device::CreatePixelShader uses a shader with 6764 bytes of bytecode.
// => Hook CreatePixelShader to insert our own. We'll do this later, because it requires disassembling and rewriting the shader.

// TO OVERRIDE
// ID3D11Device::CreatePixelShader
// ID3D11Device::CreateTexture2D
// ID3D11Device::CreateRenderTargetView
// ID3D11Device::CreateDepthStencilView
// ID3D11DeviceContext::OMSetRenderTargets
// ID3D11DeviceContext::RSSetViewports
// ID3D11DeviceContext::RSSetScissorRects (RenderDoc calls it RSSetScissors?)

// Pointers to original functions
// ID3D11Device::CreatePixelShader - TODO

// ID3D11Device::CreateTexture2D - has a SpecialK override
// The pointer to the original function.
// A variable with this name is already declared (extern) in dxgi_backend.h and instantiated in d3d11.cpp, so make it (static) here.
static D3D11Dev_CreateTexture2D_pfn           D3D11Dev_CreateTexture2D_Original = nullptr;

// CreateTexture2D already has an override defined in d3d11.cpp, for texture cacheing.
// We have to set our hook so it goes LCU_CreateTexture2D -> D3D11Dev_CreateTexture2D_Override -> D3D11Dev_CreateTexture2D_Original
// => we have to know where the middle override is.
extern HRESULT
WINAPI
D3D11Dev_CreateTexture2D_Override(
    _In_            ID3D11Device* This,
    _In_  /*const*/ D3D11_TEXTURE2D_DESC* pDesc,
    _In_opt_  const D3D11_SUBRESOURCE_DATA* pInitialData,
    _Out_opt_       ID3D11Texture2D** ppTexture2D);

// ID3D11Device::CreateRenderTargetView
static D3D11Dev_CreateRenderTargetView_pfn D3D11Dev_CreateRenderTargetView_Original = nullptr;

extern HRESULT
STDMETHODCALLTYPE
D3D11Dev_CreateRenderTargetView_Override(
    _In_            ID3D11Device* This,
    _In_            ID3D11Resource* pResource,
    _In_opt_  const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
    _Out_opt_       ID3D11RenderTargetView** ppRTView);

// ID3D11Device::CreateDepthStencilView
static D3D11Dev_CreateDepthStencilView_pfn D3D11Dev_CreateDepthStencilView_Original = nullptr;

extern HRESULT
STDMETHODCALLTYPE
D3D11Dev_CreateDepthStencilView_Override(
    _In_            ID3D11Device* This,
    _In_            ID3D11Resource* pResource,
    _In_opt_  const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
    _Out_opt_       ID3D11DepthStencilView** ppRTView);

// ID3D11DeviceContext::OMSetRenderTargets
static D3D11_OMSetRenderTargets_pfn D3D11_OMSetRenderTargets_Original = nullptr;

extern void
STDMETHODCALLTYPE
D3D11_OMSetRenderTargets_Override(
    ID3D11DeviceContext* This,
    _In_     UINT                           NumViews,
    _In_opt_ ID3D11RenderTargetView* const* ppRenderTargetViews,
    _In_opt_ ID3D11DepthStencilView* pDepthStencilView);

// ID3D11DeviceContext::RSSetViewports
static D3D11_RSSetViewports_pfn D3D11_RSSetViewports_Original = nullptr;

extern void
STDMETHODCALLTYPE
D3D11_RSSetViewports_Override(
    ID3D11DeviceContext* This,
    UINT                 NumViewports,
    const D3D11_VIEWPORT* pViewports);

// ID3D11DeviceContext::RSSetScissorRects (RenderDoc calls it RSSetScissors?)
static D3D11_RSSetScissorRects_pfn D3D11_RSSetScissorRects_Original = nullptr;

extern void
STDMETHODCALLTYPE
D3D11_RSSetScissorRects_Override(
    ID3D11DeviceContext* This,
    UINT                 NumRects,
    const D3D11_RECT* pRects);


// Actual overriding

__declspec (noinline)
HRESULT
WINAPI
SK_LCU_CreateTexture2D(
    _In_            ID3D11Device* This,
    _In_  /*const*/ D3D11_TEXTURE2D_DESC* pDesc,
    _In_opt_  const D3D11_SUBRESOURCE_DATA* pInitialData,
    _Out_opt_       ID3D11Texture2D** ppTexture2D)
{
    if (ppTexture2D == nullptr)
        return D3D11Dev_CreateTexture2D_Original(This, pDesc, pInitialData, nullptr);

    // Define this outside the switch scope so we can use it after the switch completes.
    D3D11_TEXTURE2D_DESC copy = *pDesc;

    constexpr UINT ColorBindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    constexpr UINT DepthBindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;

    switch (pDesc->Format)
    {
        // 1024x4096 color, depth+stencil textures
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R24G8_TYPELESS:
    {
        if (
            (pDesc->Width == LCU_SHADOWMAP_W && pDesc->Height == LCU_SHADOWMAP_H)
            && (pDesc->BindFlags == ColorBindFlags || pDesc->BindFlags == DepthBindFlags)
            && (pInitialData == nullptr)
            && (pDesc->MipLevels == 1)
            && (pDesc->ArraySize == 1)
            && false
            )
        {
            SK_LOG2((L"Cascaded Shadowmap Tex (%lux%lu : %lu)",
                pDesc->Width, pDesc->Height, pDesc->MipLevels),
                L"LCU");

            copy.Width = (UINT)(copy.Width * LCU_SHADOWMAP_SCALE);
            copy.Height = (UINT)(copy.Height * LCU_SHADOWMAP_SCALE);

            pDesc = &copy;
        }
    } break;
    }

    HRESULT hr = D3D11Dev_CreateTexture2D_Original(This,
        pDesc, pInitialData,
        ppTexture2D);

    return hr;
}

__declspec (noinline)
HRESULT
STDMETHODCALLTYPE
SK_LCU_CreateRenderTargetView(
    _In_            ID3D11Device* This,
    _In_            ID3D11Resource* pResource,
    _In_opt_  const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
    _Out_opt_       ID3D11RenderTargetView** ppRTView)
{
    return D3D11Dev_CreateRenderTargetView_Original(
        This, pResource,
        pDesc, ppRTView);
}

__declspec (noinline)
HRESULT
STDMETHODCALLTYPE
SK_LCU_CreateDepthStencilView(
    _In_            ID3D11Device* This,
    _In_            ID3D11Resource* pResource,
    _In_opt_  const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
    _Out_opt_       ID3D11DepthStencilView** ppRTView)
{
    return D3D11Dev_CreateDepthStencilView_Original(
        This, pResource,
        pDesc, ppRTView);
}

__declspec (noinline)
void
STDMETHODCALLTYPE
SK_LCU_OMSetRenderTargets(
    ID3D11DeviceContext* This,
    _In_     UINT                           NumViews,
    _In_opt_ ID3D11RenderTargetView* const* ppRenderTargetViews,
    _In_opt_ ID3D11DepthStencilView* pDepthStencilView)
{
    D3D11_OMSetRenderTargets_Original(This, NumViews,
        ppRenderTargetViews, pDepthStencilView);
}

__declspec (noinline)
void
STDMETHODCALLTYPE
SK_LCU_RSSetViewports(
    ID3D11DeviceContext* This,
    UINT                 NumViewports,
    const D3D11_VIEWPORT* pViewports)
{
    return D3D11_RSSetViewports_Original(This, NumViewports, pViewports);
}

__declspec (noinline)
void
STDMETHODCALLTYPE
SK_LCU_RSSetScissorRects(
    ID3D11DeviceContext* This,
    UINT                 NumRects,
    const D3D11_RECT* pRects)
{
    return D3D11_RSSetScissorRects_Original(This, NumRects, pRects);
}

void
SK_LCU_InitPlugin(void)
{
    SK_SetPluginName(LCU_VERSION_STR);

    SK_LOG0((LCU_VERSION_STR),
        L"LCU");

    // ID3D11Device::CreatePixelShader - TODO
    // ID3D11Device::CreateTexture2D
    // ID3D11Device::CreateRenderTargetView
    // ID3D11Device::CreateDepthStencilView
    // ID3D11DeviceContext::OMSetRenderTargets
    // ID3D11DeviceContext::RSSetViewports
    // ID3D11DeviceContext::RSSetScissorRects (RenderDoc calls it RSSetScissors?)

    SK_CreateFuncHook(L"ID3D11Device::CreateTexture2D",
        D3D11Dev_CreateTexture2D_Override,
        SK_LCU_CreateTexture2D,
        (LPVOID*)&D3D11Dev_CreateTexture2D_Original);
    MH_QueueEnableHook(D3D11Dev_CreateTexture2D_Override);


    SK_CreateFuncHook(L"ID3D11Device::CreateRenderTargetView",
        D3D11Dev_CreateRenderTargetView_Override,
        SK_LCU_CreateRenderTargetView,
        (LPVOID*)&D3D11Dev_CreateRenderTargetView_Original);
    MH_QueueEnableHook(D3D11Dev_CreateRenderTargetView_Override);

    SK_CreateFuncHook(L"ID3D11Device::CreateDepthStencilView",
        D3D11Dev_CreateDepthStencilView_Override,
        SK_LCU_CreateDepthStencilView,
        (LPVOID*)&D3D11Dev_CreateDepthStencilView_Original);
    MH_QueueEnableHook(D3D11Dev_CreateDepthStencilView_Override);


    SK_CreateFuncHook(L"ID3D11DeviceContext::OMSetRenderTargets",
        D3D11_OMSetRenderTargets_Override,
        SK_LCU_OMSetRenderTargets,
        (LPVOID*)&D3D11_OMSetRenderTargets_Original);
    MH_QueueEnableHook(D3D11_OMSetRenderTargets_Override);


    SK_CreateFuncHook(L"ID3D11DeviceContext::RSSetViewports",
        D3D11_RSSetViewports_Override,
        SK_LCU_RSSetViewports,
        (LPVOID*)&D3D11_RSSetViewports_Original);
    MH_QueueEnableHook(D3D11_RSSetViewports_Override);

    SK_CreateFuncHook(L"ID3D11DeviceContext::RSSetScissorRects",
        D3D11_RSSetScissorRects_Override,
        SK_LCU_RSSetScissorRects,
        (LPVOID*)&D3D11_RSSetScissorRects_Original);
    MH_QueueEnableHook(D3D11_RSSetScissorRects_Override);
}



HRESULT
STDMETHODCALLTYPE
SK_LCU_PresentFirstFrame(IDXGISwapChain* This,
    UINT            SyncInterval,
    UINT            Flags)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(SyncInterval);
    UNREFERENCED_PARAMETER(Flags);
 
    return S_OK;
}