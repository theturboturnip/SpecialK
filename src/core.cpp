﻿/**
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
#include <SpecialK/resource.h>

#include <SpecialK/render/d3d9/d3d9_backend.h>
#include <SpecialK/render/d3d11/d3d11_core.h>
#include <SpecialK/render/d3d12/d3d12_interfaces.h>

#include <SpecialK/storefront/epic.h>

#include <SpecialK/nvapi.h>
#include <SpecialK/adl.h>


#include <SpecialK/commands/mem.inl>
#include <SpecialK/commands/update.inl>

#include <filesystem>

#include <Aux_ulib.h>


#ifdef _WIN64
#pragma comment (lib, R"(depends\lib\DirectXTex\x64\DirectXTex.lib)")
#pragma comment (lib, R"(depends\lib\MinHook\x64\libMinHook64.lib)")
#pragma comment (lib, R"(depends\lib\lzma\x64\libzma.lib)")
#else
#pragma comment (lib, R"(depends\lib\DirectXTex\Win32\DirectXTex.lib)")
#pragma comment (lib, R"(depends\lib\MinHook\Win32\libMinHook.lib)")
#pragma comment (lib, R"(depends\lib\lzma\Win32\libzma.lib)")
#endif

volatile HANDLE   hInitThread     = { INVALID_HANDLE_VALUE };
volatile DWORD    dwInitThreadId  =   1;

         BOOL     nvapi_init      = FALSE;
         HMODULE  backend_dll     = nullptr;

volatile LONG     __SK_Init       = FALSE;
         bool     __SK_bypass     = false;
   const wchar_t* __SK_BootedCore = L"";
  extern bool     __SK_RunDLL_Bypass;

  extern float    __target_fps;

         BOOL     __SK_DisableQuickHook = FALSE;


using  ChangeDisplaySettingsA_pfn = LONG (WINAPI *)(
                                     _In_opt_ DEVMODEA *lpDevMode,
                                     _In_     DWORD     dwFlags );
extern ChangeDisplaySettingsA_pfn
       ChangeDisplaySettingsA_Original;


//#define _THREADED_BASIC_INIT

struct init_params_s {
  std::wstring  backend    = L"INVALID";
  void*         callback   =    nullptr;
  LARGE_INTEGER start_time {   0,   0 };
};


init_params_s&
SKX_GetInitParams (void)
{
  static init_params_s params = { };
  return               params;
};

const init_params_s&
SK_GetInitParams (void)
{
  return SKX_GetInitParams ();
};


wchar_t*
SKX_GetBackend (void)
{
  static wchar_t SK_Backend [128] = { };
  return         SK_Backend;
}

const wchar_t*
__stdcall
SK_GetBackend (void)
{
  return SKX_GetBackend ();
}

const wchar_t*
__stdcall
SK_SetBackend (const wchar_t* wszBackend)
{
  wcsncpy_s ( SKX_GetBackend (), 127,
                wszBackend,      _TRUNCATE );

  return
    SKX_GetBackend ();
}

wchar_t*
SKX_GetRootPath (void)
{
  static wchar_t SK_RootPath [MAX_PATH + 2] = { };
  return         SK_RootPath;
}

const wchar_t*
__stdcall
SK_GetRootPath (void)
{
  return SKX_GetRootPath ();
}


wchar_t*
SKX_GetInstallPath (void)
{
  static wchar_t SK_InstallPath [MAX_PATH + 2] = { };
  return         SK_InstallPath;
}

const wchar_t*
__stdcall
SK_GetInstallPath (void)
{
  return SKX_GetInstallPath ();
}



wchar_t*
SKX_GetNaiveConfigPath (void)
{
  static wchar_t SK_ConfigPath [MAX_PATH + 2] = { };
  return         SK_ConfigPath;
}

__declspec (noinline)
const wchar_t*
__stdcall
SK_GetNaiveConfigPath (void)
{
  return SKX_GetNaiveConfigPath ();
}

//
// To be used internally only, by the time any plug-in has
//   been activated, Special K will have already established
//     this path and loaded its config.
//
void
__stdcall
SK_SetConfigPath (const wchar_t* path)
{
  wcsncpy_s ( SKX_GetNaiveConfigPath (), MAX_PATH,
                path,                    _TRUNCATE );
}

void
__stdcall
SK_StartPerfMonThreads (void)
{
  // Handle edge-case that might re-spawn WMI threads during de-init.
  if (ReadAcquire (&__SK_DLL_Ending))
    return;

  auto SpawnMonitorThread =
  []( volatile HANDLE                 *phThread,
      const    wchar_t                *wszName,
               LPTHREAD_START_ROUTINE  pThunk ) ->
  bool
  {
    if ( INVALID_HANDLE_VALUE ==
           InterlockedCompareExchangePointer (phThread, nullptr, INVALID_HANDLE_VALUE)
       )
    {
      dll_log->LogEx (true, L"[ Perfmon. ] Spawning %ws...  ", wszName);

      InterlockedExchangePointer ( (void **)phThread,
        SK_Thread_CreateEx ( pThunk )
      );

      // Most WMI stuff will be replaced with NtDll in the future
      //   -- for now, CPU monitoring is the only thing that has abandoned
      //        WMI
      //
      if (pThunk != SK_MonitorCPU)
      {
        SK_RunOnce (SK_WMI_Init ());
      }

      if (ReadPointerAcquire (phThread) != INVALID_HANDLE_VALUE)
      {
        dll_log->LogEx (false, L"tid=0x%04x\n",
                          GetThreadId (ReadPointerAcquire (phThread)));
        return true;
      }

      dll_log->LogEx (false, L"Failed!\n");
    }

    return false;
  };

  //
  // Spawn CPU Refresh Thread
  //
  if (config.cpu.show || ( SK_ImGui_Widgets->cpu_monitor != nullptr &&
                           SK_ImGui_Widgets->cpu_monitor->isActive () ))
  {
    SpawnMonitorThread ( &SK_WMI_CPUStats->hThread,
                         L"CPU Monitor",      SK_MonitorCPU      );
  }

  if (config.disk.show)
  {
    SpawnMonitorThread ( &SK_WMI_DiskStats->hThread,
                         L"Disk Monitor",     SK_MonitorDisk     );
  }

  if (config.pagefile.show)
  {
    SpawnMonitorThread ( &SK_WMI_PagefileStats->hThread,
                         L"Pagefile Monitor", SK_MonitorPagefile );
  }
}

extern bool SK_ETW_EndTracing (void);


void
SK_LoadGPUVendorAPIs (void)
{
  dll_log->LogEx (false, L"================================================"
                         L"===========================================\n" );

  // None of the GPU vendor-specific APIs work in WINE.
  //
  if (config.compatibility.using_wine)
    return;

  dll_log->Log (L"[  NvAPI   ] Initializing NVIDIA API           (NvAPI)...");

  nvapi_init =
    sk::NVAPI::InitializeLibrary (SK_GetHostApp ());

  dll_log->Log (L"[  NvAPI   ]              NvAPI Init         { %s }",
                                                     nvapi_init ? L"Success" :
                                                                  L"Failed ");

  if (nvapi_init)
  {
    const int num_sli_gpus =
      sk::NVAPI::CountSLIGPUs ();

    dll_log->Log ( L"[  NvAPI   ] >> NVIDIA Driver Version: %s",
                    sk::NVAPI::GetDriverVersion ().c_str () );

    const int gpu_count =
      sk::NVAPI::CountPhysicalGPUs ();

    dll_log->Log ( gpu_count > 1 ? L"[  NvAPI   ]  * Number of Installed NVIDIA GPUs: %i  "
                                   L"{ SLI: '%s' }"
                                 :
                                   L"[  NvAPI   ]  * Number of Installed NVIDIA GPUs: %i  { '%s' }",
                                     gpu_count > 1 ? num_sli_gpus :
                                                     num_sli_gpus + 1,
                                       sk::NVAPI::EnumGPUs_DXGI ()[0].Description );

    if (num_sli_gpus > 0)
    {
      DXGI_ADAPTER_DESC* sli_adapters =
        sk::NVAPI::EnumSLIGPUs ();

      int sli_gpu_idx = 0;

      while (*sli_adapters->Description != L'\0')
      {
        dll_log->Log ( L"[  NvAPI   ]   + SLI GPU %d: %s",
                         sli_gpu_idx++,
                           (sli_adapters++)->Description );
      }
    }

    //
    // Setup a framerate limiter and (if necessary) restart
    //
    bool restart = (! sk::NVAPI::SetFramerateLimit (0));

    //
    // Install SLI Override Settings
    //
    if (sk::NVAPI::CountSLIGPUs () && config.nvidia.sli.override)
    {
      if (! sk::NVAPI::SetSLIOverride
              ( SK_GetDLLRole (),
                  config.nvidia.sli.mode.c_str (),
                    config.nvidia.sli.num_gpus.c_str (),
                      config.nvidia.sli.compatibility.c_str ()
              )
         )
      {
        restart = true;
      }
    }

    SK_NvAPI_SetAppName         (       SK_GetFullyQualifiedApp () );
    SK_NvAPI_SetAppFriendlyName (
      app_cache_mgr->getAppNameFromID ( SK_Steam_GetAppID_NoAPI () ).c_str ()
                                );

    if (! config.nvidia.bugs.snuffed_ansel)
    {
      if (SK_NvAPI_DisableAnsel (SK_GetDLLRole ()))
      {
        restart = true;

        SK_MessageBox (
          L"To Avoid Potential Compatibility Issues, Special K has Disabled Ansel for this Game.\r\n\r\n"
          L"You may re-enable Ansel for this Game using the Help Menu in Special K's Control Panel.",
            L"Special K Compatibility Layer:  [ Ansel Disabled ]",
              MB_ICONWARNING | MB_OK
        );
      }

      config.nvidia.bugs.snuffed_ansel = true;

      SK_GetDLLConfig ()->write ();
    }

    if (restart)
    {
      dll_log->Log (L"[  Nv API  ] >> Restarting to apply NVIDIA driver settings <<");

      SK_ShellExecuteW ( GetDesktopWindow (),
                          L"OPEN",
                            SK_GetHostApp (),
                              nullptr,
                                nullptr,
                                  SW_SHOWDEFAULT );
      exit (0);
    }
  }

  // Not NVIDIA, maybe AMD?
  else
  {
    dll_log->Log (L"[DisplayLib] Initializing AMD Display Library (ADL)...");

    BOOL adl_init =
      SK_InitADL ();

    dll_log->Log   (L"[DisplayLib]              ADL   Init         { %s }",
                                                      adl_init ? L"Success" :
                                                                 L"Failed ");

    // Yes, AMD driver is in working order ...
    if (adl_init > 0)
    {
      dll_log->Log ( L"[DisplayLib]  * Number of Reported AMD Adapters: %i (%i active)",
                       SK_ADL_CountPhysicalGPUs (),
                         SK_ADL_CountActiveGPUs () );
    }
  }

  const HMODULE hMod =
    SK_GetModuleHandle (SK_GetHostApp ());

  if (hMod != nullptr)
  {
    const auto* dwOptimus =
      reinterpret_cast <DWORD *> (
        SK_GetProcAddress ( SK_GetHostApp (),
                              "NvOptimusEnablement" )
      );

    if (dwOptimus != nullptr)
    {
      dll_log->Log ( L"[Hybrid GPU]  NvOptimusEnablement..................: 0x%02X (%s)",
                       *dwOptimus,
                     ((*dwOptimus) & 0x1) ? L"Max Perf." :
                                            L"Don't Care" );
    }

    else
    {
      dll_log->Log (L"[Hybrid GPU]  NvOptimusEnablement..................: UNDEFINED");
    }

    const auto* dwPowerXpress =
      reinterpret_cast <DWORD *> (
        SK_GetProcAddress ( SK_GetHostApp (),
                              "AmdPowerXpressRequestHighPerformance" )
      );

    if (dwPowerXpress != nullptr)
    {
      dll_log->Log (L"[Hybrid GPU]  AmdPowerXpressRequestHighPerformance.: 0x%02X (%s)",
         *dwPowerXpress,
         (*dwPowerXpress & 0x1) ? L"High Perf." :
                                  L"Don't Care" );
    }

    else
      dll_log->Log (L"[Hybrid GPU]  AmdPowerXpressRequestHighPerformance.: UNDEFINED");

    dll_log->LogEx (false, L"================================================"
                           L"===========================================\n" );
  }
}

void SK_FetchBuiltinSounds (void)
{
  wchar_t           wszArchive  [MAX_PATH + 2] = { };
  static const auto wszDestination =
      std::filesystem::path (SK_GetInstallPath ()) /
                        LR"(Assets\Shared\Sounds\)";

  static const std::filesystem::path
    predefined_sounds [] =
    {
      std::filesystem::path (wszDestination) / LR"(dream_theater.wav)",
      std::filesystem::path (wszDestination) / LR"(screenshot.wav)",
      std::filesystem::path (wszDestination) / LR"(psn_trophy.wav)",
      std::filesystem::path (wszDestination) / LR"(crash.wav)",
      std::filesystem::path (wszDestination) / LR"(xbox.wav)"
    };

  static bool incomplete_set = false;

  for ( auto& sound : predefined_sounds )
  {
    if (! std::filesystem::exists (sound))
    {
      incomplete_set = true;
      break;
    }
  }

  if (! incomplete_set) return;

  SK_CreateDirectories (wszDestination.c_str ());

  wcsncpy_s   ( wszArchive,               MAX_PATH,
                wszDestination.c_str (), _TRUNCATE );
  PathAppendW ( wszArchive,          L"builtin.7z" );

  SK_RunOnce (
  SK_Network_EnqueueDownload (
    sk_download_request_s (wszArchive, R"(https://sk-data.special-k.info/sounds/builtin.7z)",
      []( const std::vector <uint8_t>&& data,
          const std::wstring_view       file ) -> bool
      {
        std::filesystem::path
                    full_path (file.data ());

        if ( FILE *fPackedSounds = _wfopen (full_path.c_str (), L"wb") ;
                   fPackedSounds != nullptr )
        {
          fwrite (data.data (), 1, data.size (), fPackedSounds);
          fclose (                               fPackedSounds);

          if ( SUCCEEDED (
                 SK_Decompress7zEx ( full_path.c_str (),
                                     full_path.parent_path (
                                            ).c_str (), nullptr )
                         )
             )
          {
            DeleteFileW (full_path.c_str ());

            incomplete_set = false;
          }
        }

        return true;
      }
    ), true // High Priority
  ));
}

void
__stdcall
SK_InitCore (std::wstring, void* callback)
{
  using finish_pfn   = void (WINAPI *)  (void);
  using callback_pfn = void (WINAPI *)(_Releases_exclusive_lock_ (init_mutex) finish_pfn);

  const auto callback_fn =
    (callback_pfn)callback;

  init_mutex->lock ();

#ifdef _THREADED_BASIC_INIT
extern void BasicInit (void);
            BasicInit (    );
#endif

  SK_FetchBuiltinSounds ();

  switch (SK_GetCurrentGameID ())
  {
#ifdef _WIN64
    case SK_GAME_ID::NieRAutomata:
      SK_FAR_InitPlugin ();
      break;

    case SK_GAME_ID::BlueReflection:
      SK_IT_InitPlugin ();
      break;

    case SK_GAME_ID::DotHackGU:
      SK_DGPU_InitPlugin ();
      break;

    case SK_GAME_ID::NiNoKuni2:
      SK_NNK2_InitPlugin ();
      break;

    case SK_GAME_ID::Tales_of_Vesperia:
      SK_TVFix_InitPlugin ();
      break;

    case SK_GAME_ID::Sekiro:
      extern void SK_Sekiro_InitPlugin (void);
                  SK_Sekiro_InitPlugin (    );
      break;

    case SK_GAME_ID::FarCry5:
    {
      auto _UnpackEasyAntiCheatBypass = [&](void) ->
      void
      {
        HMODULE hModSelf =
          SK_GetDLL ();

        HRSRC res =
          FindResource ( hModSelf, MAKEINTRESOURCE (IDR_FC5_KILL_ANTI_CHEAT), L"7ZIP" );

        if (res)
        {
          DWORD   res_size     =
            SizeofResource ( hModSelf, res );

          HGLOBAL packed_anticheat =
            LoadResource   ( hModSelf, res );

          if (! packed_anticheat) return;

          const void* const locked =
            (void *)LockResource (packed_anticheat);

          if (locked != nullptr)
          {
            wchar_t      wszBackup      [MAX_PATH + 2] = { };
            wchar_t      wszArchive     [MAX_PATH + 2] = { };
            wchar_t      wszDestination [MAX_PATH + 2] = { };

            wcscpy (wszDestination, SK_GetHostPath ());

            if (GetFileAttributesW (wszDestination) == INVALID_FILE_ATTRIBUTES)
              SK_CreateDirectories (wszDestination);

            PathAppendW (wszDestination, L"EasyAntiCheat");
            wcscpy      (wszBackup,      wszDestination);
            PathAppendW (wszBackup,      L"EasyAntiCheat_x64_orig.dll");

            if (GetFileAttributesW (wszBackup) == INVALID_FILE_ATTRIBUTES)
            {
              wchar_t      wszBackupSrc [MAX_PATH + 2] = { };
              wcscpy      (wszBackupSrc, wszDestination);
              PathAppendW (wszBackupSrc, L"EasyAntiCheat_x64.dll");
              CopyFileW   (wszBackupSrc, wszBackup, TRUE);

              SK_LOG0 ( ( L"Unpacking EasyAntiCheatDefeat for FarCry 5" ),
                          L"AntiDefeat" );

              wcscpy      (wszArchive, wszDestination);
              PathAppendW (wszArchive, L"EasyAntiCheatDefeat.7z");

              FILE* fPackedCompiler =
                _wfopen   (wszArchive, L"wb");

              if (fPackedCompiler != nullptr)
              {
                fwrite    (locked, 1, res_size, fPackedCompiler);
                fclose    (fPackedCompiler);
              }

              SK_Decompress7zEx (wszArchive, wszDestination, nullptr);
              DeleteFileW       (wszArchive);
            }
          }

          UnlockResource (packed_anticheat);
        }
      };

      _UnpackEasyAntiCheatBypass ();
    } break;
    case SK_GAME_ID::Ys_Eight:
      SK_YS8_InitPlugin ();
      break;
    case SK_GAME_ID::Elex2:
      SK_ELEX2_InitPlugin ();
      break;
#else
    case SK_GAME_ID::SecretOfMana:
      SK_SOM_InitPlugin ();
      break;

    case SK_GAME_ID::DragonBallFighterZ:
    {
      wchar_t      wszPath       [MAX_PATH + 2] = { };
      wchar_t      wszWorkingDir [MAX_PATH + 2] = { };
      wcscpy      (wszWorkingDir, SK_GetHostPath  ());
      PathAppendW (wszWorkingDir, LR"(RED\Binaries\Win64\)");

      wcscpy      (wszPath,       wszWorkingDir);
      PathAppendW (wszPath,       L"RED-Win64-Shipping.exe");

      SK_ShellExecuteW (nullptr, L"open", wszPath, L"-eac-nop-loaded", wszWorkingDir, SW_SHOWNORMAL);
      ExitProcess      (0);
    } break;

    case SK_GAME_ID::ChronoCross:
    {
      SK_CC_InitPlugin ();
    } break;
#endif
  }

  // Setup the compatibility back end, which monitors loaded libraries,
  //   blacklists bad DLLs and detects render APIs...
  SK_EnumLoadedModules  (SK_ModuleEnum::PostLoad);
  SK_Memory_InitHooks   ();

  if (SK_GetDLLRole () != DLL_ROLE::DInput8)
  {
    if (SK_GetModuleHandle (L"dinput8.dll"))
      SK_Input_HookDI8  ();

    if (SK_GetModuleHandle (L"dinput.dll"))
      SK_Input_HookDI7  ();

    SK_Input_Init       ();
  }

  void
     __stdcall SK_InitFinishCallback (void);
  callback_fn (SK_InitFinishCallback);
}

// This God awful code is lockless and safe to call from anywhere, but please do not.
void
WaitForInit (void)
{
  constexpr auto _SpinMax = 32;

  LONG init =
    ReadAcquire (&__SK_Init);

  if (init != 0)
    return;

  const DWORD dwThreadId =
    GetCurrentThreadId ();

  while (ReadPointerAcquire (&hInitThread) != INVALID_HANDLE_VALUE)
  {
    const DWORD dwInitTid =
      ReadULongAcquire (&dwInitThreadId);

    if ( dwInitTid                == dwThreadId ||
         dwInitTid                == 0          ||
         ReadAcquire (&__SK_Init) == TRUE )
    {
      break;
    }

    for (int i = 0; i < _SpinMax && (ReadPointerAcquire (&hInitThread) != INVALID_HANDLE_VALUE); i++)
      ;

    HANDLE hWait =
      ReadPointerAcquire (&hInitThread);

    if ( hWait == INVALID_HANDLE_VALUE )
      break;

    HANDLE hWaitArray [] = {
           hWait
    };

    const DWORD dwWaitStatus =
      MsgWaitForMultipleObjectsEx (1, hWaitArray, 16UL, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

    if ( dwWaitStatus == WAIT_OBJECT_0 ||
         dwWaitStatus == WAIT_ABANDONED ) break;
  }
}


// Stuff might be unimplemented in Wine, limp home if it throws this exception
#define EXCEPTION_WINE_STUB 0x80000100

void SK_SEH_InitFinishCallback (void)
{
  __try
  {
    if (! (SK_GetDLLRole () & DLL_ROLE::DXGI))
      SK::DXGI::StartBudgetThread_NoAdapter ();

    static const GUID  nil_guid = {     };
                 GUID* pGUID    = nullptr;

    // Make note of the system's original power scheme
    if ( ERROR_SUCCESS ==
           PowerGetActiveScheme ( nullptr,
                                    &pGUID )
       )
    {
      config.cpu.power_scheme_guid_orig = *pGUID;

      SK_LocalFree ((HLOCAL)pGUID);
    }

    // Apply a powerscheme override if one is set
    if (! IsEqualGUID (config.cpu.power_scheme_guid, nil_guid))
    {
      PowerSetActiveScheme (nullptr, &config.cpu.power_scheme_guid);
    }

    SK_LoadGPUVendorAPIs ();
  }

  __except ( GetExceptionCode () == EXCEPTION_WINE_STUB       ?
                                    EXCEPTION_EXECUTE_HANDLER :
                                    EXCEPTION_CONTINUE_SEARCH )
  { }
}

void
__stdcall
SK_InitFinishCallback (void)
{
  bool rundll_invoked =
    (StrStrIW (SK_GetHostApp (), L"Rundll32") != nullptr);

  if (rundll_invoked || SK_IsSuperSpecialK () || *__SK_BootedCore == L'\0')
  {
    init_mutex->unlock ();
    return;
  }


  SK_DeleteTemporaryFiles ();
  SK_DeleteTemporaryFiles (L"Version", L"*.old");

  SK::Framerate::Init ();

  gsl::not_null <SK_ICommandProcessor *> cp (
    SK_GetCommandProcessor ()
  );

  extern int32_t SK_D3D11_amount_to_purge;
  cp->AddVariable (
    "VRAM.Purge",
      new SK_IVarStub <int32_t> (
        (int32_t *)&SK_D3D11_amount_to_purge
      )
  );

  cp->AddVariable (
    "GPU.StatPollFreq",
      new SK_IVarStub <float> (
        &config.gpu.interval
      )
  );

  cp->AddVariable (
    "ImGui.FontScale",
      new SK_IVarStub <float> (
        &config.imgui.scale
      )
  );

  SK_InitRenderBackends ();

  cp->AddCommand ("mem",       new skMemCmd    ());
  cp->AddCommand ("GetUpdate", new skUpdateCmd ());


  //
  // Game-Specific Stuff that I am not proud of
  //
  switch (SK_GetCurrentGameID ())
  {
#ifdef _WIN64
    case SK_GAME_ID::DarkSouls3:
      SK_DS3_InitPlugin ();
      break;
    case SK_GAME_ID::LEGOCityUndercover:
      SK_LCU_InitPlugin ();
      break;
#else
    case SK_GAME_ID::Tales_of_Zestiria:
      SK_GetCommandProcessor ()->ProcessCommandFormatted (
        "TargetFPS %f",
          config.render.framerate.target_fps
      );
      break;
#endif
  }


  // Get rid of the game output log if the user doesn't want it...
  if (! config.system.game_output)
  {
    game_debug->close ();
    game_debug->silent = true;
  }


  const wchar_t* config_name =
    SK_GetBackend ();

  // Use a generic "SpecialK" name instead of the primary wrapped/hooked API name
  //   for this DLL when it is injected at run-time rather than a proxy DLL.
  if (SK_IsInjected ())
    config_name = L"SpecialK";

  SK_SaveConfig (config_name);

  SK_Console::getInstance ()->Start ();

  // SEH to handle Wine Stub functions
  SK_SEH_InitFinishCallback ();

  SK_EnableApplyQueuedHooks ();
  SK_ApplyQueuedHooks       ();

  dll_log->LogEx (false, L"------------------------------------------------"
                         L"-------------------------------------------\n" );
  dll_log->Log   (       L"[ SpecialK ] === Initialization Finished! ===   "
                         L"       (%6.2f ms)",
                           SK_DeltaPerfMS ( SK_GetInitParams ().start_time.QuadPart, 1 )
                 );
  dll_log->LogEx (false, L"------------------------------------------------"
                         L"-------------------------------------------\n" );

  init_mutex->unlock ();
}


DWORD
WINAPI
CheckVersionThread (LPVOID)
{
  SetCurrentThreadDescription (                   L"[SK] Auto-Update Worker"   );
  SetThreadPriority           ( SK_GetCurrentThread (), THREAD_PRIORITY_LOWEST );

  // If a local repository is present, use that.
  if (GetFileAttributes (LR"(Version\installed.ini)") == INVALID_FILE_ATTRIBUTES)
  {
    if (SK_FetchVersionInfo (L"SpecialK"))
    {
      // ↑ Check, but ↓ don't update unless running the global injector version
      if ( (SK_IsInjected () && (! SK_IsSuperSpecialK ())) )
      {
        SK_UpdateSoftware (L"SpecialK");
      }
    }
  }

  SK_Thread_CloseSelf ();

  return 0;
}


extern void SK_CPU_InstallHooks               (void);
extern void SK_Display_SetMonitorDPIAwareness (bool bOnlyIfWin10);

void BasicInit (void)
{
  // Setup unhooked function pointers
  SK_PreInitLoadLibrary            (    );
  SK::Diagnostics::Debugger::Allow (true);

  if (config.system.handle_crashes)
    SK::Diagnostics::CrashHandler::Init   (    );
  SK::Diagnostics::CrashHandler::InitSyms (    );

  SK::EOS::Init (false);

  //// Do this from the startup thread [these functions queue, but don't apply]
  if (! config.input.dont_hook_core)
  {
    // TODO: Split this into Keyboard hooks and Mouse hooks
    //         and allow picking them individually
    SK_Input_PreInit        (); // Hook only symbols in user32 and kernel32
  }
  SK_HookWinAPI             ();
  SK_CPU_InstallHooks       ();
  SK_NvAPI_PreInitHDR       ();
  SK_NvAPI_InitializeHDR    ();

  ////// For the global injector, when not started by SKIM, check its version
  ////if ( (SK_IsInjected () && (! SK_IsSuperSpecialK ())) )
  ////  SK_Thread_Create ( CheckVersionThread );

  if (config.dpi.disable_scaling)   SK_Display_DisableDPIScaling      (     );
  if (config.dpi.per_monitor.aware) SK_Display_SetMonitorDPIAwareness (false);

  SK_File_InitHooks    ();
  SK_Network_InitHooks ();

  if (config.system.display_debug_out)
    SK::Diagnostics::Debugger::SpawnConsole ();

  // Steam Overlay and SteamAPI Manipulation
  //
  if (! config.platform.silent)
  {
    config.steam.force_load_steamapi = false;

    // TODO: Rename -- this initializes critical sections and performance
    //                   counters needed by SK's SteamAPI back end.
    //
    SK_Steam_InitCommandConsoleVariables  ();

    extern const wchar_t*
      SK_Steam_GetDLLPath (void);

    ///// Lazy-load SteamAPI into a process that doesn't use it; this brings
    /////   a number of general-purpose benefits (such as battery charge monitoring).
    bool kick_start = config.steam.force_load_steamapi;

    if (kick_start || (! SK_Steam_TestImports (SK_GetModuleHandle (nullptr))))
    {
      // Implicitly kick-start anything in SteamApps\common that does not import
      //   SteamAPI...
      if ((! kick_start) && config.steam.auto_inject)
      {
        if (StrStrIW (SK_GetHostPath (), LR"(SteamApps\common)") != nullptr)
        {
          // Only do this if the game doesn't have a copy of the DLL lying around somewhere,
          //   because if we init Special K's SteamAPI DLL, the game's will fail to init and
          //     the game won't be happy about that!
          kick_start =
            (! SK_Modules->LoadLibrary (SK_Steam_GetDLLPath ())) ||
                  config.steam.force_load_steamapi;
        }
      }

      if (kick_start)
        SK_Steam_KickStart (SK_Steam_GetDLLPath ());
    }

    SK_Steam_PreHookCore ();
  }

  if (SK_COMPAT_IsFrapsPresent ())
      SK_COMPAT_UnloadFraps ();
}

DWORD
WINAPI
DllThread (LPVOID user)
{
  WriteULongNoFence (&dwInitThreadId, SK_Thread_GetCurrentId ());

  SetCurrentThreadDescription (                 L"[SK] Primary Initialization Thread" );
  SetThreadPriority           ( SK_GetCurrentThread (), THREAD_PRIORITY_HIGHEST       );
  SetThreadPriorityBoost      ( SK_GetCurrentThread (), TRUE                          );

  auto* params =
    static_cast <init_params_s *> (user);

  SK_InitCore ( params->backend,
                params->callback );

  AppId64_t appid (
    SK_Steam_GetAppID_NoAPI ()
  );

  if (SK_IsInjected ())
  {
    DWORD dwPid =
      GetCurrentProcessId ();

    auto *pInjectMeta =
      SK_Inject_GetRecord (dwPid);

    if (pInjectMeta != nullptr)
    {
      pInjectMeta->process.id           = dwPid;
      pInjectMeta->platform.steam_appid = appid;

      SK_Inject_AuditRecord ( dwPid, pInjectMeta,
                            sizeof (*pInjectMeta) );
    }
  }


  if (ReadAcquire (&__SK_Init) == FALSE)
  {
    if (! InterlockedCompareExchangeAcquire (&__SK_Init, TRUE, FALSE))
    {
      SK_D3D_SetupShaderCompiler ();

      WritePointerRelease ( (volatile PVOID *)(&hInitThread),
                              INVALID_HANDLE_VALUE );
      WriteULongRelease   (                       &dwInitThreadId,
                              0                    );

      // Load user-defined DLLs (Lazy)
      SK_RunLHIfBitness ( 64, SK_LoadLazyImports64 (),
                              SK_LoadLazyImports32 () );

      if (config.system.handle_crashes)
        SK::Diagnostics::CrashHandler::Reinstall ();
    }
  }

  return 0;
}


enum SK_File_SearchStopCondition {
  FirstMatchFound,
  AllMatchesFound,
  LastMatchFound
};

std::unordered_set <std::wstring>
SK_RecursiveFileSearchEx ( const wchar_t* wszDir,
                           const wchar_t* wszSearchExt,
 std::unordered_set <std::wstring_view>& cwsFileNames,
       std::vector        <
         std::pair          < std::wstring, bool >
                          >&&             preferred_dirs = { },
              SK_File_SearchStopCondition stop_condition = FirstMatchFound )
{
  std::unordered_set <
    std::wstring
  > found_set;

  for ( auto& preferred : preferred_dirs )
  {
    if ( preferred.second == false )
    {
      preferred.second = true;

      const DWORD dwAttribs  =
        GetFileAttributesW (preferred.first.c_str ());

      if (  dwAttribs != INVALID_FILE_ATTRIBUTES &&
          !(dwAttribs &  FILE_ATTRIBUTE_DIRECTORY) )
      {
        found_set.emplace (preferred.first);

        if (stop_condition == FirstMatchFound)
        {
          return found_set;
        }
      }

      PathRemoveFileSpec (
        preferred.first.data ()
      );

      std::unordered_set <std::wstring>&& recursive_finds =
        SK_RecursiveFileSearchEx ( preferred.first.c_str (), wszSearchExt,
                                                            cwsFileNames,
                                                       { }, stop_condition );

      if (! recursive_finds.empty () )
      {
        if (stop_condition == FirstMatchFound)
        {
          return recursive_finds;
        }
      }
    }
  }

  wchar_t   wszPath [MAX_PATH + 2] = { };
  swprintf (wszPath, LR"(%s\*)", wszDir);

  WIN32_FIND_DATA fd          = {   };
  HANDLE          hFind       =
    FindFirstFileW ( wszPath, &fd);

  if (hFind == INVALID_HANDLE_VALUE) { return found_set; }

  std::vector <std::wstring> dirs_to_traverse;

  do
  {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      if ( *fd.cFileName == L'.' )
      {
        const wchar_t* wszNext =
          CharNextW (fd.cFileName);

        if ( (*wszNext == L'.'  ) ||
             (*wszNext == L'\0' )    )
        {
        //dll_log.Log (L"%ws\\%ws is a special directory", wszDir, fd.cFileName);
          continue;
        }
      }

      dirs_to_traverse.emplace_back (fd.cFileName);
    }

    wchar_t wszFoundFile      [MAX_PATH + 2] = { };
    wchar_t wszFoundFileLower [MAX_PATH + 2] = { };
    wchar_t wszFoundExtension [     16     ] = { };

    if ( _wsplitpath_s (
           fd.cFileName, nullptr, 0,
                         nullptr, 0,
                         wszFoundFile, MAX_PATH,
                         wszFoundExtension, 16
                       ) != 0
       )
    {
    //dll_log.Log (L"%ws did not split", fd.cFileName);
      continue;
    }

    if (_wcsicmp (wszFoundExtension, wszSearchExt) != 0)
    {
    //dll_log.Log (L"%ws is wrong extension    { Search Extension: %ws, First File To Check: %ws }",
    //             wszFoundExtension, wszSearchExt, cwsFileNames.begin ()->c_str () );
      continue;
    }

    wcscpy (
      wszFoundFileLower,
        wszFoundFile
    );

    wchar_t* pwszFileLower = wszFoundFileLower;
    while ( *pwszFileLower != L'\0' )
    {       *pwszFileLower = towlower  (*pwszFileLower);
             pwszFileLower = CharNextW ( pwszFileLower);
    }

    if (! cwsFileNames.count (wszFoundFileLower))
    {
      //dll_log.Log (L"%ws is not contained  { Search Extension: %ws, First File To Check: %ws }",
      //             wszFoundFileLower, wszSearchExt, cwsFileNames.begin ()->c_str () );

      continue;
    }

    else
    {
      std::wstring found (wszDir);
                   found += LR"(\)";
                   found += wszFoundFile;
                   found += wszFoundExtension;

      found_set.emplace (found);

      //dll_log.Log (L"Add to Found Set: %s", found.c_str ());

      if (stop_condition == FirstMatchFound)
      {
        FindClose (hFind);
        return found_set;
      }
    }
  } while (FindNextFile (hFind, &fd));

  FindClose (hFind);


  for ( auto& dir : dirs_to_traverse )
  {
    wchar_t   wszDescend [MAX_PATH + 2] = { };
    swprintf (wszDescend, LR"(%s\%s)", wszDir, dir.c_str () );

    const std::unordered_set <std::wstring>&& recursive_finds =
      SK_RecursiveFileSearchEx ( wszDescend,  wszSearchExt,
                                             cwsFileNames,
                                         { }, stop_condition );

    if (! recursive_finds.empty ())
    {
      for ( auto& found_entry : recursive_finds )
      {
        found_set.emplace (found_entry);

        if (stop_condition == FirstMatchFound)
          return found_set;
      }
    }
  }


  return found_set;
}

std::wstring
SK_RecursiveFileSearch ( const wchar_t* wszDir,
                         const wchar_t* wszFile )
{
  dll_log->Log ( L"Recursive File Search for '%ws', beginning in '%ws'",
                   SK_ConcealUserDir (std::wstring (wszFile).data ()),
                   SK_ConcealUserDir (std::wstring (wszDir).data  ()) );

  std::wstring extension (
    PathFindExtensionW     (wszFile)
                         ),
               filename    (wszFile);

  PathRemoveExtensionW (filename.data ());
  PathFindFileNameW    (filename.data ());

  std::unordered_set <std::wstring_view> file_pattern = {
    filename.data ()
  };

  const std::unordered_set <std::wstring> matches =
    SK_RecursiveFileSearchEx (
      wszDir, extension.c_str (),
        file_pattern, {{wszFile, false}}, FirstMatchFound
    );

  if (! matches.empty ())
  {
    dll_log->Log ( L"Success!  [%ws]",
                   SK_ConcealUserDir (std::wstring (*matches.begin ()).data ()) );
  }

  else
  {
    dll_log->Log ( L"No Such File [%ws] Exists",
                   SK_ConcealUserDir (std::wstring (*matches.begin ()).data ()) );
  }

  return matches.empty () ?
                      L"" : *matches.begin ();
}


bool
__stdcall
SK_HasGlobalInjector (void)
{
  static int
      last_test = 0;
  if (last_test == 0)
  {
    wchar_t     wszBasePath [MAX_PATH + 2] = { };
    wcsncpy_s ( wszBasePath, MAX_PATH,
          SK_GetInstallPath (), _TRUNCATE );

    lstrcatW (wszBasePath, SK_RunLHIfBitness ( 64, L"SpecialK64.dll",
                                                   L"SpecialK32.dll" ));

    bool result = (GetFileAttributesW (wszBasePath) != INVALID_FILE_ATTRIBUTES);
    last_test   = result ?
                       1 : -1;
  }

  return (last_test != -1);
}

extern std::pair <std::queue <DWORD>, BOOL> __stdcall SK_BypassInject (void);

const wchar_t*
__stdcall
SK_GetDebugSymbolPath (void)
{
  static volatile LONG    __init                            = 0;
  static          wchar_t wszDbgSymbols [MAX_PATH * 3 + 1] = { };

  if (ReadAcquire (&__init) == 2)
    return wszDbgSymbols;

  if (! InterlockedCompareExchange (&__init, 1, 0))
  {
    if (! crash_log->initialized)
    {
      crash_log->flush_freq = 0;
      crash_log->lockless   = true;
      crash_log->init       (L"logs/crash.log", L"wt+,ccs=UTF-8");
    }


    std::filesystem::path symbol_file =
      SK_GetModuleFullName (skModuleRegistry::Self ());

    wchar_t wszSelfName    [MAX_PATH + 2] = { };
    wchar_t wszGenericName [MAX_PATH + 2] = { };
    wcsncpy_s ( wszSelfName,           MAX_PATH,
                 symbol_file.c_str (), _TRUNCATE );


    PathRemoveExtensionW (wszSelfName);
    lstrcatW             (wszSelfName, L".pdb");

              wcsncpy_s ( wszGenericName, MAX_PATH,
                          wszSelfName,    _TRUNCATE );

    PathRemoveFileSpecW ( wszGenericName );
    PathAppendW         ( wszGenericName,
      SK_RunLHIfBitness ( 64, L"SpecialK64.pdb",
                              L"SpecialK32.pdb" ) );

    bool generic_symbols =
      ( INVALID_FILE_ATTRIBUTES !=
          GetFileAttributesW (wszGenericName) );

    if (generic_symbols)
    {
      symbol_file = wszGenericName;
    }

    else
    {
      symbol_file = wszSelfName;
    }

    // Not the correct way of validating the existence of this DLL's symbol file,
    //   but it is good enough for now ...
    if ( (! generic_symbols)     &&
         INVALID_FILE_ATTRIBUTES == GetFileAttributesW (wszSelfName) )
    {
      static wchar_t
           wszCurrentPath [MAX_PATH * 3 + 1] = { };
      if (*wszCurrentPath == L'\0')
      {
        SymGetSearchPathW ( GetCurrentProcess (),
                              wszCurrentPath,
                                MAX_PATH * 3 );
      }

      std::wstring dir (wszCurrentPath); dir += L";";
                   dir.append (SK_GetInstallPath ());

      wcsncpy_s ( wszDbgSymbols,  MAX_PATH * 3,
                    dir.c_str (), _TRUNCATE );

      symbol_file  = SK_GetInstallPath ();
      symbol_file /=
        SK_RunLHIfBitness ( 64, L"SpecialK64.pdb",
                                L"SpecialK32.pdb" );
    }

    else
    {
      static wchar_t                           wszCurrentPath [MAX_PATH * 3 + 1] = { };
      SymGetSearchPathW (GetCurrentProcess (), wszCurrentPath, MAX_PATH * 3);

      wcsncpy_s ( wszDbgSymbols,  MAX_PATH * 3,
                  wszCurrentPath, _TRUNCATE );
    }


    // Strip the username from the logged path
    static wchar_t wszDbgSymbolsEx  [MAX_PATH * 3 + 1] = { };
       wcsncpy_s ( wszDbgSymbolsEx,  MAX_PATH * 3,
                      wszDbgSymbols, _TRUNCATE );

    SK_ConcealUserDir (wszDbgSymbolsEx);
    crash_log->Log (L"DebugHelper Symbol Search Path......: %ws", wszDbgSymbolsEx);

    std::wstring
      stripped (symbol_file);
      stripped =
        SK_ConcealUserDir (stripped.data ());


    if (GetFileAttributesW (symbol_file.c_str ()) != INVALID_FILE_ATTRIBUTES)
    {
      crash_log->Log ( L"Special K Debug Symbols Loaded From.: %ws",
                         stripped.c_str () );
    }

    else
    {
      crash_log->Log ( L"Unable to load Special K Debug Symbols ('%ws'), "
                       L"crash log will not be accurate.",
                         stripped.c_str () );
    }

    wcsncpy_s ( wszDbgSymbols,        MAX_PATH * 3,
                symbol_file.c_str (), _TRUNCATE );

    PathRemoveFileSpec (wszDbgSymbols);

    InterlockedIncrementRelease (&__init);
  }

  else
    SK_Thread_SpinUntilAtomicMin (&__init, 2);

  return
    wszDbgSymbols;
}

void
__stdcall
SK_EstablishRootPath (void)
{
  wchar_t   wszConfigPath [MAX_PATH + 2] = { };
  GetCurrentDirectory     (MAX_PATH,
            wszConfigPath);
  lstrcatW (wszConfigPath, LR"(\)");

  // File permissions don't permit us to store logs in the game's directory,
  //   so implicitly turn on the option to relocate this stuff.
  if (! SK_File_CanUserWriteToPath (wszConfigPath))
  {
    config.system.central_repository = true;
  }

  RtlSecureZeroMemory (
    wszConfigPath, sizeof (wchar_t) * (MAX_PATH + 2)
  );

  if (SK_GetModuleName (SK_GetDLL ())._Equal (
    SK_RunLHIfBitness (64, L"SpecialK64.dll",
                           L"SpecialK32.dll")))
  {
    try {
      std::filesystem::path path =
        SK_GetModuleFullName (SK_GetDLL ());

      wcsncpy_s ( SKX_GetInstallPath (), MAX_PATH,
                    path.parent_path ().wstring ().
                                          c_str (), _TRUNCATE );
    }

    catch (const std::exception& e)
    {
      std::ignore = e;
#ifdef DEBUG
      _debugbreak ();
#endif
    }
  }

  if (*SK_GetInstallPath () == L'\0')
  {
    swprintf ( SKX_GetInstallPath (), LR"(%s\My Mods\SpecialK)",
               SK_GetDocumentsDir ().c_str () );
  }

  // Store config profiles in a centralized location rather than
  //   relative to the game's executable
  //
  //   * Currently, this location is always Documents\My Mods\SpecialK\
  //
  if (config.system.central_repository)
  {
    if (! SK_IsSuperSpecialK ())
    {
      wcsncpy_s (SKX_GetRootPath    (),  MAX_PATH,
                 SKX_GetInstallPath (), _TRUNCATE);
    }

    else
    {
      GetCurrentDirectory (MAX_PATH, SKX_GetRootPath ());
    }

    wcsncpy_s ( wszConfigPath,      MAX_PATH,
                SKX_GetRootPath (), _TRUNCATE  );
    lstrcatW  ( wszConfigPath, LR"(\Profiles\)");
    lstrcatW  ( wszConfigPath, SK_GetHostApp ());
  }


  // Relative to game's executable path
  //
  else
  {
    if (! SK_IsSuperSpecialK ())
    {
      wcsncpy_s ( SKX_GetRootPath (), MAX_PATH,
                  SK_GetHostPath  (), _TRUNCATE );
    }

    else
    {
      GetCurrentDirectory (MAX_PATH, SKX_GetRootPath ());
    }

    wcsncpy_s ( wszConfigPath,      MAX_PATH,
                SKX_GetRootPath (), _TRUNCATE );
  }


  // Not using the ShellW API because at this (init) stage,
  //   we can only reliably use Kernel32 functions.
  lstrcatW (SKX_GetRootPath (), LR"(\)");
  lstrcatW (wszConfigPath,      LR"(\)");

  SK_SetConfigPath (wszConfigPath);
}

bool
__stdcall
SK_StartupCore (const wchar_t* backend, void* callback)
{
 try
 {
  InstructionSet::deferredInit ();

  // If Global Injection Delay, block initialization thread until the delay period ends
  if (SK_IsInjected () && config.system.global_inject_delay > 0.0f)
  {
    struct packaged_params_s {
      std::wstring backend  = L""; // Persistent copy
      void*        callback =                nullptr;
      HANDLE       thread   =   INVALID_HANDLE_VALUE;
      HANDLE       parent   =   INVALID_HANDLE_VALUE;
      DWORD        resume   =                      0; // Number of times we Suspended
      CONTEXT      ctx      = {                    };
    } static  delay_params  { backend,  callback };

    if (delay_params.thread == INVALID_HANDLE_VALUE)
    {
      if (! DuplicateHandle ( GetCurrentProcess (), SK_TLS_Bottom ()->debug.handle,
                                nullptr,              &delay_params.parent,
                                  THREAD_ALL_ACCESS, FALSE, DUPLICATE_CLOSE_SOURCE |
                                                            DUPLICATE_SAME_ACCESS ) )
      {             delay_params.parent  = INVALID_HANDLE_VALUE; }
      if ((intptr_t)delay_params.parent <= 0)
                    delay_params.parent  = INVALID_HANDLE_VALUE;

      delay_params.thread =
      SK_Thread_CreateEx ([](LPVOID) -> DWORD
      {
        DWORD dwMilliseconds =
          sk::narrow_cast <DWORD> (config.system.global_inject_delay * 1000.0f);

        SK_SleepEx (dwMilliseconds, FALSE);

        if (delay_params.parent != INVALID_HANDLE_VALUE)
        {
          DWORD dwCount =
            SuspendThread (    delay_params.parent );

          if ( dwCount >= 0 &&
               dwCount < MAXIMUM_SUSPEND_COUNT )
          {
            if ( GetThreadContext (
                               delay_params.parent,
                              &delay_params.ctx   )
               )             ++delay_params.resume;
            else ResumeThread (delay_params.parent);
          }
        }

        SK_StartupCore        (delay_params.backend.c_str (),
                               delay_params.callback);

        if (delay_params.parent != INVALID_HANDLE_VALUE)
        {
          if (                 delay_params.resume > 0 )
          {
            if ( SetThreadContext (
                               delay_params.parent,
                              &delay_params.ctx   )
               ) ResumeThread (delay_params.parent);
          } else OutputDebugStringA (
             "[SK Init] Unable to Restore Suspended Thread Context");

          SK_CloseHandle (delay_params.parent);
        }

        SK_Thread_CloseSelf ();

        return 0;
      }, L"[SK] Global Injection Delay", &delay_params);

      return true;
    }
  }

  __SK_BootedCore = backend;

  // Before loading any config files, test the game's environment variables
  //  to determine if the Steam client has given us the AppID without having
  //    to initialize SteamAPI first.
  SK_Steam_GetAppID_NoAPI ();


  // Allow users to centralize all files if they want
  //
  //   Stupid hack, if the application is running with a different working-directory than
  //     the executable -- compensate!
  wchar_t          wszCentralPathVFile [MAX_PATH + 2] = { };
  SK_PathCombineW (wszCentralPathVFile, SK_GetHostPath (), L"SpecialK.central");

  if ( SK_IsInjected   () ||
       PathFileExistsW (wszCentralPathVFile) )
  {
    config.system.central_repository = true;
  }

  SK_EstablishRootPath ();
  SK_CreateDirectories (SK_GetConfigPath ());

  ///SK_Config_CreateSymLinks ();

  const bool rundll_invoked =
    (StrStrIW (SK_GetHostApp (), L"Rundll32") != nullptr);
  const bool skim           =
    rundll_invoked || SK_IsSuperSpecialK () || __SK_RunDLL_Bypass;

  __SK_bypass |= skim;


  auto& init_ =
    SKX_GetInitParams ();

  init_          = {     };
  init_.backend  = backend;
  init_.callback = callback;

  SK_SetBackend (backend);

  bool blacklist = false;

  // Injection Compatibility Menu
  if ( (sk::narrow_cast <USHORT> (SK_GetAsyncKeyState (VK_SHIFT  )) & 0x8000) != 0 &&
       (sk::narrow_cast <USHORT> (SK_GetAsyncKeyState (VK_CONTROL)) & 0x8000) != 0 )
  {
    WriteRelease (&__SK_Init, -1);
                   __SK_bypass = true;

    SK_BypassInject ();
  }

  else
  {
    blacklist =
      SK_IsInjected    () &&
       PathFileExistsW (SK_GetBlacklistFilename ());

    if (SK_GetCurrentGameID () == SK_GAME_ID::EasyAntiCheat)
      blacklist = true;

    if (! blacklist)
    {
      // Initialize MinHook before loading config file; required for some plug-ins
      SK_MinHook_Init ();

      wchar_t                log_fname [MAX_PATH + 2] = { };
      SK_PathCombineW      ( log_fname, L"logs",
                                SK_IsInjected () ?
                                        L"SpecialK" : backend);
      PathAddExtensionW ( log_fname,    L".log" );

      dll_log->init (log_fname, L"wS+,ccs=UTF-8");
      dll_log->Log  ( L"%s.log created\t\t(Special K  %s,  %hs)",
                        SK_IsInjected () ?
                             L"SpecialK" : backend,
                          SK_GetVersionStrW (),
                            __DATE__ );

      init_.start_time =
        SK_QueryPerf ();

      game_debug->init (L"logs/game_output.log", L"w");
      game_debug->lockless = true;
    }
  }


  if (skim || blacklist)
  {
    return true;
  }


  budget_log->init ( LR"(logs\dxgi_budget.log)", L"wc+,ccs=UTF-8" );

  dll_log->LogEx (false,
    L"------------------------------------------------------------------------"
    L"-------------------\n");

  std::wstring   module_name   = SK_GetModuleName (SK_GetDLL ());
  const wchar_t* wszModuleName = module_name.c_str ();

  dll_log->Log ( L">> (%s) [%s] <<",
                   SK_GetHostApp (),
                     wszModuleName );

  const wchar_t* config_name = backend;

  if (SK_IsInjected ())
    config_name = L"SpecialK";

  LARGE_INTEGER liStartConfig =
    SK_CurrentPerf ();

  dll_log->LogEx ( true, L"Loading user preferences from %s.ini... ",
                     config_name );
  if (SK_LoadConfig (config_name))
  {
    dll_log->LogEx ( false, L"done! (%6.2f ms)\n",
      SK_DeltaPerfMS (liStartConfig.QuadPart, 1) );
  }

  else
  {
    dll_log->LogEx (false, L"failed!\n");

    std::filesystem::path default_global_name (
      SK_FormatStringW (
        LR"(%ws\Global\default_%ws)",
          SK_GetInstallPath (), config_name ) );

    std::wstring default_name (
      SK_FormatStringW ( L"%s%s%s",
                           SK_GetConfigPath (),
                             L"default_",
                               config_name )
    );

    std::wstring default_global_ini (default_global_name.wstring () + L".ini");
    std::wstring default_ini        (default_name                   + L".ini");

    if (GetFileAttributesW (default_global_ini.c_str ()) != INVALID_FILE_ATTRIBUTES)
    {
      dll_log->LogEx ( true,
                       L"Loading global default preferences from %s.ini... ",
                         default_global_name.c_str () );

      if (! SK_LoadConfig (default_global_name))
        dll_log->LogEx (false, L"failed!\n");
      else
        dll_log->LogEx (false, L"done!\n");
    }

    if (GetFileAttributesW (default_ini.c_str ()) != INVALID_FILE_ATTRIBUTES)
    {
      dll_log->LogEx ( true,
                       L"Loading default preferences from %s.ini... ",
                         default_name.c_str () );

      if (! SK_LoadConfig (default_name))
        dll_log->LogEx (false, L"failed!\n");
      else
        dll_log->LogEx (false, L"done!\n");
    }

    // If no INI file exists, write one immediately.
    dll_log->LogEx (true,  L"  >> Writing base INI file, because none existed... ");

    // Fake a frame so that the config file writes
    WriteULong64Release
                   (&SK_RenderBackend::frames_drawn, 1);
    SK_SaveConfig  (config_name);
    SK_LoadConfig  (config_name);
    WriteULong64Release
                   (&SK_RenderBackend::frames_drawn, 0);

    dll_log->LogEx (false, L"done!\n");
  }

  SK_RunOnce (SK_Display_HookModeChangeAPIs ());
  SK_RunOnce (SK_ApplyQueuedHooks           ());

  dll_log->LogEx (false,
    L"----------------------------------------------------------------------"
    L"---------------------\n");

  // If the module name is this, then we need to load the system-wide DLL...
  wchar_t     wszProxyName [MAX_PATH + 2] = { };
  wcsncpy_s ( wszProxyName, MAX_PATH,
              backend,     _TRUNCATE );
  PathAddExtensionW (
              wszProxyName, L".dll"  );

#ifndef _WIN64
  //
  // TEMP HACK: dgVoodoo2
  //
  if (SK_GetDLLRole () == DLL_ROLE::D3D8)
  {
    swprintf (wszProxyName, LR"(%s\PlugIns\ThirdParty\dgVoodoo\d3d8.dll)",
                              SK_GetInstallPath ());
  }

  else if (SK_GetDLLRole () == DLL_ROLE::DDraw)
  {
    swprintf (wszProxyName, LR"(%s\PlugIns\ThirdParty\dgVoodoo\ddraw.dll)",
                              SK_GetInstallPath ());
  }
#endif


  wchar_t wszBackendDLL  [MAX_PATH + 2] = { }, wszBackendPriv [MAX_PATH + 2] = { };
  wchar_t wszWorkDir     [MAX_PATH + 2] = { }, wszWorkDirPriv [MAX_PATH + 2] = { };

  wcsncpy_s ( wszBackendDLL,             MAX_PATH,
              SK_GetSystemDirectory (), _TRUNCATE );
  wcsncpy_s ( wszBackendPriv,            MAX_PATH,
              wszBackendDLL,            _TRUNCATE );

  GetCurrentDirectoryW (                 MAX_PATH,
              wszWorkDir                          );
  wcsncpy_s ( wszWorkDirPriv,            MAX_PATH,
              wszWorkDir,               _TRUNCATE );

  dll_log->Log (L" Working Directory:          %s", SK_ConcealUserDir (wszWorkDirPriv));
  dll_log->Log (L" System Directory:           %s", SK_ConcealUserDir (wszBackendPriv));

  PathAppendW       ( wszBackendDLL, backend );
  PathAddExtensionW ( wszBackendDLL, L".dll" );

  wchar_t* dll_name = wszBackendDLL;

  if (SK_Path_wcsicmp (wszProxyName, wszModuleName))
           dll_name =  wszProxyName;

  bool load_proxy = false;

  if (! SK_IsInjected ())
  {
    for (auto& import : imports->imports)
    {
      if (            import.role != nullptr &&
           backend == import.role->get_value () )
      {
        dll_log->LogEx (true, L" Loading proxy %s.dll:    ", backend);
        wcsncpy_s ( dll_name,                                MAX_PATH,
                    import.filename->get_value ().c_str (), _TRUNCATE );

        load_proxy = true;
        break;
      }
    }
  }

  if (! load_proxy)
    dll_log->LogEx (true, L" Loading default %s.dll: ", backend);

  // Pre-Load the original DLL into memory
  if (dll_name != wszBackendDLL)
  {
    SK_Modules->LoadLibraryLL (wszBackendDLL);
  }

  backend_dll =
    SK_Modules->LoadLibraryLL (dll_name);

  if (backend_dll != nullptr)
    dll_log->LogEx (false, L" (%s)\n",         SK_ConcealUserDir (dll_name));
  else
    dll_log->LogEx (false, L" FAILED (%s)!\n", SK_ConcealUserDir (dll_name));

  dll_log->LogEx (false,
    L"----------------------------------------------------------------------"
    L"---------------------\n");


  if (! __SK_bypass)
  {
    void SK_D3D11_InitMutexes (void);
         SK_D3D11_InitMutexes (    );

    extern void SK_ImGui_Init (void);
                SK_ImGui_Init (    );

#ifndef _THREADED_BASIC_INIT
    BasicInit ();
#endif

    SK_EnumLoadedModules (SK_ModuleEnum::PreLoad);
  }

  if (config.system.silent)
  {        dll_log->silent = true;

    std::wstring log_fnameW (
      SK_IsInjected () ? L"SpecialK"
                       : backend
    );           log_fnameW += L".log";
    DeleteFile ( log_fnameW.c_str () );
  }

  else
    dll_log->silent = false;

  if (! __SK_bypass)
  {
    switch (SK_GetCurrentGameID ())
    {
#ifdef _M_AMD64
      case SK_GAME_ID::StarOcean4:
        extern bool                     SK_SO4_PlugInCfg (void);
        plugin_mgr->config_fns.emplace (SK_SO4_PlugInCfg);
        break;

      case SK_GAME_ID::AssassinsCreed_Odyssey:
        extern void
        SK_ACO_PlugInInit (void);
        SK_ACO_PlugInInit (    );
        break;
      case SK_GAME_ID::MonsterHunterWorld:
        extern void
        SK_MHW_PlugInInit (void);
        SK_MHW_PlugInInit (    );
        break;

      case SK_GAME_ID::DragonQuestXI:
        extern void
        SK_DQXI_PlugInInit (void);
        SK_DQXI_PlugInInit (    );
        break;

      case SK_GAME_ID::Shenmue:
        extern void
        SK_SM_PlugInInit (void);
        SK_SM_PlugInInit (    );
        break;

      case SK_GAME_ID::FinalFantasyXV:
        extern void
        SK_FFXV_InitPlugin (void);
        SK_FFXV_InitPlugin (    );
        break;

      case SK_GAME_ID::PillarsOfEternity2:
        extern bool                     SK_POE2_PlugInCfg (void);
        plugin_mgr->config_fns.emplace (SK_POE2_PlugInCfg);
        break;

      case SK_GAME_ID::LifeIsStrange_BeforeTheStorm:
        extern bool                     SK_LSBTS_PlugInCfg (void);
        plugin_mgr->config_fns.emplace (SK_LSBTS_PlugInCfg);
        break;

      case SK_GAME_ID::Okami:
        extern bool                     SK_Okami_PlugInCfg (void);
        plugin_mgr->config_fns.emplace (SK_Okami_PlugInCfg);
        break;

      case SK_GAME_ID::Yakuza0:
      case SK_GAME_ID::YakuzaKiwami:
      case SK_GAME_ID::YakuzaKiwami2:
      case SK_GAME_ID::YakuzaUnderflow:
        SK_Yakuza0_PlugInInit ();
        break;
#else
      case SK_GAME_ID::Persona4:
        SK_Persona4_InitPlugin ();
        break;
#endif

#ifdef _M_AMD64
      case SK_GAME_ID::NieR_Sqrt_1_5:
        extern void
        SK_NIER_RAD_InitPlugin (void);
        SK_NIER_RAD_InitPlugin (    );

        extern bool                     SK_NIER_RAD_PlugInCfg (void);
        plugin_mgr->config_fns.emplace (SK_NIER_RAD_PlugInCfg);
        break;

      case SK_GAME_ID::FinalFantasy7Remake:
        extern void SK_FF7R_InitPlugin (void);
                    SK_FF7R_InitPlugin ();
        break;

      case SK_GAME_ID::EldenRing:
        break;
#endif
    }

    extern void SK_Widget_InitHDR (void);
    SK_RunOnce (SK_Widget_InitHDR ());

    SK_ImGui_Widgets->hdr_control->run ();

    bool gl   = false, vulkan = false, d3d9  = false, d3d11 = false, d3d12 = false,
         dxgi = false, d3d8   = false, ddraw = false, glide = false;

    SK_TestRenderImports (
      SK_GetModuleHandle (nullptr),
        &gl, &vulkan,
          &d3d9, &dxgi, &d3d11, &d3d12,
            &d3d8, &ddraw, &glide );

    dxgi  |= SK_GetModuleHandle (L"dxgi.dll")     != nullptr;
    d3d11 |= SK_GetModuleHandle (L"d3d11.dll")    != nullptr;
    d3d12 |= SK_GetModuleHandle (L"d3d12.dll")    != nullptr;
    d3d9  |= SK_GetModuleHandle (L"d3d9.dll")     != nullptr;
    gl    |= SK_GetModuleHandle (L"OpenGL32.dll") != nullptr;
    gl    |= SK_GetModuleHandle (L"gdi32.dll")    != nullptr;
    gl    |= SK_GetModuleHandle (L"gdi32full.dll")!= nullptr;

    if ( ( dxgi || d3d11 || d3d12 ||
           d3d8 || ddraw ) && ( config.apis.dxgi.d3d11.hook
                             || config.apis.dxgi.d3d12.hook ) )
    {
      SK_DXGI_QuickHook ();
    }

    if (d3d9 && (config.apis.d3d9.hook || config.apis.d3d9ex.hook))
    {
      SK_D3D9_QuickHook ();
    }

    if (config.steam.preload_overlay)
    {
      SK_Steam_LoadOverlayEarly ();
    }

    InterlockedExchangePointer (
      const_cast <void **> (&hInitThread),
      SK_Thread_CreateEx ( DllThread, nullptr,
                               &init_ )
    ); // Avoid the temptation to wait on this thread
  }

  return true;
 }
 catch (const std::exception& e)
 {
   SK_LOG0 ( ( L"Exception '%hs' during StartupCore (...)", e.what () ),
               L" SpecialK " );
   return false;
 }
}


struct SK_DummyWindows {
  struct window_s {
    HWND hWnd   = 0;
    BOOL active = FALSE;

    struct {
      HWND    hWnd    = 0;
      BOOL    unicode = FALSE;
      WNDPROC wndproc = nullptr;
    } parent;
  };

  std::unordered_map <HWND, window_s> list;
  std::set           <HWND>           unique;
  std::recursive_mutex                lock;
};

SK_LazyGlobal <SK_DummyWindows> dummy_windows;

LRESULT
WINAPI
ImGui_WndProcHandler (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT
WINAPI
DummyWindowProc (_In_  HWND   hWnd,
                 _In_  UINT   uMsg,
                 _In_  WPARAM wParam,
                 _In_  LPARAM lParam)
{
  SK_DummyWindows::window_s* pWindow = nullptr;

  {
    std::scoped_lock <std::recursive_mutex> auto_lock (dummy_windows->lock);

    if (dummy_windows->unique.count (hWnd))
      pWindow = &dummy_windows->list [hWnd];
  }

  if (pWindow != nullptr && IsWindow (pWindow->parent.hWnd))
  {
    MSG msg;
    msg.hwnd    = pWindow->parent.hWnd;
    msg.message = uMsg;
    msg.lParam  = lParam;
    msg.wParam  = wParam;

    bool SK_ImGui_HandlesMessage (MSG * lpMsg, bool /*remove*/, bool /*peek*/);
    if  (SK_ImGui_HandlesMessage (&msg, false, false))
      return ImGui_WndProcHandler (pWindow->parent.hWnd, uMsg, wParam, lParam);
  }

  return
    DefWindowProcW (hWnd, uMsg, wParam, lParam);
};

