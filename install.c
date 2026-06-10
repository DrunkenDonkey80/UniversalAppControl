#include "install.h"
#include "Main.h"
#include "config.h"
#include <initguid.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdio.h>
#include <tlhelp32.h>
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "uuid.lib")

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

static void GetInstallDir(wchar_t* out, int cch) {
    wchar_t local[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local);
    swprintf_s(out, cch, L"%s\\Programs\\UniversalAppControl", local);
}

static void GetInstalledExe(wchar_t* out, int cch) {
    wchar_t dir[MAX_PATH]; GetInstallDir(dir, MAX_PATH);
    swprintf_s(out, cch, L"%s\\UniversalAppControl.exe", dir);
}

static bool CreateStartMenuShortcut(const wchar_t* target) {
    wchar_t startMenu[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenu)))
        return false;
    wchar_t lnk[MAX_PATH];
    swprintf_s(lnk, _countof(lnk), L"%s\\UniversalAppControl.lnk", startMenu);

    bool ok = false;
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IShellLinkW* sl = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void**)&sl))) {
        sl->lpVtbl->SetPath(sl, target);
        wchar_t dir[MAX_PATH]; wcscpy_s(dir, _countof(dir), target);
        wchar_t* slash = wcsrchr(dir, L'\\'); if (slash) *slash = 0;
        sl->lpVtbl->SetWorkingDirectory(sl, dir);
        sl->lpVtbl->SetDescription(sl, L"Universal App Control");
        IPersistFile* pf = NULL;
        if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void**)&pf))) {
            ok = SUCCEEDED(pf->lpVtbl->Save(pf, lnk, TRUE));
            pf->lpVtbl->Release(pf);
        }
        sl->lpVtbl->Release(sl);
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return ok;
}

bool InstallToUserPrograms(HWND parent) {
    wchar_t dir[MAX_PATH]; GetInstallDir(dir, MAX_PATH);
    SHCreateDirectoryExW(NULL, dir, NULL);

    wchar_t dst[MAX_PATH]; GetInstalledExe(dst, MAX_PATH);
    if (!CopyFileW(GetExePath(), dst, FALSE)) {
        MessageBoxW(parent, L"Failed to copy the executable.", L"Install", MB_OK | MB_ICONERROR);
        return false;
    }

    GetConfigPath();   // ensures %APPDATA%\UniversalAppControl\ dir exists

    if (!CreateStartMenuShortcut(dst))
        MessageBoxW(parent, L"Copied, but the Start Menu shortcut could not be created.",
                    L"Install", MB_OK | MB_ICONWARNING);

    // If startup is enabled, point it at the installed copy.
    if (IsStartupEnabled()) {
        HKEY hk;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE,
                            NULL, &hk, NULL) == ERROR_SUCCESS) {
            wchar_t cmd[MAX_PATH + 32];
            swprintf_s(cmd, _countof(cmd), L"\"%s\" --autostart", dst);
            RegSetValueExW(hk, RUN_NAME, 0, REG_SZ, (const BYTE*)cmd,
                           (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
            RegCloseKey(hk);
        }
    }

    // Relaunch installed copy, then quit this instance.
    ShellExecuteW(NULL, L"open", dst, NULL, NULL, SW_SHOWNORMAL);
    Shell_NotifyIconW(NIM_DELETE, &gTrayNotifyIconData);
    gIsRunning = FALSE;
    PostQuitMessage(0);
    return true;
}
