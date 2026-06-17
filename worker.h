#pragma once
#include <Windows.h>
#include <stdbool.h>
#include "Main.h"   // DISPLAY_PRESET, MAX_NAME

typedef enum {
    JOB_TOGGLE_HOTKEY   = 1,
    JOB_RELOAD_CONFIG,
    JOB_SHUTDOWN,
    JOB_APPLY_DISPLAY,      // apply / reconcile monitor preset
    JOB_SCAN_PRESETS         // probe all VCP 0xF0 codes and build name+B/C table
} JobType;

typedef struct { JobType type; int hotkeyIndex; } Job;

// The latest desired monitor state.  Written by the main-loop poll under
// gHotkeyLock, read by the worker thread.  Only one instance exists;
// rapid alt-tabs overwrite the same struct so the worker always acts on
// the most recent target (natural coalescing).
typedef struct {
    HWND          hwnd;                  // foreground window handle
    wchar_t       presetName[MAX_NAME];  // L"" => use Default baseline
    bool          valid;                 // false => display control idle
} DesiredDisplay;

extern DesiredDisplay gDesiredDisplay;  // defined in Main.c

void WorkerInit(void);
bool JobQueuePush(Job job);
bool JobQueuePop(Job* out);          // blocks until a job is available
DWORD WINAPI WorkerThreadProc(LPVOID param);