HWND
SK_Win32_CreateDummyWindow (HWND hWndParent)
{
  std::scoped_lock <std::recursive_mutex>
         auto_lock (dummy_windows->lock);

  static WNDCLASSW wc = {
    CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
    DummyWindowProc,
    0x00, 0x00,
    SK_GetDLL        (                           ),
    LoadIcon         (nullptr, IDI_APPLICATION   ),
    nullptr,
    static_cast <HBRUSH> (
      GetStockObject (         NULL_BRUSH        )
                         ),
    nullptr,
    L"Special K Dummy Window Class"
  };
  static WNDCLASSW wc_existing = { };

  if ( RegisterClassW (&wc) ||
       GetClassInfo   ( SK_GetDLL (),
                          L"Special K Dummy Window Class",
                            &wc_existing ) )
  {
    RECT                              rect = { };
    GetWindowRect (game_window.hWnd, &rect);

    HWND hWnd =
      CreateWindowExW ( WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY,
                            L"Special K Dummy Window Class",
                            L"Special K Dummy Window",
                            //IsWindow (hWndParent) ? WS_CHILD : WS_CLIPSIBLINGS,
                                    WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                              rect.left,               rect.top,
                                 rect.right - rect.left, rect.bottom - rect.top,
                        HWND_DESKTOP/*hWndParent*/, nullptr,
                                    SK_GetDLL (),   nullptr );

    if (hWnd != SK_HWND_DESKTOP)
    {
      if (dummy_windows->unique.emplace (hWnd).second)
      {
        SK_DummyWindows::window_s window;
        window.hWnd        = hWnd;
        window.active      = true;
        window.parent.hWnd = hWndParent;

        window.parent.unicode = IsWindowUnicode            (window.parent.hWnd);
        window.parent.wndproc = (WNDPROC)GetWindowLongPtrW (window.parent.hWnd, GWLP_WNDPROC);

        dummy_windows->list [hWnd] = window;

        if (hWndParent != 0 && IsWindow (hWndParent))
        {
          SK_Thread_CreateEx ([](LPVOID user)->DWORD
          {
            HWND hWnd = (HWND)user;

          //SetForegroundWindow (hWnd);
          //SetFocus            (hWnd);
          //SetActiveWindow     (hWnd);
            ShowWindow          (hWnd, SW_HIDE);

            MSG                 msg = { };
            while (GetMessage (&msg, 0, 0, 0))
            {
              TranslateMessage (&msg);
              DispatchMessage  (&msg);

              if (msg.message == WM_DESTROY && msg.hwnd == hWnd)
              {
                SK_Win32_CleanupDummyWindow (hWnd);
                break;
              }
            }

            SK_Thread_CloseSelf ();

            return 0;
          }, L"[SK] Dummy Window Proc", (LPVOID)hWnd);
        }
      }
    }

    else
      SK_ReleaseAssert (!"CreateDummyWindow Failed");

    return hWnd;
  }

  else
  {
    dll_log->Log (L"Window Class Registration Failed!");
  }

  return nullptr;
}

