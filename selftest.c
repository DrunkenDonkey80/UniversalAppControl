#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#pragma comment(lib, "Dxva2.lib")
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

static const wchar_t* CtName(MC_COLOR_TEMPERATURE ct) {
    switch (ct) {
        case MC_COLOR_TEMPERATURE_UNKNOWN:  return L"UNKNOWN";
        case MC_COLOR_TEMPERATURE_4000K:    return L"4000K";
        case MC_COLOR_TEMPERATURE_5000K:    return L"5000K";
        case MC_COLOR_TEMPERATURE_6500K:    return L"6500K";
        case MC_COLOR_TEMPERATURE_7500K:    return L"7500K";
        case MC_COLOR_TEMPERATURE_8200K:    return L"8200K";
        case MC_COLOR_TEMPERATURE_9300K:    return L"9300K";
        case MC_COLOR_TEMPERATURE_10000K:   return L"10000K";
        case MC_COLOR_TEMPERATURE_11500K:   return L"11500K";
        default:                            return L"<invalid>";
    }
}

typedef struct { HMONITOR hm; int idx; } MonEnum;
static HMONITOR gDbgHmons[16];
static int      gDbgHmonCount = 0;

static BOOL CALLBACK DbgMonitorEnum(HMONITOR hm, HDC hdc, LPRECT r, LPARAM lp) {
    (void)hdc; (void)r; (void)lp;
    if (gDbgHmonCount < 16) gDbgHmons[gDbgHmonCount++] = hm;
    return TRUE;
}

// Macro: print the call name, flush stdout, then run the call.
// The flush ensures the label appears even if the call hard-crashes.
#define STEP(label) do { wprintf(L"  >> " label L"... "); fflush(stdout); } while(0)
#define OK          wprintf(L"OK\n")
#define FAIL(r)     wprintf(L"FAILED (0x%08X)\n", (unsigned)(r))
#define SEH_OK      wprintf(L"OK (via __try)\n")
#define SEH_CRASH(c) wprintf(L"EXCEPTION 0x%08X\n", (unsigned)(c))

