#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WM_TRAYICON (WM_USER + 1)

#include <Windows.h>
#include <stdio.h>
#include <TlHelp32.h>
#include <winternl.h>
#include <stdbool.h>
#include <stdlib.h>
#include "Main.h"
#include "resource.h"
#include "keys.h"
#include "ui_ids.h"
#include "config.h"
#include "worker.h"
#include "settings_ui.h"
#include "install.h"
#include "selftest.h"
#include "display.h"

_NtSuspendProcess NtSuspendProcess;
_NtResumeProcess NtResumeProcess;
_HungWindowFromGhostWindow HungWindowFromGhostWindow;

CONFIG gConfig;

PROFILE_CONFIG gProfiles[MAX_PROFILES];
int gNumProfiles = 0;

DISPLAY_PRESET gPresets[MAX_PRESETS];
int            gNumPresets = 0;
BOOL           gDisplayControlEnabled = FALSE;
wchar_t        gDefaultPresetName[MAX_NAME] = L"Default";

DesiredDisplay gDesiredDisplay;   // guarded by gHotkeyLock

typedef struct _HotkeyInfo
{
	u32 HotKey;
	UINT Modifiers;

	bool Triggered;
	int ProfileIDs[MAX_PROFILES];
	int NumProfileIDs;
} HotkeyInfo;


HotkeyInfo gHotkeys[MAX_PROFILES + 1];
int gNumHotkeys = 0;
static bool gHotkeyHeld[MAX_PROFILES + 1];  // autorepeat suppression; cleared on hotkey rebuild

u32 gPausedProcesses[MAX_PROFILES * MAX_PROCESSES_PER_PROFILE];
int gNumPausedProcesses=0;

CRITICAL_SECTION gHotkeyLock;
UINT gTaskbarCreatedMsg = 0;

typedef struct _WindowInfo
{
	HWND WindowHandle;
	u32 ProcessID;
	wchar_t ExeName[MAX_NAME];
} WindowInfo;


#define MAX_WINDOW_INFO 8 * 1024

WindowInfo gWindowInfo[MAX_WINDOW_INFO];
int gNumWindowInfo = 0;


HANDLE gDbgConsole = INVALID_HANDLE_VALUE;
BOOL gIsRunning = TRUE;
HANDLE gMutex;
NOTIFYICONDATA gTrayNotifyIconData;


// Specify the path to the INI file
static wchar_t iniFilePath[MAX_PATH];

static bool ReadConfigBool(const wchar_t* section, const wchar_t* variable, bool defValue) {
	// Maximum buffer size for section names
	wchar_t buffer[256] = { 0 };
	if (GetPrivateProfileString(section, variable, NULL, buffer, sizeof(buffer)/sizeof(wchar_t), iniFilePath) <= 0)
		return defValue;

	if (!lstrcmpi(buffer, L"0") || !lstrcmpi(buffer, L"false") || !lstrcmpi(buffer, L"no"))
		return false;

	if (!lstrcmpi(buffer, L"1") || !lstrcmpi(buffer, L"true") || !lstrcmpi(buffer, L"yes"))
		return true;

	return defValue;
}

static bool ReadConfigString(const wchar_t* section, const wchar_t* variable, wchar_t* buffer) {
	// Maximum buffer size for section names
	if (GetPrivateProfileString(section, variable, NULL, buffer, MAX_PATH, iniFilePath) <= 0)
		return false;

	return true;
}

static void ReadConfigList(const wchar_t* section, const wchar_t* variable, wchar_t spliwchar_t, wchar_t listValues[][MAX_NAME], int *numListValues) {
	*numListValues = 0;

	// Maximum buffer size for section names
	wchar_t buffer[10 * 1024] = { 0 };

	if (GetPrivateProfileString(section, variable, NULL, buffer, sizeof(buffer) / sizeof(wchar_t), iniFilePath) <= 0)
		return;

	wchar_t* b = buffer;
	while (*b) {
		if (*b == spliwchar_t)
			*b = 0;
		b++;
	}
	//now add to the list
	b = buffer;
	while (*b) {
		if (*numListValues >= MAX_PROCESSES_PER_PROFILE) break;
		wcscpy_s(listValues[*numListValues], MAX_NAME, b);
		(*numListValues)++;
		b += lstrlen(b) + 1;
	}
}

static void RegisterConfigHotkey(const wchar_t* section, const wchar_t* variable, int profileID) {
	wchar_t raw[256] = { 0 };
	if (GetPrivateProfileStringW(section, variable, NULL, raw, _countof(raw), iniFilePath) <= 0)
		return;
	u32 hotKey = 0; UINT modifiers = 0;
	if (!ParseHotkey(raw, &hotKey, &modifiers))
		return;

	if (hotKey != 0) {
		gProfiles[profileID].HotKey = hotKey;
		gProfiles[profileID].HotKeyModifiers = modifiers;

		//we have it already registered?
		bool found = false;
		for (int i = 0; i < gNumHotkeys; i++) {
			if (gHotkeys[i].HotKey == hotKey && gHotkeys[i].Modifiers == modifiers) {
				gHotkeys[i].ProfileIDs[gHotkeys[i].NumProfileIDs++] = profileID;
				found = true;
				break;
			}
		}
		if (!found) {
			gHotkeys[gNumHotkeys].HotKey = hotKey;
			gHotkeys[gNumHotkeys].Modifiers = modifiers;
			gHotkeys[gNumHotkeys].Triggered = false;
			gHotkeys[gNumHotkeys].NumProfileIDs = 0;
			gHotkeys[gNumHotkeys].ProfileIDs[gHotkeys[gNumHotkeys].NumProfileIDs++] = profileID;

			//if (RegisterHotKey(NULL, gNumHotkeys, MOD_NOREPEAT | modifiers, hotKey) == 0)
			//{
			//	MsgBox(L"Failed to register hotkey! Error 0x%08lx", APPNAME L" Error", MB_OK | MB_ICONERROR, GetLastError());
			//}
			gNumHotkeys++;
		}
	}
}