void
SK_Win32_CleanupDummyWindow (HWND hwnd)
{
  std::scoped_lock <std::recursive_mutex>
         auto_lock (dummy_windows->lock);

  std::vector <HWND> cleaned_windows;

  if (dummy_windows->list.count (hwnd))
  {
    auto& window = dummy_windows->list [hwnd];

    if (DestroyWindow (window.hWnd))
    {
      //if (IsWindow (     window.parent.hWnd))
      //  SetActiveWindow (window.parent.hWnd);
    }
    cleaned_windows.emplace_back (window.hWnd);
  }

  else if (hwnd == 0)
  {
    for (auto& it : dummy_windows->list)
    {
      if (DestroyWindow (it.second.hWnd))
      {
        //if (it.second.parent.hWnd != 0 && IsWindow (it.second.parent.hWnd))
        //  SetActiveWindow (it.second.parent.hWnd);
      }
      cleaned_windows.emplace_back (it.second.hWnd);
    }
  }

  for ( auto& it : cleaned_windows )
    if (dummy_windows->list.count (it))
        dummy_windows->list.erase (it);

  if (dummy_windows->list.empty ())
    UnregisterClassW ( L"Special K Dummy Window Class", SK_GetDLL () );
}

bool
__stdcall
SK_ShutdownCore (const wchar_t* backend)
{
  if (SK_IsInjected ())
    SK_Inject_BroadcastExitNotify ();

  SK_DisableApplyQueuedHooks ();

  if (        __SK_DLL_TeardownEvent != nullptr)
    SetEvent (__SK_DLL_TeardownEvent);

  // Fast path for DLLs that were never really attached.
  extern __time64_t
        __SK_DLL_AttachTime;
  if (! __SK_DLL_AttachTime)
  {
    if (dll_log.isAllocated ())
        dll_log->Log (L"SK_ShutdownCore (...) on uninitialized DLL");

    SK_MinHook_UnInit ();

    return
      true;
  }

  if (__SK_bypass)
    return true;

  SK_PrintUnloadedDLLs (&dll_log.get ());

  dll_log->LogEx ( false,
                L"========================================================="
                L"========= (End  Unloads) ================================"
                L"==================================\n" );

  dll_log->Log (L"[ SpecialK ] *** Initiating DLL Shutdown ***");

  if (config.render.dxgi.temporary_dwm_hdr)
  {
    for ( auto pHDROutput : SK_GetCurrentRenderBackend ().hdr_enabled_displays )
    {
      extern void SK_Display_DisableHDR (SK_RenderBackend_V2::output_s *pOutput);
                  SK_Display_DisableHDR (pHDROutput);
    }
  }

  if (config.display.resolution.applied)
  {
    SK_ChangeDisplaySettingsEx (
      nullptr, nullptr,
        0, CDS_RESET, nullptr  );
  }

  dll_log->LogEx    (true, L"[ ETWTrace ] Shutting down ETW Trace Providers...         ");

  DWORD dwTime =
       SK_timeGetTime ();

  if (SK_ETW_EndTracing ())
    dll_log->LogEx  (false, L"done! (%4u ms)\n",            SK_timeGetTime () - dwTime); else
    dll_log->LogEx  (false, L"fail! (%4u ms -> Timeout)\n", SK_timeGetTime () - dwTime);

  const wchar_t* config_name = backend;

  if (SK_IsInjected ())
  {
    config_name = L"SpecialK";
  }

  if (sk::NVAPI::app_name != L"ds3t.exe")
  {
    dll_log->LogEx       (true,  L"[ SpecialK ] Saving user preferences to"
                                 L" %10s.ini... ", config_name);
    dwTime =
          SK_timeGetTime (           );
    SK_SaveConfig        (config_name);
    dll_log->LogEx       (false, L"done! (%4u ms)\n", SK_timeGetTime () - dwTime);
  }


  dll_log->LogEx    (true, L"[   ImGui  ] Shutting down ImGui...                       ");

  dwTime =
    SK_timeGetTime ();

  if (ImGui::GetCurrentContext () != nullptr)
  {
    ImGui::Shutdown (
      ImGui::GetCurrentContext ()
    );
  }

  dll_log->LogEx    (false, L"done! (%4u ms)\n",            SK_timeGetTime () - dwTime);

  extern bool __SKX_WinHook_InstallInputHooks (HWND hWnd);
              __SKX_WinHook_InstallInputHooks (nullptr);

  SK_Win32_CleanupDummyWindow ();

  // No more exit rumble, please :)
  SK_XInput_Enable (FALSE);

  if (config.window.background_mute)
    SK_SetGameMute (false);

  if (config.window.confine_cursor)
    SK_ClipCursor (nullptr);


  // These games do not handle resolution correctly
  switch (SK_GetCurrentGameID ())
  {
    case SK_GAME_ID::DarkSouls3:
    case SK_GAME_ID::Fallout4:
    case SK_GAME_ID::FinalFantasyX_X2:
    case SK_GAME_ID::DisgaeaPC:
      ChangeDisplaySettingsA_Original (nullptr, CDS_RESET);
      break;

#ifdef _M_AMD64
    case SK_GAME_ID::MonsterHunterWorld:
    {
      extern void SK_MHW_PlugIn_Shutdown (void);
                  SK_MHW_PlugIn_Shutdown ();
    } break;
#endif
  }

  SK_AutoClose_LogEx (game_debug, game);
  SK_AutoClose_LogEx (dll_log,    dll);

  if (SK_GetFramesDrawn () > 0)
  {
    // If an override was applied at runtime, reset it
    SK_Steam_ForceInputAppId (0);

    extern HWND SK_Inject_GetFocusWindow (void);
    extern void SK_Inject_SetFocusWindow (HWND hWndFocus);
    if (SK_Inject_GetFocusWindow (       ) == game_window.hWnd)
        SK_Inject_SetFocusWindow (nullptr);

    if (config.system.return_to_skif)
    {
      HWND hWndExisting =
        FindWindow (L"SK_Injection_Frontend", nullptr);

      if (hWndExisting != nullptr && IsWindow (hWndExisting))
      {
        DWORD                                    dwPid = 0x0;
        GetWindowThreadProcessId (hWndExisting, &dwPid);

        if ( dwPid != 0x0 &&
             dwPid != GetCurrentProcessId () )
        {
#define WM_SKIF_REPOSITION WM_USER + 0x4096

          PostMessage              (hWndExisting, WM_SKIF_REPOSITION, 0x0, 0x0);
          SetForegroundWindow      (hWndExisting);

          if (SK_Inject_GetFocusWindow (            ) == nullptr)
              SK_Inject_SetFocusWindow (hWndExisting);

          ShowWindow                   (hWndExisting, SW_NORMAL);
        }
      }
    }
  }

  SK::DXGI::ShutdownBudgetThread ();

  dll_log->LogEx    (true, L"[ GPU Stat ] Shutting down Performance Monitor Thread...  ");

  dwTime =
    SK_timeGetTime ();
  SK_EndGPUPolling ();

  dll_log->LogEx    (false, L"done! (%4u ms)\n", SK_timeGetTime () - dwTime);

  SK_Console::getInstance ()->End ();

  auto ShutdownWMIThread =
  []( volatile HANDLE&  hSignal,
      volatile HANDLE&  hThread,
        const wchar_t* wszName )
  {
    if ( ReadPointerAcquire (&hSignal) == INVALID_HANDLE_VALUE &&
         ReadPointerAcquire (&hThread) == INVALID_HANDLE_VALUE )
    {
      return;
    }

    wchar_t   wszFmtName [32] = { };
    lstrcatW (wszFmtName, wszName);
    lstrcatW (wszFmtName, L"...");

    dll_log->LogEx (true, L"[ Perfmon. ] Shutting down %-30s ", wszFmtName);

    DWORD dwTime_WMIShutdown =
         SK_timeGetTime ();

    if (hThread != INVALID_HANDLE_VALUE)
    {
      // Signal the thread to shutdown
      if ( hSignal == INVALID_HANDLE_VALUE ||
            SignalObjectAndWait (hSignal, hThread, 66UL, FALSE)
                        != WAIT_OBJECT_0 )  // Give 66 milliseconds, and
      {                                     // then we're killing
        SK_TerminateThread (hThread, 0x00); // the thing!
                            hThread = INVALID_HANDLE_VALUE;
      }

      if (hThread != INVALID_HANDLE_VALUE)
      {
        SK_CloseHandle (hThread);
                       hThread = INVALID_HANDLE_VALUE;
      }
    }

    if (hSignal != INVALID_HANDLE_VALUE)
    {
      SK_CloseHandle (hSignal);
                     hSignal = INVALID_HANDLE_VALUE;
    }

    dll_log->LogEx ( false, L"done! (%4u ms)\n",
                    SK_timeGetTime () - dwTime_WMIShutdown );
  };

  static auto& cpu_stats      = *SK_WMI_CPUStats;
  static auto& disk_stats     = *SK_WMI_DiskStats;
  static auto& pagefile_stats = *SK_WMI_PagefileStats;

  ShutdownWMIThread (cpu_stats .hShutdownSignal,
                     cpu_stats.hThread,               L"CPU Monitor"     );
  ShutdownWMIThread (disk_stats.hShutdownSignal,
                     disk_stats.hThread,              L"Disk Monitor"    );
  ShutdownWMIThread (pagefile_stats.hShutdownSignal,
                     pagefile_stats.hThread,          L"Pagefile Monitor");

  PowerSetActiveScheme (
    nullptr,
      &config.cpu.power_scheme_guid_orig
  );

  if (! SK_Debug_IsCrashing ())
  {
    SK_UnloadImports        ();
    SK::Framerate::Shutdown ();

    dll_log->LogEx             (true, L"[ WMI Perf ] Shutting down WMI WbemLocator...             ");
    dwTime = SK_timeGetTime    ();
    SK_WMI_Shutdown            ();
    dll_log->LogEx             (false, L"done! (%4u ms)\n", SK_timeGetTime () - dwTime);

    if (! config.platform.silent)
    {
      dll_log->LogEx           (true, L"[ SpecialK ] Shutting down SteamAPI integration...        ");
      dwTime = SK_timeGetTime  ();
      SK::SteamAPI::Shutdown   ();
      dll_log->LogEx           (false, L"done! (%4u ms)\n", SK_timeGetTime () - dwTime);
    }

    // Due to hooks on NvAPI methods, best to just leave the library loaded
#if 0
    if (nvapi_init)
    {
      dll_log->LogEx           (true, L"[  NV API  ] Unloading NVAPI Library...                   ");
      dwTime = SK_timeGetTime  ();
      sk::NVAPI::UnloadLibrary ();
      dll_log->LogEx           (false, L"done! (%4u ms)\n", SK_timeGetTime () - dwTime);
    }
#endif

    dll_log->LogEx             (true, L"[ SpecialK ] Shutting down MinHook...                     ");

    dwTime = SK_timeGetTime    ();
    SK_MinHook_UnInit          ();
    dll_log->LogEx             (false, L"done! (%4u ms)\n", SK_timeGetTime () - dwTime);

    LoadLibraryW_Original   = nullptr;
    LoadLibraryA_Original   = nullptr;
    LoadLibraryExA_Original = nullptr;
    LoadLibraryExW_Original = nullptr;
    FreeLibrary_Original    = nullptr;
    SleepEx_Original        = nullptr;

    // ... Many, many more...
  }


  dll_log->Log (L"[ SpecialK ] Custom %s.dll Detached (pid=0x%04x)",
    backend, GetCurrentProcessId ());


  if (! SK_Debug_IsCrashing ())
  {
    if (config.system.handle_crashes)
      SK::Diagnostics::CrashHandler::Shutdown ();
  }

  WriteRelease (&__SK_Init, -2);

#ifdef _SK_CONSISTENCY_CHECK
  // Teardown static globals
  __SK_LazyRiver.atExit ();
#endif

  return true;
}