int RunMonitorDebug(void) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
    setvbuf(stdout, NULL, _IONBF, 0);  // unbuffered: every wprintf appears immediately

    wprintf(L"=== UniversalAppControl -- Monitor DDC/CI Debug ===\n");
    wprintf(L"This test steps through every DDC/CI call individually.\n");
    wprintf(L"The LAST line printed before a crash is the culprit.\n\n");

    // Enumerate HMONITORs
    gDbgHmonCount = 0;
    EnumDisplayMonitors(NULL, NULL, DbgMonitorEnum, 0);
    wprintf(L"Found %d HMONITOR(s).\n\n", gDbgHmonCount);

    for (int mi = 0; mi < gDbgHmonCount; mi++) {
        HMONITOR hm = gDbgHmons[mi];
        wprintf(L"--- HMONITOR %d (handle %p) ---\n", mi, (void*)hm);

        MONITORINFOEXW info;
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(hm, (MONITORINFO*)&info))
            wprintf(L"  Device: %s  %s\n", info.szDevice,
                    (info.dwFlags & MONITORINFOF_PRIMARY) ? L"(primary)" : L"");

        // Physical monitor count
        DWORD nPhys = 0;
        STEP(L"GetNumberOfPhysicalMonitorsFromHMONITOR");
        BOOL ok = GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &nPhys);
        if (ok) { wprintf(L"OK  count=%lu\n", nPhys); }
        else    { FAIL(GetLastError()); continue; }

        PHYSICAL_MONITOR* pms = (PHYSICAL_MONITOR*)malloc(nPhys * sizeof(PHYSICAL_MONITOR));
        if (!pms) { wprintf(L"  malloc failed\n"); continue; }

        STEP(L"GetPhysicalMonitorsFromHMONITOR");
        ok = GetPhysicalMonitorsFromHMONITOR(hm, nPhys, pms);
        if (!ok) { FAIL(GetLastError()); free(pms); continue; }
        OK;

        for (DWORD pi = 0; pi < nPhys; pi++) {
            HANDLE h = pms[pi].hPhysicalMonitor;
            wprintf(L"\n  Physical[%lu]: \"%s\"  handle=%p\n",
                    pi, pms[pi].szPhysicalMonitorDescription, (void*)h);

            // --- Capabilities ---
            DWORD caps = 0, colorCaps = 0;
            STEP(L"GetMonitorCapabilities");
            ok = GetMonitorCapabilities(h, &caps, &colorCaps);
            if (ok) {
                wprintf(L"OK\n");
                wprintf(L"    caps=0x%08lX  colorCaps=0x%08lX\n", caps, colorCaps);
                wprintf(L"    Brightness:       %s\n", (caps & MC_CAPS_BRIGHTNESS)        ? L"YES" : L"no");
                wprintf(L"    Contrast:         %s\n", (caps & MC_CAPS_CONTRAST)          ? L"YES" : L"no");
                wprintf(L"    ColorTemperature: %s\n", (caps & MC_CAPS_COLOR_TEMPERATURE)  ? L"YES" : L"no");
            } else {
                FAIL(GetLastError());
                caps = 0;
            }

            // --- Capabilities string (optional, some monitors support it) ---
            DWORD capLen = 0;
            STEP(L"GetCapabilitiesStringLength");
            if (GetCapabilitiesStringLength(h, &capLen) && capLen > 0 && capLen < 4096) {
                wprintf(L"OK  len=%lu\n", capLen);
                char* capStr = (char*)malloc(capLen + 1);
                if (capStr) {
                    STEP(L"CapabilitiesRequestAndCapabilitiesReply");
                    if (CapabilitiesRequestAndCapabilitiesReply(h, capStr, capLen)) {
                        capStr[capLen] = 0;
                        wprintf(L"OK  \"%.120hs...\"\n", capStr);
                    } else { FAIL(GetLastError()); }
                    free(capStr);
                }
            } else {
                wprintf(L"n/a (len=%lu err=0x%08X)\n", capLen, GetLastError());
            }

            // --- Brightness ---
            DWORD bMin=0, bCur=0, bMax=0;
            wprintf(L"\n  [Brightness]\n");
            STEP(L"GetMonitorBrightness");
            DWORD seh = 0;
            __try { ok = GetMonitorBrightness(h, &bMin, &bCur, &bMax); }
            __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
            if (seh) { SEH_CRASH(seh); }
            else if (ok) { wprintf(L"OK  cur=%lu  min=%lu  max=%lu\n", bCur, bMin, bMax); }
            else { FAIL(GetLastError()); }

            if (ok && bCur >= bMin && bCur <= bMax) {
                STEP(L"SetMonitorBrightness (same value, no visual change)");
                seh = 0;
                __try { ok = SetMonitorBrightness(h, bCur); }
                __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
                if (seh) { SEH_CRASH(seh); }
                else if (ok) { OK; }
                else { FAIL(GetLastError()); }
            }

            // --- Contrast ---
            DWORD cMin=0, cCur=0, cMax=0;
            wprintf(L"\n  [Contrast]\n");
            STEP(L"GetMonitorContrast");
            seh = 0;
            __try { ok = GetMonitorContrast(h, &cMin, &cCur, &cMax); }
            __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
            if (seh) { SEH_CRASH(seh); }
            else if (ok) { wprintf(L"OK  cur=%lu  min=%lu  max=%lu\n", cCur, cMin, cMax); }
            else { FAIL(GetLastError()); }

            if (ok && cCur >= cMin && cCur <= cMax) {
                STEP(L"SetMonitorContrast (same value, no visual change)");
                seh = 0;
                __try { ok = SetMonitorContrast(h, cCur); }
                __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
                if (seh) { SEH_CRASH(seh); }
                else if (ok) { OK; }
                else { FAIL(GetLastError()); }
            }

            // --- Color temperature (always try, even without MC_CAPS flag) ---
            wprintf(L"\n  [Color Temperature]\n");
            MC_COLOR_TEMPERATURE ct = MC_COLOR_TEMPERATURE_UNKNOWN;
            STEP(L"GetMonitorColorTemperature (unconditional - ignoring caps)");
            seh = 0;
            __try { ok = GetMonitorColorTemperature(h, &ct); }
            __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
            if (seh) { SEH_CRASH(seh); }
            else if (ok) { wprintf(L"OK  ct=%s (%d)\n", CtName(ct), (int)ct); }
            else { wprintf(L"FAILED (0x%08X) -- monitor likely doesn't support color temp\n", GetLastError()); }

            if (ok && ct != MC_COLOR_TEMPERATURE_UNKNOWN) {
                STEP(L"SetMonitorColorTemperature (same value, no visual change)");
                seh = 0;
                __try { ok = SetMonitorColorTemperature(h, ct); }
                __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
                if (seh) { SEH_CRASH(seh); }
                else if (ok) { OK; }
                else { FAIL(GetLastError()); }
            }

            // --- Combined: brightness + contrast + colortemp in one shot ---
            wprintf(L"\n  [Combined: B+C+CT in sequence (same as Apply-all)]\n");
            STEP(L"SetMonitorBrightness"); seh=0;
            __try { ok = SetMonitorBrightness(h, bCur); }
            __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok=FALSE; }
            if (seh) SEH_CRASH(seh); else if (ok) OK; else FAIL(GetLastError());

            STEP(L"SetMonitorContrast"); seh=0;
            __try { ok = SetMonitorContrast(h, cCur); }
            __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok=FALSE; }
            if (seh) SEH_CRASH(seh); else if (ok) OK; else FAIL(GetLastError());

            if (ct != MC_COLOR_TEMPERATURE_UNKNOWN) {
                STEP(L"SetMonitorColorTemperature (after B+C)"); seh=0;
                __try { ok = SetMonitorColorTemperature(h, ct); }
                __except(seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { ok=FALSE; }
                if (seh) SEH_CRASH(seh); else if (ok) OK; else FAIL(GetLastError());
            }
        }

        DestroyPhysicalMonitors(nPhys, pms);
        free(pms);
        wprintf(L"\n");
    }

    wprintf(L"\n=== Done. Press Enter to exit. ===\n");
    fflush(stdout);
    getchar();
    return 0;
}
