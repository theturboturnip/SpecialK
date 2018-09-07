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

#include <SpecialK/framerate.h>
#include <SpecialK/render/backend.h>

#include <SpecialK/log.h>
#include <SpecialK/config.h>
#include <SpecialK/command.h>
#include <SpecialK/core.h>
#include <SpecialK/hooks.h>
#include <SpecialK/window.h>

#include <d3d9.h>
#include <d3d11.h>
#include <atlbase.h>

#include <SpecialK/tls.h>
#include <SpecialK/thread.h>

SK::Framerate::Stats* frame_history  = nullptr;
SK::Framerate::Stats* frame_history2 = nullptr;

// Dispatch through the trampoline, rather than hook
//
using WaitForVBlank_pfn = HRESULT (STDMETHODCALLTYPE *)(
  IDXGIOutput *This
);
extern WaitForVBlank_pfn WaitForVBlank_Original;


SK::Framerate::EventCounter* SK::Framerate::events = nullptr;

LPVOID pfnQueryPerformanceCounter = nullptr;
LPVOID pfnSleep                   = nullptr;

Sleep_pfn                   Sleep_Original                   = nullptr;
QueryPerformanceCounter_pfn QueryPerformanceCounter_Original = nullptr;
QueryPerformanceCounter_pfn RtlQueryPerformanceCounter       = nullptr;

LARGE_INTEGER
SK_QueryPerf ()
{
  return SK_CurrentPerf ();
}

HANDLE hModSteamAPI = nullptr;

LARGE_INTEGER SK::Framerate::Stats::freq;

#include <SpecialK/utility.h>
#include <SpecialK/steam_api.h>