void
SK_FrameCallback ( SK_RenderBackend& rb,
                   ULONG64           frames_drawn =
                                       SK_GetFramesDrawn () )
{
  switch (frames_drawn)
  {
    // First frame
    //
    case 0:
    {
      // Notify anything that was waiting for injection into this game
      if (SK_IsInjected ())
        SK_Inject_BroadcastInjectionNotify ();

      wchar_t *wszDescription = nullptr;

      if ( SUCCEEDED ( GetCurrentThreadDescription (&wszDescription)) &&
                                            wcslen ( wszDescription))
      {
        if (StrStrIW (wszDescription, L"[GAME] Primary Render Thread") == nullptr)
        {
          SetCurrentThreadDescription (
            SK_FormatStringW ( L"[GAME] Primary Render < %s >",
                       wszDescription ).c_str ()
                                      );
        }
      }

      else
      {
        SetCurrentThreadDescription (L"[GAME] Primary Render Thread");
      }

      SK_LocalFree (wszDescription);


      extern SK_Widget* SK_HDR_GetWidget (void);

      if ( (static_cast <int> (rb.api) & static_cast <int> (SK_RenderAPI::D3D11)) ||
           (static_cast <int> (rb.api) & static_cast <int> (SK_RenderAPI::D3D12)) )
      {
        auto hdr =
          SK_HDR_GetWidget ();

        if (hdr != nullptr)
            hdr->run ();
      }

      // Maybe make this into an option, but for now just get this the hell out
      //   of there almost no software should be shipping with FP exceptions,
      //     it causes compatibility problems.
      _controlfp (MCW_EM, MCW_EM);


      // Load user-defined DLLs (Late)
      SK_RunLHIfBitness ( 64, SK_LoadLateImports64 (),
                              SK_LoadLateImports32 () );

      if (ReadAcquire64 (&SK_SteamAPI_CallbackRunCount) < 1)
      {
        // Steam Init: Better late than never

        SK_Steam_TestImports (SK_GetModuleHandle (nullptr));
      }

      if (config.system.handle_crashes)
        SK::Diagnostics::CrashHandler::Reinstall ();

      __target_fps = config.render.framerate.target_fps;
    } break;


    // Grace period frame, just ignore it
    //
    case 1:
    case 2:
      break;


    // 2+ frames drawn
    //
    default:
    {

      if (game_window.WndProc_Original != nullptr)
      {
        // If user wants position / style overrides, kick them off on the first
        //   frame after a window procedure has been established.
        //
        //  (nb: Must be implemented asynchronously)
        //
        SK_RunOnce (SK_Window_RepositionIfNeeded ());
        SK_RunOnce (game_window.active |= (SK_GetForegroundWindow () == game_window.hWnd));
      }

      // Delayed Init  (Elden Ring vs. Flawless Widescreen compat hack)
      if (frames_drawn > 15)
      {
#ifdef _WIN64
        static bool bEldenRing =
          (SK_GetCurrentGameID () == SK_GAME_ID::EldenRing);

        if (bEldenRing)
          SK_RunOnce (SK_ER_InitPlugin ());
#else
        static bool bChronoCross =
          (SK_GetCurrentGameID () == SK_GAME_ID::ChronoCross);

        if (bChronoCross)
        {
          static auto base =
            SK_Debug_GetImageBaseAddr ();

          auto _ApplyPatch = [&]()->void
          {
            if (memcmp ((((uint8_t*)base) + 0x7116F), "\x83\x46\x14\x02", 4) == 0)
            {
              SK_LOG0 ((L"Chrono Cross Patches applied at +7116Fh"), L"ChronoCros");

              DWORD                                                           dwOldProt;
              VirtualProtect (
                      ((uint8_t*)base) + 0x7116F, 4, PAGE_EXECUTE_READWRITE, &dwOldProt);
              memcpy (((uint8_t*)base) + 0x7116F, "\x83\x46\x14\x01", 4);
              VirtualProtect (
                      ((uint8_t*)base) + 0x7116F, 4, dwOldProt,              &dwOldProt);

              VirtualProtect (
                      ((uint8_t*)base) + 0x71B16, 4, PAGE_EXECUTE_READWRITE, &dwOldProt);
              memcpy (((uint8_t*)base) + 0x71B16, "\x83\x46\x14\x01", 4);
              VirtualProtect (
                      ((uint8_t*)base) + 0x71B16, 4, dwOldProt,              &dwOldProt);

              //83 46 14 02 to 83 46 14 01
            }
          };

          SK_RunOnce (_ApplyPatch ());
        }
#endif
      }
    } break;
  }
}

