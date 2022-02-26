/**
* This file is part of Special K.
*
* Special K is free software : you can redistribute it
* and/or modify it under the terms of the GNU General Public License
* as published by The Free Software Foundation, either version 3 of
* the License, or (at your option) any later version.
*
* Special K is distributed in the hope that it will be useful,
*
* But WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Special K.
*
*   If not, see <http://www.gnu.org/licenses/>.
*
**/

#include <SpecialK/stdafx.h>

#ifdef  __SK_SUBSYSTEM__
#undef  __SK_SUBSYSTEM__
#endif
#define __SK_SUBSYSTEM__ L"  D3D 11  "

#include <SpecialK/render/dxgi/dxgi_util.h>
#include <SpecialK/render/d3d11/d3d11_tex_mgr.h>
#include <SpecialK/render/d3d11/d3d11_state_tracker.h>

//using D3D11On12CreateDevice_pfn =
//  HRESULT (WINAPI *)(              _In_ IUnknown*             pDevice,
//                                        UINT                  Flags,
//  _In_reads_opt_( FeatureLevels ) CONST D3D_FEATURE_LEVEL*    pFeatureLevels,
//                                        UINT                  FeatureLevels,
//            _In_reads_opt_( NumQueues ) IUnknown* CONST*      ppCommandQueues,
//                                        UINT                  NumQueues,
//                                        UINT                  NodeMask,
//                       _COM_Outptr_opt_ ID3D11Device**        ppDevice,
//                       _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext,
//                       _Out_opt_        D3D_FEATURE_LEVEL*    pChosenFeatureLevel );


extern "C" __declspec (dllexport) FARPROC D3D11CreateDeviceForD3D12              = nullptr;
extern "C" __declspec (dllexport) FARPROC CreateDirect3D11DeviceFromDXGIDevice   = nullptr;
extern "C" __declspec (dllexport) FARPROC CreateDirect3D11SurfaceFromDXGISurface = nullptr;
extern "C" __declspec (dllexport) D3D11On12CreateDevice_pfn
                                          D3D11On12CreateDevice                  = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTCloseAdapter                     = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTDestroyAllocation                = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTDestroyContext                   = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTDestroyDevice                    = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTDestroySynchronizationObject     = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTQueryAdapterInfo                 = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTSetDisplayPrivateDriverFormat    = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTSignalSynchronizationObject      = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTUnlock                           = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTWaitForSynchronizationObject     = nullptr;
extern "C" __declspec (dllexport) FARPROC EnableFeatureLevelUpgrade              = nullptr;
extern "C" __declspec (dllexport) FARPROC OpenAdapter10                          = nullptr;
extern "C" __declspec (dllexport) FARPROC OpenAdapter10_2                        = nullptr;
extern "C" __declspec (dllexport) FARPROC D3D11CoreCreateLayeredDevice           = nullptr;
extern "C" __declspec (dllexport) FARPROC D3D11CoreGetLayeredDeviceSize          = nullptr;
extern "C" __declspec (dllexport) FARPROC D3D11CoreRegisterLayers                = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTCreateAllocation                 = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTCreateContext                    = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTCreateDevice                     = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTCreateSynchronizationObject      = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTEscape                           = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTGetContextSchedulingPriority     = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTGetDeviceState                   = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTGetDisplayModeList               = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTGetMultisampleMethodList         = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTGetRuntimeData                   = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTGetSharedPrimaryHandle           = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTLock                             = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTOpenAdapterFromHdc               = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTOpenResource                     = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTPresent                          = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTQueryAllocationResidency         = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTQueryResourceInfo                = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTRender                           = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTSetAllocationPriority            = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTSetContextSchedulingPriority     = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTSetDisplayMode                   = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTSetGammaRamp                     = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTSetVidPnSourceOwner              = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTWaitForVerticalBlankEvent        = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DPerformance_BeginEvent              = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DPerformance_EndEvent                = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DPerformance_GetStatus               = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DPerformance_SetMarker               = nullptr;
extern "C" __declspec (dllexport) FARPROC D3DKMTOpenAdapterFromLuid              = nullptr;


