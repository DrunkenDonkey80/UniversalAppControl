#pragma once
#include <Windows.h>
#include <stdbool.h>

// Auto-update from GitHub releases: check daily, download the new exe next to
// the running one, then swap it in on "Restart to update".
#define WM_UPDATE_DONE  (WM_APP + 3)   // wParam = UPD_*, lParam = forced check
#define UPDATE_TIMER_ID 0x5550

enum { UPD_FAILED, UPD_NONE, UPD_READY };

extern wchar_t       gUpdateVersion[32];  // tag of the downloaded update, e.g. L"v1.3.0"
extern volatile LONG gUpdateReady;        // 1 once the new exe is downloaded

void UpdateInit(HWND notifyWnd);          // schedules the first check
void UpdateOnTimer(void);
void UpdateCheck(bool forced);            // async; posts WM_UPDATE_DONE
void UpdateOnDone(HWND wnd, int result, bool forced);
bool UpdateApply(HWND owner);             // swap exe, relaunch, quit

// Exposed for selftest.
int  UpdateCompareVersions(const char* a, const char* b);
bool UpdateParseRelease(const char* json, char* tag, size_t tagCch, char* url, size_t urlCch);