std::atomic_int __SK_RenderThreadCount = 0;

void
SK_MMCS_EndBufferSwap (void)
{
  auto* task  =
    SK_MMCS_GetTaskForThreadIDEx (
      SK_Thread_GetCurrentId (), "[GAME] Render Thread",
                                  "Games", "DisplayPostProcessing" );

  if (task != nullptr)
      task->disassociateWithTask ();

//SK_Thread_SetCurrentPriority (
//  SK_TLS_Bottom ()->win32->thread_prio
//);
}

void
SK_MMCS_BeginBufferSwap (void)
{
//SK_TLS_Bottom ()->win32->thread_prio =
//  SK_Thread_GetCurrentPriority (                             );
//  SK_Thread_SetCurrentPriority (THREAD_PRIORITY_TIME_CRITICAL);

  static concurrency::concurrent_unordered_set <DWORD> render_threads;

  static const auto&
    game_id = SK_GetCurrentGameID ();

  auto tid =
    SK_Thread_GetCurrentId ();

  SK_MMCS_TaskEntry* task = nullptr;

  if ( ! render_threads.count (tid) )
  {
    __SK_RenderThreadCount++;

    static bool first = true;
                task  =
      SK_MMCS_GetTaskForThreadIDEx ( tid,                       first ?
                                       "[GAME] Primary Render Thread" :
                                       "[GAME] Ancillary Render Thread",
                                        "Games", "DisplayPostProcessing" );

    if ( task != nullptr )
    {
      if (game_id != SK_GAME_ID::AssassinsCreed_Odyssey)
      {
        //if (first)
          task->setPriority (AVRT_PRIORITY_CRITICAL);
        //else
          //task->setPriority (AVRT_PRIORITY_HIGH); // Assymetric priority is not desirable
      }

      else
        task->setPriority (AVRT_PRIORITY_LOW);

      render_threads.insert (tid);

      first = false;
    }
  }

  else
  {
    task =
      SK_MMCS_GetTaskForThreadIDEx ( tid, "[GAME] Render Thread",
                                           "Games", "DisplayPostProcessing" );

    if (task != nullptr)
        task->reassociateWithTask ();
  }
}