bool
SK_D3D11_OverrideDepthStencil (DXGI_FORMAT& fmt)
{
  if (! config.render.dxgi.enhanced_depth)
    return false;

  switch (fmt)
  {
    case DXGI_FORMAT_R24G8_TYPELESS:
      fmt = DXGI_FORMAT_R32G8X24_TYPELESS;
      return true;

    case DXGI_FORMAT_D24_UNORM_S8_UINT:
      fmt = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
      return true;

    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
      fmt = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
      return true;

    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
      fmt = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
      return true;
  }

  return false;
}



__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateBuffer_Override (
  _In_           ID3D11Device            *This,
  _In_     const D3D11_BUFFER_DESC       *pDesc,
  _In_opt_ const D3D11_SUBRESOURCE_DATA  *pInitialData,
  _Out_opt_      ID3D11Buffer           **ppBuffer )
{
  return
    D3D11Dev_CreateBuffer_Original ( This, pDesc,
                                       pInitialData, ppBuffer );
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateShaderResourceView_Override (
  _In_           ID3D11Device                     *This,
  _In_           ID3D11Resource                   *pResource,
  _In_opt_ const D3D11_SHADER_RESOURCE_VIEW_DESC  *pDesc,
  _Out_opt_      ID3D11ShaderResourceView        **ppSRView )
{
  if ( pDesc != nullptr && pResource != nullptr )
  {
    D3D11_RESOURCE_DIMENSION   dim;
    pResource->GetType       (&dim);

    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
      DXGI_FORMAT newFormat    = pDesc->Format;
      UINT        newMipLevels = pDesc->Texture2D.MipLevels;

      SK_ComQIPtr <ID3D11Texture2D>
          pTex2D (pResource);
      D3D11_TEXTURE2D_DESC  tex_desc = { };

      if (pTex2D != nullptr)
      {
        pTex2D->GetDesc (&tex_desc);

        bool override = false;

        if (! DirectX::IsDepthStencil (pDesc->Format))
        {
          if (                         pDesc->Format  != DXGI_FORMAT_UNKNOWN &&
              ( DirectX::BitsPerPixel (pDesc->Format) !=
                DirectX::BitsPerPixel (tex_desc.Format) ) ||
              ( DirectX::MakeTypeless (pDesc->Format) != // Handle cases such as BC3 -> BC7: Size = Same, Fmt != Same
                DirectX::MakeTypeless (tex_desc.Format) )
             ) // Does not handle sRGB vs. non-sRGB, but generally the game
          {    //   won't render stuff correctly if injected textures change that.
            override  = true;
            newFormat = tex_desc.Format;
          }
        }

        if ( SK_D3D11_OverrideDepthStencil (newFormat) )
          override = true;

        if ( SK_D3D11_TextureIsCached ((ID3D11Texture2D *)pResource) )
        {
          auto& textures =
            SK_D3D11_Textures;

          auto& cache_desc =
            textures->Textures_2D [(ID3D11Texture2D *)pResource];

          SK_ComPtr <ID3D11Device> pCacheDevice;
          cache_desc.texture->GetDevice (&pCacheDevice.p);

          if (pCacheDevice.IsEqualObject (This))
          {
            newFormat =
              cache_desc.desc.Format;

            newMipLevels =
              pDesc->Texture2D.MipLevels;

            if (                        pDesc->Format  != DXGI_FORMAT_UNKNOWN &&
                 DirectX::MakeTypeless (pDesc->Format) !=
                 DirectX::MakeTypeless (newFormat    )  )
            {
              if (DirectX::IsSRGB (pDesc->Format))
                newFormat = DirectX::MakeSRGB (newFormat);

              override = true;

              SK_LOG1 ( ( L"Overriding Resource View Format for Cached Texture '%08x'  { Was: '%hs', Now: '%hs' }",
                            cache_desc.crc32c,
                       SK_DXGI_FormatToStr (pDesc->Format).data      (),
                                SK_DXGI_FormatToStr (newFormat).data () ),
                          L"DX11TexMgr" );
            }

            if ( config.textures.d3d11.generate_mips &&
                 cache_desc.desc.MipLevels != pDesc->Texture2D.MipLevels )
            {
              override     = true;
              newMipLevels = cache_desc.desc.MipLevels;

              SK_LOG1 ( ( L"Overriding Resource View Mip Levels for Cached Texture '%08x'  { Was: %lu, Now: %lu }",
                            cache_desc.crc32c,
                              pDesc->Texture2D.MipLevels,
                                 newMipLevels ),
                          L"DX11TexMgr" );
            }
          }

          else if (config.system.log_level > 0)
            // TODO: Texture cache needs to be per-device
            SK_ReleaseAssert (!"Attempted to use a cached texture on the wrong device!");
        }

        if (override)
        {
          auto descCopy =
            *pDesc;

          descCopy.Format = newFormat;

          if (newMipLevels != pDesc->Texture2D.MipLevels)
          {
            descCopy.Texture2D.MipLevels = sk::narrow_cast <UINT>( -1 );
            descCopy.Texture2D.MostDetailedMip = 0;
          }

          HRESULT hr =
            D3D11Dev_CreateShaderResourceView_Original (
              This,        pResource,
                &descCopy, ppSRView                       );

          if (SUCCEEDED (hr))
          {
            return hr;
          }
        }
      }
    }
  }

  HRESULT hr =
    D3D11Dev_CreateShaderResourceView_Original ( This, pResource,
                                                   pDesc, ppSRView );
  return  hr;
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateDepthStencilView_Override (
  _In_            ID3D11Device                  *This,
  _In_            ID3D11Resource                *pResource,
  _In_opt_  const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
  _Out_opt_       ID3D11DepthStencilView        **ppDepthStencilView )
{
  HRESULT hr =
    E_UNEXPECTED;

  if ( pDesc     != nullptr &&
       pResource != nullptr )
  {
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType     (&dim);

    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
      DXGI_FORMAT                   newFormat (pDesc->Format);
      SK_ComQIPtr <ID3D11Texture2D> pTex      (pResource);

      if (pTex != nullptr)
      {
        D3D11_TEXTURE2D_DESC tex_desc;
             pTex->GetDesc (&tex_desc);

        auto descCopy =
          *pDesc;

        if ( SK_D3D11_OverrideDepthStencil (newFormat) || DirectX::BitsPerPixel (newFormat) !=
                                                          DirectX::BitsPerPixel (tex_desc.Format)  )
        {
          if (                        newFormat  != DXGI_FORMAT_UNKNOWN &&
               DirectX::BitsPerPixel (newFormat) !=
               DirectX::BitsPerPixel (tex_desc.Format) )
          {
            newFormat = tex_desc.Format;
          }

          descCopy.Format = newFormat;

          hr =
            D3D11Dev_CreateDepthStencilView_Original (
              This, pResource,
                &descCopy,
                  ppDepthStencilView
            );
        }

        if (SUCCEEDED (hr))
          return hr;
      }
    }
  }

  hr =
    D3D11Dev_CreateDepthStencilView_Original ( This, pResource,
                                                 pDesc, ppDepthStencilView );
  return hr;
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateUnorderedAccessView_Override (
  _In_            ID3D11Device                     *This,
  _In_            ID3D11Resource                   *pResource,
  _In_opt_  const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc,
  _Out_opt_       ID3D11UnorderedAccessView       **ppUAView )
{
  if ( pDesc     != nullptr &&
       pResource != nullptr )
  {
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType     (&dim);

    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
      DXGI_FORMAT newFormat    = pDesc->Format;
      SK_ComQIPtr <ID3D11Texture2D> pTex      (pResource);

      if (pTex != nullptr)
      {
        bool                 override = false;

        D3D11_TEXTURE2D_DESC tex_desc = { };
             pTex->GetDesc (&tex_desc);

        if ( SK_D3D11_OverrideDepthStencil (newFormat) )
          override = true;

        if ( SK_D3D11_TextureIsCached ((ID3D11Texture2D *)pResource) )
        {
          auto& textures =
            SK_D3D11_Textures;

          auto& cache_desc =
            textures->Textures_2D [(ID3D11Texture2D *)pResource];

          newFormat =
            cache_desc.desc.Format;

          if (                        pDesc->Format  != DXGI_FORMAT_UNKNOWN &&
               DirectX::MakeTypeless (pDesc->Format) !=
               DirectX::MakeTypeless (newFormat    )  )
          {
            if (DirectX::IsSRGB (pDesc->Format))
              newFormat = DirectX::MakeSRGB (newFormat);

            override = true;

            if (config.system.log_level > 0)
              SK_ReleaseAssert (override == false && L"UAV Format Override Needed");

            SK_LOG1 ( ( L"Overriding Unordered Access View Format for Cached Texture '%08x'  { Was: '%hs', Now: '%hs' }",
                          cache_desc.crc32c,
                     SK_DXGI_FormatToStr (pDesc->Format).data      (),
                              SK_DXGI_FormatToStr (newFormat).data () ),
                        L"DX11TexMgr" );
          }
        }

        if (                        pDesc->Format  != DXGI_FORMAT_UNKNOWN &&
             DirectX::BitsPerPixel (pDesc->Format) !=
             DirectX::BitsPerPixel (tex_desc.Format) )
        {
          override  = true;
          newFormat = tex_desc.Format;
        }

        if (override)
        {
          auto descCopy =
            *pDesc;

          descCopy.Format = newFormat;

          const HRESULT hr =
            D3D11Dev_CreateUnorderedAccessView_Original ( This, pResource,
                                                            &descCopy, ppUAView );

          if (SUCCEEDED (hr))
            return hr;
        }
      }
    }
  }

  const HRESULT hr =
    D3D11Dev_CreateUnorderedAccessView_Original ( This, pResource,
                                                    pDesc, ppUAView );
  return hr;
}

