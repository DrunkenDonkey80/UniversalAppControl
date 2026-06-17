#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <objbase.h>
#include "display.h"
#pragma comment(lib, "Dxva2.lib")
#pragma comment(lib, "ole32.lib")
#include "selftest.h"
#include "config.h"
#include "worker.h"
#include "install.h"
#include "Main.h"

static int gFails = 0;
static FILE* gLog = NULL;

static void LogLine(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list a; va_start(a, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, a);
    va_end(a);
    wprintf(L"%s\n", buf);
    if (gLog) fwprintf(gLog, L"%s\n", buf);
}

#define CHECK(cond, name) do { \
    if (cond) { LogLine(L"PASS: %s", name); } \
    else { gFails++; LogLine(L"FAIL: %s", name); } \
} while (0)

int RunSelfTests(void) {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
    wchar_t logPath[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, logPath);
    wcscpy_s(logPath + n, MAX_PATH - n, L"uac-selftest.txt");
    _wfopen_s(&gLog, logPath, L"w, ccs=UTF-8");

    gFails = 0;
    CHECK(1 + 1 == 2, L"harness sanity");

    {
        const wchar_t* p = GetConfigPath();
        size_t len = wcslen(p);
        const wchar_t* suffix = L"\\UniversalAppControl\\config.ini";
        bool endsOk = len > wcslen(suffix) &&
            _wcsicmp(p + len - wcslen(suffix), suffix) == 0;
        CHECK(endsOk, L"GetConfigPath ends with UniversalAppControl\\config.ini");
        CHECK(GetFileAttributesW(GetConfigDir()) != INVALID_FILE_ATTRIBUTES,
              L"GetConfigDir exists after call");
    }

    {
        u32 vk = 0; UINT mods = 0;
        bool ok = ParseHotkey(L"Ctrl+Alt+V", &vk, &mods);
        CHECK(ok && vk == 'V' && mods == (MOD_CONTROL | MOD_ALT), L"ParseHotkey Ctrl+Alt+V");

        wchar_t buf[64];
        FormatHotkey('V', MOD_CONTROL | MOD_ALT, buf, _countof(buf));
        CHECK(wcslen(buf) > 0, L"FormatHotkey non-empty");
        u32 vk2 = 0; UINT mods2 = 0;
        CHECK(ParseHotkey(buf, &vk2, &mods2) && vk2 == 'V' && mods2 == (MOD_CONTROL | MOD_ALT),
              L"FormatHotkey round-trips through ParseHotkey");

        CHECK(!ParseHotkey(L"Ctrl+Alt", &vk, &mods), L"ParseHotkey rejects modifier-only");
    }

    {
        // SaveConfig writes the REAL %APPDATA% config, so back it up first and restore after.
        const wchar_t* cfg = GetConfigPath();
        wchar_t bak[MAX_PATH];
        swprintf_s(bak, _countof(bak), L"%s.selftest.bak", cfg);
        bool hadConfig = CopyFileW(cfg, bak, FALSE) != FALSE;

        gNumProfiles = 1;
        memset(&gProfiles[0], 0, sizeof(gProfiles[0]));
        wcscpy_s(gProfiles[0].ProgramExeName, MAX_NAME, L"Viber.exe");
        wcscpy_s(gProfiles[0].ProgramPath, MAX_PATH, L"C:\\x\\Viber.exe");
        gProfiles[0].HotKey = 'V';
        gProfiles[0].HotKeyModifiers = MOD_CONTROL | MOD_ALT;
        gProfiles[0].HideEnabled = TRUE;
        gProfiles[0].PauseEnabled = TRUE;
        SaveConfig();

        gNumProfiles = 0;
        LoadConfig();
        bool found = false;
        for (int i = 0; i < gNumProfiles; i++)
            if (_wcsicmp(gProfiles[i].ProgramExeName, L"Viber.exe") == 0 &&
                gProfiles[i].HideEnabled && gProfiles[i].PauseEnabled)
                found = true;
        CHECK(found, L"SaveConfig -> LoadConfig round-trips an entry");

        // Restore the user's real config.
        if (hadConfig) { CopyFileW(bak, cfg, FALSE); DeleteFileW(bak); }
        else { DeleteFileW(cfg); }   // there was none; remove the test artifact
    }

    {
        WorkerInit();
        Job a = { JOB_TOGGLE_HOTKEY, 1 };
        Job b = { JOB_TOGGLE_HOTKEY, 2 };
        CHECK(JobQueuePush(a), L"queue push a");
        CHECK(JobQueuePush(b), L"queue push b");
        Job out;
        CHECK(JobQueuePop(&out) && out.hotkeyIndex == 1, L"queue pops FIFO #1");
        CHECK(JobQueuePop(&out) && out.hotkeyIndex == 2, L"queue pops FIFO #2");
    }

    {
        bool was = IsStartupEnabled();
        CHECK(SetStartupEnabled(true), L"SetStartupEnabled(true) ok");
        CHECK(IsStartupEnabled(), L"startup reads back enabled");
        CHECK(SetStartupEnabled(false), L"SetStartupEnabled(false) ok");
        CHECK(!IsStartupEnabled(), L"startup reads back disabled");
        if (was) SetStartupEnabled(true);
    }

    LogLine(L"--- %d failure(s) ---", gFails);
    if (gLog) fclose(gLog);
    return gFails;
}

