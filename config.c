#include "config.h"
#include "keys.h"
#include <stdio.h>
#include <string.h>
#include <shlobj.h>
#pragma comment(lib, "Shell32.lib")

static wchar_t gConfigDir[MAX_PATH];
static wchar_t gConfigPath[MAX_PATH];

const wchar_t* GetConfigDir(void) {
    if (gConfigDir[0] == 0) {
        wchar_t appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
            swprintf_s(gConfigDir, _countof(gConfigDir), L"%s\\UniversalAppControl", appData);
            CreateDirectoryW(gConfigDir, NULL);  // ok if it already exists
        }
    }
    return gConfigDir;
}

const wchar_t* GetConfigPath(void) {
    if (gConfigPath[0] == 0)
        swprintf_s(gConfigPath, _countof(gConfigPath), L"%s\\config.ini", GetConfigDir());
    return gConfigPath;
}

bool ParseHotkey(const wchar_t* text, u32* outVk, UINT* outMods) {
    *outVk = 0; *outMods = 0;
    wchar_t buf[256];
    wcscpy_s(buf, _countof(buf), text);
    wchar_t* ctx = NULL;
    wchar_t* tok = wcstok_s(buf, L"+|&", &ctx);
    while (tok) {
        if (!lstrcmpiW(tok, L"shift") || !lstrcmpiW(tok, L"lshift") || !lstrcmpiW(tok, L"rshift"))
            *outMods |= MOD_SHIFT;
        else if (!lstrcmpiW(tok, L"alt") || !lstrcmpiW(tok, L"lalt") || !lstrcmpiW(tok, L"ralt"))
            *outMods |= MOD_ALT;
        else if (!lstrcmpiW(tok, L"ctrl") || !lstrcmpiW(tok, L"control") ||
                 !lstrcmpiW(tok, L"lctrl") || !lstrcmpiW(tok, L"rctrl") ||
                 !lstrcmpiW(tok, L"lcontrol") || !lstrcmpiW(tok, L"rcontrol"))
            *outMods |= MOD_CONTROL;
        else if (!lstrcmpiW(tok, L"win") || !lstrcmpiW(tok, L"window") || !lstrcmpiW(tok, L"windows"))
            *outMods |= MOD_WIN;
        else {
            const KEYCode* key = findKeyWithName(tok);
            if (key == NULL) return false;
            *outVk = key->vkCode;
        }
        tok = wcstok_s(NULL, L"+|&", &ctx);
    }
    return *outVk != 0;
}

void FormatHotkey(u32 vk, UINT mods, wchar_t* out, int cch) {
    out[0] = 0;
    if (mods & MOD_CONTROL) wcscat_s(out, cch, L"Ctrl+");
    if (mods & MOD_ALT)     wcscat_s(out, cch, L"Alt+");
    if (mods & MOD_SHIFT)   wcscat_s(out, cch, L"Shift+");
    if (mods & MOD_WIN)     wcscat_s(out, cch, L"Win+");
    const wchar_t* name = FindKeyNameByVk(vk);
    if (name) wcscat_s(out, cch, name);
}

static void WriteBool(const wchar_t* section, const wchar_t* key, BOOL v) {
    WritePrivateProfileStringW(section, key, v ? L"true" : L"false", GetConfigPath());
}

void SaveConfig(void) {
    const wchar_t* path = GetConfigPath();
    // Clear the whole file first so removed entries disappear.
    WritePrivateProfileStringW(NULL, NULL, NULL, path);

    WriteBool(L"general", L"Debug", gConfig.Debug);
    WriteBool(L"general", L"TrayIcon", gConfig.TrayIcon);

    for (int i = 0; i < gNumProfiles; i++) {
        PROFILE_CONFIG* p = &gProfiles[i];
        const wchar_t* sec = p->ProgramExeName[0] ? p->ProgramExeName : L"Entry";
        wchar_t section[MAX_NAME];
        swprintf_s(section, _countof(section), L"%s_%d", sec, i);

        wchar_t hk[64];
        FormatHotkey(p->HotKey, p->HotKeyModifiers, hk, _countof(hk));
        WritePrivateProfileStringW(section, L"Hotkey", hk, path);
        WritePrivateProfileStringW(section, L"ProgramExeName", p->ProgramExeName, path);
        WritePrivateProfileStringW(section, L"ProgramPath", p->ProgramPath, path);
        WriteBool(section, L"Hide", p->HideEnabled);
        WriteBool(section, L"Minimize", p->MinimizeEnabled);
        WriteBool(section, L"Pause", p->PauseEnabled);
    }
}
