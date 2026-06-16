#pragma once
#include <Windows.h>
#include <stdbool.h>
#include "Main.h"    // DISPLAY_PRESET, PRESET_UNSET, MAX_NAME

// Maximum physical monitors behind a single HMONITOR (usually 1).
#define MAX_PHYSICAL_PER_HMONITOR 8

// -----------------------------------------------------------------------
//  Public API
// -----------------------------------------------------------------------

// One-time init: enumerate all attached monitors and probe DDC/CI capability.
// Call before starting the worker thread (or from the worker thread).
void DisplayInit(void);

// Re-probe capabilities after WM_DISPLAYCHANGE.
void DisplayRefresh(void);

// Fill names[][128] with the description strings of monitors that don't
// support DDC/CI brightness control.  Returns count filled.
int DisplayListUnsupported(wchar_t names[][128], int maxOut);

// Read current brightness/contrast/colortemp from the physical monitor that
// the window hwnd lives on and fill *out.  hwnd=NULL -> primary monitor.
// Returns true if at least one value was read.
bool DisplayCaptureCurrent(HWND hwnd, DISPLAY_PRESET* out);

// Apply preset to the physical monitor the window hwnd lives on.
// Fields set to PRESET_UNSET are skipped.  Snapshots originals on first touch.
// hwnd=NULL -> primary monitor.  Returns true if any attribute was changed.
bool DisplayApplyPreset(HWND hwnd, const DISPLAY_PRESET* preset);

// Restore every monitor we have modified to the values snapshotted at first
// touch.  Call once on application exit.
void DisplayRestoreAll(void);