// ---------------------------------------------------------------------------
//  Monitor debug
// ---------------------------------------------------------------------------

static FILE* gMdLog = NULL;   // log file handle, set inside RunMonitorDebug

// L() writes to the log file (always flushed after each line so a crash
// doesn't swallow the last entry).
static void MdLog(const char* fmt, ...) {
    if (!gMdLog) return;
    va_list a; va_start(a, fmt);
    vfprintf(gMdLog, fmt, a);
    va_end(a);
    fflush(gMdLog);
}

static const char* CtName(MC_COLOR_TEMPERATURE ct) {
    switch (ct) {
        case MC_COLOR_TEMPERATURE_UNKNOWN:  return "UNKNOWN";
        case MC_COLOR_TEMPERATURE_4000K:    return "4000K";
        case MC_COLOR_TEMPERATURE_5000K:    return "5000K";
        case MC_COLOR_TEMPERATURE_6500K:    return "6500K";
        case MC_COLOR_TEMPERATURE_7500K:    return "7500K";
        case MC_COLOR_TEMPERATURE_8200K:    return "8200K";
        case MC_COLOR_TEMPERATURE_9300K:    return "9300K";
        case MC_COLOR_TEMPERATURE_10000K:   return "10000K";
        case MC_COLOR_TEMPERATURE_11500K:   return "11500K";
        default:                            return "<invalid>";
    }
}

static HMONITOR gDbgHmons[16];
static int      gDbgHmonCount = 0;

static BOOL CALLBACK DbgMonitorEnum(HMONITOR hm, HDC hdc, LPRECT r, LPARAM lp) {
    (void)hdc; (void)r; (void)lp;
    if (gDbgHmonCount < 16) gDbgHmons[gDbgHmonCount++] = hm;
    return TRUE;
}

// --- Thread-safety test state ---
static volatile BOOL gTtDone   = FALSE;
static volatile BOOL gTtResult = FALSE;
static DISPLAY_PRESET gTtPreset;  // shared with test threads

static DWORD WINAPI BgThreadNoComProc(LPVOID p) {
    (void)p;
    MdLog("[BgThread-NoCOM] start, calling DisplayApplyPreset...\n");
    BOOL r = DisplayApplyPreset(NULL, &gTtPreset, true);
    MdLog("[BgThread-NoCOM] returned %d\n", r);
    gTtResult = r; gTtDone = TRUE;
    return 0;
}

