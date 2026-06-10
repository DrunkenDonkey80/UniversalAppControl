#include "install.h"
#include "Main.h"
#include "config.h"
#include <shlobj.h>
#include <stdio.h>
#include <tlhelp32.h>
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

#define RUN_KEY  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_NAME L"UniversalAppControl"

bool IsStartupEnabled(void) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    wchar_t buf[1024]; DWORD cb = sizeof(buf); DWORD type = 0;
    LSTATUS s = RegQueryValueExW(hk, RUN_NAME, NULL, &type, (LPBYTE)buf, &cb);
    RegCloseKey(hk);
    return s == ERROR_SUCCESS && type == REG_SZ && buf[0] != 0;
}

bool SetStartupEnabled(bool enabled) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
        return false;
    bool ok;
    if (enabled) {
        wchar_t cmd[MAX_PATH + 32];
        swprintf_s(cmd, _countof(cmd), L"\"%s\" --autostart", GetExePath());
        ok = RegSetValueExW(hk, RUN_NAME, 0, REG_SZ, (const BYTE*)cmd,
                            (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LSTATUS s = RegDeleteValueW(hk, RUN_NAME);
        ok = (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hk);
    return ok;
}

void OpenConfigFolder(void) {
    wchar_t arg[MAX_PATH + 16];
    swprintf_s(arg, _countof(arg), L"/select,\"%s\"", GetConfigPath());
    ShellExecuteW(NULL, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
}

bool InstallToUserPrograms(HWND parent) {
    // Implemented in Task 21
    (void)parent;
    return false;
}

bool IsExeRunning(const wchar_t* exeName) {
    if (!exeName || !exeName[0]) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do { if (_wcsicmp(pe.szExeFile, exeName) == 0) { found = true; break; } }
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}