void UnregisterHotkeys() {
	for (int i = 0; i < gNumHotkeys; i++)
		UnregisterHotKey(NULL, i);

	gNumHotkeys = 0;
	memset(gHotkeyHeld, 0, sizeof(gHotkeyHeld));
}

void RebuildHotkeys(void) {
	UnregisterHotkeys();
	for (int i = 0; i < gNumProfiles; i++) {
		PROFILE_CONFIG* p = &gProfiles[i];
		if (!p->HotKey) continue;
		bool found = false;
		for (int j = 0; j < gNumHotkeys; j++) {
			if (gHotkeys[j].HotKey == p->HotKey && gHotkeys[j].Modifiers == p->HotKeyModifiers) {
				gHotkeys[j].ProfileIDs[gHotkeys[j].NumProfileIDs++] = i;
				found = true;
				break;
			}
		}
		if (!found && gNumHotkeys < MAX_PROFILES) {
			gHotkeys[gNumHotkeys].HotKey       = p->HotKey;
			gHotkeys[gNumHotkeys].Modifiers     = p->HotKeyModifiers;
			gHotkeys[gNumHotkeys].Triggered     = false;
			gHotkeys[gNumHotkeys].NumProfileIDs = 1;
			gHotkeys[gNumHotkeys].ProfileIDs[0] = i;
			gNumHotkeys++;
		}
	}
}

void EnableDebugConsole() {
	if (gDbgConsole != INVALID_HANDLE_VALUE) return;  // already allocated
	// Enable the debug console as early as possible if configured.
				// This is so the debug console can report on the other registry settings.
	if (AllocConsole() == FALSE)
	{
		MsgBox(L"Failed to allocate debug console!\nError: 0x%08lx", L"Error", MB_OK | MB_ICONERROR, GetLastError());
		return;
	}
	gDbgConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	if (gDbgConsole == INVALID_HANDLE_VALUE)
	{
		MsgBox(L"Failed to get stdout debug console handle!\nError: 0x%08lx", L"Error", MB_OK | MB_ICONERROR, GetLastError());
		return;
	}
	DbgPrint(L"%s version %s.", APPNAME, VERSION);
	DbgPrint(L"To disable this debug console, delete the 'Debug' reg setting at HKCU\\SOFTWARE\\%s", APPNAME);
}

static bool ReadConfig() {
	wcscpy_s(iniFilePath, MAX_PATH, GetConfigPath());


	// wipe out previous config
	UnregisterHotkeys();
	memset(gProfiles, 0, sizeof(gProfiles));
	gNumProfiles = 0;
	memset(gPresets, 0, sizeof(gPresets));
	gNumPresets = 0;
	gDisplayControlEnabled = FALSE;
	wcscpy_s(gDefaultPresetName, MAX_NAME, L"Default");

	// Maximum buffer size for section names
	wchar_t buffer[32767] = { 0 };

	// Get all section names
	DWORD charsRead = GetPrivateProfileSectionNames(buffer, sizeof(buffer) / sizeof(wchar_t), iniFilePath);

	if (charsRead == 0) {
		// No config file yet (first run) or empty file - treat as empty config
		return true;
	}

	// Parse the buffer to extract section names
	wchar_t* sectionName = buffer;

	while (*sectionName) {
		if (!lstrcmpi(sectionName, L"general")) {
			gConfig.Debug = ReadConfigBool(sectionName, L"Debug", false);
			if (gConfig.Debug)
				EnableDebugConsole();
			gDisplayControlEnabled = ReadConfigBool(sectionName, L"DisplayControl", false);
			ReadConfigString(sectionName, L"DefaultPreset", gDefaultPresetName);
			if (!gDefaultPresetName[0])
				wcscpy_s(gDefaultPresetName, MAX_NAME, L"Default");
		}
		else if (wcsncmp(sectionName, L"preset:", 7) == 0 && gNumPresets < MAX_PRESETS) {
			// --- Display preset section ---
			const wchar_t* presetName = sectionName + 7;  // skip "preset:"
			wcscpy_s(gPresets[gNumPresets].Name, MAX_NAME, presetName);
			// Read raw values; -1 sentinel means unset
			wchar_t tmp[64] = { 0 };
			gPresets[gNumPresets].Brightness = PRESET_UNSET;
			gPresets[gNumPresets].Contrast   = PRESET_UNSET;
			gPresets[gNumPresets].ColorTemp   = PRESET_UNSET;
			gPresets[gNumPresets].ProfileMode = PRESET_UNSET;
			if (GetPrivateProfileStringW(sectionName, L"Brightness", L"", tmp, _countof(tmp), iniFilePath) > 0 && tmp[0])
				gPresets[gNumPresets].Brightness = _wtoi(tmp);
			memset(tmp, 0, sizeof(tmp));
			if (GetPrivateProfileStringW(sectionName, L"Contrast", L"", tmp, _countof(tmp), iniFilePath) > 0 && tmp[0])
				gPresets[gNumPresets].Contrast = _wtoi(tmp);
			memset(tmp, 0, sizeof(tmp));
			if (GetPrivateProfileStringW(sectionName, L"ColorTemp", L"", tmp, _countof(tmp), iniFilePath) > 0 && tmp[0])
				gPresets[gNumPresets].ColorTemp = _wtoi(tmp);
			if (GetPrivateProfileStringW(sectionName, L"ProfileMode", L"", tmp, _countof(tmp), iniFilePath) > 0 && tmp[0])
				gPresets[gNumPresets].ProfileMode = _wtoi(tmp);
			gNumPresets++;
		}
		else if (wcsncmp(sectionName, L"preset:", 7) != 0 && gNumProfiles < MAX_PROFILES) {
			//profile
			RegisterConfigHotkey(sectionName, L"Hotkey", gNumProfiles);

			gProfiles[gNumProfiles].HideEnabled = ReadConfigBool(sectionName, L"Hide", false);
			gProfiles[gNumProfiles].PauseEnabled = ReadConfigBool(sectionName, L"Pause", false);
			gProfiles[gNumProfiles].MinimizeEnabled = ReadConfigBool(sectionName, L"Minimize", false);

			ReadConfigString(sectionName, L"ProgramExeName", gProfiles[gNumProfiles].ProgramExeName);
			ReadConfigString(sectionName, L"ProgramPath", gProfiles[gNumProfiles].ProgramPath);
			ReadConfigList(sectionName, L"WindowNames", '|', &gProfiles[gNumProfiles].WindowNames, &gProfiles[gNumProfiles].NumWindows);
			ReadConfigString(sectionName, L"DisplayPreset", gProfiles[gNumProfiles].DisplayPreset);

			gNumProfiles++;
		}

		sectionName += lstrlen(sectionName) + 1; // Move to the next section name
	}
	return true;
}

