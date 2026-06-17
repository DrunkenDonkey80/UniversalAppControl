#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "display.h"
#include <Windows.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Dxva2.lib")

// -----------------------------------------------------------------------
//  Primary-monitor state  (everything here is primary-only)
// -----------------------------------------------------------------------

typedef struct {
    DWORD bMin, bOrig, bMax;   // brightness snapshot
    DWORD cMin, cOrig, cMax;   // contrast snapshot
    DWORD origVcp14;            // VCP 0x14 color-temp code at first touch
    DWORD origVcpF0;            // VCP 0xF0 preset mode at first touch
    DWORD caps;                 // MC_CAPS_* bitmask
    bool  hasSnapshot;
    bool  capsProbed;
} PrimState;

static PrimState         gPrim       = {0};
static HMONITOR          gPrimaryHM  = NULL;
static CRITICAL_SECTION  gMonLock;
static bool              gMonInited  = false;

// Exported: supported VCP 0x14 codes for primary monitor (populated at init).
BYTE gPrimaryVcp14Vals[MAX_VCP14_VALS] = {0};
int  gPrimaryVcp14Count = 0;  // 0 = not probed, -1 = not supported

// Exported: supported VCP 0xF0 codes (named preset modes, e.g. ComfortView/FPS/Game1).
BYTE gPrimaryVcpE2Vals[MAX_VCP14_VALS] = {0};
int  gPrimaryVcpE2Count = 0;
BYTE gPrimaryVcpF0Vals[MAX_VCP14_VALS] = {0}; // VCP F0 picture-mode codes (writable on Dell)
int  gPrimaryVcpF0Count = 0;

// Probed preset table — one entry per VCP 0xF0 code.
MonPresetInfo    gMonPresets[MAX_VCP14_VALS] = {0};
int              gMonPresetCount  = 0;

// Last values WE applied (worker-thread only; no locking needed).
static struct { int b, c, ct, pm; } gLastApplied =
    { PRESET_UNSET, PRESET_UNSET, PRESET_UNSET, PRESET_UNSET };

// -----------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------

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

// Parse "14(XX XX ...)" out of an ASCII capabilities string.
static void ParseVcp14Caps(const char* cap, BYTE* out, int* count) {
    *count = 0;
    if (!cap) return;
    const char* p = cap;
    while (*p) {
        if (p[0]=='1' && p[1]=='4' && p[2]=='(') break;
        p++;
    }
    if (!*p) return;
    p += 3;
    while (*p && *p != ')' && *count < MAX_VCP14_VALS) {
        while (*p == ' ') p++;
        if (*p == ')') break;
        unsigned v = 0; int d = 0;
        while ((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F')) {
            v = v*16 + ((*p>='0'&&*p<='9')? *p-'0':
                        (*p>='a'&&*p<='f')? *p-'a'+10: *p-'A'+10);
            p++; d++;
        }
        if (d > 0) out[(*count)++] = (BYTE)v;
        while (*p == ' ') p++;
    }
}

// Find the primary HMONITOR (callback stops at first primary).
static BOOL CALLBACK FindPrimaryProc(HMONITOR hm, HDC dc, LPRECT r, LPARAM lp) {
    (void)dc; (void)r;
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hm, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY)) {
        *(HMONITOR*)lp = hm;
        return FALSE;
    }
    return TRUE;
}

// Open physical monitors for the primary HMONITOR.
// Caller must DestroyPhysicalMonitors when done.
static DWORD OpenPrimaryPhysicals(PHYSICAL_MONITOR* arr, DWORD maxn) {
    if (!gPrimaryHM) return 0;
    DWORD n = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(gPrimaryHM, &n)) return 0;
    if (n == 0 || n > maxn) return 0;
    if (!GetPhysicalMonitorsFromHMONITOR(gPrimaryHM, n, arr)) return 0;
    return n;
}

