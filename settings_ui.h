#pragma once
#include <Windows.h>
#include <stdbool.h>
extern HWND          gSettingsWnd;
extern volatile BOOL gHotkeyEditActive; // set by hotkey edit subclass; read by LL hook
void ShowSettingsWindow(HINSTANCE inst, HWND owner);
bool PickRunningProcess(HWND parent, wchar_t* outName, int nameCch, wchar_t* outPath, int pathCch);
