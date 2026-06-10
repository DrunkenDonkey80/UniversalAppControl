#pragma once

#pragma warning(disable:4820) // padding in structures

#define VERSION L"1.0.0"
#define APPNAME L"UniversalAppControl"

// The Lord's data types.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;
typedef signed char i8;
typedef signed short i16;
typedef signed long i32;
typedef signed long long i64;
typedef float f32;
typedef double f64;

// WARNING: Undocumented Win32 API functions!
typedef LONG(NTAPI* _NtSuspendProcess) (IN HANDLE ProcessHandle);
typedef LONG(NTAPI* _NtResumeProcess) (IN HANDLE ProcessHandle);
typedef HWND(NTAPI* _HungWindowFromGhostWindow) (IN HWND GhostWindowHandle);

_NtSuspendProcess NtSuspendProcess;
_NtResumeProcess NtResumeProcess;
_HungWindowFromGhostWindow HungWindowFromGhostWindow;

#define PRIFILE_ID_BOSS 1000
#define PROFILE_ID_CURRENT 1001

#define MAX_PROFILES 10
#define MAX_PROCESSES_PER_PROFILE 40
#define MAX_NAME 128


// Configurable registry settings.
typedef struct _CONFIG
{
	BOOL Debug;
	BOOL TrayIcon;

	u32 BossHotKey;
	UINT BossHotKeyModifiers;

	wchar_t BossSections[MAX_PROFILES][128];
	int NumBossSections;
} CONFIG;

typedef struct _PROFILE_CONFIG
{
	u32 HotKey;
	UINT HotKeyModifiers;

	BOOL HideEnabled;
	BOOL PauseEnabled;
	BOOL MinimizeEnabled;

	wchar_t ProgramExeName[MAX_NAME];
	u32 ProcessID;

	wchar_t WindowNames[MAX_PROCESSES_PER_PROFILE][MAX_NAME];
	int NumWindows;

	HWND HiddenWindows[MAX_PROCESSES_PER_PROFILE];
	int NumHiddenWindows;

	HWND ForegroundWindow;
} PROFILE_CONFIG;

// Function declarations.
void MsgBox(const wchar_t* Message, const wchar_t* Caption, u32 Flags, ...);
void DbgPrint(const wchar_t* Message, ...);
LRESULT CALLBACK SysTrayCallback(_In_ HWND Window, _In_ UINT Message, _In_ WPARAM WParam, _In_ LPARAM LParam);
void HandleKeyboardHotkey(int hkID);