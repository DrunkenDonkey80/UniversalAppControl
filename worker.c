#include "worker.h"
#include "Main.h"
#include "config.h"
#include "display.h"
#include <objbase.h>
#pragma comment(lib, "ole32.lib")

#define JOB_QUEUE_CAP 256
static Job gQueue[JOB_QUEUE_CAP];
static int gHead = 0, gTail = 0;
static CRITICAL_SECTION gQueueLock;
static HANDLE gQueueEvent = NULL;
static bool gInited = false;

void WorkerInit(void) {
    if (gInited) return;
    InitializeCriticalSection(&gQueueLock);
    gQueueEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    gHead = gTail = 0;
    gInited = true;
}

bool JobQueuePush(Job job) {
    EnterCriticalSection(&gQueueLock);
    int next = (gTail + 1) % JOB_QUEUE_CAP;
    if (next == gHead) { LeaveCriticalSection(&gQueueLock); return false; }
    gQueue[gTail] = job;
    gTail = next;
    LeaveCriticalSection(&gQueueLock);
    SetEvent(gQueueEvent);
    return true;
}

bool JobQueuePop(Job* out) {
    for (;;) {
        EnterCriticalSection(&gQueueLock);
        if (gHead != gTail) {
            *out = gQueue[gHead];
            gHead = (gHead + 1) % JOB_QUEUE_CAP;
            LeaveCriticalSection(&gQueueLock);
            return true;
        }
        LeaveCriticalSection(&gQueueLock);
        WaitForSingleObject(gQueueEvent, INFINITE);
    }
}

DWORD WINAPI WorkerThreadProc(LPVOID param) {
    (void)param;
    // DDC/CI (Dxva2.dll) internally uses COM; initialize it on this thread.
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CrashLog("[worker] CoInitializeEx hr=0x%08X\n", (unsigned)hr);
    Job job;
    while (JobQueuePop(&job)) {
        switch (job.type) {
            case JOB_TOGGLE_HOTKEY:  DispatchHotkey(job.hotkeyIndex); break;
            case JOB_RELOAD_CONFIG: {
                EnterCriticalSection(&gHotkeyLock);
                LoadConfig();
                LeaveCriticalSection(&gHotkeyLock);
                break;
            }
            case JOB_APPLY_DISPLAY: {
                CrashLog("[worker] JOB_APPLY_DISPLAY received hotkeyIndex=%d\n", job.hotkeyIndex);
                bool force = (job.hotkeyIndex == 1);
                EnterCriticalSection(&gHotkeyLock);
                DesiredDisplay d = gDesiredDisplay;
                LeaveCriticalSection(&gHotkeyLock);
                CrashLog("[worker] valid=%d force=%d displayCtrl=%d\n",
                         d.valid, force, gDisplayControlEnabled);

                if (!d.valid || (!gDisplayControlEnabled && !force)) { CrashLog("[worker] skipped\n"); break; }

                int pi = FindPresetByName(d.presetName);
                CrashLog("[worker] FindPresetByName='%ls' -> pi=%d\n", d.presetName, pi);
                if (pi < 0) pi = FindPresetByName(gDefaultPresetName);
                if (pi < 0) { CrashLog("[worker] no preset, abort\n"); break; }

                if (force) DisplayResetLastApplied(); // bypass skip-if-unchanged cache
                CrashLog("[worker] calling DisplayApplyPreset preset[%d]='%ls' B=%d C=%d CT=%d hwnd=%p\n",
                         pi, gPresets[pi].Name,
                         gPresets[pi].Brightness, gPresets[pi].Contrast, gPresets[pi].ColorTemp,
                         (void*)d.hwnd);
                DisplayApplyPreset(d.hwnd, &gPresets[pi], force);
                CrashLog("[worker] DisplayApplyPreset returned\n");
                break;
            }
            case JOB_SCAN_PRESETS: {
                extern HWND gScanNotifyHwnd;
                DisplayScanPresets(gScanNotifyHwnd);
                gScanNotifyHwnd = NULL;
                break;
            }
            case JOB_SHUTDOWN:
                CoUninitialize();
                return 0;
        }
    }
    return 0;
}
