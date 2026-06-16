#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "display.h"     // includes Main.h -> DISPLAY_PRESET, PRESET_UNSET

#include <Windows.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Dxva2.lib")

// -----------------------------------------------------------------------
//  Per-physical-monitor state table
//  Handles are NEVER cached here — they are opened fresh for each
//  apply/capture call to avoid stale handles after unplug/replug.
//  What IS cached: capability flags and a snapshot of the original values.
// -----------------------------------------------------------------------

#define MAX_MON_ENTRIES 32

typedef struct {
    wchar_t deviceId[128];   // PHYSICAL_MONITOR description + "_N" index suffix
    DWORD   caps;            // MC_CAPS_* bitmask; 0 = not probed yet
    // Original values snapshotted at first touch
    DWORD   bMin, bOrig, bMax;     // brightness
    DWORD   cMin, cOrig, cMax;     // contrast
    DWORD   origColorTemp;         // MC_COLOR_TEMPERATURE enum value
    bool    hasSnapshot;
} MonEntry;

static MonEntry          gMon[MAX_MON_ENTRIES];
static int               gNumMon     = 0;
static CRITICAL_SECTION  gMonLock;
static bool              gMonInited  = false;

// -----------------------------------------------------------------------
//  Internal helpers
// -----------------------------------------------------------------------

static int FindMon(const wchar_t* id) {
    for (int i = 0; i < gNumMon; i++)
        if (wcsncmp(gMon[i].deviceId, id, 127) == 0) return i;
    return -1;
}

static int GetOrAddMon(const wchar_t* id) {
    int i = FindMon(id);
    if (i >= 0) return i;
    if (gNumMon >= MAX_MON_ENTRIES) return -1;
    i = gNumMon++;
    memset(&gMon[i], 0, sizeof(gMon[i]));
    wcsncpy_s(gMon[i].deviceId, 128, id, _TRUNCATE);
    return i;
}

// Build the device ID used as the map key.
// Multiple physical monitors on the same HMONITOR differ only by index.
static void MakeDevId(const wchar_t* desc, int idx, wchar_t* out, int cch) {
    swprintf_s(out, (size_t)cch, L"%s_%d", desc, idx);
}

// Map an MC_COLOR_TEMPERATURE enum to the nearest integer Kelvin we store.
static int CtToKelvin(MC_COLOR_TEMPERATURE ct) {
    switch (ct) {
        case MC_COLOR_TEMPERATURE_4000K:  return 4000;
        case MC_COLOR_TEMPERATURE_5000K:  return 5000;
        case MC_COLOR_TEMPERATURE_6500K:  return 6500;
        case MC_COLOR_TEMPERATURE_7500K:  return 7500;
        case MC_COLOR_TEMPERATURE_8200K:  return 8200;
        case MC_COLOR_TEMPERATURE_9300K:  return 9300;
        case MC_COLOR_TEMPERATURE_10000K: return 10000;
        case MC_COLOR_TEMPERATURE_11500K: return 11500;
        default: return PRESET_UNSET;
    }
}

// Map an integer Kelvin value to the nearest MC_COLOR_TEMPERATURE enum.
static MC_COLOR_TEMPERATURE KelvinToCt(int kelvin) {
    if (kelvin == PRESET_UNSET) return MC_COLOR_TEMPERATURE_UNKNOWN;
    if (kelvin <= 4500)  return MC_COLOR_TEMPERATURE_4000K;
    if (kelvin <= 5750)  return MC_COLOR_TEMPERATURE_5000K;
    if (kelvin <= 7000)  return MC_COLOR_TEMPERATURE_6500K;
    if (kelvin <= 7850)  return MC_COLOR_TEMPERATURE_7500K;
    if (kelvin <= 8750)  return MC_COLOR_TEMPERATURE_8200K;
    if (kelvin <= 9650)  return MC_COLOR_TEMPERATURE_9300K;
    if (kelvin <= 10750) return MC_COLOR_TEMPERATURE_10000K;
    return MC_COLOR_TEMPERATURE_11500K;
}

static DWORD PctToRaw(int pct, DWORD mn, DWORD mx) {
    if (pct <= 0)   return mn;
    if (pct >= 100) return mx;
    return mn + (DWORD)((mx - mn) * (DWORD)pct / 100u);
}

static int RawToPct(DWORD raw, DWORD mn, DWORD mx) {
    if (mx <= mn || raw <= mn) return 0;
    if (raw >= mx) return 100;
    return (int)((raw - mn) * 100u / (mx - mn));
}