bool LoadConfig(void) { return ReadConfig(); }

const wchar_t* GetExePath(void) {
    static wchar_t path[MAX_PATH];
    if (path[0] == 0) GetModuleFileNameW(NULL, path, MAX_PATH);
    return path;
}

const wchar_t* GetLastErorText() {
	static wchar_t messageBuffer[1024];

	messageBuffer[0] = 0;
	//Get the error message ID, if any.
	DWORD errorMessageID = GetLastError();
	if (errorMessageID != 0) {
		//Ask Win32 to give us the string version of that message ID.
		//The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
		size_t size = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), &messageBuffer, sizeof(messageBuffer) / sizeof(wchar_t), NULL);

		//concat the error code
		swprintf(&messageBuffer[size], sizeof(messageBuffer) /sizeof(wchar_t), L"Code: 0x%08lx", errorMessageID);
	}

	return messageBuffer;
}

bool IsProcessSuspended(DWORD processId) {
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		//std::cerr << "Failed to create snapshot." << std::endl;
		return false;
	}

	THREADENTRY32 threadEntry;
	threadEntry.dwSize = sizeof(THREADENTRY32);
	bool isSuspended = true;  // Assume process is suspended unless proven otherwise

	if (Thread32First(hSnapshot, &threadEntry)) {
		do {
			if (threadEntry.th32OwnerProcessID == processId) {
				// Open the thread
				HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION| THREAD_SUSPEND_RESUME, FALSE, threadEntry.th32ThreadID);
				if (hThread) {
					// Get the thread's suspend count
					DWORD suspendCount = SuspendThread(hThread);
					ResumeThread(hThread); // Undo the suspend caused by SuspendThread

					if (suspendCount == 0) {
						isSuspended = false; // At least one thread is not suspended
					}
					CloseHandle(hThread);
				}
			}
		} while (Thread32Next(hSnapshot, &threadEntry) && isSuspended);
	}
	else {
		//std::cerr << "Failed to retrieve thread information." << std::endl;
	}

	CloseHandle(hSnapshot);
	return isSuspended;
}

bool SuspendProcess(u32 id) {
	//do not suspend if already suspended
	if (IsProcessSuspended(id)) {
		return true;
	}

	HANDLE ProcessHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, id);
	if (ProcessHandle == NULL)
	{
		MsgBox(L"Failed to open process %d! Error 0x%08lx", APPNAME L" Error", MB_OK | MB_ICONERROR, id, GetLastError());
		return false;
	}

	//u8 buf[0x1000];
	//ULONG cb = sizeof(buf);
	//SYSTEM_INFORMATION_CLASS sysInfo;
	//if (NtQuerySystemInformation(SystemProcessInformation, buf, cb, &cb) == 0)
	//{
	//}
	NtSuspendProcess(ProcessHandle);
	gPausedProcesses[gNumPausedProcesses++] = id;
	CloseHandle(ProcessHandle);
	DbgPrint(L"Process paused!");
	return true;
}

bool IsProcessPaused(u32 id) {
	for (int i = 0; i < gNumPausedProcesses; i++) {
		if (gPausedProcesses[i] == id) {
			return true;
		}
	}
	return false;
}

bool ResumeProcess(u32 id) {
	for (int i = 0; i < gNumPausedProcesses; i++) {
		if (gPausedProcesses[i] == id) {
			memmove(&gPausedProcesses[i], &gPausedProcesses[i + 1], (gNumPausedProcesses - i - 1) * sizeof(u32));
			gNumPausedProcesses--;
			break;
		}
	}
	HANDLE ProcessHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, id);
	if (ProcessHandle == NULL)
	{
		MsgBox(L"Failed to open process %d! Error 0x%08lx", APPNAME L" Error", MB_OK | MB_ICONERROR, id, GetLastError());
		return false;
	}
	NtResumeProcess(ProcessHandle);
	CloseHandle(ProcessHandle);
	DbgPrint(L"Process resumed!");
	return true;
}

static BOOL CALLBACK enumWindowCallback(HWND hWnd, LPARAM lparam) {
	if (gNumWindowInfo >= MAX_WINDOW_INFO)
		return FALSE;

	if (GetWindow(hWnd, GW_OWNER) != NULL)
		return TRUE;

	int n = gNumWindowInfo;
	gWindowInfo[n].WindowHandle = hWnd;
	gWindowInfo[n].ProcessID    = 0;
	gWindowInfo[n].ExeName[0]   = 0;
	GetWindowThreadProcessId(hWnd, &gWindowInfo[n].ProcessID);
	if (gWindowInfo[n].ProcessID) {
		HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, gWindowInfo[n].ProcessID);
		if (h) {
			wchar_t path[MAX_PATH]; DWORD len = MAX_PATH;
			if (QueryFullProcessImageNameW(h, 0, path, &len)) {
				wchar_t* slash = wcsrchr(path, L'\\');
				wcscpy_s(gWindowInfo[n].ExeName, MAX_NAME, slash ? slash + 1 : path);
			}
			CloseHandle(h);
		}
	}
	gNumWindowInfo++;
	return TRUE;
}

