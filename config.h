#pragma once
#include <Windows.h>
#include <stdbool.h>
#include "Main.h"

const wchar_t* GetConfigPath(void);   // installed path if present, else exe-dir fallback
const wchar_t* GetConfigDir(void);    // directory of the resolved config path
void SaveConfig(void);                // writes gProfiles + [general] to the ini
bool ParseHotkey(const wchar_t* text, u32* outVk, UINT* outMods);
void FormatHotkey(u32 vk, UINT mods, wchar_t* out, int cch);