// Open the physical monitor array for the HMONITOR containing hwnd.
// Caller must call DestroyPhysicalMonitors when done.
// Returns count opened (0 on failure).
static DWORD OpenPhysicalsForHwnd(HWND hwnd,
                                   PHYSICAL_MONITOR* arr, DWORD maxArr) {
    HMONITOR hm = MonitorFromWindow(
        hwnd ? hwnd : GetDesktopWindow(),
        MONITOR_DEFAULTTONEAREST);
    if (!hm) return 0;

    DWORD n = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &n)) return 0;
    if (n == 0 || n > maxArr) return 0;
    if (!GetPhysicalMonitorsFromHMONITOR(hm, n, arr)) return 0;
    return n;
}

// Probe capabilities for all physicals on one HMONITOR; update gMon[].
static void ProbeHMonitor(HMONITOR hm) {
    DWORD n = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &n) || n == 0) return;

    PHYSICAL_MONITOR* pms = (PHYSICAL_MONITOR*)
        malloc(sizeof(PHYSICAL_MONITOR) * n);
    if (!pms) return;

    if (GetPhysicalMonitorsFromHMONITOR(hm, n, pms)) {
        for (DWORD i = 0; i < n; i++) {
            wchar_t id[128];
            MakeDevId(pms[i].szPhysicalMonitorDescription, (int)i, id, 128);

            EnterCriticalSection(&gMonLock);
            int idx = GetOrAddMon(id);
            if (idx >= 0) {
                DWORD caps = 0, col = 0;
                if (!GetMonitorCapabilities(pms[i].hPhysicalMonitor,
                                             &caps, &col))
                    caps = 0;
                gMon[idx].caps = caps;
                DbgPrint(L"[display] '%s' caps=0x%08lx", id, caps);
            }
            LeaveCriticalSection(&gMonLock);
        }
        DestroyPhysicalMonitors(n, pms);
    }
    free(pms);
}

static BOOL CALLBACK ProbeEnumProc(HMONITOR hm, HDC hdc,
                                    LPRECT rc, LPARAM lp) {
    (void)hdc; (void)rc; (void)lp;
    ProbeHMonitor(hm);
    return TRUE;
}

// -----------------------------------------------------------------------
//  DisplayInit / DisplayRefresh
// -----------------------------------------------------------------------

void DisplayInit(void) {
    if (!gMonInited) {
        InitializeCriticalSection(&gMonLock);
        gMonInited = true;
    }
    gNumMon = 0;
    memset(gMon, 0, sizeof(gMon));
    EnumDisplayMonitors(NULL, NULL, ProbeEnumProc, 0);
}

void DisplayRefresh(void) {
    if (!gMonInited) { DisplayInit(); return; }
    // Reset caps but keep snapshots so we don't lose originals.
    EnterCriticalSection(&gMonLock);
    for (int i = 0; i < gNumMon; i++) gMon[i].caps = 0;
    LeaveCriticalSection(&gMonLock);
    EnumDisplayMonitors(NULL, NULL, ProbeEnumProc, 0);
}

// -----------------------------------------------------------------------
//  DisplayListUnsupported
// -----------------------------------------------------------------------

int DisplayListUnsupported(wchar_t names[][128], int maxOut) {
    if (!gMonInited) return 0;
    int n = 0;
    EnterCriticalSection(&gMonLock);
    for (int i = 0; i < gNumMon && n < maxOut; i++) {
        if (!(gMon[i].caps & MC_CAPS_BRIGHTNESS)) {
            wcsncpy_s(names[n], 128, gMon[i].deviceId, _TRUNCATE);
            n++;
        }
    }
    LeaveCriticalSection(&gMonLock);
    return n;
}

// -----------------------------------------------------------------------
//  DisplayCaptureCurrent
// -----------------------------------------------------------------------

