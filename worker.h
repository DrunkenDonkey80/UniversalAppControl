#pragma once
#include <Windows.h>
#include <stdbool.h>

typedef enum { JOB_TOGGLE_HOTKEY = 1, JOB_RELOAD_CONFIG, JOB_SHUTDOWN } JobType;
typedef struct { JobType type; int hotkeyIndex; } Job;

void WorkerInit(void);
bool JobQueuePush(Job job);
bool JobQueuePop(Job* out);          // blocks until a job is available
DWORD WINAPI WorkerThreadProc(LPVOID param);