// Map a ColorTemp preset value to a VCP 0x14 code to send.
//   PRESET_UNSET  → 0xFF (skip)
//   1-255         → use directly as VCP code
//   >= 256        → legacy Kelvin; map to closest supported code
static BYTE CtToVcp(int colorTemp) {
    if (colorTemp == PRESET_UNSET) return 0xFF;
    if (colorTemp >= 1 && colorTemp <= 255) return (BYTE)colorTemp; // direct VCP code
    // Legacy Kelvin: find closest among supported codes
    if (gPrimaryVcp14Count > 0) {
        // kVcp14Kelvin: approximate Kelvin for each standard MCCS code
        static const int kK[] = {0,6500,6500,4000,5000,6500,7500,8200,9300,10000,11500,6500,6500};
        BYTE best = gPrimaryVcp14Vals[0]; int bestD = 99999;
        for (int i = 0; i < gPrimaryVcp14Count; i++) {
            BYTE code = gPrimaryVcp14Vals[i];
            if ((code==0x0B||code==0x0C) && gPrimaryVcp14Count > 1) continue;
            int k = (code < 13) ? kK[code] : 6500;
            int d = colorTemp - k; if (d<0) d=-d;
            if (d < bestD) { bestD=d; best=code; }
        }
        return best;
    }
    // No list — fall back to MCCS standard mapping
    if (colorTemp <= 4500) return 0x03;
    if (colorTemp <= 5500) return 0x04;
    if (colorTemp <= 7000) return 0x05;
    if (colorTemp <= 7800) return 0x06;
    if (colorTemp <= 8700) return 0x07;
    if (colorTemp <= 9600) return 0x08;
    if (colorTemp <= 10700) return 0x09;
    return 0x0A;
}

// -----------------------------------------------------------------------
//  Public helpers
// -----------------------------------------------------------------------

const wchar_t* GetVcp14Label(BYTE code) {
    switch (code) {
        case 0x01: return L"sRGB (6500K)";
        case 0x02: return L"Native";
        case 0x03: return L"4000K";
        case 0x04: return L"5000K";
        case 0x05: return L"6500K";
        case 0x06: return L"7500K";
        case 0x07: return L"8200K";
        case 0x08: return L"9300K";
        case 0x09: return L"10000K";
        case 0x0A: return L"11500K";
        case 0x0B: return L"Custom Color";
        case 0x0C: return L"User Color (warm)";
        default:   return L"Unknown";
    }
}

// Label for a (vcpCode, vcpValue) pair: e.g. "VCP E2:0E"
// Used for gMonPresets and combo display.
const wchar_t* GetVcpE2Label(BYTE code) {
    // Legacy: called with just the value; kept for DisplayRecordProfile path
    static wchar_t buf[16];
    swprintf_s(buf, 16, L"Profile %02X", code);
    return buf;
}

const wchar_t* FormatPresetLabel(BYTE vcpCode, BYTE vcpValue) {
    static wchar_t buf[24];
    swprintf_s(buf, 24, L"VCP %02X:%02X", vcpCode, vcpValue);
    return buf;
}

void DisplayResetLastApplied(void) {
    gLastApplied.b  = PRESET_UNSET;
    gLastApplied.c  = PRESET_UNSET;
    gLastApplied.ct = PRESET_UNSET;
    gLastApplied.pm = PRESET_UNSET;
}

// Read the current VCP 0xF0 value — GET only, never changes anything.
bool DisplayIsInited(void)  { return gMonInited; }

// Returns the handle of the first primary physical monitor.
// Opens a new set each call — caller must call DisplayReleasePrimaryHandle() when done.
static PHYSICAL_MONITOR s_hPrimPm[MAX_PHYSICAL_PER_HMONITOR];
static DWORD            s_hPrimN  = 0;
HANDLE DisplayGetPrimaryHandle(void) {
    if (s_hPrimN > 0) DestroyPhysicalMonitors(s_hPrimN, s_hPrimPm);
    s_hPrimN = OpenPrimaryPhysicals(s_hPrimPm, MAX_PHYSICAL_PER_HMONITOR);
    return (s_hPrimN > 0) ? s_hPrimPm[0].hPhysicalMonitor : NULL;
}