HRESULT
WINAPI
D3D11Dev_CreateRasterizerState_Override (
        ID3D11Device            *This,
  const D3D11_RASTERIZER_DESC   *pRasterizerDesc,
        ID3D11RasterizerState  **ppRasterizerState )
{
  return
    D3D11Dev_CreateRasterizerState_Original ( This,
                                                pRasterizerDesc,
                                                  ppRasterizerState );
}

HRESULT
WINAPI
D3D11Dev_CreateSamplerState_Override
(
  _In_            ID3D11Device        *This,
  _In_      const D3D11_SAMPLER_DESC  *pSamplerDesc,
  _Out_opt_       ID3D11SamplerState **ppSamplerState )
{
  D3D11_SAMPLER_DESC new_desc = *pSamplerDesc;

  static bool bShenmue =
    SK_GetCurrentGameID () == SK_GAME_ID::Shenmue;

  if (bShenmue)
  {
    config.textures.d3d11.uncompressed_mips = true;
    config.textures.d3d11.cache_gen_mips    = true;
    config.textures.d3d11.generate_mips     = true;

    ///dll_log.Log ( L"CreateSamplerState - Filter: %s, MaxAniso: %lu, MipLODBias: %f, MinLOD: %f, MaxLOD: %f, Comparison: %x, U:%x,V:%x,W:%x - %ws",
    ///             SK_D3D11_FilterToStr (new_desc.Filter), new_desc.MaxAnisotropy, new_desc.MipLODBias, new_desc.MinLOD, new_desc.MaxLOD,
    ///             new_desc.ComparisonFunc, new_desc.AddressU, new_desc.AddressV, new_desc.AddressW, SK_SummarizeCaller ().c_str () );

    if (new_desc.Filter != D3D11_FILTER_MIN_MAG_MIP_POINT)
    {
      //if ( new_desc.ComparisonFunc == D3D11_COMPARISON_ALWAYS /*&&
           //new_desc.MaxLOD         == D3D11_FLOAT32_MAX        */)
      //{
        new_desc.MaxAnisotropy =  16;
        new_desc.Filter        =  D3D11_FILTER_ANISOTROPIC;
        new_desc.MaxLOD        =  D3D11_FLOAT32_MAX;
        new_desc.MinLOD        = -D3D11_FLOAT32_MAX;
      //}
    }

    return
      D3D11Dev_CreateSamplerState_Original (This, &new_desc, ppSamplerState);
  }

#ifdef _M_AMD64
  static bool __yakuza =
    ( SK_GetCurrentGameID () == SK_GAME_ID::Yakuza0 ||
      SK_GetCurrentGameID () == SK_GAME_ID::YakuzaKiwami2 ||
      SK_GetCurrentGameID () == SK_GAME_ID::YakuzaUnderflow );

  if (__yakuza)
  {
    extern bool __SK_Y0_FixAniso;
    extern bool __SK_Y0_ClampLODBias;
    extern int  __SK_Y0_ForceAnisoLevel;

    if (__SK_Y0_ClampLODBias)
    {
      new_desc.MipLODBias = std::max (0.0f, new_desc.MipLODBias);
    }

    if (__SK_Y0_ForceAnisoLevel != 0)
    {
      new_desc.MinLOD        = -D3D11_FLOAT32_MAX;
      new_desc.MaxLOD        =  D3D11_FLOAT32_MAX;

      new_desc.MaxAnisotropy = __SK_Y0_ForceAnisoLevel;
    }

    if (__SK_Y0_FixAniso)
    {
      if (new_desc.Filter <= D3D11_FILTER_ANISOTROPIC)
      {
        if (new_desc.MaxAnisotropy > 1)
        {
          new_desc.Filter = D3D11_FILTER_ANISOTROPIC;
        }
      }

      if (new_desc.Filter > D3D11_FILTER_ANISOTROPIC && new_desc.ComparisonFunc == 4)
      {
        if (new_desc.MaxAnisotropy > 1)
        {
          new_desc.Filter = D3D11_FILTER_COMPARISON_ANISOTROPIC;
        }
      }
    }

    const HRESULT hr =
      D3D11Dev_CreateSamplerState_Original (This, &new_desc, ppSamplerState);

    if (SUCCEEDED (hr))
      return hr;
  }
#endif

  static bool bLegoMarvel2 =
    ( SK_GetCurrentGameID () == SK_GAME_ID::LEGOMarvelSuperheroes2 );

  if (bLegoMarvel2)
  {
    if (new_desc.Filter <= D3D11_FILTER_ANISOTROPIC)
    {
      new_desc.Filter        = D3D11_FILTER_ANISOTROPIC;
      new_desc.MaxAnisotropy = 16;

      new_desc.MipLODBias    = 0.0f;
      new_desc.MinLOD        = 0.0f;
      new_desc.MaxLOD        = D3D11_FLOAT32_MAX;

      const HRESULT hr =
        D3D11Dev_CreateSamplerState_Original (This, &new_desc, ppSamplerState);

      if (SUCCEEDED (hr))
        return hr;
    }
  }

  static bool bYs8 =
    (SK_GetCurrentGameID () == SK_GAME_ID::Ys_Eight);

  if (bYs8)
  {
    if (config.textures.d3d11.generate_mips && new_desc.Filter <= D3D11_FILTER_ANISOTROPIC)
    {
      //if (new_desc.Filter != D3D11_FILTER_MIN_MAG_MIP_POINT)
      {
        new_desc.Filter        = D3D11_FILTER_ANISOTROPIC;
        new_desc.MaxAnisotropy = 16;

        if (new_desc.MipLODBias < 0.0f)
          new_desc.MipLODBias   = 0.0f;

        new_desc.MinLOD        = 0.0f;
        new_desc.MaxLOD        = D3D11_FLOAT32_MAX;
      }

      const HRESULT hr =
        D3D11Dev_CreateSamplerState_Original (This, &new_desc, ppSamplerState);

      if (SUCCEEDED (hr))
        return hr;
    }

    if ( config.textures.d3d11.generate_mips                          &&
          ( ( new_desc.Filter >  D3D11_FILTER_ANISOTROPIC &&
              new_desc.Filter <= D3D11_FILTER_COMPARISON_ANISOTROPIC) ||
            new_desc.ComparisonFunc != D3D11_COMPARISON_NEVER ) )
    {
      new_desc.Filter        = D3D11_FILTER_COMPARISON_ANISOTROPIC;
      new_desc.MaxAnisotropy = 16;

      if (pSamplerDesc->Filter != new_desc.Filter)
      {
        SK_LOG0 ( ( L"Changing Shadow Filter from '%s' to '%s'",
                      SK_D3D11_DescribeFilter (pSamplerDesc->Filter),
                           SK_D3D11_DescribeFilter (new_desc.Filter) ),
                    L" TexCache " );
      }

      const HRESULT hr =
        D3D11Dev_CreateSamplerState_Original (This, &new_desc, ppSamplerState);

      if (SUCCEEDED (hr))
        return hr;
    }
  }

  return
    D3D11Dev_CreateSamplerState_Original (This, pSamplerDesc, ppSamplerState);
}