__declspec (noinline)
void
__stdcall
SK_BeginBufferSwapEx (BOOL bWaitOnFail)
{
  if (config.render.framerate.enable_mmcss)
  {
    SK_MMCS_BeginBufferSwap ();
  }

  static auto& rb =
    SK_GetCurrentRenderBackend ();

  static SK_RenderAPI LastKnownAPI =
    SK_RenderAPI::Reserved;

  if ( (int)rb.api        &
       (int)SK_RenderAPI::D3D11 )
  {
    SK_D3D11_BeginFrame ();
  }

  else if ( (int)rb.api &
            (int)SK_RenderAPI::D3D12 )
  {
    SK_D3D12_BeginFrame ();
  }

  rb.driverSleepNV      (0);
  rb.setLatencyMarkerNV (RENDERSUBMIT_END);

  if (config.render.framerate.enforcement_policy == 0 && rb.swapchain.p != nullptr)
  {
    SK::Framerate::Tick ( bWaitOnFail, 0.0, { 0,0 }, rb.swapchain.p );
  }

  rb.present_staging.begin_overlays.time.QuadPart =
    SK_QueryPerf ().QuadPart;

  // Invoke any plug-in's frame begin callback
  for ( auto begin_frame_fn : plugin_mgr->begin_frame_fns )
  {
    begin_frame_fn ();
  }

  // Handle init. actions that depend on the number of frames
  //   drawn...
  SK_FrameCallback (rb);

  if (config.render.framerate.enforcement_policy == 1 && rb.swapchain.p != nullptr)
  {
    SK::Framerate::Tick ( bWaitOnFail, 0.0, { 0,0 }, rb.swapchain.p );
  }

  if (SK_Steam_PiratesAhoy () && (! SK_ImGui_Active ()))
  {
    SK_ImGui_Toggle ();
  }

  LastKnownAPI =
    rb.api;

  rb.present_staging.submit.time =
    SK_QueryPerf ();

  if (config.render.framerate.enforcement_policy == 4)
  {
    SK::Framerate::Tick ( bWaitOnFail, 0.0, { 0,0 }, rb.swapchain.p );
  }
}