static DWORD WINAPI BgThreadWithComProc(LPVOID p) {
    (void)p;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    MdLog("[BgThread-WithCOM] CoInitializeEx hr=0x%08X\n", (unsigned)hr);
    BOOL r = DisplayApplyPreset(NULL, &gTtPreset, true);
    MdLog("[BgThread-WithCOM] returned %d\n", r);
    CoUninitialize();
    gTtResult = r; gTtDone = TRUE;
    return 0;
}

// STEP: log label and flush BEFORE the call; result logged AFTER.
// If the process crashes, the log shows the last STEP that ran.
#define STEP(s)      MdLog("  >> " s "... ")
#define OK_          MdLog("OK\n")
#define FAIL_(e)     MdLog("FAILED  err=0x%08X\n", (unsigned)(e))
#define CRASH_(c)    MdLog("SEH EXCEPTION 0x%08X\n", (unsigned)(c))

int RunMonitorDebug(void) {
    char logPath[MAX_PATH];
    GetTempPathA(MAX_PATH, logPath);
    strcat_s(logPath, MAX_PATH, "uac-monitor-debug.txt");
    fopen_s(&gMdLog, logPath, "w");
    if (!gMdLog) return 1;

    MdLog("=== UniversalAppControl Monitor DDC/CI Debug ===\n");
    MdLog("Log: %s\n", logPath);
    MdLog("Each DDC/CI call is logged BEFORE it runs.\n");
    MdLog("Last line before a crash = the culprit.\n\n");

    gDbgHmonCount = 0;
    EnumDisplayMonitors(NULL, NULL, DbgMonitorEnum, 0);
    MdLog("HMONITORs found: %d\n\n", gDbgHmonCount);

    for (int mi = 0; mi < gDbgHmonCount; mi++) {
        HMONITOR hm = gDbgHmons[mi];
        MdLog("--- HMONITOR[%d] %p ---\n", mi, (void*)hm);

        MONITORINFOEXW info; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(hm, (MONITORINFO*)&info)) {
            char dev[64]; WideCharToMultiByte(CP_ACP,0,info.szDevice,-1,dev,64,0,0);
            MdLog("  Device: %s  %s\n", dev,
                  (info.dwFlags & MONITORINFOF_PRIMARY) ? "(primary)" : "");
        }

        // Physical count
        DWORD nPhys = 0;
        STEP("GetNumberOfPhysicalMonitorsFromHMONITOR");
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &nPhys)) {
            FAIL_(GetLastError()); continue;
        }
        MdLog("OK  count=%lu\n", nPhys);

        PHYSICAL_MONITOR* pms = (PHYSICAL_MONITOR*)malloc(nPhys * sizeof(PHYSICAL_MONITOR));
        if (!pms) { MdLog("  malloc failed\n"); continue; }

        STEP("GetPhysicalMonitorsFromHMONITOR");
        if (!GetPhysicalMonitorsFromHMONITOR(hm, nPhys, pms)) {
            FAIL_(GetLastError()); free(pms); continue;
        }
        OK_;

        for (DWORD pi = 0; pi < nPhys; pi++) {
            HANDLE h = pms[pi].hPhysicalMonitor;
            char desc[128]; WideCharToMultiByte(CP_ACP,0,pms[pi].szPhysicalMonitorDescription,-1,desc,128,0,0);
            MdLog("\n  Physical[%lu]: \"%s\"  handle=%p\n", pi, desc, (void*)h);

            // Capabilities
            DWORD caps = 0, colorCaps = 0;
            STEP("GetMonitorCapabilities");
            if (GetMonitorCapabilities(h, &caps, &colorCaps)) {
                MdLog("OK  caps=0x%08lX  colorCaps=0x%08lX\n", caps, colorCaps);
                MdLog("    Brightness:       %s\n", (caps & MC_CAPS_BRIGHTNESS)       ? "YES" : "no");
                MdLog("    Contrast:         %s\n", (caps & MC_CAPS_CONTRAST)         ? "YES" : "no");
                MdLog("    ColorTemp:        %s\n", (caps & MC_CAPS_COLOR_TEMPERATURE) ? "YES" : "no");
            } else { FAIL_(GetLastError()); caps = 0; }

            // Capability string
            DWORD capLen = 0;
            STEP("GetCapabilitiesStringLength");
            if (GetCapabilitiesStringLength(h, &capLen) && capLen > 0 && capLen < 4096) {
                MdLog("OK  len=%lu\n", capLen);
                char* cs = (char*)malloc(capLen + 2);
                if (cs) {
                    STEP("CapabilitiesRequestAndCapabilitiesReply");
                    if (CapabilitiesRequestAndCapabilitiesReply(h, cs, capLen)) {
                        cs[capLen] = 0;
                        // Print full string in 200-char chunks
                        MdLog("OK  len=%lu full string:\n", capLen);
                        for (DWORD off = 0; off < capLen; off += 200)
                            MdLog("  %.200s\n", cs + off);

                        // --- Probe VCP 0xDC (Preset Mode / Display Mode) ---
                        MdLog("\n  [VCP 0xDC - Preset/Display Mode]\n");
                        {
                            DWORD dType=0, dCur=0, dMax=0; BOOL dok=FALSE; DWORD dSeh=0;
                            STEP("GetVCPFeatureAndVCPFeatureReply(0xDC)");
                            __try { dok = GetVCPFeatureAndVCPFeatureReply(h, 0xDC, &dType, &dCur, &dMax); }
                            __except(dSeh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){dok=FALSE;}
                            if (dSeh) { CRASH_(dSeh); }
                            else if (dok) {
                                MdLog(" OK  cur=0x%02lX (%lu)  max=0x%02lX (%lu)  type=%lu\n",
                                      dCur, dCur, dMax, dMax, dType);
                                // Parse DC(...) from caps string to list supported values
                                const char* dcp = cs;
                                while (*dcp) {
                                    if (dcp[0]=='D'&&dcp[1]=='C'&&dcp[2]=='(') break;
                                    dcp++;
                                }
                                if (*dcp) {
                                    dcp += 3;
                                    MdLog("  Supported DC values: ");
                                    while (*dcp && *dcp!=')') {
                                        while (*dcp==' ') dcp++;
                                        unsigned v=0; int nd=0;
                                        while((*dcp>='0'&&*dcp<='9')||(*dcp>='a'&&*dcp<='f')||(*dcp>='A'&&*dcp<='F')){
                                            v=v*16+((*dcp>='0'&&*dcp<='9')?*dcp-'0':(*dcp>='a'&&*dcp<='f')?*dcp-'a'+10:*dcp-'A'+10);
                                            dcp++; nd++;
                                        }
                                        if (nd>0) MdLog("0x%02X ", v);
                                        while (*dcp==' ') dcp++;
                                    }
                                    MdLog("\n");
                                }
                                // Try SetVCPFeature idempotent
                                BOOL sdc=FALSE;
                                STEP("SetVCPFeature(0xDC, cur)");
                                __try { sdc = SetVCPFeature(h, 0xDC, dCur); }
                                __except(dSeh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){sdc=FALSE;}
                                if (dSeh) CRASH_(dSeh);
                                else if (sdc) OK_;
                                else FAIL_(GetLastError());
                            } else { MdLog(" FAILED err=0x%lx\n", GetLastError()); }
                        }

                        // --- Probe VCP 0xF0 (11-profile Preset Mode) ---
                        MdLog("\n  [VCP 0xF0 - Preset Mode (11 profiles)]\n");
                        {
                            DWORD fType=0,fCur=0,fMax=0; BOOL fok=FALSE; DWORD fSeh=0;
                            STEP("GetVCPFeatureAndVCPFeatureReply(0xF0)");
                            __try { fok = GetVCPFeatureAndVCPFeatureReply(h,0xF0,&fType,&fCur,&fMax); }
                            __except(fSeh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){fok=FALSE;}
                            if (fSeh) { CRASH_(fSeh); }
                            else if (fok) {
                                MdLog(" OK  cur=0x%02lX  max=0x%02lX  type=%lu\n",fCur,fMax,fType);
                                // Extract F0(...) values from caps string
                                const char* fp=cs;
                                while(*fp){if(fp[0]=='F'&&fp[1]=='0'&&fp[2]=='(')break;fp++;}
                                if(*fp){ fp+=3;
                                    MdLog("  Supported F0 values: ");
                                    while(*fp&&*fp!=')'){
                                        while(*fp==' ')fp++;
                                        unsigned v=0;int nd=0;
                                        while((*fp>='0'&&*fp<='9')||(*fp>='a'&&*fp<='f')||(*fp>='A'&&*fp<='F')){
                                            v=v*16+((*fp>='0'&&*fp<='9')?*fp-'0':(*fp>='a'&&*fp<='f')?*fp-'a'+10:*fp-'A'+10);
                                            fp++;nd++;
                                        }
                                        if(nd>0)MdLog("0x%02X ",v);
                                        while(*fp==' ')fp++;
                                    }
                                    MdLog("\n");
                                }
                                BOOL sof=FALSE;
                                STEP("SetVCPFeature(0xF0, cur)");
                                __try{sof=SetVCPFeature(h,0xF0,fCur);}
                                __except(fSeh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){sof=FALSE;}
                                if(fSeh)CRASH_(fSeh);
                                else if(sof)OK_;
                                else FAIL_(GetLastError());
                            } else { MdLog(" FAILED err=0x%lx\n",GetLastError()); }
                        }
                    } else { FAIL_(GetLastError()); }
                    free(cs);
                }
            } else { MdLog("n/a (len=%lu err=0x%08X)\n", capLen, GetLastError()); }

            // Brightness
            DWORD bMin=0, bCur=0, bMax=0; BOOL ok; DWORD seh;
            MdLog("\n  [Brightness]\n");
            STEP("GetMonitorBrightness"); seh=0;
            __try { ok = GetMonitorBrightness(h, &bMin, &bCur, &bMax); }
            __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
            if (seh) CRASH_(seh);
            else if (ok) MdLog("OK  cur=%lu  min=%lu  max=%lu\n", bCur, bMin, bMax);
            else FAIL_(GetLastError());

            if (ok) {
                STEP("SetMonitorBrightness(cur)"); seh=0;
                __try { ok = SetMonitorBrightness(h, bCur); }
                __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
                if (seh) CRASH_(seh); else if (ok) OK_; else FAIL_(GetLastError());
            }

            // Contrast
            DWORD cMin=0, cCur=0, cMax=0;
            MdLog("\n  [Contrast]\n");
            STEP("GetMonitorContrast"); seh=0;
            __try { ok = GetMonitorContrast(h, &cMin, &cCur, &cMax); }
            __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
            if (seh) CRASH_(seh);
            else if (ok) MdLog("OK  cur=%lu  min=%lu  max=%lu\n", cCur, cMin, cMax);
            else FAIL_(GetLastError());

            if (ok) {
                STEP("SetMonitorContrast(cur)"); seh=0;
                __try { ok = SetMonitorContrast(h, cCur); }
                __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
                if (seh) CRASH_(seh); else if (ok) OK_; else FAIL_(GetLastError());
            }

            // Color temperature - try unconditionally even if not in caps
            MC_COLOR_TEMPERATURE ct = MC_COLOR_TEMPERATURE_UNKNOWN;
            MdLog("\n  [Color Temperature]  (caps flag: %s)\n",
                  (caps & MC_CAPS_COLOR_TEMPERATURE) ? "YES" : "no");
            STEP("GetMonitorColorTemperature"); seh=0;
            __try { ok = GetMonitorColorTemperature(h, &ct); }
            __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
            if (seh) CRASH_(seh);
            else if (ok) MdLog("OK  ct=%s (%d)\n", CtName(ct), (int)ct);
            else MdLog("FAILED (0x%08X) -- not supported\n", GetLastError());

            if (ok && ct != MC_COLOR_TEMPERATURE_UNKNOWN) {
                STEP("SetMonitorColorTemperature(cur)"); seh=0;
                __try { ok = SetMonitorColorTemperature(h, ct); }
                __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
                if (seh) CRASH_(seh); else if (ok) OK_; else FAIL_(GetLastError());
            }

            // Combined sequence: B then C then CT
            MdLog("\n  [Combined B+C+CT sequence]\n");
            STEP("SetMonitorBrightness"); seh=0;
            __try { ok = SetMonitorBrightness(h, bCur); }
            __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
            if (seh) CRASH_(seh); else if (ok) OK_; else FAIL_(GetLastError());

            STEP("SetMonitorContrast"); seh=0;
            __try { ok = SetMonitorContrast(h, cCur); }
            __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
            if (seh) CRASH_(seh); else if (ok) OK_; else FAIL_(GetLastError());

            if (ct != MC_COLOR_TEMPERATURE_UNKNOWN) {
                STEP("SetMonitorColorTemperature (after B+C)"); seh=0;
                __try { ok = SetMonitorColorTemperature(h, ct); }
                __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
                if (seh) CRASH_(seh); else if (ok) OK_; else FAIL_(GetLastError());
            }
            MdLog("  Combined sequence complete.\n");

            // --- VCP 0x14 low-level color preset ---
            MdLog("\n  [VCP 0x14 Color Preset (low-level DDC/CI)]\n");
            {
                DWORD vcpType=0, vcpCur=0, vcpMax=0;
                BOOL vok = FALSE; int vseh = 0;
                STEP("GetVCPFeatureAndVCPFeatureReply(0x14)"); vseh=0;
                __try { vok = GetVCPFeatureAndVCPFeatureReply(h, 0x14, &vcpType, &vcpCur, &vcpMax); }
                __except(vseh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER) { vok=FALSE; }
                if (vseh) { CRASH_(vseh); }
                else if (vok) {
                    const char* ctNames[] = {"?","sRGB","Native","4000K","5000K","6500K","7500K","8200K","9300K","10000K","11500K","Custom"};
                    MdLog(" OK  cur=0x%02lX (%s) max=%lu\n", vcpCur,
                          (vcpCur < 12) ? ctNames[vcpCur] : "?", vcpMax);

                    // Idempotent write: set same value back
                    BOOL sok = FALSE; vseh=0;
                    STEP("SetVCPFeature(0x14, cur)");
                    __try { sok = SetVCPFeature(h, 0x14, vcpCur); }
                    __except(vseh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER) { sok=FALSE; }
                    if (vseh) CRASH_(vseh);
                    else if (sok) OK_;
                    else FAIL_(GetLastError());
                }
                else { MdLog(" FAILED  err=0x%lx (no VCP 0x14 support)\n", GetLastError()); }
            }
        }

        DestroyPhysicalMonitors(nPhys, pms);
        free(pms);
    }

    // --- Thread-safety test ---
    // Prove that DDC/CI works (or fails) from a background thread,
    // both with and without COM initialized — mimics the worker thread path.
    MdLog("\n--- Thread-safety test (mimics worker thread) ---\n");

    DisplayInit();
    MdLog("DisplayInit() done\n");

    // Build preset from current values so Apply has no visible effect
    memset(&gTtPreset, 0, sizeof(gTtPreset));
    wcscpy_s(gTtPreset.Name, MAX_NAME, L"__thread_test__");
    gTtPreset.Brightness = PRESET_UNSET;
    gTtPreset.Contrast   = PRESET_UNSET;
    gTtPreset.ColorTemp  = PRESET_UNSET;
    if (gDbgHmonCount > 0) {
        PHYSICAL_MONITOR pm[MAX_PHYSICAL_PER_HMONITOR]; DWORD nc = 0;
        if (GetNumberOfPhysicalMonitorsFromHMONITOR(gDbgHmons[0], &nc) && nc > 0
            && GetPhysicalMonitorsFromHMONITOR(gDbgHmons[0], nc, pm)) {
            DWORD mn, cur, mx;
            if (GetMonitorBrightness(pm[0].hPhysicalMonitor, &mn, &cur, &mx) && mx > mn) {
                int pct = (int)(cur * 100 / mx);
                // Offset by +1 so Set* is actually called (not a no-op)
                gTtPreset.Brightness = (pct >= 100) ? pct - 1 : pct + 1;
                MdLog("  Brightness actual=%d%%, preset=%d%% (to force Set call)\n",
                      pct, gTtPreset.Brightness);
            }
            if (GetMonitorContrast(pm[0].hPhysicalMonitor, &mn, &cur, &mx) && mx > mn) {
                int pct = (int)(cur * 100 / mx);
                gTtPreset.Contrast = (pct >= 100) ? pct - 1 : pct + 1;
                MdLog("  Contrast   actual=%d%%, preset=%d%% (to force Set call)\n",
                      pct, gTtPreset.Contrast);
            }
            DestroyPhysicalMonitors(nc, pm);
        }
    }
    int bOrig = gTtPreset.Brightness - 1;  // original value (we added +1 above)
    int cOrig = gTtPreset.Contrast   - 1;
    MdLog("Preset B=%d C=%d CT=%d  (original B=%d C=%d)\n",
          gTtPreset.Brightness, gTtPreset.Contrast, gTtPreset.ColorTemp,
          bOrig, cOrig);

    // Test 1: main thread, apply +1 offset
    MdLog("\nTest1: MAIN THREAD  set B=%d C=%d ...\n",
          gTtPreset.Brightness, gTtPreset.Contrast);
    bool r1 = DisplayApplyPreset(NULL, &gTtPreset, true);
    MdLog("Test1: %s  (expected OK - Set must be called)\n", r1 ? "OK (Set called)" : "no-op UNEXPECTED");

    // Test 2: background thread, NO COM -- restore original values (B-1, C-1)
    // After Test1 the monitor is at +1; setting back to original calls Set* again.
    gTtPreset.Brightness = bOrig;
    gTtPreset.Contrast   = cOrig;
    MdLog("\nTest2: BACKGROUND THREAD (no COM)  set B=%d C=%d ...\n",
          gTtPreset.Brightness, gTtPreset.Contrast);
    gTtDone = FALSE; gTtResult = FALSE;
    HANDLE t2 = CreateThread(NULL, 0, BgThreadNoComProc, NULL, 0, NULL);
    if (t2) { WaitForSingleObject(t2, 15000); CloseHandle(t2); }
    MdLog("Test2: %s\n",
          gTtDone ? (gTtResult ? "OK (Set called from bg thread!)" : "no-op UNEXPECTED")
                  : "TIMED OUT / CRASHED");

    // Test 3: background thread, WITH COM -- bump back to +1
    gTtPreset.Brightness = bOrig + 1;
    gTtPreset.Contrast   = cOrig + 1;
    MdLog("\nTest3: BACKGROUND THREAD (with COM)  set B=%d C=%d ...\n",
          gTtPreset.Brightness, gTtPreset.Contrast);
    gTtDone = FALSE; gTtResult = FALSE;
    HANDLE t3 = CreateThread(NULL, 0, BgThreadWithComProc, NULL, 0, NULL);
    if (t3) { WaitForSingleObject(t3, 15000); CloseHandle(t3); }
    MdLog("Test3: %s\n",
          gTtDone ? (gTtResult ? "OK (Set called from bg thread with COM!)" : "no-op UNEXPECTED")
                  : "TIMED OUT / CRASHED");

    DisplayRestoreAll();
    MdLog("DisplayRestoreAll() done - monitor brightness/contrast restored\n");
    MdLog("\n=== COMPLETE ===\n");
    fclose(gMdLog); gMdLog = NULL;
    return 0;
}