__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateTexture2D_Override (
  _In_            ID3D11Device           *This,
  _In_      const D3D11_TEXTURE2D_DESC   *pDesc,
  _In_opt_  const D3D11_SUBRESOURCE_DATA *pInitialData,
  _Out_opt_       ID3D11Texture2D        **ppTexture2D )
{
  if ((pDesc == nullptr) ||
                 ( pDesc->Width  < 4 &&
                   pDesc->Height < 4    ))
  {
    D3D11_TEXTURE2D_DESC descCopy =
                   pDesc != nullptr ?
                  *pDesc :
                   D3D11_TEXTURE2D_DESC { };

    return
      D3D11Dev_CreateTexture2D_Impl (
        This, &descCopy, pInitialData,
          ppTexture2D, _ReturnAddress ()
      );
  }

  const D3D11_TEXTURE2D_DESC* pDescOrig =  pDesc;
                         auto descCopy  = *pDescOrig;

  const HRESULT hr =
    D3D11Dev_CreateTexture2D_Impl (
      This, &descCopy, pInitialData,
            ppTexture2D, _ReturnAddress ()
    );

  if (SUCCEEDED (hr))
  {
    //auto orig_se =
    //SK_SEH_SetTranslator (SK_FilteringStructuredExceptionTranslator (EXCEPTION_ACCESS_VIOLATION));
    //try {
    //  *const_cast <D3D11_TEXTURE2D_DESC *> ( pDesc ) =
    //    descCopy;
    //}
    //catch (const SK_SEH_IgnoredException&) { };
    //SK_SEH_SetTranslator (orig_se);


  //if (pDesc && pDesc->Usage == D3D11_USAGE_STAGING)
  //{
  //  dll_log.Log ( L"Code Origin ('%s') - Staging: %lux%lu - Format: %hs, CPU Access: %x, Misc Flags: %x",
  //                  SK_GetCallerName ().c_str (), pDesc->Width, pDesc->Height,
  //                  SK_DXGI_FormatToStr          (pDesc->Format).c_str (),
  //                         pDesc->CPUAccessFlags, pDesc->MiscFlags );
  //}
  }

  return hr;
}


