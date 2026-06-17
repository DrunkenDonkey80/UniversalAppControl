#pragma once
#include <Windows.h>
#include <stdbool.h>
#include "Main.h"

#define MAX_PHYSICAL_PER_HMONITOR 8
#define MAX_VCP14_VALS            16

// -----------------------------------------------------------------------
//  Supported VCP 0x14 color-preset codes for the primary monitor.
//  Populated by DisplayInit(); read by the settings UI to build the
//  color-temperature dropdown.
//  Count == 0: not yet probed.  Count == -1: VCP 0x14 not supported.
// -----------------------------------------------------------------------
extern BYTE gPrimaryVcp14Vals[MAX_VCP14_VALS];
extern int  gPrimaryVcp14Count;

// Supported VCP 0xF0 codes for primary monitor (11 named preset modes).
extern BYTE gPrimaryVcpF0Vals[MAX_VCP14_VALS];
extern int  gPrimaryVcpF0Count;

// Human-readable labels.
const wchar_t* GetVcp14Label(BYTE code);
const wchar_t* GetVcpF0Label(BYTE code);

// -----------------------------------------------------------------------
//  Public API
// -----------------------------------------------------------------------

void DisplayInit(void);
void DisplayRefresh(void);

// Reset last-applied tracking so the next Apply always sends to hardware.
// Call this before a force-apply (Apply button) to ensure values are sent
// even if the preset hasn't changed since the last foreground switch.
void DisplayResetLastApplied(void);

int  DisplayListUnsupported(wchar_t names[][128], int maxOut);

// Capture current brightness/contrast/colortemp from the PRIMARY monitor.
// hwnd is ignored (kept for API compatibility).
// ColorTemp is returned as a raw VCP 0x14 code (1-12), not Kelvin.
bool DisplayCaptureCurrent(HWND hwnd, DISPLAY_PRESET* out);

// Apply preset to the PRIMARY monitor.
// force=true  : ignore last-applied cache and always send (Apply button).
// force=false : skip unchanged values (foreground poll path).
// hwnd is ignored.
bool DisplayApplyPreset(HWND hwnd, const DISPLAY_PRESET* preset, bool force);

void DisplayRestoreAll(void);
