#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include "selftest.h"
#include "config.h"
#include "worker.h"
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

    LogLine(L"--- %d failure(s) ---", gFails);
    if (gLog) fclose(gLog);
    return gFails;
}
