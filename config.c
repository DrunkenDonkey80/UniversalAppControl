#include "config.h"
#include "display.h"
#include "keys.h"
#include <stdio.h>
#include <string.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

static wchar_t gConfigDir[MAX_PATH];
static wchar_t gConfigPath[MAX_PATH];

static void ResolveConfig(void) {
    if (gConfigPath[0]) return;

    // Prefer the installed location so all instances share one config
    wchar_t local[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local);
    wchar_t candidate[MAX_PATH];
    swprintf_s(candidate, _countof(candidate),
               L"%s\\Programs\\UniversalAppControl\\UniversalAppControl.ini", local);

    if (PathFileExistsW(candidate)) {
        wcscpy_s(gConfigPath, _countof(gConfigPath), candidate);
    } else {
        // Fall back to the directory the exe is running from
        wcscpy_s(gConfigPath, _countof(gConfigPath), GetExePath());
        wchar_t* slash = wcsrchr(gConfigPath, L'\\');
        if (slash)
            swprintf_s(slash + 1,
                       _countof(gConfigPath) - (int)(slash + 1 - gConfigPath),
                       L"UniversalAppControl.ini");
    }

    wcscpy_s(gConfigDir, _countof(gConfigDir), gConfigPath);
    wchar_t* slash = wcsrchr(gConfigDir, L'\\');
    if (slash) *slash = 0;
}

const wchar_t* GetConfigDir(void) {
    ResolveConfig();
    return gConfigDir;
}

const wchar_t* GetConfigPath(void) {
    ResolveConfig();
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

int FindPresetByName(const wchar_t* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < gNumPresets; i++)
        if (_wcsicmp(gPresets[i].Name, name) == 0) return i;
    return -1;
}

int GetOrCreatePreset(const wchar_t* name) {
    int i = FindPresetByName(name);
    if (i >= 0) return i;
    if (gNumPresets >= MAX_PRESETS) return -1;
    i = gNumPresets++;
    memset(&gPresets[i], 0, sizeof(gPresets[i]));
    wcscpy_s(gPresets[i].Name, MAX_NAME, name);
    gPresets[i].Brightness = PRESET_UNSET;
    gPresets[i].Contrast   = PRESET_UNSET;
    gPresets[i].ColorTemp   = PRESET_UNSET;
    gPresets[i].ProfileMode = PRESET_UNSET;
    return i;
}

static void WriteBool(const wchar_t* section, const wchar_t* key, BOOL v) {
    WritePrivateProfileStringW(section, key, v ? L"true" : L"false", GetConfigPath());
}

// Save the scanned preset table so we don't need to re-scan every session.
void SaveMonPresets(void) {
    const wchar_t* path = GetConfigPath();
    // Write each scanned entry under [MonitorPresets]
    // Key format: "XX" where XX = hex VCP code; value = "Name|B|C"
    for (int i = 0; i < gMonPresetCount; i++) {
        if (!gMonPresets[i].vcpCode) continue;
        wchar_t key[8], val[128];
        swprintf_s(key, _countof(key), L"%02X", gMonPresets[i].vcpCode);
        swprintf_s(val, _countof(val), L"%s|%d|%d",
                   gMonPresets[i].name,
                   gMonPresets[i].brightness,
                   gMonPresets[i].contrast);
        WritePrivateProfileStringW(L"MonitorPresets", key, val, path);
    }
}

// Load previously-scanned preset table into gMonPresets[].
void LoadMonPresets(void) {
    const wchar_t* path = GetConfigPath();
    for (int i = 0; i < gMonPresetCount; i++) {
        if (!gMonPresets[i].vcpCode) continue;
        wchar_t key[8], val[256];
        swprintf_s(key, _countof(key), L"%02X", gMonPresets[i].vcpCode);
        if (GetPrivateProfileStringW(L"MonitorPresets", key, L"",
                                     val, _countof(val), path) > 0 && val[0]) {
            // Parse "Name|B|C"
            wchar_t* ctx = NULL;
            wchar_t* name = wcstok_s(val, L"|", &ctx);
            wchar_t* bStr = wcstok_s(NULL, L"|", &ctx);
            wchar_t* cStr = wcstok_s(NULL, L"|", &ctx);
            if (name && name[0]) wcscpy_s(gMonPresets[i].name, 64, name);
            if (bStr) { int b = _wtoi(bStr); gMonPresets[i].brightness = b; }
            if (cStr) { int c = _wtoi(cStr); gMonPresets[i].contrast   = c; }
            gMonPresets[i].scanned = (bStr != NULL);
        }
    }
}

void SaveConfig(void) {
    const wchar_t* path = GetConfigPath();
    // Delete and recreate to ensure all old sections are gone.
    DeleteFileW(path);

    WriteBool(L"general", L"Debug", gConfig.Debug);
    WriteBool(L"general", L"DisplayControl", gDisplayControlEnabled);
    WritePrivateProfileStringW(L"general", L"DefaultPreset", gDefaultPresetName, path);

    // Write display presets
    for (int i = 0; i < gNumPresets; i++) {
        wchar_t section[MAX_NAME + 8];
        swprintf_s(section, _countof(section), L"preset:%s", gPresets[i].Name);
        wchar_t tmp[32];
        if (gPresets[i].Brightness != PRESET_UNSET) {
            swprintf_s(tmp, _countof(tmp), L"%d", gPresets[i].Brightness);
            WritePrivateProfileStringW(section, L"Brightness", tmp, path);
        }
        if (gPresets[i].Contrast != PRESET_UNSET) {
            swprintf_s(tmp, _countof(tmp), L"%d", gPresets[i].Contrast);
            WritePrivateProfileStringW(section, L"Contrast", tmp, path);
        }
        if (gPresets[i].ColorTemp != PRESET_UNSET) {
            swprintf_s(tmp, _countof(tmp), L"%d", gPresets[i].ColorTemp);
            WritePrivateProfileStringW(section, L"ColorTemp", tmp, path);
        }
        if (gPresets[i].ProfileMode != PRESET_UNSET) {
            swprintf_s(tmp, _countof(tmp), L"%d", gPresets[i].ProfileMode);
            WritePrivateProfileStringW(section, L"ProfileMode", tmp, path);
        }
    }

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
        if (p->DisplayPreset[0])
            WritePrivateProfileStringW(section, L"DisplayPreset",
                                       p->DisplayPreset, path);
    }
}