void UpdateWindowProcessIDs() {
	gNumWindowInfo = 0;

	EnumWindows(enumWindowCallback, NULL);
}

void UpdateProcessIDs() {
	HANDLE ProcessSnapshot = NULL;
	PROCESSENTRY32W ProcessEntry = { sizeof(PROCESSENTRY32W) };

	//walk over processes from config to get their names
	for (int i = 0; i < gNumProfiles; i++) {
		gProfiles[i].ProcessID = 0;
	}

	//DbgPrint(L"Pause key pressed. Attempting to pause named process %s...", gConfig.ProcessNameToPause);
	ProcessSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (ProcessSnapshot == INVALID_HANDLE_VALUE)
	{
		MsgBox(L"Failed to create snapshot of running processes! Error 0x%08lx", APPNAME L" Error", MB_OK | MB_ICONERROR, GetLastError());
		return;
	}

	if (Process32FirstW(ProcessSnapshot, &ProcessEntry) == FALSE)
	{
		MsgBox(L"Failed to retrieve list of running processes! Error 0x%08lx", APPNAME L" Error", MB_OK | MB_ICONERROR, GetLastError());
		CloseHandle(ProcessSnapshot);
		return;
	}

	do
	{
		//walk over each profiles to fill in revelant processid info
		for (int i = 0; i < gNumProfiles; i++) {
			bool found = _wcsicmp(ProcessEntry.szExeFile, gProfiles[i].ProgramExeName) == 0;
			//DbgPrint(L"Found process %s, comparing to %s: %d", ProcessEntry.szExeFile, gProfiles[i].ProgramExeName, found);
			if (found) {
				DbgPrint(L"Found process %s, comparing to %s: %d", ProcessEntry.szExeFile, gProfiles[i].ProgramExeName, found);
				gProfiles[i].ProcessID = ProcessEntry.th32ProcessID;
			}
		}
	} while (Process32NextW(ProcessSnapshot, &ProcessEntry));

	CloseHandle(ProcessSnapshot);
}

HWND FindWindowWithTitle(LPCTSTR title) {
	wchar_t search[MAX_PATH];
	wcscpy_s(search, MAX_PATH, title);
	int len = wcslen(search);
	bool prefixed = len > 3 && search[len - 1] == L'.' && search[len - 2] == L'.' && search[len - 3] == L'.';
	if (prefixed)
		search[len - 1] = 0;

	HWND hwnd = INVALID_HANDLE_VALUE;  // Start with no window

	while ((hwnd = FindWindowEx(NULL, hwnd, NULL, NULL)) != NULL) {
		wchar_t windowTitle[256];
		GetWindowText(hwnd, windowTitle, sizeof(windowTitle) / sizeof(wchar_t));

		if (prefixed) {
			// Check if the window title starts with the specified prefix
			if (_tcsncmp(windowTitle, search, _tcslen(search)) == 0) {
				return hwnd;  // Found the window
			}
		}
		else {
			if (lstrcmpi(windowTitle, search) == 0) {
				return hwnd;  // Found the window
			}
		}
	}

	return NULL;  // No matching window found
}

u32 GetForegroundWindowProcessID() {
	HWND ForegroundWindow = GetForegroundWindow();
	if (ForegroundWindow == NULL)
	{
		MsgBox(L"Failed to detect foreground window!", APPNAME L" Error", MB_OK | MB_ICONERROR);
		return 0;
	}
	u32 ProcessId;
	GetWindowThreadProcessId(ForegroundWindow, &ProcessId);

	return ProcessId;
}

bool HideWindowForProfile(HWND hWnd, int profileId, HWND activeWindow) {
	bool windowChanged = false;
	
	if (hWnd != NULL) {
		if (hWnd == activeWindow)
			gProfiles[profileId].ForegroundWindow = hWnd;

		if (gProfiles[profileId].HideEnabled && IsWindowVisible(hWnd)) {
			ShowWindow(hWnd, SW_HIDE);
			windowChanged = true;
		}
		else if (gProfiles[profileId].MinimizeEnabled && IsWindowVisible(hWnd) && !IsIconic(hWnd)) {
			ShowWindow(hWnd, SW_MINIMIZE);
			windowChanged = true;
		}

		if (windowChanged && gProfiles[profileId].NumHiddenWindows < MAX_PROCESSES_PER_PROFILE) {
			gProfiles[profileId].HiddenWindows[gProfiles[profileId].NumHiddenWindows++] = hWnd;
		}
	}

	return windowChanged;
}

bool HideWindowsForPofile(int profileId) {
	bool windowChanged = false;
	HWND activeWindow = GetForegroundWindow();

	if (gProfiles[profileId].NumWindows > 0) {
		for (int i = 0; i < gProfiles[profileId].NumWindows; i++) {
			HWND found = FindWindowWithTitle(gProfiles[profileId].WindowNames[i]);
			if (HideWindowForProfile(found, profileId, activeWindow))
				windowChanged = true;
		}
	}
	else if (gProfiles[profileId].ProgramExeName[0] &&
	         (gProfiles[profileId].HideEnabled || gProfiles[profileId].MinimizeEnabled)) {
		for (int i = 0; i < gNumWindowInfo; i++) {
			if (_wcsicmp(gWindowInfo[i].ExeName, gProfiles[profileId].ProgramExeName) == 0) {
				if (HideWindowForProfile(gWindowInfo[i].WindowHandle, profileId, activeWindow))
					windowChanged = true;
			}
		}
	}
	return windowChanged;
}

