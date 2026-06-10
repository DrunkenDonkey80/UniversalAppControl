#pragma once
#include <Windows.h>
#include <stdbool.h>
#include "Main.h"

const wchar_t* GetConfigPath(void);   // %APPDATA%\UniversalAppControl\config.ini, ensures dir
const wchar_t* GetConfigDir(void);    // %APPDATA%\UniversalAppControl
void SaveConfig(void);                // writes gProfiles + [general] to the ini
bool ParseHotkey(const wchar_t* text, u32* outVk, UINT* outMods);
void FormatHotkey(u32 vk, UINT mods, wchar_t* out, int cch);
