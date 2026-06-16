#include "worker.h"
#include "Main.h"
#include "config.h"
#include "display.h"

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
                // Copy the latest desired state atomically, then act on the copy.
                // This coalesces rapid alt-tab: intermediate states are overwritten
                // before the worker wakes up.
                // job.hotkeyIndex == 1 means "force" (ignore gDisplayControlEnabled).
                bool force = (job.hotkeyIndex == 1);
                EnterCriticalSection(&gHotkeyLock);
                DesiredDisplay d = gDesiredDisplay;
                LeaveCriticalSection(&gHotkeyLock);

                if (!d.valid || (!gDisplayControlEnabled && !force)) break;

                // Resolve preset by name; fall back to Default.
                int pi = FindPresetByName(d.presetName);
                if (pi < 0) pi = FindPresetByName(gDefaultPresetName);
                if (pi < 0) break;  // no preset defined yet

                DbgPrint(L"[worker] ApplyDisplay preset='%s' hwnd=%p",
                         gPresets[pi].Name, (void*)d.hwnd);
                DisplayApplyPreset(d.hwnd, &gPresets[pi]);
                break;
            }
            case JOB_SHUTDOWN:       return 0;
        }
    }
    return 0;
}