bool DisplayCaptureCurrent(HWND hwnd, DISPLAY_PRESET* out) {
    if (!out) return false;
    out->Brightness = PRESET_UNSET;
    out->Contrast   = PRESET_UNSET;
    out->ColorTemp  = PRESET_UNSET;

    PHYSICAL_MONITOR pms[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPhysicalsForHwnd(hwnd, pms, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) return false;

    HANDLE h    = pms[0].hPhysicalMonitor;
    bool   ok   = false;
    DWORD  caps = 0, col = 0;

    if (GetMonitorCapabilities(h, &caps, &col)) {
        DWORD mn, cur, mx;
        if ((caps & MC_CAPS_BRIGHTNESS) &&
            GetMonitorBrightness(h, &mn, &cur, &mx)) {
            out->Brightness = RawToPct(cur, mn, mx);
            ok = true;
        }
        if ((caps & MC_CAPS_CONTRAST) &&
            GetMonitorContrast(h, &mn, &cur, &mx)) {
            out->Contrast = RawToPct(cur, mn, mx);
            ok = true;
        }
        // Try color temp unconditionally: many monitors support VCP 0x14 but
        // don't advertise MC_CAPS_COLOR_TEMPERATURE in the high-level caps.
        MC_COLOR_TEMPERATURE ct = MC_COLOR_TEMPERATURE_UNKNOWN;
        if (GetMonitorColorTemperature(h, &ct) &&
            ct != MC_COLOR_TEMPERATURE_UNKNOWN) {
            out->ColorTemp = CtToKelvin(ct);
            ok = true;
        }
    }

    DestroyPhysicalMonitors(n, pms);
    return ok;
}

// -----------------------------------------------------------------------
//  DisplayApplyPreset
// -----------------------------------------------------------------------

bool DisplayApplyPreset(HWND hwnd, const DISPLAY_PRESET* preset) {
    if (!preset) return false;

    PHYSICAL_MONITOR pms[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPhysicalsForHwnd(hwnd, pms, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) {
        DbgPrint(L"[display] ApplyPreset: no physical monitors found");
        return false;
    }

    bool anySet = false;

    for (DWORD pi = 0; pi < n; pi++) {
        HANDLE h = pms[pi].hPhysicalMonitor;
        wchar_t id[128];
        MakeDevId(pms[pi].szPhysicalMonitorDescription, (int)pi, id, 128);

        // Phase 1: read state under lock (quick — no DDC/CI here)
        DWORD caps        = 0;
        bool  firstTouch  = false;
        EnterCriticalSection(&gMonLock);
        int si = GetOrAddMon(id);
        if (si >= 0) {
            if (gMon[si].caps == 0) {
                // Lazy probe: GetMonitorCapabilities is fast and not I2C-intensive
                DWORD c = 0, col = 0;
                if (!GetMonitorCapabilities(h, &c, &col)) c = 0;
                gMon[si].caps = c;
            }
            caps       = gMon[si].caps;
            firstTouch = !gMon[si].hasSnapshot;
        }
        LeaveCriticalSection(&gMonLock);

        // Phase 2: DDC/CI work — performed WITHOUT holding gMonLock.
        // gMonLock guards gMon[] state only; I2C bus access is serialised
        // by the single worker thread (all Apply/poll jobs run on it).
        // Each Set* call is wrapped in __try/__except because some monitor
        // drivers throw SEH exceptions (hardware faults) on specific
        // command sequences.
        DWORD bMin=0, bOrig=0, bMax=0;
        DWORD cMin=0, cOrig=0, cMax=0;
        DWORD ctOrig = 0;

        if (preset->Brightness != PRESET_UNSET && (caps & MC_CAPS_BRIGHTNESS)) {
            DWORD mn=0, cur=0, mx=0;
            if (GetMonitorBrightness(h, &mn, &cur, &mx)) {
                if (firstTouch) { bMin=mn; bOrig=cur; bMax=mx; }
                DWORD want = PctToRaw(preset->Brightness, mn, mx);
                if (want != cur) {
                    __try { SetMonitorBrightness(h, want); anySet = true; }
                    __except(EXCEPTION_EXECUTE_HANDLER) {
                        DbgPrint(L"[display] SEH 0x%08x in SetMonitorBrightness",
                                 GetExceptionCode());
                    }
                }
            }
        }

        if (preset->Contrast != PRESET_UNSET && (caps & MC_CAPS_CONTRAST)) {
            DWORD mn=0, cur=0, mx=0;
            if (GetMonitorContrast(h, &mn, &cur, &mx)) {
                if (firstTouch) { cMin=mn; cOrig=cur; cMax=mx; }
                DWORD want = PctToRaw(preset->Contrast, mn, mx);
                if (want != cur) {
                    __try { SetMonitorContrast(h, want); anySet = true; }
                    __except(EXCEPTION_EXECUTE_HANDLER) {
                        DbgPrint(L"[display] SEH 0x%08x in SetMonitorContrast",
                                 GetExceptionCode());
                    }
                }
            }
        }

        if (preset->ColorTemp != PRESET_UNSET &&
            (caps & MC_CAPS_COLOR_TEMPERATURE)) {
            MC_COLOR_TEMPERATURE ctCur = MC_COLOR_TEMPERATURE_UNKNOWN;
            if (GetMonitorColorTemperature(h, &ctCur)) {
                if (firstTouch) ctOrig = (DWORD)ctCur;
                MC_COLOR_TEMPERATURE ctNew = KelvinToCt(preset->ColorTemp);
                if (ctNew != MC_COLOR_TEMPERATURE_UNKNOWN && ctNew != ctCur) {
                    __try { SetMonitorColorTemperature(h, ctNew); anySet = true; }
                    __except(EXCEPTION_EXECUTE_HANDLER) {
                        DbgPrint(L"[display] SEH 0x%08x in SetMonitorColorTemperature",
                                 GetExceptionCode());
                    }
                }
            }
        }

        // Phase 3: write snapshot back under lock
        if (firstTouch && si >= 0) {
            EnterCriticalSection(&gMonLock);
            if (!gMon[si].hasSnapshot) {  // re-check: another thread might have set it
                if (bMax > bMin) { gMon[si].bMin=bMin; gMon[si].bOrig=bOrig; gMon[si].bMax=bMax; }
                if (cMax > cMin) { gMon[si].cMin=cMin; gMon[si].cOrig=cOrig; gMon[si].cMax=cMax; }
                if (ctOrig)        gMon[si].origColorTemp = ctOrig;
                gMon[si].hasSnapshot = true;
            }
            LeaveCriticalSection(&gMonLock);
        }
    }

    DestroyPhysicalMonitors(n, pms);
    return anySet;
}

// -----------------------------------------------------------------------
//  DisplayRestoreAll
//  Enumerate all monitors, open physicals, match by description, restore.
//  Called at exit (effectively single-threaded at that point).
// -----------------------------------------------------------------------

// File-scope collector for the restore enum callback.
#define RESTORE_PM_MAX (MAX_MON_ENTRIES * MAX_PHYSICAL_PER_HMONITOR)
static PHYSICAL_MONITOR gRestorePMs[RESTORE_PM_MAX];
static int               gRestorePMCount = 0;

static BOOL CALLBACK RestoreCollectProc(HMONITOR hm, HDC hdc,
                                         LPRECT rc, LPARAM lp) {
    (void)hdc; (void)rc; (void)lp;
    DWORD n = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &n) || n == 0)
        return TRUE;
    if ((int)n > RESTORE_PM_MAX - gRestorePMCount) return TRUE;
    if (GetPhysicalMonitorsFromHMONITOR(hm, n, &gRestorePMs[gRestorePMCount]))
        gRestorePMCount += (int)n;
    return TRUE;
}