void
SK_Thread_WaitWhilePumpingMessages (DWORD dwMilliseconds)
{
  if (SK_Win32_IsGUIThread ())
  {
    HWND hWndThis = GetActiveWindow ();
    bool bUnicode =
      IsWindowUnicode (hWndThis);

    auto PeekAndDispatch =
    [&]
    {
      MSG msg     = {      };
      msg.hwnd    = hWndThis;
      msg.message = WM_NULL ;

      // Avoid having Windows marshal Unicode messages like a dumb ass
      if (bUnicode)
      {
        if ( PeekMessageW ( &msg, hWndThis, 0, 0,
                                              PM_REMOVE | QS_ALLINPUT | QS_ALLPOSTMESSAGE)
                 &&          msg.message != WM_NULL
           )
        {
          DispatchMessageW (&msg);
        }
      }

      else
      {
        if ( PeekMessageA ( &msg, hWndThis, 0, 0,
                                              PM_REMOVE | QS_ALLINPUT | QS_ALLPOSTMESSAGE)
                 &&          msg.message != WM_NULL
           )
        {
          DispatchMessageA (&msg);
        }
      }
    };

    LARGE_INTEGER liStart      = SK_CurrentPerf ();
    long long     liTicksPerMS = SK_GetPerfFreq ().QuadPart / 1000LL;
    long long     liEnd        = liStart.QuadPart + ( liTicksPerMS * dwMilliseconds );

    LARGE_INTEGER liNow = liStart;

    while ((liNow = SK_CurrentPerf ()).QuadPart < liEnd)
    {
      DWORD dwMaxWait =
        narrow_cast <DWORD> ((liEnd - liNow.QuadPart) / liTicksPerMS);

      if (dwMaxWait < INT_MAX)
      {
        if (MsgWaitForMultipleObjectsEx (0, nullptr, dwMaxWait, QS_ALLINPUT | QS_ALLPOSTMESSAGE,
                                                                MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0)
        {
          PeekAndDispatch ();
        }
      }
    }
  }
}

bool
fix_sleep_0 = true;


float
SK_Sched_ThreadContext::most_recent_wait_s::getRate (void)
{
  if (sequence > 0)
  {
    double ms =
      SK_DeltaPerfMS (
        SK_CurrentPerf ().QuadPart - (last_wait.QuadPart - start.QuadPart), 1
      );

    return static_cast <float> ( ms / static_cast <double> (sequence) );
  }

  // Sequence just started
  return -1.0f;
}

typedef
DWORD (WINAPI *WaitForSingleObjectEx_pfn)(
  _In_ HANDLE hHandle,
  _In_ DWORD  dwMilliseconds,
  _In_ BOOL   bAlertable
);

WaitForSingleObjectEx_pfn
WaitForSingleObjectEx_Original = nullptr;

// -------------------
// This code is largely obsolete, but will rate-limit
//   Unity's Asynchronous Procedure Calls if they are
//     ever shown to devestate performance like they
//       were in PoE2.
// -------------------
//

extern volatile LONG SK_POE2_Horses_Held;
extern volatile LONG SK_POE2_SMT_Assists;
extern volatile LONG SK_POE2_ThreadBoostsKilled;
extern          bool SK_POE2_FixUnityEmployment;
extern          bool SK_POE2_Stage2UnityFix;
extern          bool SK_POE2_Stage3UnityFix;
DWORD
WINAPI
WaitForSingleObjectEx_Detour (
  _In_ HANDLE hHandle,
  _In_ DWORD  dwMilliseconds,
  _In_ BOOL   bAlertable )
{
  if (! SK_GetFramesDrawn ())
    return WaitForSingleObjectEx_Original (
             hHandle, dwMilliseconds, bAlertable
           ); 

  SK_TLS *pTLS =
    SK_TLS_Bottom ();

  if (bAlertable)
    InterlockedIncrement (&pTLS->scheduler.alert_waits);

  // Consider double-buffering this since the information
  //   is used almost exclusively by OHTER threads, and
  //     we have to do a synchronous copy the get at this
  //       thing without thread A murdering thread B.
  SK_Sched_ThreadContext::wait_record_s& scheduled_wait =
    (*pTLS->scheduler.objects_waited) [hHandle];

  scheduled_wait.calls++;

  if (dwMilliseconds == INFINITE)
    scheduled_wait.time = 0;//+= dwMilliseconds;
   
  LARGE_INTEGER liStart =
    SK_QueryPerf ();

  bool same_as_last_time =
    ( pTLS->scheduler.mru_wait.handle == hHandle );

      pTLS->scheduler.mru_wait.handle = hHandle;

  auto ret =
    WaitForSingleObjectEx_Original (
      hHandle, dwMilliseconds, bAlertable
    );

  InterlockedAdd64 ( &scheduled_wait.time_blocked,
                       static_cast <uint64_t> (
                         SK_DeltaPerfMS (liStart.QuadPart, 1)
                       )
                   );

  // We're waiting on the same event as last time on this thread
  if ( same_as_last_time )
  {
    pTLS->scheduler.mru_wait.last_wait = liStart;
    pTLS->scheduler.mru_wait.sequence++;
  }

  // This thread found actual work and has stopped abusing the kernel
  //   waiting on the same always-signaled event; it can have its
  //     normal preemption behavior back  (briefly anyway).
  else
  {
    pTLS->scheduler.mru_wait.start     = liStart;
    pTLS->scheduler.mru_wait.last_wait = liStart;
    pTLS->scheduler.mru_wait.sequence  = 0;
  }   

  if ( ret            == WAIT_OBJECT_0 &&   SK_POE2_FixUnityEmployment &&
       dwMilliseconds == INFINITE      && ( bAlertable != FALSE ) )
  {
    // Not to be confused with the other thing
    bool hardly_working =
      (! StrCmpW (pTLS->debug.name, L"Worker Thread"));

    if ( SK_POE2_Stage3UnityFix || hardly_working )
    {
      if (pTLS->scheduler.mru_wait.getRate () >= 0.00666f)
      {
        // This turns preemption of threads in the same priority level off.
        //
        //    * Yes, TRUE means OFF.  Use this wrong and you will hurt
        //                              performance; just sayin'
        //
        if (pTLS->scheduler.mru_wait.preemptive == -1)
        {
          GetThreadPriorityBoost ( GetCurrentThread (), 
                                   &pTLS->scheduler.mru_wait.preemptive );
        }

        if (pTLS->scheduler.mru_wait.preemptive == FALSE)
        {
          SetThreadPriorityBoost ( GetCurrentThread (), TRUE );
          InterlockedIncrement   (&SK_POE2_ThreadBoostsKilled);
        }

        //
        // (Everything below applies to the Unity unemployment office only)
        //
        if (hardly_working)
        {
          // Unity Worker Threads have special additional considerations to
          //   make them less of a pain in the ass for the kernel.
          //
          LARGE_INTEGER core_sleep_begin =
            SK_QueryPerf ();

          if (SK_DeltaPerfMS (liStart.QuadPart, 1) < 0.05)
          {
            InterlockedIncrement (&SK_POE2_Horses_Held);
            SwitchToThread       ();

            if (SK_POE2_Stage2UnityFix)
            {
              // Micro-sleep the core this thread is running on to try
              //   and salvage its logical (HyperThreaded) partner's
              //     ability to do work.
              //
              while (SK_DeltaPerfMS (core_sleep_begin.QuadPart, 1) < 0.0000005)
              {
                InterlockedIncrement (&SK_POE2_SMT_Assists);

                // Very brief pause that is good for next to nothing
                //   aside from voluntarily giving up execution resources
                //     on this core's superscalar pipe and hoping the
                //       related Logical Processor can work more
                //         productively if we get out of the way.
                //
                YieldProcessor       (                    );
                //
                // ^^^ Literally does nothing, but an even less useful
                //       nothing if the processor does not support SMT.
                //
              }
            }
          };
        }
      }
      
      else
      {
        if (pTLS->scheduler.mru_wait.preemptive == -1)
        {
          GetThreadPriorityBoost ( GetCurrentThread (), 
                                  &pTLS->scheduler.mru_wait.preemptive );
        }

        if (pTLS->scheduler.mru_wait.preemptive != FALSE)
        {
          SetThreadPriorityBoost (GetCurrentThread (), FALSE);
          InterlockedIncrement   (&SK_POE2_ThreadBoostsKilled);
        }
      }
    }
  }

  // They took our jobs!
  else if (pTLS->scheduler.mru_wait.preemptive != -1)
  {
    SetThreadPriorityBoost (
      GetCurrentThread (),
        pTLS->scheduler.mru_wait.preemptive );

    // Status Quo restored: Jobs nobody wants are back and have
    //   zero future relevance and should be ignored if possible.
    pTLS->scheduler.mru_wait.preemptive = -1;
  }

  return ret;
}


typedef BOOL (WINAPI *SwitchToThread_pfn)(void);
                      SwitchToThread_pfn
                      SwitchToThread_Original = nullptr;

BOOL
WINAPI
SwitchToThread_Detour (void)
{ 
  static bool is_mwh =
    ( SK_GetCurrentGameID () == SK_GAME_ID::MonsterHunterWorld );

  if (! is_mwh)
  {
    return
      SwitchToThread_Original ();
  }


  static DWORD dwAntiDebugTid =
    GetCurrentThreadId ();

  if (dwAntiDebugTid != GetCurrentThreadId ())
    return SwitchToThread_Original ();

  extern int
       __SK_MHW_AntiDebugSleep;
  extern bool
      __SK_MHW_KillAntiDebug;
  if (__SK_MHW_KillAntiDebug)
  {
    SK_TLS *pTLS =
      SK_TLS_Bottom ();

    if (pTLS->scheduler.last_frame != SK_GetFramesDrawn ())
        pTLS->scheduler.switch_count = 0;

    if (__SK_MHW_AntiDebugSleep == 0)
    {
      if (pTLS->scheduler.last_frame   < (SK_GetFramesDrawn () - 2) ||
          pTLS->scheduler.switch_count > 7)
      {
        pTLS->scheduler.switch_count = 0;
      }

      else SleepEx (pTLS->scheduler.switch_count++, FALSE);
    }

    else
    {
      if (pTLS->scheduler.switch_count++ < __SK_MHW_AntiDebugSleep)
      {
        SleepEx (1, FALSE);
      }

      else
        SleepEx (0, TRUE);
    }

    pTLS->scheduler.last_frame =
      SK_GetFramesDrawn ();

    return TRUE;
  }

  return
    SwitchToThread_Original ();
}


void
WINAPI
Sleep_Detour (DWORD dwMilliseconds)
{
  //if (dwMilliseconds == 0) { dll_log.Log (L"Sleep (0) from thread with priority=%lu", GetThreadPriority (GetCurrentThread ())); }
  ////if (dwMilliseconds == 0 && fix_sleep_0)
  ////{
  ////  int Switches =
  ////    ++SK_TLS_Bottom ()->scheduler.sleep0_count;
  ////
  ////  if ((Switches % 65) == 0)
  ////  {
  ////    SleepEx (1, TRUE);
  ////  //SwitchToThread ();
  ////  }
  ////
  ////  else
  ////  {
  ////    SleepEx (0, TRUE);
  ////  }
  ////
  ////  return;
  ////}

  //
  // 0 is a special case that only yields if there are waiting threads at the
  //   EXACT SAME thread priority level.
  //
  //  Many developers do not know this and may attempt this from a thread with
  //    altered priority, causing major problems for everyone.
  //
  ///DWORD dwThreadPrio = 0;
  ///if (dwMilliseconds == 0 && (dwThreadPrio = GetThreadPriority (GetCurrentThread ())) != THREAD_PRIORITY_NORMAL)
  ///{
  ///  if (dwThreadPrio < THREAD_PRIORITY_NORMAL)
  ///  {
  ///    static bool reported = false;
  ///    if (! reported)
  ///    {
  ///      dll_log.Log ( L"[Compliance] Sleep (0) called from thread with "
  ///                    L"altered priority (tid=%x, prio=%lu)!",
  ///                      GetCurrentThreadId (),
  ///                        dwThreadPrio );
  ///      reported = true;
  ///    }
  ///    SwitchToThread ();
  ///  }
  ///  else
  ///  {
  ///    static bool reported = false;
  ///    if (! reported)
  ///    {
  ///      dll_log.Log ( L"[Compliance] Sleep (0) called from thread with "
  ///                    L"altered priority (tid=%x, prio=%lu)!",
  ///                      GetCurrentThreadId (),
  ///                        dwThreadPrio );
  ///      reported = true;
  ///    }
  ///    dwMilliseconds = 1;
  ///  }
  ///}

  BOOL bGUIThread    = SK_Win32_IsGUIThread ();
  BOOL bRenderThread = ((DWORD)ReadAcquire (&SK_GetCurrentRenderBackend ().thread) == GetCurrentThreadId ());

  if (bRenderThread)
  {
    if (config.render.framerate.sleepless_render && dwMilliseconds != INFINITE)
    {
      static bool reported = false;
            if (! reported)
            {
              dll_log.Log (L"[FrameLimit] Sleep called from render thread: %lu ms!", dwMilliseconds);
              reported = true;
            }

      SK::Framerate::events->getRenderThreadStats ().wake (dwMilliseconds);

      if (bGUIThread)
        SK::Framerate::events->getMessagePumpStats ().wake (dwMilliseconds);

      if (dwMilliseconds <= 1)
      {
        if (SK_TLS_Bottom ()->win32.getThreadPriority () == THREAD_PRIORITY_NORMAL)
          SleepEx (0, FALSE);
        else
          YieldProcessor ();
      }

      return;
    }

    SK::Framerate::events->getRenderThreadStats ().sleep  (dwMilliseconds);
  }

  if (bGUIThread)
  {
    if (config.render.framerate.sleepless_window && dwMilliseconds != INFINITE)
    {
      static bool reported = false;
            if (! reported)
            {
              dll_log.Log (L"[FrameLimit] Sleep called from GUI thread: %lu ms!", dwMilliseconds);
              reported = true;
            }

      SK::Framerate::events->getMessagePumpStats ().wake   (dwMilliseconds);

      if (bRenderThread)
        SK::Framerate::events->getMessagePumpStats ().wake (dwMilliseconds);

      SK_Thread_WaitWhilePumpingMessages (dwMilliseconds);

      return;
    }

    SK::Framerate::events->getMessagePumpStats ().sleep (dwMilliseconds);
  }

  // TODO: Stop this nonsense and make an actual parameter for this...
  //         (min sleep?)
  if ( narrow_cast <DWORD> (config.render.framerate.max_delta_time) <=
                            dwMilliseconds
     )
  {
    Sleep_Original (dwMilliseconds);
  }
}

float __SK_SHENMUE_ClockFuzz = 20.0f;
extern volatile LONG SK_BypassResult;

typedef NTSTATUS (*NtQueryPerformanceCounter_pfn)(PLARGE_INTEGER, PLARGE_INTEGER);
                   NtQueryPerformanceCounter_pfn
            static NtQueryPerformanceCounter = nullptr;
BOOL
WINAPI
SK_QueryPerformanceCounter (_Out_ LARGE_INTEGER *lpPerformanceCount)
{
  if (NtQueryPerformanceCounter == nullptr)
  {
    NtQueryPerformanceCounter =
      (NtQueryPerformanceCounter_pfn)
        SK_GetProcAddress ( L"ntdll.dll",
                             "NtQueryPerformanceCounter" );
  }

#define STATUS_SUCCESS 0x0

  //if (NtQueryPerformanceCounter != nullptr)
  //{
  //  return
  //  ( STATUS_SUCCESS ==
  //      NtQueryPerformanceCounter (lpPerformanceCount, nullptr)
  //  );
  //}

  if (QueryPerformanceCounter_Original != nullptr)
  {
    return
      QueryPerformanceCounter_Original (lpPerformanceCount);
  }

  else
    return QueryPerformanceCounter (lpPerformanceCount);
}

#include <unordered_set>

extern bool SK_Shenmue_IsLimiterBypassed   (void              );
extern bool SK_Shenmue_InitLimiterOverride (LPVOID pQPCRetAddr);
extern bool SK_Shenmue_UseNtDllQPC;

BOOL
WINAPI
QueryPerformanceCounter_Detour (_Out_ LARGE_INTEGER *lpPerformanceCount)
{
  struct SK_ShenmueLimitBreaker
  {
    bool detected = false;
    bool pacified = false;
  } static
      shenmue_clock {
        SK_GetCurrentGameID () == SK_GAME_ID::Shenmue,
      //SK_GetCurrentGameID () == SK_GAME_ID::DragonQuestXI,
        false
      };


  if ( lpPerformanceCount != nullptr   &&
       shenmue_clock.detected          &&
       shenmue_clock.pacified == false &&
    (! SK_Shenmue_IsLimiterBypassed () ) )
  {
    extern volatile LONG
      __SK_SHENMUE_FinishedButNotPresented;

    static DWORD dwRenderThread = 0;

    if (ReadAcquire (&__SK_SHENMUE_FinishedButNotPresented))
    {
      if (dwRenderThread == 0)
          dwRenderThread = ReadAcquire (&SK_GetCurrentRenderBackend ().thread);

      if ( GetCurrentThreadId () == dwRenderThread )
      {
        if ( SK_GetCallingDLL () == SK_Modules.HostApp () )
        {
          static std::unordered_set <LPCVOID> ret_addrs;

          if (ret_addrs.emplace (_ReturnAddress ()).second)
          {
            SK_LOG1 ( ( L"New QueryPerformanceCounter Return Addr: %ph -- %s",
                        _ReturnAddress (), SK_SummarizeCaller ().c_str () ),
                        L"ShenmueDbg" );

            // The instructions we're looking for are jl ...
            //
            //   Look-ahead up to about 12-bytes and if not found,
            //     this isn't the primary limiter code.
            //
            if ( reinterpret_cast <uintptr_t> (
                   SK_ScanAlignedEx (
                     "\x7C\xEE",          2,
                     nullptr,     (void *)_ReturnAddress ()
                   )
                 ) < (uintptr_t)_ReturnAddress () + 12 )
            {
              shenmue_clock.pacified =
                SK_Shenmue_InitLimiterOverride (_ReturnAddress ());

              SK_LOG0 ( (L"Shenmue Framerate Limiter Located and Pacified"),
                         L"ShenmueDbg" );
            }
          }

          InterlockedExchange (&__SK_SHENMUE_FinishedButNotPresented, 0);

          BOOL bRet =
            QueryPerformanceCounter_Original (lpPerformanceCount);

          static LARGE_INTEGER last_poll {
            lpPerformanceCount->u.LowPart,
            lpPerformanceCount->u.HighPart
          };

          LARGE_INTEGER pre_fuzz { 
            lpPerformanceCount->u.LowPart,
            lpPerformanceCount->u.HighPart
          };

          lpPerformanceCount->QuadPart    +=
            (lpPerformanceCount->QuadPart - last_poll.QuadPart) *
                                          __SK_SHENMUE_ClockFuzz;

          last_poll.QuadPart =
           pre_fuzz.QuadPart;

          return bRet;
        }
      }
    }
  }


  if (NtQueryPerformanceCounter == nullptr)
  {
    NtQueryPerformanceCounter =
      (NtQueryPerformanceCounter_pfn)
        SK_GetProcAddress ( L"ntdll.dll",
                             "NtQueryPerformanceCounter" );
  }
  
  if (SK_Shenmue_UseNtDllQPC && NtQueryPerformanceCounter != nullptr)
  {
    static LARGE_INTEGER
      last_query = { };

#define STATUS_SUCCESS 0x0
  
    BOOL ret =
    ( STATUS_SUCCESS ==
        NtQueryPerformanceCounter (lpPerformanceCount, nullptr)
    );

    if (ret && lpPerformanceCount != nullptr)
    {
      if ( lpPerformanceCount->QuadPart <=
                    last_query.QuadPart    ) {
        ++(lpPerformanceCount->QuadPart);    }

               last_query.QuadPart =
      lpPerformanceCount->QuadPart;

      return ret;
    }
  }

  if (RtlQueryPerformanceCounter == nullptr)
  {
    RtlQueryPerformanceCounter =
      (QueryPerformanceCounter_pfn)
        SK_GetProcAddress ( L"Ntdll.dll",
                             "RtlQueryPerformanceCounter" );
  }

  return
    RtlQueryPerformanceCounter (lpPerformanceCount);

  //return
  //  QueryPerformanceCounter_Original (lpPerformanceCount);
}

using NTSTATUS = _Return_type_success_(return >= 0) LONG;

using NtQueryTimerResolution_pfn = NTSTATUS (NTAPI *)
(
  OUT PULONG              MinimumResolution,
  OUT PULONG              MaximumResolution,
  OUT PULONG              CurrentResolution
);

using NtSetTimerResolution_pfn = NTSTATUS (NTAPI *)
(
  IN  ULONG               DesiredResolution,
  IN  BOOLEAN             SetResolution,
  OUT PULONG              CurrentResolution
);

HMODULE                    NtDll                  = nullptr;

NtQueryTimerResolution_pfn NtQueryTimerResolution = nullptr;
NtSetTimerResolution_pfn   NtSetTimerResolution   = nullptr;


float target_fps = 0.0;

static volatile LONG frames_ahead = 0;

void
SK::Framerate::Init (void)
{
  static SK::Framerate::Stats        _frame_history;
  static SK::Framerate::Stats        _frame_history2;
  static SK::Framerate::EventCounter _events;
  
  static bool basic_init = false;

  if (! basic_init)
  {
    basic_init = true;

    frame_history         = &_frame_history;
    frame_history2        = &_frame_history2;
    SK::Framerate::events = &_events;

    SK_ICommandProcessor* pCommandProc =
      SK_GetCommandProcessor ();

    // TEMP HACK BECAUSE THIS ISN'T STORED in D3D9.INI
    if (GetModuleHandle (L"AgDrag.dll"))
      config.render.framerate.max_delta_time = 5;

    if (GetModuleHandle (L"tsfix.dll"))
      config.render.framerate.max_delta_time = 0;

    pCommandProc->AddVariable ( "WaitForVBLANK",
            new SK_IVarStub <bool> (&config.render.framerate.wait_for_vblank));
    pCommandProc->AddVariable ( "MaxDeltaTime",
            new SK_IVarStub <int> (&config.render.framerate.max_delta_time));

    pCommandProc->AddVariable ( "LimiterTolerance",
            new SK_IVarStub <float> (&config.render.framerate.limiter_tolerance));
    pCommandProc->AddVariable ( "TargetFPS",
            new SK_IVarStub <float> (&target_fps));

    pCommandProc->AddVariable ( "MaxRenderAhead",
            new SK_IVarStub <int> (&config.render.framerate.max_render_ahead));

//#define NO_HOOK_QPC
#ifndef NO_HOOK_QPC
  SK_CreateDLLHook2 (      L"kernel32",
                            "QueryPerformanceCounter",
                             QueryPerformanceCounter_Detour,
    static_cast_p2p <void> (&QueryPerformanceCounter_Original),
    static_cast_p2p <void> (&pfnQueryPerformanceCounter) );
#endif

    SK_CreateDLLHook2 (      L"kernel32",
                              "Sleep",
                               Sleep_Detour,
      static_cast_p2p <void> (&Sleep_Original),
      static_cast_p2p <void> (&pfnSleep) );

        SK_CreateDLLHook2 (  L"KernelBase.dll",
                              "SwitchToThread",
                               SwitchToThread_Detour,
      static_cast_p2p <void> (&SwitchToThread_Original) );

    ////SK_CreateDLLHook2 (      L"kernel32",
    ////                          "WaitForSingleObjectEx",
    ////                           WaitForSingleObjectEx_Detour,
    ////  static_cast_p2p <void> (&WaitForSingleObjectEx_Original) );

#ifdef NO_HOOK_QPC
      QueryPerformanceCounter_Original =
        reinterpret_cast <QueryPerformanceCounter_pfn> (
          GetProcAddress ( GetModuleHandle (L"kernel32"),
                             "QueryPerformanceCounter" )
        );
#endif

    if (! config.render.framerate.enable_mmcss)
    {
      if (NtDll == nullptr)
      {
        NtDll = LoadLibrary (L"ntdll.dll");

        NtQueryTimerResolution =
          reinterpret_cast <NtQueryTimerResolution_pfn> (
            GetProcAddress (NtDll, "NtQueryTimerResolution")
          );

        NtSetTimerResolution =
          reinterpret_cast <NtSetTimerResolution_pfn> (
            GetProcAddress (NtDll, "NtSetTimerResolution")
          );

        if (NtQueryTimerResolution != nullptr &&
            NtSetTimerResolution   != nullptr)
        {
          ULONG min, max, cur;
          NtQueryTimerResolution (&min, &max, &cur);
          dll_log.Log ( L"[  Timing  ] Kernel resolution.: %f ms",
                          static_cast <float> (cur * 100)/1000000.0f );
          NtSetTimerResolution   (max, TRUE,  &cur);
          dll_log.Log ( L"[  Timing  ] New resolution....: %f ms",
                          static_cast <float> (cur * 100)/1000000.0f );
        }
      }
    }
  }
}

void
SK::Framerate::Shutdown (void)
{
  if (NtDll != nullptr)
    FreeLibrary (NtDll);

  SK_DisableHook (pfnSleep);
  //SK_DisableHook (pfnQueryPerformanceCounter);
}

SK::Framerate::Limiter::Limiter (double target)
{
  effective_ms = 0.0;

  init (target);
}


IDirect3DDevice9Ex*
SK_D3D9_GetTimingDevice (void)
{
  static auto* pTimingDevice =
    reinterpret_cast <IDirect3DDevice9Ex *> (-1);

  if (pTimingDevice == reinterpret_cast <IDirect3DDevice9Ex *> (-1))
  {
    CComPtr <IDirect3D9Ex> pD3D9Ex = nullptr;

    using Direct3DCreate9ExPROC = HRESULT (STDMETHODCALLTYPE *)(UINT           SDKVersion,
                                                                IDirect3D9Ex** d3d9ex);

    extern Direct3DCreate9ExPROC Direct3DCreate9Ex_Import;

    // For OpenGL, bootstrap D3D9
    SK_BootD3D9 ();

    HRESULT hr = (config.apis.d3d9ex.hook) ?
      Direct3DCreate9Ex_Import (D3D_SDK_VERSION, &pD3D9Ex)
                                    :
                               E_NOINTERFACE;

    HWND hwnd = nullptr;

    IDirect3DDevice9Ex* pDev9Ex = nullptr;

    if (SUCCEEDED (hr))
    {
      hwnd = 
        SK_Win32_CreateDummyWindow ();

      D3DPRESENT_PARAMETERS pparams = { };
      
      pparams.SwapEffect       = D3DSWAPEFFECT_FLIPEX;
      pparams.BackBufferFormat = D3DFMT_UNKNOWN;
      pparams.hDeviceWindow    = hwnd;
      pparams.Windowed         = TRUE;
      pparams.BackBufferCount  = 2;
      pparams.BackBufferHeight = 2;
      pparams.BackBufferWidth  = 2;
      
      if ( FAILED ( pD3D9Ex->CreateDeviceEx (
                      D3DADAPTER_DEFAULT,
                        D3DDEVTYPE_HAL,
                          hwnd,
                            D3DCREATE_HARDWARE_VERTEXPROCESSING,
                              &pparams,
                                nullptr,
                                  &pDev9Ex )
                  )
          )
      {
        pTimingDevice = nullptr;
      } else {
        pDev9Ex->AddRef ();
        pTimingDevice = pDev9Ex;
      }
    }
    else {
      pTimingDevice = nullptr;
    }
  }

  return pTimingDevice;
}


void
SK::Framerate::Limiter::init (double target)
{
  QueryPerformanceFrequency (&freq);

  ms  = 1000.0 / target;
  fps = target;

  frames = 0;

  CComPtr <IDirect3DDevice9Ex> d3d9ex      = nullptr;
  CComPtr <IDXGISwapChain>     dxgi_swap   = nullptr;
  CComPtr <IDXGIOutput>        dxgi_output = nullptr;

  SK_RenderBackend& rb =
   SK_GetCurrentRenderBackend ();

  SK_RenderAPI api = rb.api;

  if (                    api ==                    SK_RenderAPI::D3D10  ||
       static_cast <int> (api) & static_cast <int> (SK_RenderAPI::D3D11) ||
       static_cast <int> (api) & static_cast <int> (SK_RenderAPI::D3D12) )
  {
    if (rb.swapchain != nullptr)
    {
      HRESULT hr =
        rb.swapchain->QueryInterface <IDXGISwapChain> (&dxgi_swap);

      if (SUCCEEDED (hr))
      {
        if (SUCCEEDED (dxgi_swap->GetContainingOutput (&dxgi_output)))
        {
          //WaitForVBlank_Original (dxgi_output);
          dxgi_output->WaitForVBlank ();
        }
      }
    }
  }

  else if (static_cast <int> (api) & static_cast <int> (SK_RenderAPI::D3D9))
  {
    if (rb.device != nullptr)
    {
      rb.device->QueryInterface ( IID_PPV_ARGS (&d3d9ex) );

      // Align the start to VBlank for minimum input latency
      if (d3d9ex != nullptr || (d3d9ex = SK_D3D9_GetTimingDevice ()))
      {
        UINT orig_latency = 3;
        d3d9ex->GetMaximumFrameLatency (&orig_latency);

        d3d9ex->SetMaximumFrameLatency (1);
        d3d9ex->WaitForVBlank          (0);
        d3d9ex->SetMaximumFrameLatency (
          config.render.framerate.pre_render_limit == -1 ?
               orig_latency : config.render.framerate.pre_render_limit );
      }
    }
  }

  QueryPerformanceCounter_Original (&start);

  InterlockedExchange (&frames_ahead, 0);

  time.QuadPart = 0ULL;
  last.QuadPart = static_cast <LONGLONG> (start.QuadPart - (ms / 1000.0) * freq.QuadPart);
  next.QuadPart = static_cast <LONGLONG> (start.QuadPart + (ms / 1000.0) * freq.QuadPart);
}

#include <SpecialK/window.h>

bool
SK::Framerate::Limiter::try_wait (void)
{
  if (limit_behavior != LIMIT_APPLY)
    return false;

  if (target_fps <= 0.0f)
    return false;

  LARGE_INTEGER next_;
  next_.QuadPart =
    static_cast <LONGLONG> (
      start.QuadPart                                +
      (  static_cast <long double> (frames) + 1.0 ) *
                                   (ms  / 1000.0L ) *
         static_cast <long double>  (freq.QuadPart)
    );

  QueryPerformanceCounter_Original (&time);

  if (time.QuadPart < next_.QuadPart)
    return true;

  return false;
}

void
SK::Framerate::Limiter::wait (void)
{
  if (limit_behavior != LIMIT_APPLY)
    return;

  // Don't limit under certain circumstances or exiting / alt+tabbing takes
  //   longer than it should.
  if (ReadAcquire (&__SK_DLL_Ending))
    return;

  SK_RunOnce ( SetThreadPriority ( SK_GetCurrentThread (),
                                     THREAD_PRIORITY_ABOVE_NORMAL ) );

  static bool restart      = false;
  static bool full_restart = false;

  if (fps != target_fps)
    init (target_fps);

  if (target_fps <= 0.0f)
    return;

  frames++;

  QueryPerformanceCounter_Original (&time);


  bool bNeedWait =
    time.QuadPart < next.QuadPart;

  if (bNeedWait && ReadAcquire (&frames_ahead) < config.render.framerate.max_render_ahead)
  {
    InterlockedIncrement (&frames_ahead);
    return;
  }

  else if (bNeedWait && InterlockedCompareExchange (&frames_ahead, 0, 1) > 1)
                        InterlockedDecrement       (&frames_ahead);


  // Actual frametime before we forced a delay
  effective_ms =
    1000.0 * ( static_cast <double> (time.QuadPart - last.QuadPart) /
               static_cast <double> (freq.QuadPart)                 );


  long double to_next =
    (long double)(time.QuadPart - next.QuadPart) /
    (long double) freq.QuadPart;

  to_next /= 0.7853f;

  LARGE_INTEGER liDelay;
                liDelay.QuadPart =
    (LONGLONG)(to_next * 10000000);

  // Create an unnamed waitable timer.
  static HANDLE hLimitTimer =
    CreateWaitableTimer (NULL, FALSE, NULL);

  if ( SetWaitableTimer ( hLimitTimer, &liDelay,
                            0, NULL, NULL, 0 ) )
  {
    WaitForSingleObject (hLimitTimer, INFINITE);
  }


  if ( static_cast <double> (time.QuadPart - next.QuadPart) /
       static_cast <double> (freq.QuadPart)                 /
                            ( ms / 1000.0 )                 >
      ( config.render.framerate.limiter_tolerance * fps )
     )
  {
    //dll_log.Log ( L" * Frame ran long (%3.01fx expected) - restarting"
                  //L" limiter...",
            //(double)(time.QuadPart - next.QuadPart) /
            //(double)freq.QuadPart / (ms / 1000.0) / fps );
    restart = true;

#if 0
    extern SK::Framerate::Stats frame_history;
    extern SK::Framerate::Stats frame_history2;

    double mean    = frame_history.calcMean     ();
    double sd      = frame_history.calcSqStdDev (mean);

    if (sd > 5.0f)
      full_restart = true;
#endif
  }

  if (restart || full_restart)
  {
    frames         = 0;
    start.QuadPart = static_cast <LONGLONG> (
                       static_cast <double> (time.QuadPart) +
                                            ( ms / 1000.0 ) *
                       static_cast <double> (freq.QuadPart)
                     );
    restart        = false;

    if (full_restart)
    {
      init (target_fps);
      full_restart = false;
    }
    //return;
  }

  next.QuadPart =
    static_cast <LONGLONG> (
      static_cast <long double> (start.QuadPart) +
      static_cast <long double> (    frames    ) *
                                (  ms / 1000.0 ) *
      static_cast <long double> ( freq.QuadPart)
    );

  if (next.QuadPart > 0ULL)
  {
    // If available (Windows 7+), wait on the swapchain
    CComPtr <IDirect3DDevice9Ex>  d3d9ex = nullptr;

    // D3D10/11/12
    CComPtr <IDXGISwapChain> dxgi_swap   = nullptr;
    CComPtr <IDXGIOutput>    dxgi_output = nullptr;

    SK_RenderBackend& rb =
      SK_GetCurrentRenderBackend ();

    if (config.render.framerate.wait_for_vblank)
    {
      SK_RenderAPI api = rb.api;

      if (                    api ==                    SK_RenderAPI::D3D10  ||
           static_cast <int> (api) & static_cast <int> (SK_RenderAPI::D3D11) ||
           static_cast <int> (api) & static_cast <int> (SK_RenderAPI::D3D12) )
      {
        if (rb.swapchain != nullptr)
        {
          HRESULT hr =
            rb.swapchain->QueryInterface <IDXGISwapChain> (&dxgi_swap);

          if (SUCCEEDED (hr))
          {
            dxgi_swap->GetContainingOutput (&dxgi_output);
          }
        }
      }

      else if ( static_cast <int> (api)       &
                static_cast <int> (SK_RenderAPI::D3D9) )
      {
        if (rb.device != nullptr)
        {
          if (FAILED (rb.device->QueryInterface <IDirect3DDevice9Ex> (&d3d9ex)))
          {
            d3d9ex =
              SK_D3D9_GetTimingDevice ();
          }
        }
      }
    }

    bool bGUI =
      SK_Win32_IsGUIThread () && GetActiveWindow () == game_window.hWnd;

    bool bYielded = false;

    while (time.QuadPart < next.QuadPart)
    {
      // Attempt to use a deeper sleep when possible instead of hammering the
      //   CPU into submission ;)
      if ( ( static_cast <double> (next.QuadPart  - time.QuadPart) >
             static_cast <double> (freq.QuadPart) * 0.001 *
                                   config.render.framerate.busy_wait_limiter) &&
                                  (! (config.render.framerate.yield_once && bYielded))
         )
      {
        if ( config.render.framerate.wait_for_vblank )
        {
          if (d3d9ex != nullptr)
            d3d9ex->WaitForVBlank (0);

          else if (dxgi_output != nullptr)
            dxgi_output->WaitForVBlank ();
        }

        else if (! config.render.framerate.busy_wait_limiter)
        {                
          auto dwWaitMS =
            static_cast <DWORD>
              ( (config.render.framerate.max_sleep_percent * 10.0f) / target_fps ); // 10% of full frame

          if ( ( static_cast <double> (next.QuadPart - time.QuadPart) /
                 static_cast <double> (freq.QuadPart                ) ) * 1000.0 >
                   dwWaitMS )
          {
            if (bGUI && config.render.framerate.min_input_latency)
            {
              SK_Thread_WaitWhilePumpingMessages (dwWaitMS);
            }

            else
              SleepEx                            (dwWaitMS,   FALSE);

            bYielded = true;
          }
        }
      }

      SK_QueryPerformanceCounter (&time);
    }
  }

  else
  {
    dll_log.Log (L"[FrameLimit] Framerate limiter lost time?! (non-monotonic clock)");
    start.QuadPart += -next.QuadPart;
  }

  last.QuadPart = time.QuadPart;
}

void
SK::Framerate::Limiter::set_limit (double target)
{
  init (target);
}

double
SK::Framerate::Limiter::effective_frametime (void)
{
  return effective_ms;
}


SK::Framerate::Limiter*
SK::Framerate::GetLimiter (void)
{
  static Limiter* limiter = nullptr;

  if (limiter == nullptr)
  {
    limiter =
      new Limiter (config.render.framerate.target_fps);
  }

  return limiter;
}

void
SK::Framerate::Tick (double& dt, LARGE_INTEGER& now)
{
  static LARGE_INTEGER last_frame = { };

  now = SK_CurrentPerf ();

  dt = static_cast <double> (now.QuadPart -  last_frame.QuadPart) /
       static_cast <double> (SK::Framerate::Stats::freq.QuadPart);


  // What the bloody hell?! How do we ever get a dt value near 0?
  if (dt > 0.000001)
    frame_history->addSample (1000.0 * dt, now);
  else // Less than single-precision FP epsilon, toss this frame out
    frame_history->addSample (INFINITY, now);


  frame_history2->addSample (
    SK::Framerate::GetLimiter ()->effective_frametime (), 
      now
  );


  last_frame = now;
};


double
SK::Framerate::Stats::calcMean (double seconds)
{
  return
    calcMean (SK_DeltaPerf (seconds, freq.QuadPart));
}

double
SK::Framerate::Stats::calcSqStdDev (double mean, double seconds)
{
  return
    calcSqStdDev (mean, SK_DeltaPerf (seconds, freq.QuadPart));
}

double
SK::Framerate::Stats::calcMin (double seconds)
{
  return
    calcMin (SK_DeltaPerf (seconds, freq.QuadPart));
}

double
SK::Framerate::Stats::calcMax (double seconds)
{
  return
    calcMax (SK_DeltaPerf (seconds, freq.QuadPart));
}

int
SK::Framerate::Stats::calcHitches (double tolerance, double mean, double seconds)
{
  return
    calcHitches (tolerance, mean, SK_DeltaPerf (seconds, freq.QuadPart));
}

int
SK::Framerate::Stats::calcNumSamples (double seconds)
{
  return
    calcNumSamples (SK_DeltaPerf (seconds, freq.QuadPart));
}


LARGE_INTEGER&
SK_GetPerfFreq (void)
{
  static LARGE_INTEGER freq = { 0UL };
  static bool          init = false;

  if (NtQueryPerformanceCounter == nullptr)
  {
    NtQueryPerformanceCounter =
      (NtQueryPerformanceCounter_pfn)
        SK_GetProcAddress ( L"ntdll.dll",
                             "NtQueryPerformanceCounter" );
  }
  
  if (! init)
  {
    //LARGE_INTEGER IntFreq;
    //LARGE_INTEGER IntCounter;

    QueryPerformanceFrequency (&freq);// (&IntCounter, &IntFreq);

    //   freq.QuadPart =
    //IntFreq.QuadPart;

    init = true;
  }

  return freq;
}