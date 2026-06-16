#pragma once
#include <Windows.h>
#include <stdbool.h>
#include "Main.h"

const wchar_t* GetConfigPath(void);   // installed path if present, else exe-dir fallback
const wchar_t* GetConfigDir(void);    // directory of the resolved config path
void SaveConfig(void);                // writes gProfiles + [general] + presets to the ini
bool ParseHotkey(const wchar_t* text, u32* outVk, UINT* outMods);
void FormatHotkey(u32 vk, UINT mods, wchar_t* out, int cch);

// Find a preset by name (case-insensitive); returns index or -1.
int FindPresetByName(const wchar_t* name);

// Find or create a preset by name; returns index or -1 if table full.
int GetOrCreatePreset(const wchar_t* name);