void DisplayProbeVcp(BYTE vcpCode, BOOL* outOk, DWORD* outCur, DWORD* outMax) {
    *outOk = FALSE; *outCur = 0; *outMax = 0;
    PHYSICAL_MONITOR pm[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPrimaryPhysicals(pm, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) return;
    HANDLE h = pm[0].hPhysicalMonitor;
    DWORD vcpType=0, cur=0, mx=0;
    BOOL ok = FALSE;
    __try { ok = GetVCPFeatureAndVCPFeatureReply(h, vcpCode, &vcpType, &cur, &mx); }
    __except(EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
    *outOk = ok; *outCur = cur; *outMax = mx;
    DestroyPhysicalMonitors(n, pm);
}

int DisplayReadCurrentPreset(void) {
    PHYSICAL_MONITOR pm[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPrimaryPhysicals(pm, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) return PRESET_UNSET;
    HANDLE h = pm[0].hPhysicalMonitor;
    static const BYTE kCandidates[] = { 0xE2, 0xDC, 0x14 };
    int result = PRESET_UNSET;
    for (int ci = 0; ci < (int)(sizeof(kCandidates)/sizeof(kCandidates[0])); ci++) {
        DWORD vt=0, vc=0, vm=0; BOOL ok=FALSE;
        __try { ok = GetVCPFeatureAndVCPFeatureReply(h, kCandidates[ci], &vt, &vc, &vm); }
        __except(EXCEPTION_EXECUTE_HANDLER) { ok=FALSE; }
        if (ok && vm > 1) { result = (int)vc; break; }
    }
    DestroyPhysicalMonitors(n, pm);
    return result;
}

// Add vcpCode to gMonPresets[] with a label from GetVcpF0Label().
// No-ops if the code is already in the list.  Returns true if newly added.
bool DisplayRecordProfile(int vcpReg, int vcpCode) {
    if (vcpCode < 0 || vcpCode > 255) return false;
    // Already recorded if same reg+code pair exists
    for (int i = 0; i < gMonPresetCount; i++)
        if (gMonPresets[i].vcpReg == (BYTE)vcpReg && gMonPresets[i].vcpCode == (BYTE)vcpCode)
            return false;
    if (gMonPresetCount >= MAX_VCP14_VALS) return false;
    int i = gMonPresetCount++;
    memset(&gMonPresets[i], 0, sizeof(gMonPresets[i]));
    gMonPresets[i].vcpReg     = (BYTE)vcpReg;
    gMonPresets[i].vcpCode    = (BYTE)vcpCode;
    gMonPresets[i].brightness = PRESET_UNSET;
    gMonPresets[i].contrast   = PRESET_UNSET;
    gMonPresets[i].scanned    = false;
    wcscpy_s(gMonPresets[i].name, 64, FormatPresetLabel((BYTE)vcpReg, (BYTE)vcpCode));
    CrashLog("[display] RecordProfile VCP%02X:%02X\n", (BYTE)vcpReg, (BYTE)vcpCode);
    return true;
}

// -----------------------------------------------------------------------
//  DisplayInit / DisplayRefresh
// -----------------------------------------------------------------------

void DisplayInit(void) {
    if (!gMonInited) {
        InitializeCriticalSection(&gMonLock);
        gMonInited = true;
    }

    // Find the primary HMONITOR
    gPrimaryHM = NULL;
    EnumDisplayMonitors(NULL, NULL, FindPrimaryProc, (LPARAM)&gPrimaryHM);

    // Reset last-applied tracking on every init
    DisplayResetLastApplied();

    // Clear caps / VCP list (keep snapshot so originals survive a display change)
    EnterCriticalSection(&gMonLock);
    gPrim.caps        = 0;
    gPrim.capsProbed  = false;
    gPrimaryVcp14Count = 0;
    LeaveCriticalSection(&gMonLock);

    if (!gPrimaryHM) return;

    // Open the primary physical and read capabilities once at startup.
    PHYSICAL_MONITOR pm[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPrimaryPhysicals(pm, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) return;

    HANDLE h = pm[0].hPhysicalMonitor;

    // High-level caps
    DWORD caps = 0, col = 0;
    if (GetMonitorCapabilities(h, &caps, &col)) {
        EnterCriticalSection(&gMonLock);
        gPrim.caps       = caps;
        gPrim.capsProbed = true;
        LeaveCriticalSection(&gMonLock);
    }

    // VCP 0x14 supported values from capabilities string
    DWORD capLen = 0;
    if (GetCapabilitiesStringLength(h, &capLen) && capLen > 0) {
        char* capStr = (char*)malloc(capLen + 2);
        if (capStr) {
            capStr[0] = '\0';
            if (CapabilitiesRequestAndCapabilitiesReply(h, capStr, capLen)) {
                int cnt = 0;
                BYTE vals[MAX_VCP14_VALS];
                ParseVcp14Caps(capStr, vals, &cnt);
                EnterCriticalSection(&gMonLock);
                // Parse VCP 0x14 (color temperature presets)
                if (cnt > 0) {
                    memcpy(gPrimaryVcp14Vals, vals, (size_t)cnt);
                    gPrimaryVcp14Count = cnt;
                } else {
                    gPrimaryVcp14Count = -1;
                }
                // Helper lambda-style macro to parse one VCP block e.g. "E2(0C 0D 0E)"
                #define PARSE_VCP_BLOCK(TAG1, TAG2, OUTVALS, OUTCOUNT) do { \
                    int _cnt=0; BYTE _v[MAX_VCP14_VALS]; const char*_p=capStr; \
                    while(*_p){if(_p[0]==(TAG1)&&_p[1]==(TAG2)&&_p[2]=='('){break;}_p++;} \
                    if(*_p){_p+=3; while(*_p&&*_p!=')'&&_cnt<MAX_VCP14_VALS){ \
                        while(*_p==' ')_p++; if(*_p==')')break; \
                        unsigned _val=0;int _d=0; \
                        while((*_p>='0'&&*_p<='9')||(*_p>='a'&&*_p<='f')||(*_p>='A'&&*_p<='F')){ \
                            _val=_val*16+((*_p>='0'&&*_p<='9')?*_p-'0':(*_p>='a'&&*_p<='f')?*_p-'a'+10:*_p-'A'+10); \
                            _p++;_d++; } if(_d>0)_v[_cnt++]=(BYTE)_val; while(*_p==' ')_p++; \
                    }} \
                    if(_cnt>0){memcpy((OUTVALS),_v,(size_t)_cnt);(OUTCOUNT)=_cnt;}else{(OUTCOUNT)=-1;} \
                } while(0)

                // VCP 0xE2 — Dell picture-mode READ register (cur=active mode, but WRITES do nothing)
                PARSE_VCP_BLOCK('E','2', gPrimaryVcpE2Vals, gPrimaryVcpE2Count);
                // VCP 0xF0 — Dell picture-mode WRITE register (confirmed writable via old scan)
                PARSE_VCP_BLOCK('F','0', gPrimaryVcpF0Vals, gPrimaryVcpF0Count);
                LeaveCriticalSection(&gMonLock);
            }
            free(capStr);
        }
    }
    if (gPrimaryVcp14Count == 0) {
        EnterCriticalSection(&gMonLock);
        gPrimaryVcp14Count = -1;
        LeaveCriticalSection(&gMonLock);
    }

    DestroyPhysicalMonitors(n, pm);
    CrashLog("[display] Init done: caps=0x%08lX vcp14Count=%d vcpE2Count=%d\n",
             gPrim.caps, gPrimaryVcp14Count, gPrimaryVcpE2Count);
}

// Populate gMonPresets[] from the capabilities-string VCP codes discovered during
// DisplayInit (gPrimaryVcpE2Vals).  Called once after LoadMonPresets() so that INI
// entries loaded first are not re-added.  This means the combo is always pre-filled
// with every mode the monitor advertises — no Capture button needed.
void DisplayPopulatePresetsFromCaps(void) {
    // Prefer VCP 0xF0 — confirmed writable on Dell monitors (old scan).
    // E2 reads the current mode but writes are silently ignored on this hardware.
    if (gPrimaryVcpF0Count > 0) {
        for (int i = 0; i < gPrimaryVcpF0Count; i++)
            DisplayRecordProfile(0xF0, (int)gPrimaryVcpF0Vals[i]);
        CrashLog("[display] PopulatePresetsFromCaps: %d F0 entries -> gMonPresetCount=%d\n",
                 gPrimaryVcpF0Count, gMonPresetCount);
    } else if (gPrimaryVcpE2Count > 0) {
        // Fallback: monitor has E2 but not F0 — may be writable on non-Dell hardware
        for (int i = 0; i < gPrimaryVcpE2Count; i++)
            DisplayRecordProfile(0xE2, (int)gPrimaryVcpE2Vals[i]);
        CrashLog("[display] PopulatePresetsFromCaps: %d E2 entries (F0 absent) -> gMonPresetCount=%d\n",
                 gPrimaryVcpE2Count, gMonPresetCount);
    } else {
        CrashLog("[display] PopulatePresetsFromCaps: no F0 or E2 caps found\n");
    }
}

void DisplayRefresh(void) {
    if (!gMonInited) { DisplayInit(); return; }
    // Re-probe without clearing the originals snapshot
    gPrimaryHM = NULL;
    EnumDisplayMonitors(NULL, NULL, FindPrimaryProc, (LPARAM)&gPrimaryHM);

    EnterCriticalSection(&gMonLock);
    gPrim.caps        = 0;
    gPrim.capsProbed  = false;
    gPrimaryVcp14Count = 0;
    LeaveCriticalSection(&gMonLock);

    DisplayResetLastApplied();

    if (!gPrimaryHM) return;

    PHYSICAL_MONITOR pm[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPrimaryPhysicals(pm, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) return;

    HANDLE h = pm[0].hPhysicalMonitor;
    DWORD caps = 0, col = 0;
    if (GetMonitorCapabilities(h, &caps, &col)) {
        EnterCriticalSection(&gMonLock);
        gPrim.caps = caps; gPrim.capsProbed = true;
        LeaveCriticalSection(&gMonLock);
    }
    DWORD capLen = 0;
    if (GetCapabilitiesStringLength(h, &capLen) && capLen > 0) {
        char* capStr = (char*)malloc(capLen + 2);
        if (capStr) {
            capStr[0] = '\0';
            if (CapabilitiesRequestAndCapabilitiesReply(h, capStr, capLen)) {
                int cnt = 0; BYTE vals[MAX_VCP14_VALS];
                ParseVcp14Caps(capStr, vals, &cnt);
                EnterCriticalSection(&gMonLock);
                if (cnt > 0) { memcpy(gPrimaryVcp14Vals,vals,(size_t)cnt); gPrimaryVcp14Count=cnt; }
                else gPrimaryVcp14Count = -1;
                LeaveCriticalSection(&gMonLock);
            }
            free(capStr);
        }
    }
    if (gPrimaryVcp14Count == 0) {
        EnterCriticalSection(&gMonLock);
        gPrimaryVcp14Count = -1;
        LeaveCriticalSection(&gMonLock);
    }
    DestroyPhysicalMonitors(n, pm);
}

// -----------------------------------------------------------------------
//  DisplayListUnsupported
// -----------------------------------------------------------------------

int DisplayListUnsupported(wchar_t names[][128], int maxOut) {
    if (!gMonInited || maxOut <= 0) return 0;
    EnterCriticalSection(&gMonLock);
    bool supported = gPrim.capsProbed && (gPrim.caps & MC_CAPS_BRIGHTNESS);
    LeaveCriticalSection(&gMonLock);
    if (!supported) {
        wcsncpy_s(names[0], 128, L"Primary monitor", _TRUNCATE);
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------
//  DisplayCaptureCurrent  (primary monitor only)
// -----------------------------------------------------------------------

bool DisplayCaptureCurrent(HWND hwnd, DISPLAY_PRESET* out) {
    (void)hwnd;   // always uses primary
    if (!out || !gMonInited) return false;
    out->Brightness  = PRESET_UNSET;
    out->Contrast    = PRESET_UNSET;
    out->ColorTemp   = PRESET_UNSET;
    out->ProfileMode = PRESET_UNSET;

    PHYSICAL_MONITOR pm[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPrimaryPhysicals(pm, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) return false;

    HANDLE h = pm[0].hPhysicalMonitor;
    bool ok  = false;

    EnterCriticalSection(&gMonLock);
    DWORD caps = gPrim.caps;
    LeaveCriticalSection(&gMonLock);

    if (!caps) { GetMonitorCapabilities(h, &caps, &(DWORD){0}); }

    DWORD mn, cur, mx;
    if ((caps & MC_CAPS_BRIGHTNESS) && GetMonitorBrightness(h, &mn, &cur, &mx))
        { out->Brightness = RawToPct(cur, mn, mx); ok = true; }
    if ((caps & MC_CAPS_CONTRAST)   && GetMonitorContrast(h, &mn, &cur, &mx))
        { out->Contrast   = RawToPct(cur, mn, mx); ok = true; }

    // Color temperature: VCP 0x14, store raw code (1-12)
    DWORD vcpType=0, vcpCur=0, vcpMax=0;
    if (GetVCPFeatureAndVCPFeatureReply(h, 0x14, &vcpType, &vcpCur, &vcpMax) && vcpCur >= 1) {
        out->ColorTemp = (int)vcpCur;
        ok = true;
    }

    // Picture-mode preset: probe common VCP registers in priority order.
    // First non-zero hit wins — this auto-discovers the right register for any monitor:
    //   0xE2 = Dell S-series / AW-series picture mode
    //   0xDC = MCCS Display Mode (LG, Samsung, others)
    //   0x14 = MCCS Color Preset / temperature (most monitors)
    // Storing both the register (ProfileModeVcp) and value (ProfileMode) means
    // the same value is written back to the correct register on apply.
    out->ProfileMode    = PRESET_UNSET;
    out->ProfileModeVcp = 0;
    {
        static const BYTE kCandidates[] = { 0xE2, 0xDC, 0x14 };
        for (int ci = 0; ci < (int)(sizeof(kCandidates)/sizeof(kCandidates[0])); ci++) {
            BYTE vcp = kCandidates[ci];
            DWORD vt=0, vc=0, vm=0; BOOL vok=FALSE;
            __try { vok = GetVCPFeatureAndVCPFeatureReply(h, vcp, &vt, &vc, &vm); }
            __except(EXCEPTION_EXECUTE_HANDLER) { vok=FALSE; }
            if (vok && vm > 1) { // max>1 means it's a multi-value selector, not a boolean
                out->ProfileMode    = (int)vc;
                out->ProfileModeVcp = (int)vcp;
                CrashLog("[display] Capture: VCP 0x%02X cur=0x%02lX max=0x%02lX\n", vcp, vc, vm);
                ok = true;
                break;
            }
        }
    }

    DestroyPhysicalMonitors(n, pm);
    return ok;
}

// -----------------------------------------------------------------------
//  DisplayApplyPreset  (primary monitor only)
//
//  force=true  : bypass gLastApplied (always send, e.g. Apply button)
//  force=false : skip unchanged values (foreground-poll path)
// -----------------------------------------------------------------------

bool DisplayApplyPreset(HWND hwnd, const DISPLAY_PRESET* preset, bool force) {
    (void)hwnd;  // always primary
    if (!preset || !gMonInited || !gPrimaryHM) return false;

    CrashLog("[display] ApplyPreset B=%d C=%d CT=%d force=%d\n",
             preset->Brightness, preset->Contrast, preset->ColorTemp, force);

    PHYSICAL_MONITOR pm[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPrimaryPhysicals(pm, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) return false;

    HANDLE h = pm[0].hPhysicalMonitor;
    bool anySet = false;

    // ---- Phase 1: caps + firstTouch (under lock) ----
    EnterCriticalSection(&gMonLock);
    if (!gPrim.capsProbed) {
        DWORD c=0, col=0;
        if (GetMonitorCapabilities(h, &c, &col)) gPrim.caps = c;
        gPrim.capsProbed = true;
    }
    DWORD caps      = gPrim.caps;
    bool  firstTouch = !gPrim.hasSnapshot;
    LeaveCriticalSection(&gMonLock);

    // ---- Phase 2: DDC/CI work (no lock held) ----
    DWORD bMin=0, bOrig=0, bMax=0;
    DWORD cMin=0, cOrig=0, cMax=0;
    DWORD vcp14Orig = 0;
    DWORD vcpE2Orig = 0;

    // Picture-mode preset.
    // ProfileModeVcp=0 means captured before VCP tracking was added; probe the
    // standard candidates at apply-time to find the right register automatically.
    if (preset->ProfileMode != PRESET_UNSET) {
        BYTE vcp  = (preset->ProfileModeVcp > 0) ? (BYTE)preset->ProfileModeVcp : 0;
        if (vcp == 0) {
            // Auto-discover which register to use (same logic as capture)
            static const BYTE kApplyCands[] = { 0xE2, 0xDC, 0x14 };
            for (int ci = 0; ci < 3; ci++) {
                DWORD vt2=0, vc2=0, vm2=0; BOOL ok2=FALSE;
                __try { ok2 = GetVCPFeatureAndVCPFeatureReply(h, kApplyCands[ci], &vt2, &vc2, &vm2); }
                __except(EXCEPTION_EXECUTE_HANDLER) { ok2=FALSE; }
                if (ok2 && vm2 > 1) { vcp = kApplyCands[ci]; break; }
            }
        }
        if (vcp == 0) goto skip_preset; // no usable register found
        // ProfileMode=0 with ProfileModeVcp=0 = legacy uncaptured value; skip
        if (preset->ProfileMode == 0 && preset->ProfileModeVcp == 0) goto skip_preset;
        BYTE want = (BYTE)preset->ProfileMode;
        bool skip = !force && ((int)want == gLastApplied.pm);
        if (skip) {
            CrashLog("[display] PM VCP=0x%02X val=0x%02X unchanged, skip\n", vcp, want);
        } else {
            DWORD fType=0, fCur=0, fMax=0; BOOL got=FALSE;
            __try { got = GetVCPFeatureAndVCPFeatureReply(h, vcp, &fType, &fCur, &fMax); }
            __except(EXCEPTION_EXECUTE_HANDLER) { }
            if (got) {
                if (firstTouch) vcpE2Orig = fCur;
                if (force || fCur != want) {
                    BOOL setOk=FALSE;
                    __try { setOk = SetVCPFeature(h, vcp, want); anySet = true; }
                    __except(EXCEPTION_EXECUTE_HANDLER) { }
                    gLastApplied.pm = (int)want;
                    CrashLog("[display] SetVCP 0x%02X val=0x%02X -> ok=%d\n", vcp, want, setOk);
                    Sleep(80);
                } else {
                    gLastApplied.pm = (int)want;
                }
            }
        }
    }
    skip_preset:;

    // Brightness
    if (preset->Brightness != PRESET_UNSET && (caps & MC_CAPS_BRIGHTNESS)) {
        bool skip = !force && (preset->Brightness == gLastApplied.b);
        if (skip) {
            CrashLog("[display] B=%d unchanged, skip\n", preset->Brightness);
        } else {
            DWORD mn=0, cur=0, mx=0; BOOL got=FALSE;
            __try { got = GetMonitorBrightness(h, &mn, &cur, &mx); }
            __except(EXCEPTION_EXECUTE_HANDLER) { }
            if (got) {
                if (firstTouch) { bMin=mn; bOrig=cur; bMax=mx; }
                DWORD want = PctToRaw(preset->Brightness, mn, mx);
                if (force || want != cur) {
                    __try { SetMonitorBrightness(h, want); anySet = true; }
                    __except(EXCEPTION_EXECUTE_HANDLER) { }
                    gLastApplied.b = preset->Brightness;
                    CrashLog("[display] SetBrightness %d%% (raw %lu)\n", preset->Brightness, want);
                } else {
                    gLastApplied.b = preset->Brightness; // already there
                }
            }
        }
    }

    // Contrast
    if (preset->Contrast != PRESET_UNSET && (caps & MC_CAPS_CONTRAST)) {
        bool skip = !force && (preset->Contrast == gLastApplied.c);
        if (skip) {
            CrashLog("[display] C=%d unchanged, skip\n", preset->Contrast);
        } else {
            DWORD mn=0, cur=0, mx=0; BOOL got=FALSE;
            __try { got = GetMonitorContrast(h, &mn, &cur, &mx); }
            __except(EXCEPTION_EXECUTE_HANDLER) { }
            if (got) {
                if (firstTouch) { cMin=mn; cOrig=cur; cMax=mx; }
                DWORD want = PctToRaw(preset->Contrast, mn, mx);
                if (force || want != cur) {
                    __try { SetMonitorContrast(h, want); anySet = true; }
                    __except(EXCEPTION_EXECUTE_HANDLER) { }
                    gLastApplied.c = preset->Contrast;
                    CrashLog("[display] SetContrast %d%% (raw %lu)\n", preset->Contrast, want);
                } else {
                    gLastApplied.c = preset->Contrast;
                }
            }
        }
    }

    // Color temperature (VCP 0x14)
    if (preset->ColorTemp != PRESET_UNSET) {
        BYTE want14 = CtToVcp(preset->ColorTemp);
        bool skip = !force && ((int)want14 == gLastApplied.ct);
        if (skip) {
            CrashLog("[display] CT=VCP%02X unchanged, skip\n", want14);
        } else if (want14 != 0xFF) {
            DWORD vcpType=0, vcpCur=0, vcpMax=0; BOOL got=FALSE;
            __try { got = GetVCPFeatureAndVCPFeatureReply(h, 0x14, &vcpType, &vcpCur, &vcpMax); }
            __except(EXCEPTION_EXECUTE_HANDLER) { }
            if (got) {
                if (firstTouch && vcpCur) vcp14Orig = vcpCur;
                if (force || want14 != (BYTE)vcpCur) {
                    BOOL setOk=FALSE;
                    __try { setOk = SetVCPFeature(h, 0x14, want14); anySet = true; }
                    __except(EXCEPTION_EXECUTE_HANDLER) { }
                    gLastApplied.ct = (int)want14;
                    CrashLog("[display] SetVCP14 0x%02X -> %d\n", want14, setOk);
                } else {
                    gLastApplied.ct = (int)want14;
                }
            }
        }
    }

    // ---- Phase 3: write snapshot (under lock, first touch only) ----
    if (firstTouch) {
        EnterCriticalSection(&gMonLock);
        if (!gPrim.hasSnapshot) {
            if (bMax > bMin) { gPrim.bMin=bMin; gPrim.bOrig=bOrig; gPrim.bMax=bMax; }
            if (cMax > cMin) { gPrim.cMin=cMin; gPrim.cOrig=cOrig; gPrim.cMax=cMax; }
            if (vcp14Orig)    gPrim.origVcp14  = vcp14Orig;
            gPrim.origVcpF0  = vcpE2Orig;
            gPrim.hasSnapshot = true;
        }
        LeaveCriticalSection(&gMonLock);
    }

    DestroyPhysicalMonitors(n, pm);
    CrashLog("[display] ApplyPreset done anySet=%d\n", anySet);
    return anySet;
}

// -----------------------------------------------------------------------
//  DisplayRestoreAll  (called on exit)
// -----------------------------------------------------------------------

void DisplayRestoreAll(void) {
    if (!gMonInited) return;
    EnterCriticalSection(&gMonLock);
    bool has = gPrim.hasSnapshot;
    PrimState snap = gPrim;
    LeaveCriticalSection(&gMonLock);
    if (!has) return;

    PHYSICAL_MONITOR pm[MAX_PHYSICAL_PER_HMONITOR];
    DWORD n = OpenPrimaryPhysicals(pm, MAX_PHYSICAL_PER_HMONITOR);
    if (n == 0) return;

    HANDLE h = pm[0].hPhysicalMonitor;
    // Restore preset mode first, then B/C/CT on top
    if (snap.hasSnapshot)      SetVCPFeature(h, 0xE2,  snap.origVcpF0);
    if (snap.origVcp14)        SetVCPFeature(h, 0x14,  snap.origVcp14);
    if (snap.bMax > snap.bMin) SetMonitorBrightness(h, snap.bOrig);
    if (snap.cMax > snap.cMin) SetMonitorContrast(h,   snap.cOrig);

    DestroyPhysicalMonitors(n, pm);

    EnterCriticalSection(&gMonLock);
    gPrim.hasSnapshot = false;
    LeaveCriticalSection(&gMonLock);

    DisplayResetLastApplied();
    CrashLog("[display] RestoreAll done\n");
}
