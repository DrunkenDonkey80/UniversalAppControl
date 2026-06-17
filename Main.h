#pragma once

#pragma warning(disable:4820) // padding in structures

#define VERSION L"1.1.0"
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

extern _NtSuspendProcess NtSuspendProcess;
extern _NtResumeProcess NtResumeProcess;
extern _HungWindowFromGhostWindow HungWindowFromGhostWindow;

#define MAX_PROFILES 20
#define MAX_PROCESSES_PER_PROFILE 40
#define MAX_NAME 128
#define MAX_PRESETS 16
#define PRESET_UNSET  (-1)   // sentinel: "leave this attribute unchanged"
// DISPLAY_PRESET.ColorTemp stores:
//   PRESET_UNSET (-1)  : don't change color temperature
//   1-255             : VCP 0x14 code sent directly to the monitor
//                       (e.g. 0x05=6500K, 0x08=9300K, 0x0C=User Warm)
//   >= 256            : legacy Kelvin — mapped to nearest supported VCP code

// ---------------------------------------------------------------------------
//  Display preset: a named set of monitor settings applied automatically
//  when a linked app profile is in the foreground.
//  Each field is 0-100 percent (brightness/contrast) or a Kelvin value
//  (colortemp); PRESET_UNSET means "don't touch this attribute".
// ---------------------------------------------------------------------------
typedef struct _DISPLAY_PRESET
{
	wchar_t Name[MAX_NAME];
	int Brightness;   // 0-100 percent, or PRESET_UNSET
	int Contrast;     // 0-100 percent, or PRESET_UNSET
	int ColorTemp;    // VCP 0x14 code (1-255), legacy Kelvin (>=256), or PRESET_UNSET
	int ProfileMode;  // VCP 0xF0 code (1-255) or PRESET_UNSET
	                  // Selects the monitor's named preset (ComfortView=0x0C, FPS=0x0F...)
	                  // Applied FIRST so B/C/CT can override the preset defaults.
} DISPLAY_PRESET;


// Configurable registry settings.
typedef struct _CONFIG
{
	BOOL Debug;
} CONFIG;

typedef struct _PROFILE_CONFIG
{
	u32 HotKey;
	UINT HotKeyModifiers;

	int  Operation;       // PROF_OP_* — replaces HideEnabled/PauseEnabled/MinimizeEnabled
	// Legacy fields kept so old config files load correctly; derived into Operation on load.
	BOOL HideEnabled;
	BOOL PauseEnabled;
	BOOL MinimizeEnabled;

	wchar_t ProgramExeName[MAX_NAME];
	wchar_t ProgramPath[MAX_PATH];
	u32 ProcessID;

	wchar_t WindowNames[MAX_PROCESSES_PER_PROFILE][MAX_NAME];
	int NumWindows;

	HWND HiddenWindows[MAX_PROCESSES_PER_PROFILE];
	int NumHiddenWindows;

	HWND ForegroundWindow;

	wchar_t DisplayPreset[MAX_NAME]; // name of the DISPLAY_PRESET to apply, or L"" for none
} PROFILE_CONFIG;

// Function declarations.
void MsgBox(const wchar_t* Message, const wchar_t* Caption, u32 Flags, ...);
void DbgPrint(const wchar_t* Message, ...);
void CrashLog(const char* fmt, ...);   // always-on step log -> %TEMP%\uac-crash.txt
LRESULT CALLBACK SysTrayCallback(_In_ HWND Window, _In_ UINT Message, _In_ WPARAM WParam, _In_ LPARAM LParam);

extern CONFIG gConfig;
extern PROFILE_CONFIG gProfiles[MAX_PROFILES];
extern int gNumProfiles;
extern DISPLAY_PRESET gPresets[MAX_PRESETS];
extern int  gNumPresets;
extern BOOL gDisplayControlEnabled;   // [general] DisplayControl
extern wchar_t gDefaultPresetName[MAX_NAME]; // [general] DefaultPreset
extern BOOL gIsRunning;
extern HANDLE gMutex;
extern NOTIFYICONDATA gTrayNotifyIconData;
bool LoadConfig(void);

extern CRITICAL_SECTION gHotkeyLock;   // guards gHotkeys/gNumHotkeys + Triggered
extern HWND gScanNotifyHwnd;           // preset editor HWND to notify when scan done
void DispatchHotkey(int hotkeyIndex);  // worker-side: runs the heavy toggle work
void RebuildHotkeys(void);             // rebuild gHotkeys from in-memory gProfiles; caller holds gHotkeyLock
const wchar_t* GetExePath(void);