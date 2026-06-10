#pragma once
#include <Windows.h>
#include <stdbool.h>
extern HWND gSettingsWnd;
void ShowSettingsWindow(HINSTANCE inst, HWND owner);
bool PickRunningProcess(HWND parent, wchar_t* outName, int nameCch, wchar_t* outPath, int pathCch);
