#pragma once
#include <Windows.h>
#include <stdbool.h>
bool IsStartupEnabled(void);
bool SetStartupEnabled(bool enabled);
void OpenConfigFolder(void);
bool InstallToUserPrograms(HWND parent);
bool IsExeRunning(const wchar_t* exeName);