bool RestoreWindow(HWND hWnd, bool hide, bool minimise) {
	bool windowChanged = false;
	if (hide) {
		ShowWindow(hWnd, SW_SHOW);
		windowChanged = true;
	}
	else if (minimise) {
		ShowWindow(hWnd, SW_RESTORE);
		windowChanged = true;
	}

	return windowChanged;
}

bool RestoreWindowsForPofile(int profileId) {
	bool windowChanged = false;

	// No tracked windows: find current windows for this profile (handles UAC restart + unfocused-but-visible case).
	// Only include visible windows — invisible helpers (e.g. QTrayIconMessageWindow) have WS_VISIBLE cleared
	// and must not be SW_SHOW'd by RestoreWindow below.
	if (gProfiles[profileId].NumHiddenWindows == 0 && gProfiles[profileId].ProgramExeName[0]) {
		for (int i = 0; i < gNumWindowInfo; i++) {
			if (_wcsicmp(gWindowInfo[i].ExeName, gProfiles[profileId].ProgramExeName) == 0 &&
			    gProfiles[profileId].NumHiddenWindows < MAX_PROCESSES_PER_PROFILE &&
			    IsWindowVisible(gWindowInfo[i].WindowHandle)) {
				gProfiles[profileId].HiddenWindows[gProfiles[profileId].NumHiddenWindows++] = gWindowInfo[i].WindowHandle;
			}
		}
	}

	HWND toFocus = NULL;
	for (int i = 0; i < gProfiles[profileId].NumHiddenWindows; i++) {
		HWND hw = gProfiles[profileId].HiddenWindows[i];
		if (!IsWindow(hw)) continue;
		if (RestoreWindow(hw, gProfiles[profileId].HideEnabled, gProfiles[profileId].MinimizeEnabled))
			windowChanged = true;
		if (toFocus == NULL) toFocus = hw;
		if (hw == gProfiles[profileId].ForegroundWindow) toFocus = hw;
	}
	if (toFocus) {
		if (IsIconic(toFocus)) ShowWindow(toFocus, SW_RESTORE);
		SetForegroundWindow(toFocus);
		SetActiveWindow(toFocus);
		SetFocus(toFocus);
	}
	gProfiles[profileId].NumHiddenWindows = 0;
	return windowChanged;
}

//bool RestoreWindowsForProcess(u32 processId, bool hide, bool minimise) {
//	bool windowChanged = false;
//
//	if (processId != 0 && (hide || minimise)) {
//		for (int i = 0; i < gNumWindowInfo; i++) {
//			if (gWindowInfo[i].ProcessID == processId) {
//				if (RestoreWindow(gWindowInfo[i].WindowHandle, hide, minimise))
//					windowChanged = true;
//			}
//		}
//	}
//	return windowChanged;
//}

void HandleProfile(int profileId, bool trigger) {

	if (trigger) {
		//order is - hide/minimize/pause here, windows first, processes last
		bool windowChanged = false;
		gProfiles[profileId].ForegroundWindow = 0;

		//work with all process windows then
		windowChanged = HideWindowsForPofile(profileId);

		//now pause stuff if needed
		if (gProfiles[profileId].PauseEnabled) {
			if (gProfiles[profileId].ProcessID != 0) {
				if (windowChanged) {
					//give it some time to react before pausing
					Sleep(10);
					windowChanged = false;
				}

				SuspendProcess(gProfiles[profileId].ProcessID);
			}
		}
	}
	else {
		//order is - unpause / show / restore here, processes first, windows last
		bool processAwoken = false;
		if (gProfiles[profileId].PauseEnabled && gProfiles[profileId].ProcessID != 0) {
			ResumeProcess(gProfiles[profileId].ProcessID);
			processAwoken = true;
		}
		if (processAwoken) Sleep(10);
		RestoreWindowsForPofile(profileId);
	}
}

void DispatchHotkey(int hkID)
{
	UpdateProcessIDs();
	UpdateWindowProcessIDs();

	EnterCriticalSection(&gHotkeyLock);
	int ids[MAX_PROFILES]; int n = gHotkeys[hkID].NumProfileIDs;
	for (int i = 0; i < n; i++) ids[i] = gHotkeys[hkID].ProfileIDs[i];
	LeaveCriticalSection(&gHotkeyLock);

	// Hide if any profile window is currently focused; otherwise restore/bring to front.
	// A minimized (iconic) window is never "in front" even if GetForegroundWindow returns it.
	HWND fg = GetForegroundWindow();
	bool trigger = false;
	if (fg && !IsIconic(fg)) {
		for (int i = 0; i < n && !trigger; i++) {
			int pid = ids[i];
			if (gProfiles[pid].NumWindows > 0) {
				for (int j = 0; j < gProfiles[pid].NumWindows && !trigger; j++) {
					HWND found = FindWindowWithTitle(gProfiles[pid].WindowNames[j]);
					if (found && found == fg) trigger = true;
				}
			} else {
				for (int k = 0; k < gNumWindowInfo && !trigger; k++) {
					if (_wcsicmp(gWindowInfo[k].ExeName, gProfiles[pid].ProgramExeName) == 0 &&
					    gWindowInfo[k].WindowHandle == fg)
						trigger = true;
				}
			}
		}
	}

	for (int i = 0; i < n; i++)
		HandleProfile(ids[i], trigger);
}


void MsgBox(const wchar_t* Message, const wchar_t* Caption, u32 Flags, ...)
{
	wchar_t FormattedMessage[1024] = { 0 };
	va_list Args = NULL;

	va_start(Args, Flags);
	_vsnwprintf_s(FormattedMessage, _countof(FormattedMessage), _TRUNCATE, Message, Args);
	va_end(Args);
	DbgPrint(FormattedMessage);
	MessageBoxW(NULL, FormattedMessage, Caption, Flags);
}