__declspec (noinline)
HRESULT
STDMETHODCALLTYPE
D3D11Dev_CreateRenderTargetView_Override (
  _In_            ID3D11Device                   *This,
  _In_            ID3D11Resource                 *pResource,
  _In_opt_  const D3D11_RENDER_TARGET_VIEW_DESC  *pDesc,
  _Out_opt_       ID3D11RenderTargetView        **ppRTView )
{
  return
    SK_D3D11Dev_CreateRenderTargetView_Impl ( This,
                                                pResource, pDesc,
                                                  ppRTView, FALSE );
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateVertexShader_Override (
  _In_            ID3D11Device        *This,
  _In_      const void                *pShaderBytecode,
  _In_            SIZE_T               BytecodeLength,
  _In_opt_        ID3D11ClassLinkage  *pClassLinkage,
  _Out_opt_       ID3D11VertexShader **ppVertexShader )
{
  return
    SK_D3D11_CreateShader_Impl ( This,
                                   pShaderBytecode, BytecodeLength,
                                     pClassLinkage,
                         (IUnknown **)(ppVertexShader),
                                         sk_shader_class::Vertex );
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreatePixelShader_Override (
  _In_            ID3D11Device        *This,
  _In_      const void                *pShaderBytecode,
  _In_            SIZE_T               BytecodeLength,
  _In_opt_        ID3D11ClassLinkage  *pClassLinkage,
  _Out_opt_       ID3D11PixelShader  **ppPixelShader )
{
  return
    SK_D3D11_CreateShader_Impl ( This,
                                   pShaderBytecode, BytecodeLength,
                                     pClassLinkage,
                         (IUnknown **)(ppPixelShader),
                                         sk_shader_class::Pixel );
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateGeometryShader_Override (
  _In_            ID3D11Device          *This,
  _In_      const void                  *pShaderBytecode,
  _In_            SIZE_T                 BytecodeLength,
  _In_opt_        ID3D11ClassLinkage    *pClassLinkage,
  _Out_opt_       ID3D11GeometryShader **ppGeometryShader )
{
  return
    SK_D3D11_CreateShader_Impl ( This,
                                   pShaderBytecode, BytecodeLength,
                                     pClassLinkage,
                         (IUnknown **)(ppGeometryShader),
                                         sk_shader_class::Geometry );
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateGeometryShaderWithStreamOutput_Override (
  _In_            ID3D11Device               *This,
  _In_      const void                       *pShaderBytecode,
  _In_            SIZE_T                     BytecodeLength,
  _In_opt_  const D3D11_SO_DECLARATION_ENTRY *pSODeclaration,
  _In_            UINT                       NumEntries,
  _In_opt_  const UINT                       *pBufferStrides,
  _In_            UINT                       NumStrides,
  _In_            UINT                       RasterizedStream,
  _In_opt_        ID3D11ClassLinkage         *pClassLinkage,
  _Out_opt_       ID3D11GeometryShader      **ppGeometryShader )
{
  if (pShaderBytecode == nullptr)
    return E_POINTER;

  const HRESULT hr =
    D3D11Dev_CreateGeometryShaderWithStreamOutput_Original ( This, pShaderBytecode,
                                                               BytecodeLength,
                                                                 pSODeclaration, NumEntries,
                                                                   pBufferStrides, NumStrides,
                                                                     RasterizedStream, pClassLinkage,
                                                                       ppGeometryShader );

  if (SUCCEEDED (hr) && ppGeometryShader)
  {
    auto& geo_shaders =
      SK_D3D11_Shaders->geometry;

    uint32_t checksum =
      SK_D3D11_ChecksumShaderBytecode (pShaderBytecode, BytecodeLength);

    if (checksum == 0x00)
      checksum = (uint32_t)BytecodeLength;

    cs_shader_gs->lock ();

    if (! geo_shaders.descs [This].count (checksum))
    {
      SK_D3D11_ShaderDesc desc;

      desc.type   = SK_D3D11_ShaderType::Geometry;
      desc.crc32c = checksum;

      desc.bytecode.insert (  desc.bytecode.cend  (),
        &((const uint8_t *) pShaderBytecode) [0],
        &((const uint8_t *) pShaderBytecode) [BytecodeLength]
      );

      geo_shaders.descs [This].emplace (std::make_pair (checksum, desc));
    }

    SK_D3D11_ShaderDesc* pDesc =
      &geo_shaders.descs [This][checksum];

    if ( geo_shaders.rev [This].count (*ppGeometryShader) &&
               geo_shaders.rev [This][*ppGeometryShader]->crc32c != checksum )
         geo_shaders.rev [This].erase (*ppGeometryShader);

    geo_shaders.rev [This].emplace (std::make_pair (*ppGeometryShader, pDesc));

    cs_shader_gs->unlock ();

    InterlockedExchange64 ((volatile LONG64 *)&pDesc->usage.last_frame, SK_GetFramesDrawn ());
              //_time64 (&desc.usage.last_time);
  }

  return hr;
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateHullShader_Override (
  _In_            ID3D11Device        *This,
  _In_      const void                *pShaderBytecode,
  _In_            SIZE_T               BytecodeLength,
  _In_opt_        ID3D11ClassLinkage  *pClassLinkage,
  _Out_opt_       ID3D11HullShader   **ppHullShader )
{
  if (pShaderBytecode == nullptr)
    return E_POINTER;

  return
    SK_D3D11_CreateShader_Impl ( This,
                                   pShaderBytecode, BytecodeLength,
                                     pClassLinkage,
                         (IUnknown **)(ppHullShader),
                                         sk_shader_class::Hull );
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateDomainShader_Override (
  _In_            ID3D11Device        *This,
  _In_      const void                *pShaderBytecode,
  _In_            SIZE_T               BytecodeLength,
  _In_opt_        ID3D11ClassLinkage  *pClassLinkage,
  _Out_opt_       ID3D11DomainShader **ppDomainShader )
{
  if (pShaderBytecode == nullptr)
    return E_POINTER;

  return
    SK_D3D11_CreateShader_Impl ( This,
                                   pShaderBytecode, BytecodeLength,
                                     pClassLinkage,
                         (IUnknown **)(ppDomainShader),
                                         sk_shader_class::Domain );
}

__declspec (noinline)
HRESULT
WINAPI
D3D11Dev_CreateComputeShader_Override (
  _In_            ID3D11Device         *This,
  _In_      const void                 *pShaderBytecode,
  _In_            SIZE_T                BytecodeLength,
  _In_opt_        ID3D11ClassLinkage   *pClassLinkage,
  _Out_opt_       ID3D11ComputeShader **ppComputeShader )
{
  if (pShaderBytecode == nullptr)
    return E_POINTER;

  return
    SK_D3D11_CreateShader_Impl ( This,
                                   pShaderBytecode, BytecodeLength,
                                     pClassLinkage,
                         (IUnknown **)(ppComputeShader),
                                         sk_shader_class::Compute );
}