__declspec (noinline)
void
__stdcall
SK_BeginBufferSwap (void)
{
  return
    SK_BeginBufferSwapEx (TRUE);
}

// Todo, move out of here
void
SK_Input_PollKeyboard (void)
{
  constexpr ULONGLONG poll_interval = 1ULL;

  //
  // Do not poll the keyboard while the game window is inactive
  //
  bool skip = true;

  if ( SK_IsGameWindowActive () )
    skip = false;

  if (skip)
    return;

  static ULONGLONG last_osd_scale { 0ULL };
  static ULONGLONG last_poll      { 0ULL };
  static ULONGLONG last_drag      { 0ULL };

  SYSTEMTIME    stNow;
  FILETIME      ftNow;
  LARGE_INTEGER ullNow;

  GetSystemTime        (&stNow);
  SystemTimeToFileTime (&stNow, &ftNow);

  ullNow.HighPart = ftNow.dwHighDateTime;
  ullNow.LowPart  = ftNow.dwLowDateTime;

  static bool toggle_drag = false;

  static const auto& io =
    ImGui::GetIO ();

  if ( io.KeysDown [VK_CONTROL] &&
       io.KeysDown [  VK_SHIFT] &&
       io.KeysDown [ VK_SCROLL] )
  {
    if (! toggle_drag)
    {
      config.window.drag_lock =
        (! config.window.drag_lock);
    }

    toggle_drag = true;

    if (config.window.drag_lock)
    {
      ClipCursor (nullptr);
    }
  }

  else
  {
    toggle_drag = false;
  }

  if (config.window.drag_lock)
  {
    SK_CenterWindowAtMouse (config.window.persistent_drag);
  }


  if (ullNow.QuadPart - last_osd_scale > 25ULL * poll_interval)
  {
    if (io.KeysDown [config.osd.keys.expand [0]] &&
        io.KeysDown [config.osd.keys.expand [1]] &&
        io.KeysDown [config.osd.keys.expand [2]])
    {
      last_osd_scale = ullNow.QuadPart;
      SK_ResizeOSD (+0.1f);
    }

    if (io.KeysDown [config.osd.keys.shrink [0]] &&
        io.KeysDown [config.osd.keys.shrink [1]] &&
        io.KeysDown [config.osd.keys.shrink [2]])
    {
      last_osd_scale = ullNow.QuadPart;
      SK_ResizeOSD (-0.1f);
    }
  }

  static bool toggle_time = false;
  if (io.KeysDown [config.time.keys.toggle [0]] &&
      io.KeysDown [config.time.keys.toggle [1]] &&
      io.KeysDown [config.time.keys.toggle [2]])
  {
    if (! toggle_time)
    {
      //
      // HACK: Test Achievement Unlock (TODO: REMOVE)
      //
      static bool
          bSteam = (SK::SteamAPI::AppID () != 0);
      if (bSteam)
        SK_Steam_UnlockAchievement (0);

      else if (SK::EOS::GetTicksRetired ( ) > 0)
               SK_EOS_UnlockAchievement (0);


      config.time.show =
        (! config.time.show);
    }

    toggle_time = true;
  }

  else
  {
    toggle_time = false;
  }
}


extern bool
  SK_Window_OnFocusChange ( HWND hWndNewTarget,
                            HWND hWndOld );

void
SK_BackgroundRender_EndFrame (void)
{
  static auto& rb =
    SK_GetCurrentRenderBackend ();

  static bool background_last_frame = false;
  static bool fullscreen_last_frame = false;
  static bool first_frame           = true;

  if (            first_frame ||
       (background_last_frame != config.window.background_render) )
  {
    first_frame = false;

    // Does not indicate the window was IN the background, but that it
    //   was rendering in a special mode that would allow the game to
    //     continue running while it is in the background.
    background_last_frame =
        config.window.background_render;
    if (config.window.background_render)
    {
      HWND     hWndGame  = SK_GetGameWindow ();
      LONG_PTR lpStyleEx =
        SK_GetWindowLongPtrW ( hWndGame,
                                 GWL_EXSTYLE );

      LONG_PTR lpStyleExNew =
            // Add style to ensure the game shows in the taskbar ...
        ( ( lpStyleEx |  WS_EX_APPWINDOW  )
                      & ~WS_EX_TOOLWINDOW );
                     // And remove one that prevents taskbar activation...

      ShowWindowAsync            ( hWndGame,
                                     SW_SHOW );

      if (lpStyleExNew != lpStyleEx)
      {
        SK_SetWindowLongPtrW     ( hWndGame,
                                     GWL_EXSTYLE,
                                       lpStyleExNew );
      }

      SK_RealizeForegroundWindow ( hWndGame );
    }
  }

  fullscreen_last_frame =
        rb.fullscreen_exclusive;
  if (! rb.fullscreen_exclusive)
  {
    static bool last_foreground = false;

    DWORD                           dwForegroundPid = 0x0;
    DWORD                           dwForegroundTid =
      GetWindowThreadProcessId (
        SK_GetForegroundWindow (), &dwForegroundPid);

  if (//dwForegroundTid!= GetCurrentThreadId  () &&
       dwForegroundPid != GetCurrentProcessId () )
  {
      GUITHREADINFO gti        = {                    };
                    gti.cbSize = sizeof (GUITHREADINFO);
      if ( GetGUIThreadInfo (       dwForegroundTid,
                                           &gti ) )
      {
        static HWND        hWndLastFocus  = 0;
        if (               hWndLastFocus != gti.hwndFocus) {
          SK_Window_OnFocusChange (         gti.hwndFocus,
            std::exchange (hWndLastFocus,   gti.hwndFocus)
          );
        }
      }

      if (std::exchange (last_foreground, false) && config.window.always_on_top < 1)
        SK_DeferCommand ("Window.TopMost false");
    }

  else
  {
    if (! std::exchange (last_foreground, true))
      SK_DeferCommand ("Window.TopMost true");
  }
  }
}

void
SK_SLI_UpdateStatus (IUnknown *device)
{
  if (config.sli.show && device != nullptr)
  {
    // Get SLI status for the frame we just displayed... this will show up
    //   one frame late, but this is the safest approach.
    if (nvapi_init && sk::NVAPI::CountSLIGPUs () > 0)
    {
      *SK_NV_sli_state =
        sk::NVAPI::GetSLIState (device);
    }
  }
}

__declspec (noinline) // lol
HRESULT
__stdcall
SK_EndBufferSwap (HRESULT hr, IUnknown* device, SK_TLS* pTLS)
{
  auto qpcTimeOfSwap =
    SK_QueryPerf ();

  static auto& rb =
    SK_GetCurrentRenderBackend ();

  rb.frame_delta.markFrame ();


  // Invoke any plug-in's frame end callback
  for ( auto end_frame_fn : plugin_mgr->end_frame_fns )
  {
    end_frame_fn ();
  }


  auto _FrameTick = [&](void) -> void
  {
    bool bWait =
      SUCCEEDED (hr);

    // Only implement waiting on successful Presents,
    //   unsuccessful Presents must return immediately
    SK::Framerate::Tick ( bWait, 0.0,
                        { 0,0 }, rb.swapchain.p );
    // Tock
  };

  if (config.render.framerate.enforcement_policy == 3 && rb.swapchain.p != nullptr)
  {
    _FrameTick ();
  }

  // Various required actions at the end of every frame in order to
  //   support the background render mode in most games.
  SK_BackgroundRender_EndFrame ();

  rb.updateActiveAPI ();

  // Determine Fullscreen Exclusive state in D3D9 / DXGI
  //
  SK_RenderBackendUtil_IsFullscreen ();

  if ( static_cast <int> (rb.api)  &
       static_cast <int> (SK_RenderAPI::D3D11) )
  {
    // Clear any resources we were tracking for the shader mod subsystem
    SK_D3D11_EndFrame (pTLS);
  }

  else if ( static_cast <int> (rb.api) &
            static_cast <int> (SK_RenderAPI::D3D12) )
  {
    SK_D3D12_EndFrame (pTLS);
  }

  // TODO: Add a per-frame callback for plug-ins, because this is stupid
  //
#ifdef _M_AMD64
  static const auto
          game_id = SK_GetCurrentGameID ();
  switch (game_id)
  {
    case SK_GAME_ID::Shenmue:
      extern volatile LONG  __SK_SHENMUE_FinishedButNotPresented;
      WriteRelease        (&__SK_SHENMUE_FinishedButNotPresented, 0L);
      break;
    case SK_GAME_ID::FinalFantasyXV:
      void SK_FFXV_SetupThreadPriorities (void);
           SK_FFXV_SetupThreadPriorities ();
      break;
    case SK_GAME_ID::FinalFantasy7Remake:
    {
      uintptr_t pBase =
        (uintptr_t)SK_Debug_GetImageBaseAddr ();

      static uint8_t orig_bytes_3304DC8 [6] = { 0x0 };
      static uint8_t orig_bytes_3304DD0 [6] = { 0x0 };
      static uint8_t orig_bytes_3304E03 [2] = { 0x0 };
      static uint8_t orig_bytes_1C0C42A [5] = { 0x0 };
      static uint8_t orig_bytes_1C0C425 [5] = { 0x0 };

      std::queue <DWORD> suspended;

      auto orig_se =
        SK_SEH_ApplyTranslator (SK_FilteringStructuredExceptionTranslator (EXCEPTION_ACCESS_VIOLATION));
      try
      {
        static bool        bPatchable    = false;
        static bool        bInit         = false;
        if (std::exchange (bInit, true) == false)
        {
          DWORD dwOldProtect = 0x0;

          //suspended =
          //  SK_SuspendAllOtherThreads ();

          VirtualProtect ((LPVOID)(pBase + 0x3304DC6), 2, PAGE_EXECUTE_READWRITE, &dwOldProtect);
          bPatchable = ( 0 == memcmp (
                          (LPVOID)(pBase + 0x3304DC6), "\x73\x3d", 2) );
          VirtualProtect ((LPVOID)(pBase + 0x3304DC6), 2, dwOldProtect,           &dwOldProtect);

          if (bPatchable)
          {
            VirtualProtect ((LPVOID)(pBase + 0x3304DC8), 6, PAGE_EXECUTE_READWRITE, &dwOldProtect);
            memcpy (                orig_bytes_3304DC8,
                            (LPVOID)(pBase + 0x3304DC8),                                        6);
            memcpy (        (LPVOID)(pBase + 0x3304DC8), "\x90\x90\x90\x90\x90\x90",            6);
            VirtualProtect ((LPVOID)(pBase + 0x3304DC8), 6, dwOldProtect,           &dwOldProtect);
            VirtualProtect ((LPVOID)(pBase + 0x3304DD0), 6, PAGE_EXECUTE_READWRITE, &dwOldProtect);
            memcpy (                orig_bytes_3304DD0,
                            (LPVOID)(pBase + 0x3304DD0),                                        6);
            memcpy (        (LPVOID)(pBase + 0x3304DD0), "\x90\x90\x90\x90\x90\x90",            6);
            VirtualProtect ((LPVOID)(pBase + 0x3304DD0), 6, dwOldProtect,           &dwOldProtect);

            VirtualProtect ((LPVOID)(pBase + 0x3304E03), 2, PAGE_EXECUTE_READWRITE, &dwOldProtect);
            memcpy (                orig_bytes_3304E03,
                            (LPVOID)(pBase + 0x3304E03),                                        2);
            memcpy (        (LPVOID)(pBase + 0x3304E03), "\x90\x90",                            2);
            VirtualProtect ((LPVOID)(pBase + 0x3304E03), 2, dwOldProtect,           &dwOldProtect);

#if 0
#if 1
            VirtualProtect ((LPVOID)(pBase + 0x1C0C42A), 5, PAGE_EXECUTE_READWRITE, &dwOldProtect);
            memcpy (                orig_bytes_1C0C42A,
                            (LPVOID)(pBase + 0x1C0C42A),                                        5);
            memcpy (        (LPVOID)(pBase + 0x1C0C42A), "\x90\x90\x90\x90\x90",                5);
            VirtualProtect ((LPVOID)(pBase + 0x1C0C42A), 5, dwOldProtect,           &dwOldProtect);
#endif
#if 1
            VirtualProtect ((LPVOID)(pBase + 0x1C0C425), 5, PAGE_EXECUTE_READWRITE, &dwOldProtect);
            memcpy (                orig_bytes_1C0C425,
                            (LPVOID)(pBase + 0x1C0C425),                                        5);
            memcpy (        (LPVOID)(pBase + 0x1C0C425), "\x90\x90\x90\x90\x90",                5);
            VirtualProtect ((LPVOID)(pBase + 0x1C0C425), 5, dwOldProtect,           &dwOldProtect);
#endif
#endif
          }
        }

        if (bPatchable && __target_fps > 0.0f)
        {
          **(float **)(pBase + 0x0590C750) =
                          __target_fps;
        }
      }

      catch (...)
      {
      }
      SK_SEH_RemoveTranslator (orig_se);

      //if (! suspended.empty ())
      //  SK_ResumeThreads (suspended);
    } break;
    default:
      break;
  }
#endif

  rb.updateActiveAPI (
    config.apis.last_known =
        rb.api
  );

  rb.driverSleepNV (1);

  if (config.render.framerate.enforcement_policy == 2)
  {
    _FrameTick ();
  }

  SK_RunOnce (
    SK::DXGI::StartBudgetThread_NoAdapter ()
  );

  SK_SLI_UpdateStatus   (device);
  SK_Input_PollKeyboard (      );

  // Always refresh at the beginning of a frame rather than the end,
  //   a failure event may cause a lengthy delay, missing VBLANK.
  SK_XInput_DeferredStatusChecks ();

  extern void SK_ScePad_PaceMaker (void);
              SK_ScePad_PaceMaker ();


  InterlockedIncrementAcquire (
    &SK_RenderBackend::frames_drawn
  );

  SK_StartPerfMonThreads ();


  if (pTLS == nullptr)
      pTLS =
    SK_TLS_Bottom ();

  ULONG64 ullFramesPresented =
    InterlockedIncrement (&pTLS->render->frames_presented);

#ifdef _RENDER_THREAD_TRANSIENCE
  if (ullFramesPresented > rb.most_frames)
  {
    InterlockedExchange ( &rb.most_frames,
                       ullFramesPresented );
    InterlockedExchange ( &rb.thread,
                           SK_Thread_GetCurrentId () );
  }

  InterlockedExchange ( &rb.last_thread,
                              SK_Thread_GetCurrentId () );
#else
  (void)ullFramesPresented;

  if (! ReadULongAcquire (&rb.thread))
  {
    InterlockedExchange ( &rb.thread,
                            SK_Thread_GetCurrentId () );
  }
#endif


  extern LONGLONG __SK_LatentSyncPostDelay;
  if (            __SK_LatentSyncPostDelay > 1LL)
  {
    SK_AutoHandle hTimer (
      INVALID_HANDLE_VALUE
    );

    extern void
    SK_Framerate_WaitUntilQPC (LONGLONG llQPC, HANDLE& hTimer);
    SK_Framerate_WaitUntilQPC (
      qpcTimeOfSwap.QuadPart + __SK_LatentSyncPostDelay,
                  hTimer.m_h  );
  }


  if (config.render.framerate.enable_mmcss)
  {
    SK_MMCS_EndBufferSwap ();
  }


#ifdef __UNRELIABLE_WINDOW_ACTIVATION_WORKAROUND
  // Handle missed window activation events; most commonly this is
  //   because of games that use multiple windows.
  DWORD                                                 dwProcess;
  GetWindowThreadProcessId (SK_GetForegroundWindow (), &dwProcess);

  void
  ActivateWindow ( HWND hWnd,
                   bool active          = false,
                   HWND hWndDeactivated = 0 );

  if (GetCurrentProcessId () == dwProcess)
  {
    if (! game_window.active)
    {
      ActivateWindow (game_window.hWnd, true);
    }
  }

  else
  {
    if (game_window.active)
    {
      ActivateWindow (game_window.hWnd, false);
    }
  }
#endif


#if 0
  MONITORINFO
    mi        = {                  };
    mi.cbSize = sizeof (MONITORINFO);

  if (rb.next_monitor != rb.monitor || FALSE == GetMonitorInfo (rb.monitor, &mi))
  {
    if (rb.next_monitor == rb.monitor)
        rb.monitor       = 0;

    SK_Window_RepositionIfNeeded ();
  }
#else
  if ( rb.next_monitor != nullptr &&
       rb.next_monitor != rb.monitor )
  {
    SK_Window_RepositionIfNeeded ();
  }
#endif


  return hr;
}