void DbgPrint(const wchar_t* Message, ...)
{
	if (gConfig.Debug == FALSE || gDbgConsole == INVALID_HANDLE_VALUE)
	{
		return;
	}

	wchar_t FormattedMessage[1024] = { 0 };
	u32 MsgLen = 0;
	wchar_t TimeString[64] = { 0 };
	SYSTEMTIME Time;
	va_list Args = NULL;

	va_start(Args, Message);
	_vsnwprintf_s(FormattedMessage, _countof(FormattedMessage), _TRUNCATE, Message, Args);
	va_end(Args);

	MsgLen = (u32)wcslen(FormattedMessage);
	FormattedMessage[MsgLen] = '\n';
	FormattedMessage[MsgLen + 1] = '\0';

	GetLocalTime(&Time);
	_snwprintf_s(TimeString, _countof(TimeString), _TRUNCATE, L"[%02d.%02d.%04d %02d.%02d.%02d.%03d] ", Time.wMonth, Time.wDay, Time.wYear, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds);
	WriteConsoleW(gDbgConsole, TimeString, (u32)wcslen(TimeString), NULL, NULL);
	WriteConsoleW(gDbgConsole, FormattedMessage, (u32)wcslen(FormattedMessage), NULL, NULL);	
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;

 		if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN || wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
			bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;

			WORD vkCode = pKeyboard->vkCode;                           // virtual-key code

			// KBDLLHOOKSTRUCT.flags uses LLKHF_* bit layout (different from KF_* in WM_KEYDOWN lParam).
			// Bit 7 (0x80 = LLKHF_UP): 1 if key released, 0 if pressed.
			// There is no repeat bit in KBDLLHOOKSTRUCT; repeat is tracked manually below.
			BOOL isKeyReleased = (pKeyboard->flags & LLKHF_UP) != 0;

			DWORD32 t = GetTickCount();
			DbgPrint(L"Key %s: released:%d Enter\n", down ? L"pressed" : L"released", isKeyReleased);

			//search for hotkey for that VK
			EnterCriticalSection(&gHotkeyLock);
			int matchIndex = -1;
			for (int i = 0; i < gNumHotkeys; i++) {
				if (gHotkeys[i].HotKey == vkCode) {
					// Use GetAsyncKeyState for modifiers: it reflects actual hardware state,
					// unlike GetKeyState which depends on the thread's message-queue processing
					// and can lag in LL hook context (causing missed hotkeys in some apps).
					BOOL shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
					BOOL ctrl  = GetAsyncKeyState(VK_CONTROL) & 0x8000;
					BOOL alt   = GetAsyncKeyState(VK_MENU) & 0x8000;
					u32 modifiers = (shift ? MOD_SHIFT : 0) | (ctrl ? MOD_CONTROL : 0) | (alt ? MOD_ALT : 0);
					if (gHotkeys[i].Modifiers == modifiers) { matchIndex = i; break; }
				}
			}
			LeaveCriticalSection(&gHotkeyLock);

			if (matchIndex >= 0) {
				if (down && !gHotkeyHeld[matchIndex]) {
					gHotkeyHeld[matchIndex] = true;
					Job j = { JOB_TOGGLE_HOTKEY, matchIndex };
					JobQueuePush(j);
				} else if (!down) {
					gHotkeyHeld[matchIndex] = false;
				}
				return 1;
			}
			DbgPrint(L"Key %s: released:%d Exit, time taken: %d\n", down ? L"pressed" : L"released", isKeyReleased, (GetTickCount() - t));
		}
	}

	// Pass the hook information to the next hook procedure in the current hook chain
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK SysTrayCallback(_In_ HWND Window, _In_ UINT Message, _In_ WPARAM WParam, _In_ LPARAM LParam)
{
	LRESULT Result = 0;

	if (Message == gTaskbarCreatedMsg && gTaskbarCreatedMsg != 0) {
		Shell_NotifyIconW(NIM_ADD, &gTrayNotifyIconData);
		return 0;
	}

	switch (Message)
	{
		case WM_TRAYICON:
		{
			if (LParam == WM_LBUTTONUP || LParam == WM_LBUTTONDBLCLK) {
				ShowSettingsWindow(GetModuleHandleW(NULL), Window);
				break;
			}
			if (LParam == WM_RBUTTONUP) {
				FixStartupPathIfStale();
				HMENU menu = CreatePopupMenu();
				AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings...");
				AppendMenuW(menu, MF_STRING | (IsStartupEnabled() ? MF_CHECKED : 0),
				            IDM_STARTUP, L"Run at Windows startup");
				AppendMenuW(menu, MF_STRING, IDM_INSTALL, L"Install to user programs...");
				AppendMenuW(menu, MF_STRING, IDM_OPENINI, L"Open INI folder");
				AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
				AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quit");

				POINT pt; GetCursorPos(&pt);
				SetForegroundWindow(Window);
				TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, Window, NULL);
				DestroyMenu(menu);
			}
			break;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(WParam)) {
				case IDM_SETTINGS: ShowSettingsWindow(GetModuleHandleW(NULL), Window); break;
				case IDM_STARTUP:  SetStartupEnabled(!IsStartupEnabled()); break;
				case IDM_INSTALL:  InstallToUserPrograms(Window); break;
				case IDM_OPENINI:  OpenConfigFolder(); break;
				case IDM_QUIT:
					Shell_NotifyIconW(NIM_DELETE, &gTrayNotifyIconData);
					gIsRunning = FALSE;
					PostQuitMessage(0);
					break;
			}
			break;
		}
		default:
		{
			Result = DefWindowProcW(Window, Message, WParam, LParam);
			break;
		}
	}
	return(Result);
}

// ---------------------------------------------------------------------------
//  Crash logger: catches unhandled exceptions and writes a report to %TEMP%
// ---------------------------------------------------------------------------
static FILE* gCrashLog = NULL;

void CrashLog(const char* fmt, ...) {
    if (!gCrashLog) return;
    va_list a; va_start(a, fmt); vfprintf(gCrashLog, fmt, a); va_end(a);
    fflush(gCrashLog);
}

