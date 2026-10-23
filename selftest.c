#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include "display.h"
#pragma comment(lib, "Dxva2.lib")
#include "selftest.h"
#include "config.h"
#include "worker.h"
#include "install.h"
#include "Main.h"
#include "ui_ids.h"
#include "update.h"

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
        const wchar_t* suffix = L"\\UniversalAppControl.ini";
        bool endsOk = len > wcslen(suffix) &&
            _wcsicmp(p + len - wcslen(suffix), suffix) == 0;
        CHECK(endsOk, L"GetConfigPath ends with UniversalAppControl.ini");
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
        // SaveConfig writes the REAL config, so back it up first and restore after.
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
        gProfiles[0].Operation = PROF_OP_PAUSE;  // load derives Hide/PauseEnabled from this
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

    {
        CHECK(UpdateCompareVersions("v1.3.0", "1.2.9") > 0, L"update: newer tag wins");
        CHECK(UpdateCompareVersions("v1.2.10", "1.2.9") > 0, L"update: numeric not lexical");
        CHECK(UpdateCompareVersions("v1.3.0", "1.3.0") == 0, L"update: same version");
        CHECK(UpdateCompareVersions("v1.2.0", "1.3.0") < 0, L"update: older tag");

        const char* json =
            "{\"tag_name\": \"v1.4.0\", \"assets\": ["
            "{\"browser_download_url\": \"https://evil.example/UniversalAppControl.exe\"},"
            "{\"browser_download_url\": \"https://github.com/DrunkenDonkey80/UniversalAppControl/releases/download/v1.4.0/notes.txt\"},"
            "{\"browser_download_url\":\"https://github.com/DrunkenDonkey80/UniversalAppControl/releases/download/v1.4.0/UniversalAppControl.exe\"}]}";
        char tag[32], url[512];
        bool ok = UpdateParseRelease(json, tag, sizeof(tag), url, sizeof(url));
        CHECK(ok && strcmp(tag, "v1.4.0") == 0 && strstr(url, "/v1.4.0/UniversalAppControl.exe"),
              L"update: parses tag and picks this repo's exe asset");
        CHECK(!UpdateParseRelease("{\"tag_name\":\"v2\",\"assets\":[]}", tag, sizeof(tag), url, sizeof(url)),
              L"update: no exe asset -> no update");
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
    MdLog("Read-only probe: this command never changes monitor settings.\n");
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
                            MC_VCP_CODE_TYPE dType = MC_MOMENTARY;
                            DWORD dCur=0, dMax=0; BOOL dok=FALSE; DWORD dSeh=0;
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
                            } else { MdLog(" FAILED err=0x%lx\n", GetLastError()); }
                        }

                        // --- Probe VCP 0xF0 (11-profile Preset Mode) ---
                        MdLog("\n  [VCP 0xF0 - Preset Mode (11 profiles)]\n");
                        {
                            MC_VCP_CODE_TYPE fType = MC_MOMENTARY;
                            DWORD fCur=0,fMax=0; BOOL fok=FALSE; DWORD fSeh=0;
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

            // Contrast
            DWORD cMin=0, cCur=0, cMax=0;
            MdLog("\n  [Contrast]\n");
            STEP("GetMonitorContrast"); seh=0;
            __try { ok = GetMonitorContrast(h, &cMin, &cCur, &cMax); }
            __except(seh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){ok=FALSE;}
            if (seh) CRASH_(seh);
            else if (ok) MdLog("OK  cur=%lu  min=%lu  max=%lu\n", cCur, cMin, cMax);
            else FAIL_(GetLastError());

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

            // --- VCP 0x14 low-level color preset ---
            MdLog("\n  [VCP 0x14 Color Preset (low-level DDC/CI)]\n");
            {
                MC_VCP_CODE_TYPE vcpType = MC_MOMENTARY;
                DWORD vcpCur=0, vcpMax=0;
                BOOL vok = FALSE; int vseh = 0;
                STEP("GetVCPFeatureAndVCPFeatureReply(0x14)"); vseh=0;
                __try { vok = GetVCPFeatureAndVCPFeatureReply(h, 0x14, &vcpType, &vcpCur, &vcpMax); }
                __except(vseh=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER) { vok=FALSE; }
                if (vseh) { CRASH_(vseh); }
                else if (vok) {
                    const char* ctNames[] = {"?","sRGB","Native","4000K","5000K","6500K","7500K","8200K","9300K","10000K","11500K","Custom"};
                    MdLog(" OK  cur=0x%02lX (%s) max=%lu\n", vcpCur,
                          (vcpCur < 12) ? ctNames[vcpCur] : "?", vcpMax);
                }
                else { MdLog(" FAILED  err=0x%lx (no VCP 0x14 support)\n", GetLastError()); }
            }
        }

        DestroyPhysicalMonitors(nPhys, pms);
        free(pms);
    }
    MdLog("\n=== COMPLETE ===\n");
    fclose(gMdLog); gMdLog = NULL;
    return 0;
}

// -----------------------------------------------------------------------
//  RunVcpSweep  --vcp-sweep
//  Calls DisplayInit() then probes all 256 VCP codes using DisplayProbeVcp
//  (same physical monitor path as the real app).  Logs to %TEMP%\uac-vcp-sweep.txt
// -----------------------------------------------------------------------
int RunVcpSweep(void) {
    char logPath[MAX_PATH];
    GetTempPathA(MAX_PATH, logPath);
    strcat_s(logPath, MAX_PATH, "uac-vcp-sweep.txt");
    FILE* f = NULL;
    fopen_s(&f, logPath, "w");
    if (!f) return 1;

    fprintf(f, "=== VCP Sweep (DisplayInit context) ===\n");
    fprintf(f, "Log: %s\n\n", logPath);

    DisplayInit();
    if (!DisplayIsInited()) {
        fprintf(f, "DisplayInit failed -- no primary monitor found\n");
        fclose(f); return 1;
    }
    fprintf(f, "DisplayInit OK. Sweeping all 256 VCP codes...\n");
    fprintf(f, "Monitor in 'Warm' mode during this sweep.\n\n");
    fprintf(f, "VCP   ok   cur        max        type\n");
    fprintf(f, "----  ---  ---------  ---------  ----\n");

    int hits = 0;
    for (int code = 0; code <= 0xFF; code++) {
        BOOL ok = FALSE; DWORD cur = 0, max = 0;
        DisplayProbeVcp((BYTE)code, &ok, &cur, &max);
        if (ok) {
            fprintf(f, "0x%02X  yes  0x%02lX (%3lu)  0x%02lX (%3lu)\n",
                    code, cur, cur, max, max);
            hits++;
        }
        Sleep(15); // let DDC/CI bus breathe
    }
    fprintf(f, "\nResponded: %d / 256 VCP codes\n", hits);
    fclose(f);

    wchar_t msg[MAX_PATH + 80];
    swprintf_s(msg, _countof(msg),
        L"VCP sweep complete (%d codes responded).\nResults written to:\n%hs",
        hits, logPath);
    MessageBoxW(NULL, msg, L"--vcp-sweep done", MB_OK | MB_ICONINFORMATION);
    return 0;
}