DLL_ROLE&
SKX_GetDLLRole (void)
{
  static DLL_ROLE dll_role (DLL_ROLE::INVALID);
  return          dll_role;
}

extern "C"
DLL_ROLE
__stdcall
SK_GetDLLRole (void)
{
  return SKX_GetDLLRole ();
}


void
__cdecl
SK_SetDLLRole (DLL_ROLE role)
{
  SKX_GetDLLRole () = role;
}


DWORD SK_GetParentProcessId (void) // By Napalm @ NetCore2K
{
  ULONG_PTR pbi    [6];
  ULONG     ulSize = 0;

  LONG (WINAPI *NtQueryInformationProcess)(HANDLE ProcessHandle,      ULONG ProcessInformationClass,
                                           PVOID  ProcessInformation, ULONG ProcessInformationLength,
                                           PULONG ReturnLength);
  *(FARPROC *)&NtQueryInformationProcess = SK_GetProcAddress (L"NtDll",
              "NtQueryInformationProcess");

  if (NtQueryInformationProcess != nullptr)
  {
    if ( NtQueryInformationProcess (
           SK_GetCurrentProcess (), 0, &pbi,
                                sizeof (pbi), &ulSize ) >= 0 &&
                                               ulSize   == sizeof (pbi)
       )
    {
      return
        (DWORD)pbi [5];
    }
  }

  return
    (DWORD)-1;
}

// Timeout after 500 ms
void
SK_WaitForParentToExit (void)
{
  SK_AutoHandle hProc (
    OpenProcess ( PROCESS_QUERY_INFORMATION, FALSE,
                    SK_GetParentProcessId () )
                );

  if (reinterpret_cast <intptr_t> (hProc.m_h) > 0)
  {
    for (int tries = 0 ; tries < 10 ; ++tries)
    {
      DWORD                           dwExitCode  = STILL_ACTIVE;
      GetExitCodeProcess (hProc.m_h, &dwExitCode);
      if (                            dwExitCode != STILL_ACTIVE)
      {
        break;
      }

      SK_SleepEx (50UL, FALSE);
    }
  }
}

// Stupid solution, but very effective way of re-launching a game as admin without
//   the Steam client throwing a temper tantrum.
void
CALLBACK
RunDLL_ElevateMe ( HWND  hwnd,        HINSTANCE hInst,
                   LPSTR lpszCmdLine, int       nCmdShow )
{
  UNREFERENCED_PARAMETER (hInst);

  hwnd     = HWND_DESKTOP;
  nCmdShow = SW_SHOWNORMAL;

  SK_WaitForParentToExit ();

  // Strip enclosing quotes
  if (*lpszCmdLine == '"')
     ++lpszCmdLine;

  std::string work_dir;
  char*      szArgs  = nullptr;
  char* szExecutable =
    StrStrIA (lpszCmdLine, ".exe");

  if (szExecutable != nullptr)
  {
    if (     *(szExecutable + 4) != '\0')
      szArgs = szExecutable + 5;

    *(szExecutable + 4) = '\0';

    work_dir =
      std::filesystem::path (lpszCmdLine).parent_path ().string ();
  }

  szExecutable = lpszCmdLine;

  if (szArgs == nullptr || (! StrStrIA (szArgs, "-epicapp=")))
  {
    SK_ShellExecuteA ( hwnd, "runas", szExecutable,
                         szArgs, work_dir.empty () ?
                                           nullptr :
                                 work_dir.c_str (), nCmdShow );
  }

  // Epic Game, needs launcher assistance to restart
  else
  {
    char *szAppId =
      StrStrIA (szArgs, "-epicapp=") + 9;
    char *szSpace =
      StrStrIA (szAppId, " ");

    if (szSpace != nullptr)
       *szSpace = '\0';

    const std::string epic_launch (
      SK_FormatString ("com.epicgames.launcher://apps/%s?action=launch&silent=true", szAppId)
    );

    SK_ShellExecuteA ( hwnd, "runas", epic_launch.c_str (), nullptr, nullptr, nCmdShow );
  }
}

void
CALLBACK
RunDLL_RestartGame ( HWND  hwnd,        HINSTANCE hInst,
                     LPSTR lpszCmdLine, int       nCmdShow )
{
  UNREFERENCED_PARAMETER (hInst);

  hwnd     = HWND_DESKTOP;
  nCmdShow = SW_SHOWNORMAL;

  SK_WaitForParentToExit ();

  // Strip enclosing quotes
  if (*lpszCmdLine == '"')
     ++lpszCmdLine;

  std::string work_dir;
  char*      szArgs  = nullptr;
  char* szExecutable =
    StrStrIA (lpszCmdLine, ".exe");

  if (szExecutable != nullptr)
  {
    if (     *(szExecutable + 4) != '\0')
      szArgs = szExecutable + 5;

    *(szExecutable + 4) = '\0';

    work_dir =
      std::filesystem::path (lpszCmdLine).parent_path ().string ();
  }

  szExecutable = lpszCmdLine;

  if (szArgs == nullptr || (! StrStrIA (szArgs, "-epicapp=")))
  {
    SK_ShellExecuteA ( hwnd, "open", szExecutable,
                         szArgs, work_dir.empty () ?
                                           nullptr :
                                 work_dir.c_str (), nCmdShow );
  }

  // Epic Game, needs launcher assistance to restart
  else
  {
    char *szAppId =
      StrStrIA (szArgs, "-epicapp=") + 9;
    char *szSpace =
      StrStrIA (szAppId, " ");

    if (szSpace != nullptr)
       *szSpace = '\0';

    const std::string epic_launch (
      SK_FormatString ("com.epicgames.launcher://apps/%s?action=launch&silent=true", szAppId)
    );

    SK_ShellExecuteA ( hwnd, "open", epic_launch.c_str (), nullptr, nullptr, nCmdShow );
  }
}


SK_LazyGlobal <SK_ImGui_WidgetRegistry> SK_ImGui_Widgets;

HANDLE
WINAPI
SK_CreateEvent (
  _In_opt_ LPSECURITY_ATTRIBUTES lpEventAttributes,
  _In_     BOOL                  bManualReset,
  _In_     BOOL                  bInitialState,
  _In_opt_ LPCWSTR               lpName )
{
  return
    CreateEventW (
      lpEventAttributes, bManualReset, bInitialState, lpName
    );
}

auto SK_Config_CreateSymLinks = [](void) ->
void
{
  if (config.system.central_repository)
  {
    // Create Symlink for end-user's convenience
    if ( GetFileAttributes ( ( std::wstring (SK_GetHostPath ()) +
                               std::wstring (LR"(\SpecialK\)")
                             ).c_str ()
                           ) == INVALID_FILE_ATTRIBUTES )
    {
      std::wstring link (SK_GetHostPath ());
                   link += LR"(\SpecialK\)";

      CreateSymbolicLink (
        link.c_str         (),
          SK_GetConfigPath (),
            SYMBOLIC_LINK_FLAG_DIRECTORY
      );
    }

    if ( GetFileAttributes ( ( std::wstring (SK_GetConfigPath ()) +
                               std::wstring (LR"(Game\)") ).c_str ()
                           ) == INVALID_FILE_ATTRIBUTES )
    {
      std::wstring link (SK_GetConfigPath ());
                   link += LR"(Game\)";

      CreateSymbolicLink (
        link.c_str         (),
          SK_GetHostPath   (),
            SYMBOLIC_LINK_FLAG_DIRECTORY
      );
    }
  }
};

bool
SK_API_IsDXGIBased (SK_RenderAPI api)
{
  switch (api)
  {
    case SK_RenderAPI::D3D10:
    case SK_RenderAPI::D3D11:
    case SK_RenderAPI::D3D12:
    case SK_RenderAPI::D3D8On11:
    case SK_RenderAPI::DDrawOn11:
    case SK_RenderAPI::GlideOn11:
      return true;
    default:
      return false;
  }
}

bool
SK_API_IsDirect3D9 (SK_RenderAPI api)
{
  switch (api)
  {
    case SK_RenderAPI::D3D9:
    case SK_RenderAPI::D3D9Ex:
      return true;
    default:
      return false;
  }
}

bool
SK_API_IsGDIBased (SK_RenderAPI api)
{
  switch (api)
  {
    case SK_RenderAPI::OpenGL:
      return true;
    default:
      return false;
  }
}

bool
SK_API_IsPlugInBased (SK_RenderAPI api)
{
  switch (api)
  {
    case SK_RenderAPI::DDraw:
    case SK_RenderAPI::DDrawOn11:
    case SK_RenderAPI::D3D8:
    case SK_RenderAPI::Glide:
    case SK_RenderAPI::GlideOn11:
      return true;
    default:
      return false;
  }
}

bool
SK_GetStoreOverlayState (bool bReal)
{
  return
    SK::SteamAPI::GetOverlayState (bReal) ||
    SK::EOS::     GetOverlayState (bReal);
}

SK_LazyGlobal <iSK_Logger> dll_log;
SK_LazyGlobal <iSK_Logger> crash_log;
SK_LazyGlobal <iSK_Logger> budget_log;
SK_LazyGlobal <iSK_Logger> game_debug;
SK_LazyGlobal <iSK_Logger> tex_log;
SK_LazyGlobal <iSK_Logger> steam_log;
SK_LazyGlobal <iSK_Logger> epic_log;