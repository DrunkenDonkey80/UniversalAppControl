#include "worker.h"

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
    // Full implementation added in Task 6.
    return 0;
}
