#include <Windows.h>
#include <stdio.h>
#include "selftest.h"
#include "config.h"

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

    LogLine(L"--- %d failure(s) ---", gFails);
    if (gLog) fclose(gLog);
    return gFails;
}