void DisplayRestoreAll(void) {
    if (!gMonInited) return;
    DbgPrint(L"[display] RestoreAll begin (%d entries)", gNumMon);

    gRestorePMCount = 0;
    EnumDisplayMonitors(NULL, NULL, RestoreCollectProc, 0);

    // For each collected physical monitor, find the matching gMon entry
    // by matching the description prefix (deviceId is "desc_N").
    for (int pi = 0; pi < gRestorePMCount; pi++) {
        PHYSICAL_MONITOR* pm = &gRestorePMs[pi];
        const wchar_t* desc  = pm->szPhysicalMonitorDescription;

        for (int si = 0; si < gNumMon; si++) {
            if (!gMon[si].hasSnapshot) continue;
            // deviceId = "description_index"; strip the _N suffix to compare
            wchar_t prefix[128];
            wcsncpy_s(prefix, 128, gMon[si].deviceId, _TRUNCATE);
            wchar_t* us = wcsrchr(prefix, L'_');
            if (us) *us = L'\0';

            if (wcsncmp(desc, prefix, 127) != 0) continue;

            HANDLE h = pm->hPhysicalMonitor;
            if (gMon[si].bMax > gMon[si].bMin)
                SetMonitorBrightness(h, gMon[si].bOrig);
            if (gMon[si].cMax > gMon[si].cMin)
                SetMonitorContrast(h, gMon[si].cOrig);
            if (gMon[si].origColorTemp != 0 &&
                gMon[si].origColorTemp != (DWORD)MC_COLOR_TEMPERATURE_UNKNOWN)
                SetMonitorColorTemperature(
                    h, (MC_COLOR_TEMPERATURE)gMon[si].origColorTemp);

            DbgPrint(L"[display] restored '%s'", gMon[si].deviceId);
            gMon[si].hasSnapshot = false; // don't restore again
            break;
        }
    }

    if (gRestorePMCount > 0)
        DestroyPhysicalMonitors((DWORD)gRestorePMCount, gRestorePMs);
}