static LONG WINAPI UnhandledCrash(EXCEPTION_POINTERS* ep) {
    CrashLog("*** UNHANDLED EXCEPTION ***\n");
    CrashLog("  Code    : 0x%08X\n", (unsigned)ep->ExceptionRecord->ExceptionCode);
    CrashLog("  Address : %p\n",              ep->ExceptionRecord->ExceptionAddress);
    CrashLog("  Flags   : 0x%08X\n", (unsigned)ep->ExceptionRecord->ExceptionFlags);
    if (ep->ExceptionRecord->NumberParameters > 0)
        CrashLog("  Info[0] : 0x%p\n", (void*)ep->ExceptionRecord->ExceptionInformation[0]);
    if (ep->ExceptionRecord->NumberParameters > 1)
        CrashLog("  Info[1] : 0x%p\n", (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    if (gCrashLog) { fclose(gCrashLog); gCrashLog = NULL; }
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI wWinMain(_In_ HINSTANCE Instance, _In_opt_ HINSTANCE PrevInstance, _In_ PWSTR CmdLine, _In_ int CmdShow)
{
	for (int ai = 1; ai < __argc; ai++) {
		if (lstrcmpiW(__wargv[ai], L"--selftest") == 0)
			return RunSelfTests();
		if (lstrcmpiW(__wargv[ai], L"--monitor-debug") == 0)
			return RunMonitorDebug();
	}

	// Install crash logger
	{
		char p[MAX_PATH]; GetTempPathA(MAX_PATH, p);
		strcat_s(p, MAX_PATH, "uac-crash.txt");
		fopen_s(&gCrashLog, p, "w");
		SetUnhandledExceptionFilter(UnhandledCrash);
		CrashLog("=== UAC crash log started ===\n");
	}

	bool autostart = false;
	for (int ai = 1; ai < __argc; ai++)
		if (lstrcmpiW(__wargv[ai], L"--autostart") == 0) autostart = true;

	UNREFERENCED_PARAMETER(PrevInstance);
	UNREFERENCED_PARAMETER(CmdLine);
	UNREFERENCED_PARAMETER(CmdShow);

	HHOOK hHook = NULL;
	HANDLE workerThread = NULL;

	HMODULE NtDll = NULL;
	MSG WndMsg = { 0 };

	if (!ReadConfig())
	{
		MsgBox(L"Program settings failed to load. Error %s", APPNAME L" Error", MB_OK | MB_ICONERROR, GetLastErorText());
		goto Exit;
	}

	InitializeCriticalSection(&gHotkeyLock);
	WorkerInit();
	DisplayInit();   // must be called before worker thread starts
	CrashLog("[main] DisplayInit done\n");
	workerThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);
	if (workerThread == NULL) {
		MsgBox(L"Failed to start worker thread!", APPNAME L" Error", MB_OK | MB_ICONERROR);
		goto Exit;
	}

	gMutex = CreateMutexW(NULL, FALSE, APPNAME);
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(gMutex); gMutex = NULL;
		HWND existingWnd = FindWindowW(APPNAME L"_WndClass", NULL);
		if (existingWnd)
			PostMessageW(existingWnd, WM_COMMAND, MAKEWPARAM(IDM_SETTINGS, 0), 0);
		goto Exit;
	}
	if ((NtDll = GetModuleHandleW(L"ntdll.dll")) == NULL)
	{
		MsgBox(L"Unable to locate ntdll.dll!\nError: 0x%08lx", APPNAME L" Error", MB_OK | MB_ICONERROR, GetLastError());
		goto Exit;
	}
	if ((NtSuspendProcess = (_NtSuspendProcess)((void*)GetProcAddress(NtDll, "NtSuspendProcess"))) == NULL)
	{
		MsgBox(L"Unable to locate the NtSuspendProcess procedure in the ntdll.dll module!", APPNAME L" Error", MB_OK | MB_ICONERROR);
		goto Exit;
	}
	if ((NtResumeProcess = (_NtResumeProcess)((void*)GetProcAddress(NtDll, "NtResumeProcess"))) == NULL)
	{
		MsgBox(L"Unable to locate the NtResumeProcess procedure in the ntdll.dll module!", APPNAME L" Error", MB_OK | MB_ICONERROR);
		goto Exit;
	}

	hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);

	if (hHook == NULL) {
		printf("Failed to install hook!\n");
		return 1;
	}

	// There will be no visible window either way, but in one case, there will be a system tray icon,
	// and in the other case there will be no icon. This is because someone requested that I make this
	// app work even when the user has no shell (explorer.exe) or has replaced their shell with an alternative shell.
	// The Windows system tray API obviously won't work if there is no Windows system tray. I don't know if the user's
	// shell even has a taskbar so I'm skipping that too.

	{
		WNDCLASSW WndClass = { 0 };
		HWND HWnd = NULL;

		WndClass.hInstance = Instance;
		WndClass.lpszClassName = APPNAME L"_WndClass";
		WndClass.lpfnWndProc = SysTrayCallback;

		if (RegisterClassW(&WndClass) == 0)
		{
			MsgBox(L"Failed to register WindowClass! Error 0x%08lx", APPNAME L" Error", MB_OK | MB_ICONERROR, GetLastError());
			goto Exit;
		}

		HWnd = CreateWindowExW(
			WS_EX_TOOLWINDOW,
			WndClass.lpszClassName,
			APPNAME L"_Systray_Window",
			WS_ICONIC,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			0,
			0,
			Instance,
			0);

		if (HWnd == NULL)
		{
			MsgBox(L"Failed to create window! Error 0x%08lx", APPNAME L" Error", MB_OK | MB_ICONERROR, GetLastError());
			goto Exit;
		}

		gTrayNotifyIconData.cbSize = sizeof(NOTIFYICONDATA);
		gTrayNotifyIconData.hWnd = HWnd;
		gTrayNotifyIconData.uID = 1982;
		gTrayNotifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		gTrayNotifyIconData.uCallbackMessage = WM_TRAYICON;
		wcscpy_s(gTrayNotifyIconData.szTip, _countof(gTrayNotifyIconData.szTip), APPNAME L" v" VERSION);
		gTrayNotifyIconData.hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 0, 0, 0);

		if (gTrayNotifyIconData.hIcon == NULL)
		{
			MsgBox(L"Failed to load systray icon resource!", APPNAME L" Error", MB_OK | MB_ICONERROR);
			goto Exit;
		}

		gTaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

		if (Shell_NotifyIconW(NIM_ADD, &gTrayNotifyIconData) == FALSE)
		{
			MsgBox(L"Failed to register systray icon!", APPNAME L" Error", MB_OK | MB_ICONERROR);
			goto Exit;
		}
	}

	if (!autostart)
		ShowSettingsWindow(Instance, gTrayNotifyIconData.hWnd);

	while (gIsRunning)
	{
		while (PeekMessageW(&WndMsg, NULL, 0, 0, PM_REMOVE)) {
			if (WndMsg.message == WM_QUIT) { gIsRunning = FALSE; break; }
			if (gSettingsWnd && IsDialogMessageW(gSettingsWnd, &WndMsg))
				continue;
			TranslateMessage(&WndMsg);
			DispatchMessageW(&WndMsg);
		}

		// Foreground-app polling for display presets (~400 ms).
		// Detect which app is in focus, find its profile's DisplayPreset,
		// and push JOB_APPLY_DISPLAY so the worker updates the monitor.
		if (gDisplayControlEnabled) {
			static HWND  sFgLast   = NULL;
			static DWORD sFgPollMs = 0;
			DWORD tNow = GetTickCount();
			if (tNow - sFgPollMs >= 400) {
				sFgPollMs = tNow;
				HWND fg = GetForegroundWindow();
				if (fg != sFgLast) {
					// Only act when the window is on the PRIMARY monitor.
					// If it's on a secondary display, leave the last preset active.
					if (fg) {
						HMONITOR hfg = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
						MONITORINFO mfi = { sizeof(mfi) };
						if (hfg && GetMonitorInfo(hfg, &mfi) &&
						    !(mfi.dwFlags & MONITORINFOF_PRIMARY)) {
							// Secondary monitor — skip, don't update sFgLast
							goto fg_poll_done;
						}
					}
					sFgLast = fg;
					// Find which preset applies: scan profiles by exe name.
					wchar_t presetName[MAX_NAME] = {0};
					if (fg) {
						DWORD pid = 0;
						GetWindowThreadProcessId(fg, &pid);
						HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
						if (hp) {
							wchar_t exePath[MAX_PATH] = {0};
							DWORD sz = MAX_PATH;
							if (QueryFullProcessImageNameW(hp, 0, exePath, &sz)) {
								wchar_t* slash = wcsrchr(exePath, L'\\');
								wchar_t* exeFile = slash ? slash + 1 : exePath;
								for (int i = 0; i < gNumProfiles; i++) {
									if (gProfiles[i].ProgramExeName[0] &&
									    _wcsicmp(gProfiles[i].ProgramExeName, exeFile) == 0 &&
									    gProfiles[i].DisplayPreset[0]) {
										wcscpy_s(presetName, MAX_NAME, gProfiles[i].DisplayPreset);
										break;
									}
								}
							}
							CloseHandle(hp);
						}
					}
					// Fall back to default preset when no profile matched
					if (!presetName[0] && gDefaultPresetName[0])
						wcscpy_s(presetName, MAX_NAME, gDefaultPresetName);

					if (presetName[0]) {
						EnterCriticalSection(&gHotkeyLock);
						gDesiredDisplay.hwnd  = fg ? fg : GetDesktopWindow();
						gDesiredDisplay.valid = true;
						wcscpy_s(gDesiredDisplay.presetName, MAX_NAME, presetName);
						LeaveCriticalSection(&gHotkeyLock);
						Job jd = { JOB_APPLY_DISPLAY, 0 };
						JobQueuePush(jd);
					}
				}
			}
			fg_poll_done:;
		}

		// MsgWaitForMultipleObjects instead of Sleep(5): the thread wakes immediately
		// when any message (including LL keyboard hook callbacks) arrives, rather than
		// blocking up to 5 ms. Sleep() cannot process hook callbacks at all while blocked.
		MsgWaitForMultipleObjects(0, NULL, FALSE, 5, QS_ALLINPUT);
	}

	{ Job j = { JOB_SHUTDOWN, 0 }; JobQueuePush(j); }
	if (workerThread) WaitForSingleObject(workerThread, 2000);

	// Resume any processes we suspended
	for (int i = 0; i < gNumPausedProcesses; i++) {
		HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, gPausedProcesses[i]);
		if (h) { NtResumeProcess(h); CloseHandle(h); }
	}
	if (gNumPausedProcesses > 0) Sleep(50);
	gNumPausedProcesses = 0;

	// On exit: only un-hide windows that were fully hidden (SW_HIDE).
	// Windows that were merely minimized are left as-is — the user didn't ask us to restore them.
	for (int i = 0; i < gNumProfiles; i++) {
		if (gProfiles[i].HideEnabled) {
			for (int j = 0; j < gProfiles[i].NumHiddenWindows; j++) {
				HWND hw = gProfiles[i].HiddenWindows[j];
				if (IsWindow(hw))
					ShowWindow(hw, SW_SHOW);
			}
		}
		gProfiles[i].NumHiddenWindows = 0;
	}

	// Restore all monitors we adjusted to their original values.
	DisplayRestoreAll();

Exit:
	// Unhook the hook before exiting
	if(hHook != NULL)
		UnhookWindowsHookEx(hHook);

	return(0);
}
