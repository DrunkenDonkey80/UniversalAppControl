#include "config.h"
#include <stdio.h>
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
