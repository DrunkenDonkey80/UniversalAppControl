# UniversalAppControl Extension Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the two reliability bugs (vanishing tray icon, hotkeys freezing) and extend UniversalAppControl into a usable native hotkey manager with a settings UI, process picker, startup registration, and a per-user install flow.

**Architecture:** Keep the single self-contained Win32 C executable. Move all heavy process/window work off the keyboard-hook thread onto one dedicated worker thread fed by a job queue (the hook only enqueues and returns immediately). Re-add the tray icon on the `TaskbarCreated` broadcast. Build the settings UI and process picker as programmatically-created Win32 windows (no `.rc` dialog templates). Config lives in `%APPDATA%\UniversalAppControl\config.ini`.

**Tech Stack:** C (MSVC v143), Win32 API, Common Controls (ListView), COM (`IShellLink` for the Start Menu shortcut), MSBuild. Source spec: `docs/superpowers/specs/2026-06-10-universalappcontrol-extension-design.md`.

---

## Conventions (read once, applies to every task)

**Build command** (run from "Developer PowerShell for VS 2022", working dir = repo root):

```powershell
msbuild UniversalAppControl.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:m
```

Build output: `x64\Debug\UniversalAppControl.exe`. "Build passes" = the command prints `Build succeeded` / exits 0, with no new errors.

**Self-test run** (the exe is a Windows-subsystem app, so capture the exit code explicitly):

```powershell
$p = Start-Process "x64\Debug\UniversalAppControl.exe" -ArgumentList "--selftest" -Wait -PassThru -NoNewWindow
"exit=$($p.ExitCode)"; Get-Content "$env:TEMP\uac-selftest.txt"
```

Exit code = number of failed checks (`exit=0` means all passed). The self-test also writes a human-readable log to `%TEMP%\uac-selftest.txt`.

**String safety:** the project builds with `/sdl` and `EnableAllWarnings`. Always use the `_s` string functions (`wcscpy_s`, `wcscat_s`, `swprintf_s`, `_snwprintf_s`) and size every buffer. Functions declared returning `bool`/`int` must return a value on every path (the existing `SuspendProcess`/`ResumeProcess` have bare `return;` bugs - fix them when you touch them).

**Commit convention:** Conventional Commits. Each task ends with one commit. End every commit message body with:

```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

**Threading ownership rule (do not violate):** after Task 6, the *worker thread* is the only thread that may touch `gProfiles[*]` runtime fields, `gPausedProcesses`, `gNumPausedProcesses`, and each hotkey's `Triggered` flag. The hook thread and UI thread communicate with it only by pushing jobs. `gHotkeys`/`gNumHotkeys` are read by the hook thread and rebuilt by the worker, so every access goes through `gHotkeyLock`.

---

## Task 1: Project scaffolding, new files, and self-test harness

Creates the empty module files, wires them into the project in one edit, and stands up the `--selftest` loop with one trivial passing check so every later task has a working build+test cycle.

**Files:**
- Create: `ui_ids.h`, `config.h`, `config.c`, `worker.h`, `worker.c`, `settings_ui.h`, `settings_ui.c`, `install.h`, `install.c`, `selftest.h`, `selftest.c`
- Modify: `UniversalAppControl.vcxproj`, `UniversalAppControl.filters`, `Main.c:761` (wWinMain entry)

- [ ] **Step 1: Create `ui_ids.h`** (control and menu command IDs; plain ASCII)

```c
#pragma once

// Tray menu commands
#define IDM_SETTINGS   2001
#define IDM_STARTUP    2002
#define IDM_INSTALL    2003
#define IDM_OPENINI    2004
#define IDM_QUIT       2005

// Settings window controls
#define IDC_LIST       3001
#define IDC_NAME       3002
#define IDC_PATH       3003
#define IDC_HOTKEY     3004
#define IDC_HIDE       3005
#define IDC_MIN        3006
#define IDC_PAUSE      3007
#define IDC_ADD        3008
#define IDC_REMOVE     3009
#define IDC_STARTUP    3010
#define IDC_OPENINI    3011
#define IDC_INSTALL    3012
#define IDC_STATUS     3013

// Process picker controls
#define IDC_PICKLIST   3101
#define IDC_PICKOK     3102
#define IDC_PICKCANCEL 3103
```

- [ ] **Step 2: Create stub headers and source files** so the project compiles with them present. Each stub:

`config.h`:
```c
#pragma once
#include <Windows.h>
#include "Main.h"

const wchar_t* GetConfigPath(void);   // %APPDATA%\UniversalAppControl\config.ini, ensures dir
const wchar_t* GetConfigDir(void);    // %APPDATA%\UniversalAppControl
void SaveConfig(void);                // writes gProfiles + [general] to the ini
bool ParseHotkey(const wchar_t* text, u32* outVk, UINT* outMods);
void FormatHotkey(u32 vk, UINT mods, wchar_t* out, int cch);
```
`config.c`:
```c
#include "config.h"
// implementations added in later tasks
```
`worker.h`:
```c
#pragma once
#include <Windows.h>

typedef enum { JOB_TOGGLE_HOTKEY = 1, JOB_RELOAD_CONFIG, JOB_SHUTDOWN } JobType;
typedef struct { JobType type; int hotkeyIndex; } Job;

void WorkerInit(void);
bool JobQueuePush(Job job);
bool JobQueuePop(Job* out);          // blocks until a job is available
DWORD WINAPI WorkerThreadProc(LPVOID param);
```
`worker.c`:
```c
#include "worker.h"
// implementations added in later tasks
```
`settings_ui.h`:
```c
#pragma once
#include <Windows.h>
void ShowSettingsWindow(HINSTANCE inst, HWND owner);
bool PickRunningProcess(HWND parent, wchar_t* outName, int nameCch, wchar_t* outPath, int pathCch);
```
`settings_ui.c`:
```c
#include "settings_ui.h"
// implementations added in later tasks
```
`install.h`:
```c
#pragma once
#include <Windows.h>
bool IsStartupEnabled(void);
bool SetStartupEnabled(bool enabled);
void OpenConfigFolder(void);
bool InstallToUserPrograms(HWND parent);
```
`install.c`:
```c
#include "install.h"
// implementations added in later tasks
```
`selftest.h`:
```c
#pragma once
int RunSelfTests(void);   // returns number of failed checks
```
`selftest.c`:
```c
#include <Windows.h>
#include <stdio.h>
#include "selftest.h"

static int gFails = 0;
static FILE* gLog = NULL;

static void LogLine(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list a; va_start(a, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, a);
    va_end(a);
    wprintf(L"%s\n", buf);
    if (gLog) fwprintf(gLog, L"%s\n", buf);
}

#define CHECK(cond, name) do { \
    if (cond) { LogLine(L"PASS: %s", name); } \
    else { gFails++; LogLine(L"FAIL: %s", name); } \
} while (0)

int RunSelfTests(void) {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
    wchar_t logPath[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, logPath);
    wcscpy_s(logPath + n, MAX_PATH - n, L"uac-selftest.txt");
    _wfopen_s(&gLog, logPath, L"w, ccs=UTF-8");

    gFails = 0;
    CHECK(1 + 1 == 2, L"harness sanity");
    // later tasks append CHECK(...) calls here

    LogLine(L"--- %d failure(s) ---", gFails);
    if (gLog) fclose(gLog);
    return gFails;
}
```

- [ ] **Step 3: Add the new files to `UniversalAppControl.vcxproj`.** Replace the two existing `ItemGroup`s for compile/include with these (keep the `ResourceCompile` and `Image` groups unchanged):

```xml
  <ItemGroup>
    <ClCompile Include="keys.c" />
    <ClCompile Include="Main.c" />
    <ClCompile Include="config.c" />
    <ClCompile Include="worker.c" />
    <ClCompile Include="settings_ui.c" />
    <ClCompile Include="install.c" />
    <ClCompile Include="selftest.c" />
  </ItemGroup>
  <ItemGroup>
    <ClInclude Include="keys.h" />
    <ClInclude Include="Main.h" />
    <ClInclude Include="resource.h" />
    <ClInclude Include="ui_ids.h" />
    <ClInclude Include="config.h" />
    <ClInclude Include="worker.h" />
    <ClInclude Include="settings_ui.h" />
    <ClInclude Include="install.h" />
    <ClInclude Include="selftest.h" />
  </ItemGroup>
```

- [ ] **Step 4: Add the same files to `UniversalAppControl.filters`.** In the Source-Files `ItemGroup` add `config.c`, `worker.c`, `settings_ui.c`, `install.c`, `selftest.c` (each `<Filter>Source Files</Filter>`); in the Header-Files `ItemGroup` add `ui_ids.h`, `config.h`, `worker.h`, `settings_ui.h`, `install.h`, `selftest.h` (each `<Filter>Header Files</Filter>`). Example entry:

```xml
    <ClCompile Include="config.c">
      <Filter>Source Files</Filter>
    </ClCompile>
```

- [ ] **Step 5: Wire `--selftest` into `wWinMain`.** In `Main.c`, add includes near the existing includes (after line 17):

```c
#include <stdlib.h>   // __argc, __wargv
#include "ui_ids.h"
#include "config.h"
#include "worker.h"
#include "settings_ui.h"
#include "install.h"
#include "selftest.h"
```

Then as the very first statements inside `wWinMain` (Main.c:762, before `UNREFERENCED_PARAMETER`):

```c
    for (int ai = 1; ai < __argc; ai++) {
        if (lstrcmpiW(__wargv[ai], L"--selftest") == 0)
            return RunSelfTests();
    }
```

- [ ] **Step 6: Build**

Run the Build command. Expected: `Build succeeded`, produces `x64\Debug\UniversalAppControl.exe`.

- [ ] **Step 7: Run the self-test**

Run the Self-test command. Expected: `exit=0`, log shows `PASS: harness sanity` and `--- 0 failure(s) ---`.

- [ ] **Step 8: Commit**

```powershell
git add ui_ids.h config.h config.c worker.h worker.c settings_ui.h settings_ui.c install.h install.c selftest.h selftest.c UniversalAppControl.vcxproj UniversalAppControl.filters Main.c
git commit -m "chore: scaffold modules and --selftest harness"
```

---

## Task 2: Move config to %APPDATA% and add the config-path helper

**Files:**
- Modify: `config.c`, `Main.c` (`ReadConfig` at Main.c:198-201 path setup; remove the file-static `iniFilePath` usage)
- Test: `selftest.c`

- [ ] **Step 1: Add the failing check** in `selftest.c` (inside `RunSelfTests`, after the sanity check). Add `#include "config.h"` at the top of `selftest.c` first.

```c
    {
        const wchar_t* p = GetConfigPath();
        size_t len = wcslen(p);
        const wchar_t* suffix = L"\\UniversalAppControl\\config.ini";
        bool endsOk = len > wcslen(suffix) &&
            _wcsicmp(p + len - wcslen(suffix), suffix) == 0;
        CHECK(endsOk, L"GetConfigPath ends with UniversalAppControl\\config.ini");
        CHECK(GetFileAttributesW(GetConfigDir()) != INVALID_FILE_ATTRIBUTES,
              L"GetConfigDir exists after call");
    }
```

- [ ] **Step 2: Run self-test to verify it fails**

Run the Self-test command. Expected: link error or `exit` nonzero (`GetConfigPath`/`GetConfigDir` unresolved or returning empty). If it fails to build because the functions are stubs, that is the expected failing state.

- [ ] **Step 3: Implement the helpers in `config.c`**

```c
#include "config.h"
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
```

- [ ] **Step 4: Point `ReadConfig` at the new path.** In `Main.c`, replace the path setup at the top of `ReadConfig` (Main.c:199-201):

```c
    wcscpy_s(iniFilePath, MAX_PATH, GetConfigPath());
```

Delete the old three lines that derived the path from `GetModuleFileName`. Keep the file-static `iniFilePath` buffer declaration (Main.c:61) - it is still used by all the `GetPrivateProfileString` calls.

- [ ] **Step 5: Build and run self-test**

Run Build, then the Self-test command. Expected: `Build succeeded`, `exit=0`, both new checks `PASS`.

- [ ] **Step 6: Migration note + manual check.** If you have an existing `.ini` next to the old exe, copy it to `%APPDATA%\UniversalAppControl\config.ini` before running normally. Launch `x64\Debug\UniversalAppControl.exe` (no args) - it should start without the "settings failed to load" error if a config exists, or show it if none does (expected until later tasks). No crash.

- [ ] **Step 7: Commit**

```powershell
git add config.c Main.c selftest.c
git commit -m "feat: store config in %APPDATA%\UniversalAppControl"
```

---

## Task 3: Add ProgramPath and remove the boss-key dead code

**Files:**
- Modify: `Main.h` (struct `_PROFILE_CONFIG` at Main.h:50-69, `_CONFIG` at Main.h:38-48, IDs at Main.h:29-30), `Main.c` (`ReadConfig` boss calls at Main.c:230-233, `HandleKeyboardHotkey` dead branches at Main.c:621-628)

- [ ] **Step 1: Edit `Main.h`.** In `_PROFILE_CONFIG` add after `ProgramExeName` (Main.h:59):

```c
	wchar_t ProgramPath[MAX_PATH];
```

In `_CONFIG` (Main.h:38-48) remove `BossHotKey`, `BossHotKeyModifiers`, `BossSections`, `NumBossSections`, leaving:

```c
typedef struct _CONFIG
{
	BOOL Debug;
	BOOL TrayIcon;
} CONFIG;
```

Remove the ID defines at Main.h:29-30 (`PRIFILE_ID_BOSS`, `PROFILE_ID_CURRENT`).

Add `extern` declarations for the globals that other translation units (config.c, settings_ui.c) will reference, at the end of `Main.h` (after the `_PROFILE_CONFIG` struct, alongside the existing function declarations):

```c
extern CONFIG gConfig;
extern PROFILE_CONFIG gProfiles[MAX_PROFILES];
extern int gNumProfiles;
```

(`gConfig`, `gProfiles`, `gNumProfiles` are defined non-`static` in `Main.c` at lines 19-22, so these `extern`s just publish them. `gHotkeys`/`HotkeyInfo` stay private to `Main.c`.)

- [ ] **Step 2: Edit `ReadConfig` in `Main.c`.** In the `general` section branch (Main.c:223-234) delete the two `RegisterConfigHotkey(... BossHotkey ...)` / `... CurrentWindowHotkey ...` calls and the `ReadConfigList(... BossSections ...)` call. The branch keeps only `Debug` and `TrayIcon` handling.

In the profile branch (Main.c:236-247) add, after the `ProgramExeName` read (Main.c:243):

```c
		ReadConfigString(sectionName, L"ProgramPath", gProfiles[gNumProfiles].ProgramPath);
```

- [ ] **Step 3: Remove dead branches in `HandleKeyboardHotkey`.** Delete the `if (profileId == PRIFILE_ID_BOSS)` and `if (profileId == PROFILE_ID_CURRENT)` blocks (Main.c:621-628). The loop body becomes just the normal-profile `HandleProfile(profileId, gHotkeys[hkID].Triggered);`.

- [ ] **Step 4: Build**

Run Build. Expected: `Build succeeded`, no references to the removed symbols remain.

- [ ] **Step 5: Run self-test**

Run the Self-test command. Expected: `exit=0` (existing checks still pass).

- [ ] **Step 6: Commit**

```powershell
git add Main.h Main.c
git commit -m "refactor: add ProgramPath, remove boss-key dead code"
```

---

## Task 4: Hotkey parse/format + config save (round-trip tested)

Extracts the hotkey string parser out of `RegisterConfigHotkey` into a pure, testable function, adds a formatter for display, and implements `SaveConfig` so the UI can persist edits.

**Files:**
- Modify: `config.c`, `Main.c` (`RegisterConfigHotkey` at Main.c:111-170 to reuse `ParseHotkey`)
- Test: `selftest.c`

- [ ] **Step 1: Add failing checks** in `selftest.c`:

```c
    {
        u32 vk = 0; UINT mods = 0;
        bool ok = ParseHotkey(L"Ctrl+Alt+V", &vk, &mods);
        CHECK(ok && vk == 'V' && mods == (MOD_CONTROL | MOD_ALT), L"ParseHotkey Ctrl+Alt+V");

        wchar_t buf[64];
        FormatHotkey('V', MOD_CONTROL | MOD_ALT, buf, _countof(buf));
        CHECK(wcslen(buf) > 0, L"FormatHotkey non-empty");
        u32 vk2 = 0; UINT mods2 = 0;
        CHECK(ParseHotkey(buf, &vk2, &mods2) && vk2 == 'V' && mods2 == (MOD_CONTROL | MOD_ALT),
              L"FormatHotkey round-trips through ParseHotkey");

        CHECK(!ParseHotkey(L"Ctrl+Alt", &vk, &mods), L"ParseHotkey rejects modifier-only");
    }
```

- [ ] **Step 2: Run self-test to verify it fails**

Run the Self-test command. Expected: build/link failure or failing checks (functions are stubs).

- [ ] **Step 3: Implement `ParseHotkey` and `FormatHotkey` in `config.c`.** Add includes at top: `#include "keys.h"`.

```c
bool ParseHotkey(const wchar_t* text, u32* outVk, UINT* outMods) {
    *outVk = 0; *outMods = 0;
    wchar_t buf[256];
    wcscpy_s(buf, _countof(buf), text);
    for (int i = 0; buf[i]; i++)
        if (buf[i] == L'+' || buf[i] == L'|' || buf[i] == L'&') buf[i] = 0;

    wchar_t* b = buf;
    while (*b) {
        if (!lstrcmpiW(b, L"shift") || !lstrcmpiW(b, L"lshift") || !lstrcmpiW(b, L"rshift"))
            *outMods |= MOD_SHIFT;
        else if (!lstrcmpiW(b, L"alt") || !lstrcmpiW(b, L"lalt") || !lstrcmpiW(b, L"ralt"))
            *outMods |= MOD_ALT;
        else if (!lstrcmpiW(b, L"ctrl") || !lstrcmpiW(b, L"control") ||
                 !lstrcmpiW(b, L"lctrl") || !lstrcmpiW(b, L"rctrl") ||
                 !lstrcmpiW(b, L"lcontrol") || !lstrcmpiW(b, L"rcontrol"))
            *outMods |= MOD_CONTROL;
        else if (!lstrcmpiW(b, L"win") || !lstrcmpiW(b, L"window") || !lstrcmpiW(b, L"windows"))
            *outMods |= MOD_WIN;
        else {
            const KEYCode* key = findKeyWithName(b);
            if (key == NULL) return false;
            *outVk = key->vkCode;
        }
        b += lstrlen(b) + 1;
    }
    return *outVk != 0;   // must have a non-modifier key
}

void FormatHotkey(u32 vk, UINT mods, wchar_t* out, int cch) {
    out[0] = 0;
    if (mods & MOD_CONTROL) wcscat_s(out, cch, L"Ctrl+");
    if (mods & MOD_ALT)     wcscat_s(out, cch, L"Alt+");
    if (mods & MOD_SHIFT)   wcscat_s(out, cch, L"Shift+");
    if (mods & MOD_WIN)     wcscat_s(out, cch, L"Win+");
    // find the key name (strip the "VK_" prefix for readability)
    extern const wchar_t* FindKeyNameByVk(u32 vk);
    const wchar_t* name = FindKeyNameByVk(vk);
    if (name) wcscat_s(out, cch, name);
}
```

- [ ] **Step 4: Add `FindKeyNameByVk` to `keys.c`** (reverse lookup, returns the readable name without the `VK_` prefix). Add at the end of `keys.c`, and its declaration to `keys.h` after `findKeyWithName` (keys.h:549):

`keys.c`:
```c
const wchar_t* FindKeyNameByVk(u32 vk) {
    for (int i = 0; keyCodes[i].vkCode != HID_KEY_NONE; i++)
        if (keyCodes[i].vkCode == (unsigned char)vk)
            return &keyCodes[i].vkName[3];  // skip "VK_"
    return NULL;
}
```
`keys.h`:
```c
extern const wchar_t* FindKeyNameByVk(unsigned long vk);
```

- [ ] **Step 5: Refactor `RegisterConfigHotkey` to reuse `ParseHotkey`.** In `Main.c`, replace the manual parse block (Main.c:120-144) with a call to `ParseHotkey` on the raw INI string:

```c
	wchar_t raw[256] = { 0 };
	if (GetPrivateProfileStringW(section, variable, NULL, raw, _countof(raw), iniFilePath) <= 0)
		return;
	u32 hotKey = 0; UINT modifiers = 0;
	if (!ParseHotkey(raw, &hotKey, &modifiers))
		return;
```

Keep the existing dedupe/registration logic below it (Main.c:146-169) unchanged.

- [ ] **Step 6: Implement `SaveConfig` in `config.c`.** Writes `[general]` plus one section per profile. Uses the global `gProfiles`/`gNumProfiles`/`gConfig` (declared in `Main.h`, defined in `Main.c`).

```c
static void WriteBool(const wchar_t* section, const wchar_t* key, BOOL v) {
    WritePrivateProfileStringW(section, key, v ? L"true" : L"false", GetConfigPath());
}

void SaveConfig(void) {
    const wchar_t* path = GetConfigPath();
    // Clear the whole file first so removed entries disappear.
    WritePrivateProfileStringW(NULL, NULL, NULL, path);

    WriteBool(L"general", L"Debug", gConfig.Debug);
    WriteBool(L"general", L"TrayIcon", gConfig.TrayIcon);

    for (int i = 0; i < gNumProfiles; i++) {
        PROFILE_CONFIG* p = &gProfiles[i];
        const wchar_t* sec = p->ProgramExeName[0] ? p->ProgramExeName : L"Entry";
        wchar_t section[MAX_NAME];
        // ensure a unique, non-empty section name
        swprintf_s(section, _countof(section), L"%s_%d", sec, i);

        wchar_t hk[64];
        FormatHotkey(p->HotKey, p->HotKeyModifiers, hk, _countof(hk));
        WritePrivateProfileStringW(section, L"Hotkey", hk, path);
        WritePrivateProfileStringW(section, L"ProgramExeName", p->ProgramExeName, path);
        WritePrivateProfileStringW(section, L"ProgramPath", p->ProgramPath, path);
        WriteBool(section, L"Hide", p->HideEnabled);
        WriteBool(section, L"Minimize", p->MinimizeEnabled);
        WriteBool(section, L"Pause", p->PauseEnabled);
    }
}
```

Note: `RegisterConfigHotkey` reads `Hotkey`; `SaveConfig` writes `Hotkey`; `ReadConfig` reads `ProgramExeName`/`ProgramPath`/`Hide`/`Minimize`/`Pause`. These key names must match the reader in `ReadConfig` (Main.c:237-244) - they do.

- [ ] **Step 7: Add a round-trip check** in `selftest.c` (exercises Save then Read). Add `#include "Main.h"` and `extern` the reader. Because `ReadConfig` is `static`, expose a thin non-static wrapper: in `Main.c` add `bool LoadConfig(void) { return ReadConfig(); }` and declare `bool LoadConfig(void);` in `Main.h`. Then:

```c
    {
        // SaveConfig writes the REAL %APPDATA% config, so back it up first and
        // restore it after, exactly like the startup test does (no data loss).
        const wchar_t* cfg = GetConfigPath();
        wchar_t bak[MAX_PATH];
        swprintf_s(bak, _countof(bak), L"%s.selftest.bak", cfg);
        bool hadConfig = CopyFileW(cfg, bak, FALSE) != FALSE;

        gNumProfiles = 1;
        memset(&gProfiles[0], 0, sizeof(gProfiles[0]));
        wcscpy_s(gProfiles[0].ProgramExeName, MAX_NAME, L"Viber.exe");
        wcscpy_s(gProfiles[0].ProgramPath, MAX_PATH, L"C:\\x\\Viber.exe");
        gProfiles[0].HotKey = 'V';
        gProfiles[0].HotKeyModifiers = MOD_CONTROL | MOD_ALT;
        gProfiles[0].HideEnabled = TRUE;
        gProfiles[0].PauseEnabled = TRUE;
        SaveConfig();

        gNumProfiles = 0;
        LoadConfig();
        bool found = false;
        for (int i = 0; i < gNumProfiles; i++)
            if (_wcsicmp(gProfiles[i].ProgramExeName, L"Viber.exe") == 0 &&
                gProfiles[i].HideEnabled && gProfiles[i].PauseEnabled)
                found = true;
        CHECK(found, L"SaveConfig -> LoadConfig round-trips an entry");

        // Restore the user's real config.
        if (hadConfig) { CopyFileW(bak, cfg, FALSE); DeleteFileW(bak); }
        else { DeleteFileW(cfg); }   // there was none; remove the test artifact
    }
```

- [ ] **Step 8: Run self-test to verify failure, then build + pass**

Run the Self-test command (expect a failure first if you run before implementing). After Steps 3-7, run Build then Self-test. Expected: `Build succeeded`, `exit=0`, all hotkey + round-trip checks `PASS`.

- [ ] **Step 9: Commit**

```powershell
git add config.c keys.c keys.h Main.c Main.h selftest.c
git commit -m "feat: hotkey parse/format and SaveConfig with round-trip tests"
```

---

## Task 5: Job queue (FIFO, thread-safe)

**Files:**
- Modify: `worker.c`
- Test: `selftest.c`

- [ ] **Step 1: Add failing checks** in `selftest.c` (add `#include "worker.h"`):

```c
    {
        WorkerInit();
        Job a = { JOB_TOGGLE_HOTKEY, 1 };
        Job b = { JOB_TOGGLE_HOTKEY, 2 };
        CHECK(JobQueuePush(a), L"queue push a");
        CHECK(JobQueuePush(b), L"queue push b");
        Job out;
        CHECK(JobQueuePop(&out) && out.hotkeyIndex == 1, L"queue pops FIFO #1");
        CHECK(JobQueuePop(&out) && out.hotkeyIndex == 2, L"queue pops FIFO #2");
    }
```

(Note: `JobQueuePop` must be non-blocking when items are present; the blocking wait only applies when empty, which this test never hits.)

- [ ] **Step 2: Run self-test to verify it fails**

Run the Self-test command. Expected: link error or failing checks.

- [ ] **Step 3: Implement the queue in `worker.c`**

```c
#include "worker.h"

#define JOB_QUEUE_CAP 256
static Job gQueue[JOB_QUEUE_CAP];
static int gHead = 0, gTail = 0;          // gTail == gHead => empty
static CRITICAL_SECTION gQueueLock;
static HANDLE gQueueEvent = NULL;          // auto-reset, signaled when an item is enqueued
static bool gInited = false;

void WorkerInit(void) {
    if (gInited) return;
    InitializeCriticalSection(&gQueueLock);
    gQueueEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    gHead = gTail = 0;
    gInited = true;
}

bool JobQueuePush(Job job) {
    EnterCriticalSection(&gQueueLock);
    int next = (gTail + 1) % JOB_QUEUE_CAP;
    if (next == gHead) { LeaveCriticalSection(&gQueueLock); return false; } // full
    gQueue[gTail] = job;
    gTail = next;
    LeaveCriticalSection(&gQueueLock);
    SetEvent(gQueueEvent);
    return true;
}

bool JobQueuePop(Job* out) {
    for (;;) {
        EnterCriticalSection(&gQueueLock);
        if (gHead != gTail) {
            *out = gQueue[gHead];
            gHead = (gHead + 1) % JOB_QUEUE_CAP;
            LeaveCriticalSection(&gQueueLock);
            return true;
        }
        LeaveCriticalSection(&gQueueLock);
        WaitForSingleObject(gQueueEvent, INFINITE);   // block until pushed
    }
}
```

- [ ] **Step 4: Build and run self-test**

Run Build, then Self-test. Expected: `Build succeeded`, `exit=0`, all four queue checks `PASS`.

- [ ] **Step 5: Commit**

```powershell
git add worker.c selftest.c
git commit -m "feat: thread-safe FIFO job queue"
```

---

## Task 6: Worker thread + hook only enqueues (fixes Bug 2)

Moves all process/window manipulation onto the worker thread. The hook does a locked lookup and pushes a job, then returns immediately.

**Files:**
- Modify: `worker.c`, `Main.c` (`LowLevelKeyboardProc` at Main.c:676-725, `wWinMain` setup at Main.c:802-884, add `gHotkeyLock`), `Main.h` (declare shared bits)

- [ ] **Step 1: Declare the shared lock and worker entry.** In `Main.h` add:

```c
extern CRITICAL_SECTION gHotkeyLock;   // guards gHotkeys/gNumHotkeys + Triggered
void DispatchHotkey(int hotkeyIndex);  // worker-side: runs the heavy toggle work
```

Also **remove the old `void HandleKeyboardHotkey(int hkID);` declaration** at Main.h:75 (it is being renamed to `DispatchHotkey`).

In `Main.c` define near the other globals (after Main.c:36):

```c
CRITICAL_SECTION gHotkeyLock;
```

- [ ] **Step 2: Extract the heavy work into `DispatchHotkey`.** In `Main.c`, rename the body of `HandleKeyboardHotkey` into `DispatchHotkey` (this is the function the worker calls). It keeps doing `UpdateProcessIDs(); UpdateWindowProcessIDs();` then toggles and calls `HandleProfile`. Guard the `Triggered` toggle with the lock:

```c
void DispatchHotkey(int hkID) {
    UpdateProcessIDs();
    UpdateWindowProcessIDs();

    EnterCriticalSection(&gHotkeyLock);
    gHotkeys[hkID].Triggered = !gHotkeys[hkID].Triggered;
    bool triggered = gHotkeys[hkID].Triggered;
    int ids[MAX_PROFILES]; int n = gHotkeys[hkID].NumProfileIDs;
    for (int i = 0; i < n; i++) ids[i] = gHotkeys[hkID].ProfileIDs[i];
    LeaveCriticalSection(&gHotkeyLock);

    for (int i = 0; i < n; i++)
        HandleProfile(ids[i], triggered);
}
```

The only other caller of the old name is the dead `WM_HOTKEY` branch in the message loop (Main.c:875-878). **Delete those lines** (`if (WndMsg.message == WM_HOTKEY) { HandleKeyboardHotkey((int)WndMsg.wParam); }`) - `RegisterHotKey` was never used (it is commented out), so this branch never fires. The loop body becomes just `DispatchMessageW(&WndMsg);` until Task 13 restructures it.

- [ ] **Step 3: Implement `WorkerThreadProc` in `worker.c`.** Add `#include "Main.h"` and `#include "config.h"`.

```c
DWORD WINAPI WorkerThreadProc(LPVOID param) {
    (void)param;
    Job job;
    while (JobQueuePop(&job)) {
        switch (job.type) {
            case JOB_TOGGLE_HOTKEY:  DispatchHotkey(job.hotkeyIndex); break;
            case JOB_RELOAD_CONFIG:  /* implemented in Task 8 */        break;
            case JOB_SHUTDOWN:       return 0;
        }
    }
    return 0;
}
```

- [ ] **Step 4: Make the hook enqueue and return fast.** In `Main.c`, rewrite the matching block of `LowLevelKeyboardProc` (Main.c:705-718) so it does a locked lookup and pushes a job - no `UpdateProcessIDs`, no suspend, no sleep:

```c
				EnterCriticalSection(&gHotkeyLock);
				int matchIndex = -1;
				for (int i = 0; i < gNumHotkeys; i++) {
					if (gHotkeys[i].HotKey == vkCode) {
						BOOL shift = GetKeyState(VK_SHIFT) & 0x8000;
						BOOL ctrl  = GetKeyState(VK_CONTROL) & 0x8000;
						BOOL alt   = GetKeyState(VK_MENU) & 0x8000;
						u32 modifiers = (shift ? MOD_SHIFT : 0) | (ctrl ? MOD_CONTROL : 0) | (alt ? MOD_ALT : 0);
						if (gHotkeys[i].Modifiers == modifiers) { matchIndex = i; break; }
					}
				}
				LeaveCriticalSection(&gHotkeyLock);

				if (matchIndex >= 0) {
					if (down && !wasKeyDown) {
						Job j = { JOB_TOGGLE_HOTKEY, matchIndex };
						JobQueuePush(j);
					}
					return 1;   // swallow the key; return immediately
				}
```

- [ ] **Step 5: Start the worker and init locks in `wWinMain`.** In `Main.c`, after `ReadConfig()` succeeds and before installing the hook (around Main.c:801), add:

```c
	InitializeCriticalSection(&gHotkeyLock);
	WorkerInit();
	HANDLE workerThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);
	if (workerThread == NULL) {
		MsgBox(L"Failed to start worker thread!", APPNAME L" Error", MB_OK | MB_ICONERROR);
		goto Exit;
	}
```

- [ ] **Step 6: Drain on shutdown.** Replace the manual restore loop at exit (Main.c:886-895) by pushing a shutdown job and letting the worker restore. Simplest: before unhooking, push `JOB_SHUTDOWN` after first restoring on the worker. Keep it minimal - in the main thread after the message loop, push a shutdown job and `WaitForSingleObject(workerThread, 2000)`. Move the "restore all triggered profiles" loop into `DispatchHotkey`-style handling is unnecessary; instead keep the existing restore loop but run it on the main thread *after* the worker has exited (no more jobs will mutate state):

```c
	{ Job j = { JOB_SHUTDOWN, 0 }; JobQueuePush(j); }
	if (workerThread) WaitForSingleObject(workerThread, 2000);
	// now safe: worker is gone, restore triggered profiles on this thread
	for (int i = 0; i < gNumHotkeys; i++) {
		if (gHotkeys[i].Triggered) {
			gHotkeys[i].Triggered = false;
			for (int j = 0; j < gHotkeys[i].NumProfileIDs; j++)
				HandleProfile(gHotkeys[i].ProfileIDs[j], false);
		}
	}
```

- [ ] **Step 7: Fix the `SuspendProcess`/`ResumeProcess` return bugs** you are now relying on (Main.c:310-333, 344-361): make every path return a `bool` (`return false;` / `return true;`). In `IsProcessSuspended`-guarded early exit in `SuspendProcess` (Main.c:312-314) change bare `return;` to `return true;`.

- [ ] **Step 8: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 9: Run self-test**

Run Self-test. Expected: `exit=0`.

- [ ] **Step 10: Manual verification (the actual bug fix).** With a valid config (one entry, e.g. Notepad, with a Pause+Hide hotkey), run `x64\Debug\UniversalAppControl.exe`. Verify:
  - The hotkey toggles hide/pause as before.
  - Press the hotkey rapidly ~20 times; the app keeps responding and never needs a restart.
  - The hook stays installed: leave the app running, press the hotkey after a minute of idle - still works.

- [ ] **Step 11: Commit**

```powershell
git add Main.c Main.h worker.c
git commit -m "fix: move process/window work to a worker thread so the keyboard hook never stalls"
```

---

## Task 7: Re-add tray icon on TaskbarCreated (fixes Bug 1)

**Files:**
- Modify: `Main.c` (`SysTrayCallback` at Main.c:727-759, add a registered-message global)

- [ ] **Step 1: Register the message.** In `Main.c` add a global near the other globals (after Main.c:57):

```c
UINT gTaskbarCreatedMsg = 0;
```

In `wWinMain`, right after the tray window class is registered and before `Shell_NotifyIconW(NIM_ADD ...)` (around Main.c:828), add:

```c
		gTaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
```

- [ ] **Step 2: Handle it in `SysTrayCallback`.** Add this near the top of `SysTrayCallback`, before the `switch` (Main.c:732), because the message id is dynamic and cannot be a `case` label:

```c
	if (Message == gTaskbarCreatedMsg && gTaskbarCreatedMsg != 0) {
		Shell_NotifyIconW(NIM_ADD, &gTrayNotifyIconData);
		return 0;
	}
```

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** Run the app with `TrayIcon=true` in config. Confirm the tray icon shows. Then restart Explorer: Task Manager -> Windows Explorer -> Restart (or `Stop-Process -Name explorer` and let it relaunch). After the taskbar comes back, confirm the tray icon reappears automatically (before this fix it would be gone).

- [ ] **Step 5: Commit**

```powershell
git add Main.c
git commit -m "fix: re-add tray icon on TaskbarCreated (explorer restart)"
```

---

## Task 8: Live config reload on the worker

**Files:**
- Modify: `worker.c` (`JOB_RELOAD_CONFIG` case), `Main.c` (`ReadConfig` guard for double console alloc)

- [ ] **Step 1: Guard against double console allocation** in `ReadConfig`/`EnableDebugConsole` (reload re-runs `ReadConfig`). In `EnableDebugConsole` (Main.c:180-196), at the top add:

```c
	if (gDbgConsole != INVALID_HANDLE_VALUE) return;  // already allocated
```

- [ ] **Step 2: Implement the reload case** in `worker.c` `WorkerThreadProc`. The worker owns profile state, so reloading here is race-free against toggles. Wrap the hotkey-table rebuild in the lock the hook uses:

```c
            case JOB_RELOAD_CONFIG: {
                EnterCriticalSection(&gHotkeyLock);
                LoadConfig();   // wipes + rebuilds gProfiles and gHotkeys
                LeaveCriticalSection(&gHotkeyLock);
                break;
            }
```

Add `#include "Main.h"` (already added in Task 6) - it declares `LoadConfig` (added in Task 4).

- [ ] **Step 3: Build and run self-test**

Run Build, then Self-test. Expected: `Build succeeded`, `exit=0`.

- [ ] **Step 4: Manual verification.** Run the app. While running, edit `%APPDATA%\UniversalAppControl\config.ini` by hand to change a hotkey, then trigger a reload by pushing the job - there is no UI yet, so temporarily verify via the self-test or wait for Task 15 where the UI posts the reload. For now, confirm the case compiles and the app still toggles correctly after a manual `JOB_RELOAD_CONFIG` is wired in Task 15. (No standalone manual step here.)

- [ ] **Step 5: Commit**

```powershell
git add worker.c Main.c
git commit -m "feat: live config reload job on the worker thread"
```

---

## Task 9: Startup registry helpers + arg helper (tested)

**Files:**
- Modify: `install.c`, `Main.c` (add a reusable `GetExePath` helper)
- Test: `selftest.c`

- [ ] **Step 1: Add a `GetExePath` helper** in `Main.c` (used by startup + install) and declare in `Main.h`:

`Main.c`:
```c
const wchar_t* GetExePath(void) {
    static wchar_t path[MAX_PATH];
    if (path[0] == 0) GetModuleFileNameW(NULL, path, MAX_PATH);
    return path;
}
```
`Main.h`:
```c
const wchar_t* GetExePath(void);
```

- [ ] **Step 2: Add failing checks** in `selftest.c` (`#include "install.h"`):

```c
    {
        // Round-trip the startup registration against the real HKCU Run key.
        bool was = IsStartupEnabled();
        CHECK(SetStartupEnabled(true), L"SetStartupEnabled(true) ok");
        CHECK(IsStartupEnabled(), L"startup reads back enabled");
        CHECK(SetStartupEnabled(false), L"SetStartupEnabled(false) ok");
        CHECK(!IsStartupEnabled(), L"startup reads back disabled");
        if (was) SetStartupEnabled(true);  // restore prior state
    }
```

- [ ] **Step 3: Run self-test to verify it fails**

Run Self-test. Expected: link error or failing checks.

- [ ] **Step 4: Implement startup helpers in `install.c`**

```c
#include "install.h"
#include "Main.h"
#include <shlobj.h>
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

#define RUN_KEY  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_NAME L"UniversalAppControl"

bool IsStartupEnabled(void) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    wchar_t buf[1024]; DWORD cb = sizeof(buf); DWORD type = 0;
    LSTATUS s = RegQueryValueExW(hk, RUN_NAME, NULL, &type, (LPBYTE)buf, &cb);
    RegCloseKey(hk);
    return s == ERROR_SUCCESS && type == REG_SZ && buf[0] != 0;
}

bool SetStartupEnabled(bool enabled) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
        return false;
    bool ok;
    if (enabled) {
        wchar_t cmd[MAX_PATH + 32];
        swprintf_s(cmd, _countof(cmd), L"\"%s\" --autostart", GetExePath());
        ok = RegSetValueExW(hk, RUN_NAME, 0, REG_SZ, (const BYTE*)cmd,
                            (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LSTATUS s = RegDeleteValueW(hk, RUN_NAME);
        ok = (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hk);
    return ok;
}
```

- [ ] **Step 5: Build, verify failure first then pass.** Run Build, then Self-test. Expected: `Build succeeded`, `exit=0`, all four startup checks `PASS`. (The test restores your prior startup state.)

- [ ] **Step 6: Commit**

```powershell
git add install.c Main.c Main.h selftest.c
git commit -m "feat: HKCU Run-key startup helpers with round-trip test"
```

---

## Task 10: Launch-mode dispatch (autostart vs manual)

**Files:**
- Modify: `Main.c` (`wWinMain`)

- [ ] **Step 1: Detect `--autostart`.** In `wWinMain`, after the `--selftest` loop (from Task 1), add:

```c
    bool autostart = false;
    for (int ai = 1; ai < __argc; ai++)
        if (lstrcmpiW(__wargv[ai], L"--autostart") == 0) autostart = true;
```

- [ ] **Step 2: Open settings on manual launch.** After the tray-icon setup block and before the message loop (around Main.c:870), add:

```c
    if (!autostart)
        ShowSettingsWindow(Instance, gConfig.TrayIcon ? gTrayNotifyIconData.hWnd : NULL);
```

(`ShowSettingsWindow` is still a stub until Task 13 - it will compile and do nothing.)

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** Run with no args - app starts (settings stub does nothing yet, no crash). Run with `--autostart` - app starts the same way (tray only once the stub is real). No errors either way.

- [ ] **Step 5: Commit**

```powershell
git add Main.c
git commit -m "feat: open settings on manual launch, tray-only on --autostart"
```

---

## Task 11: Tray context menu

**Files:**
- Modify: `Main.c` (`SysTrayCallback`: replace the left-click quit prompt with a right-click menu + `WM_COMMAND` handling)

- [ ] **Step 1: Build the popup menu on right-click.** First delete the now-unused `static BOOL QuitMessageBoxIsShowing = FALSE;` line at the top of `SysTrayCallback` (Main.c:730) - the new menu replaces the old quit-prompt. Then, in `SysTrayCallback`'s `WM_TRAYICON` case (Main.c:734-751), replace the body with:

```c
		case WM_TRAYICON:
		{
			if (LParam == WM_LBUTTONUP || LParam == WM_LBUTTONDBLCLK) {
				ShowSettingsWindow(GetModuleHandleW(NULL), Window);
				break;
			}
			if (LParam == WM_RBUTTONUP) {
				HMENU menu = CreatePopupMenu();
				AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings...");
				AppendMenuW(menu, MF_STRING | (IsStartupEnabled() ? MF_CHECKED : 0),
				            IDM_STARTUP, L"Run at Windows startup");
				AppendMenuW(menu, MF_STRING, IDM_INSTALL, L"Install to user programs...");
				AppendMenuW(menu, MF_STRING, IDM_OPENINI, L"Open INI folder");
				AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
				AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quit");

				POINT pt; GetCursorPos(&pt);
				SetForegroundWindow(Window);   // so the menu dismisses correctly
				TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, Window, NULL);
				DestroyMenu(menu);
			}
			break;
		}
```

- [ ] **Step 2: Handle `WM_COMMAND`.** Add a new case in the same `switch`:

```c
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
```

(`InstallToUserPrograms` and `OpenConfigFolder` are stubs until Tasks 12/18-20 - they compile.)

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** Run with `TrayIcon=true`. Right-click the tray icon: the menu appears with all five items and the startup item shows a checkmark matching the registry. Click "Run at Windows startup" and confirm it toggles the checkmark on the next right-click and the `HKCU\...\Run\UniversalAppControl` value appears/disappears (check with `reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v UniversalAppControl`). "Quit" exits cleanly and removes the icon.

- [ ] **Step 5: Commit**

```powershell
git add Main.c
git commit -m "feat: tray context menu (settings, startup, install, open ini, quit)"
```

---

## Task 12: Open INI folder

**Files:**
- Modify: `install.c`

- [ ] **Step 1: Implement `OpenConfigFolder`** in `install.c`. Opens Explorer with the ini file selected.

```c
#include "config.h"   // GetConfigPath

void OpenConfigFolder(void) {
    wchar_t arg[MAX_PATH + 16];
    swprintf_s(arg, _countof(arg), L"/select,\"%s\"", GetConfigPath());
    ShellExecuteW(NULL, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
}
```

- [ ] **Step 2: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 3: Manual verification.** Right-click tray -> "Open INI folder". Explorer opens at `%APPDATA%\UniversalAppControl\` with `config.ini` selected.

- [ ] **Step 4: Commit**

```powershell
git add install.c
git commit -m "feat: open INI folder from tray menu"
```

---

## Task 13: Settings window shell (master-detail, programmatic controls)

Creates the settings window with all controls built in `WM_CREATE`. No data binding yet.

**Files:**
- Modify: `settings_ui.c`, `Main.c` (pump `IsDialogMessage` for the modeless window in the main loop)

- [ ] **Step 1: Implement the window class + control creation in `settings_ui.c`.**

```c
#include "settings_ui.h"
#include "ui_ids.h"
#include "Main.h"
#include "config.h"
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

HWND gSettingsWnd = NULL;

static LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);

static HWND MakeChild(HWND parent, const wchar_t* cls, const wchar_t* text,
                      DWORD style, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}

static void CreateControls(HWND wnd) {
    // Left: list of entries
    HWND list = MakeChild(wnd, WC_LISTVIEWW, L"",
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER,
        10, 10, 300, 360, IDC_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    LVCOLUMNW c = { 0 }; c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = L"Program"; c.cx = 150; ListView_InsertColumn(list, 0, &c);
    c.pszText = L"Hotkey";  c.cx = 90;  ListView_InsertColumn(list, 1, &c);
    c.pszText = L"Status";  c.cx = 55;  ListView_InsertColumn(list, 2, &c);

    // Right: detail fields
    int rx = 325, lblW = 60, fx = rx + lblW, fw = 240;
    MakeChild(wnd, L"STATIC", L"Name:",   SS_RIGHT, rx, 14, lblW, 20, 0);
    MakeChild(wnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, fx, 12, fw, 22, IDC_NAME);
    MakeChild(wnd, L"STATIC", L"Path:",   SS_RIGHT, rx, 44, lblW, 20, 0);
    MakeChild(wnd, L"STATIC", L"",        SS_LEFTNOWORDWRAP, fx, 44, fw, 20, IDC_PATH);
    MakeChild(wnd, L"STATIC", L"Hotkey:", SS_RIGHT, rx, 74, lblW, 20, 0);
    MakeChild(wnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, fx, 72, fw, 22, IDC_HOTKEY);

    MakeChild(wnd, L"BUTTON", L"Hide / show",     BS_AUTOCHECKBOX, fx, 104, fw, 22, IDC_HIDE);
    MakeChild(wnd, L"BUTTON", L"Minimize / restore", BS_AUTOCHECKBOX, fx, 128, fw, 22, IDC_MIN);
    MakeChild(wnd, L"BUTTON", L"Pause / resume",  BS_AUTOCHECKBOX, fx, 152, fw, 22, IDC_PAUSE);

    MakeChild(wnd, L"STATIC", L"", SS_LEFTNOWORDWRAP, fx, 182, fw, 40, IDC_STATUS);

    // Bottom row
    MakeChild(wnd, L"BUTTON", L"+ Add from running...", BS_PUSHBUTTON, 10, 380, 150, 26, IDC_ADD);
    MakeChild(wnd, L"BUTTON", L"Remove", BS_PUSHBUTTON, 168, 380, 80, 26, IDC_REMOVE);
    MakeChild(wnd, L"BUTTON", L"Run at startup", BS_AUTOCHECKBOX, 325, 384, 140, 22, IDC_STARTUP);
    MakeChild(wnd, L"BUTTON", L"Open INI folder", BS_PUSHBUTTON, 470, 380, 110, 26, IDC_OPENINI);
    MakeChild(wnd, L"BUTTON", L"Install to user programs...", BS_PUSHBUTTON, 325, 412, 255, 26, IDC_INSTALL);
}

void ShowSettingsWindow(HINSTANCE inst, HWND owner) {
    if (gSettingsWnd) { SetForegroundWindow(gSettingsWnd); return; }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"UAC_SettingsWnd";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }

    gSettingsWnd = CreateWindowExW(0, L"UAC_SettingsWnd", APPNAME L" - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 615, 470, owner, NULL, inst, NULL);
    ShowWindow(gSettingsWnd, SW_SHOW);
}

static LRESULT CALLBACK SettingsProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: CreateControls(wnd); return 0;
        case WM_CLOSE:  DestroyWindow(wnd); return 0;
        case WM_DESTROY: gSettingsWnd = NULL; return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}
```

Add `extern HWND gSettingsWnd;` to `settings_ui.h`.

- [ ] **Step 2: Pump the modeless window** in the main loop. In `Main.c`'s message loop (Main.c:873-881), change the `PeekMessage` dispatch so dialog navigation works:

The inner `while (PeekMessageW(...))` body (after Task 6 it is just `DispatchMessageW(&WndMsg);`) becomes:

```c
			if (gSettingsWnd && IsDialogMessageW(gSettingsWnd, &WndMsg))
				continue;
			TranslateMessage(&WndMsg);
			DispatchMessageW(&WndMsg);
```

`gSettingsWnd` is visible because `Main.c` includes `settings_ui.h` (Task 1) and Task 13 Step 1 adds `extern HWND gSettingsWnd;` to that header.

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** Launch with no args (or tray -> Settings). The settings window opens showing an empty list with three columns and the right-hand fields/buttons. Tab moves between controls. Closing the window does not exit the app (tray stays).

- [ ] **Step 5: Commit**

```powershell
git add settings_ui.c settings_ui.h Main.c
git commit -m "feat: settings window shell with programmatic controls"
```

---

## Task 14: Populate the list and bind the detail fields

**Files:**
- Modify: `settings_ui.c`

- [ ] **Step 1: Add list population + selection loading.** Add these helpers and `WM_NOTIFY` handling to `settings_ui.c`. Add `#include "install.h"` and a forward decl for status formatting (filled in Task 16; for now status shows the exe name running state minimally).

```c
static int gSelected = -1;   // index into gProfiles, -1 = none

static void RefreshList(HWND wnd) {
    HWND list = GetDlgItem(wnd, IDC_LIST);
    ListView_DeleteAllItems(list);
    for (int i = 0; i < gNumProfiles; i++) {
        LVITEMW it = { 0 };
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i; it.lParam = i;
        it.pszText = gProfiles[i].ProgramExeName;
        ListView_InsertItem(list, &it);
        wchar_t hk[64];
        FormatHotkey(gProfiles[i].HotKey, gProfiles[i].HotKeyModifiers, hk, _countof(hk));
        ListView_SetItemText(list, i, 1, hk);
        ListView_SetItemText(list, i, 2, L"");   // status filled in Task 16
    }
    // reflect the startup checkbox
    CheckDlgButton(wnd, IDC_STARTUP, IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
}

static void LoadSelectionToFields(HWND wnd, int idx) {
    gSelected = idx;
    if (idx < 0 || idx >= gNumProfiles) {
        SetDlgItemTextW(wnd, IDC_NAME, L"");
        SetDlgItemTextW(wnd, IDC_PATH, L"");
        SetDlgItemTextW(wnd, IDC_HOTKEY, L"");
        CheckDlgButton(wnd, IDC_HIDE, BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_MIN, BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_PAUSE, BST_UNCHECKED);
        return;
    }
    PROFILE_CONFIG* p = &gProfiles[idx];
    SetDlgItemTextW(wnd, IDC_NAME, p->ProgramExeName);
    SetDlgItemTextW(wnd, IDC_PATH, p->ProgramPath);
    wchar_t hk[64]; FormatHotkey(p->HotKey, p->HotKeyModifiers, hk, _countof(hk));
    SetDlgItemTextW(wnd, IDC_HOTKEY, hk);
    CheckDlgButton(wnd, IDC_HIDE,  p->HideEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_MIN,   p->MinimizeEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_PAUSE, p->PauseEnabled ? BST_CHECKED : BST_UNCHECKED);
}
```

- [ ] **Step 2: Wire it into `SettingsProc`.** Call `RefreshList(wnd)` at the end of the `WM_CREATE` handler. Add:

```c
        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lp;
            if (nh->idFrom == IDC_LIST && nh->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW nv = (LPNMLISTVIEW)lp;
                if (nv->uChanged & LVIF_STATE && (nv->uNewState & LVIS_SELECTED))
                    LoadSelectionToFields(wnd, (int)nv->lParam);
            }
            return 0;
        }
```

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** With a config containing entries, open Settings. The list shows each entry's name and hotkey. Click a row - the Name/Path/Hotkey fields and the three checkboxes populate from that entry.

- [ ] **Step 5: Commit**

```powershell
git add settings_ui.c
git commit -m "feat: populate settings list and bind detail fields"
```

---

## Task 15: Save edits + live reload + remove

**Files:**
- Modify: `settings_ui.c`

- [ ] **Step 1: Add a save-from-fields function** that validates the hotkey, writes the model, persists, and posts a reload. Add `#include "worker.h"`.

```c
static void ApplyFieldsToSelection(HWND wnd) {
    if (gSelected < 0 || gSelected >= gNumProfiles) return;
    PROFILE_CONFIG* p = &gProfiles[gSelected];

    GetDlgItemTextW(wnd, IDC_NAME, p->ProgramExeName, MAX_NAME);

    wchar_t hk[64]; GetDlgItemTextW(wnd, IDC_HOTKEY, hk, _countof(hk));
    u32 vk = 0; UINT mods = 0;
    if (hk[0] && ParseHotkey(hk, &vk, &mods)) {
        p->HotKey = vk; p->HotKeyModifiers = mods;
    } else if (hk[0]) {
        MessageBoxW(wnd, L"Invalid hotkey. Example: Ctrl+Alt+V", APPNAME, MB_OK | MB_ICONWARNING);
        FormatHotkey(p->HotKey, p->HotKeyModifiers, hk, _countof(hk));
        SetDlgItemTextW(wnd, IDC_HOTKEY, hk);
    }

    p->HideEnabled     = IsDlgButtonChecked(wnd, IDC_HIDE)  == BST_CHECKED;
    p->MinimizeEnabled = IsDlgButtonChecked(wnd, IDC_MIN)   == BST_CHECKED;
    p->PauseEnabled    = IsDlgButtonChecked(wnd, IDC_PAUSE) == BST_CHECKED;

    SaveConfig();
    Job j = { JOB_RELOAD_CONFIG, 0 };
    JobQueuePush(j);
    RefreshList(wnd);
}
```

- [ ] **Step 2: Handle the control commands** in `SettingsProc` `WM_COMMAND`:

```c
        case WM_COMMAND: {
            int id = LOWORD(wp), code = HIWORD(wp);
            switch (id) {
                case IDC_NAME:
                case IDC_HOTKEY:
                    if (code == EN_KILLFOCUS) ApplyFieldsToSelection(wnd);
                    break;
                case IDC_HIDE: case IDC_MIN: case IDC_PAUSE:
                    if (code == BN_CLICKED) ApplyFieldsToSelection(wnd);
                    break;
                case IDC_REMOVE:
                    if (gSelected >= 0 && gSelected < gNumProfiles) {
                        memmove(&gProfiles[gSelected], &gProfiles[gSelected + 1],
                                (gNumProfiles - gSelected - 1) * sizeof(PROFILE_CONFIG));
                        gNumProfiles--;
                        gSelected = -1;
                        SaveConfig();
                        Job j = { JOB_RELOAD_CONFIG, 0 }; JobQueuePush(j);
                        RefreshList(wnd);
                        LoadSelectionToFields(wnd, -1);
                    }
                    break;
                case IDC_STARTUP:
                    if (code == BN_CLICKED)
                        SetStartupEnabled(IsDlgButtonChecked(wnd, IDC_STARTUP) == BST_CHECKED);
                    break;
                case IDC_OPENINI: OpenConfigFolder(); break;
                case IDC_INSTALL: InstallToUserPrograms(wnd); break;
                case IDC_ADD:     /* Task 17 */ break;
            }
            return 0;
        }
```

Because `RefreshList` rebuilds via `LoadConfig` indirectly only on the worker, but the UI also reads `gProfiles` directly: note the worker's `JOB_RELOAD_CONFIG` calls `LoadConfig`, which wipes and rebuilds `gProfiles` from the file we just wrote. There is a brief window where the UI thread and worker both touch `gProfiles`. To keep this simple and safe, **guard `gProfiles` reads/writes in the UI with `gHotkeyLock`** in `ApplyFieldsToSelection`, the `IDC_REMOVE` handler, `RefreshList`, and `LoadSelectionToFields` (Enter/Leave around the blocks that touch `gProfiles`). Add `#include "Main.h"` for `gHotkeyLock` (already included).

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** Open Settings, select an entry, toggle a checkbox - the change persists to the ini (open it) and takes effect live (test the hotkey without restarting). Edit the hotkey field to a valid combo, tab out - it saves; enter garbage - you get the warning and the field reverts. Remove an entry - it disappears from the list and the ini.

- [ ] **Step 5: Commit**

```powershell
git add settings_ui.c
git commit -m "feat: live-save settings edits and remove entries"
```

---

## Task 16: Status indicators (running / file present)

**Files:**
- Modify: `settings_ui.c`, `install.c` (add `IsExeRunning`), `install.h`

- [ ] **Step 1: Add `IsExeRunning`** in `install.c` (declare in `install.h`):

```c
#include <tlhelp32.h>

bool IsExeRunning(const wchar_t* exeName) {
    if (!exeName || !exeName[0]) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do { if (_wcsicmp(pe.szExeFile, exeName) == 0) { found = true; break; } }
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}
```
`install.h`: `bool IsExeRunning(const wchar_t* exeName);`

- [ ] **Step 2: Show status in the list and detail panel.** In `settings_ui.c`, in `RefreshList` replace the empty status text:

```c
        ListView_SetItemText(list, i, 2,
            IsExeRunning(gProfiles[i].ProgramExeName) ? L"run" : L"off");
```

In `LoadSelectionToFields`, after setting the path, set the status static (`Shlwapi`'s `PathFileExistsW`; add `#include <shlwapi.h>` and `#pragma comment(lib,"Shlwapi.lib")` in `settings_ui.c`):

```c
    wchar_t status[128];
    bool running = IsExeRunning(p->ProgramExeName);
    bool present = p->ProgramPath[0] ? (PathFileExistsW(p->ProgramPath) != FALSE) : true;
    swprintf_s(status, _countof(status), L"%s   %s",
        running ? L"\x25CF running" : L"\x25CB not running",
        p->ProgramPath[0] ? (present ? L"| file present" : L"| FILE MISSING") : L"");
    SetDlgItemTextW(wnd, IDC_STATUS, status);
```

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** Open Settings. An entry whose program is running shows "run" in the list and "running" in the detail; one that is closed shows "off"/"not running". For an entry with a captured path that you then rename/move on disk, the detail shows "FILE MISSING".

- [ ] **Step 5: Commit**

```powershell
git add settings_ui.c install.c install.h
git commit -m "feat: running and file-present status indicators"
```

---

## Task 17: Process picker (icon, name, path)

**Files:**
- Modify: `settings_ui.c` (implement `PickRunningProcess`, wire `IDC_ADD`)

- [ ] **Step 1: Implement `PickRunningProcess`** as a modal window with an owner-disabled loop. Add includes: `#include <tlhelp32.h>`, `#include <shellapi.h>` (`#pragma comment(lib,"Shell32.lib")`).

```c
typedef struct { wchar_t name[MAX_NAME]; wchar_t path[MAX_PATH]; } PickRow;
static PickRow gPickRows[2048];
static int gPickCount = 0;
static int gPickResult = -1;
static bool gPickDone = false;

static void FillProcessList(HWND list, HIMAGELIST il) {
    gPickCount = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            if (gPickCount >= 2048) break;
            PickRow* r = &gPickRows[gPickCount];
            wcscpy_s(r->name, MAX_NAME, pe.szExeFile);
            r->path[0] = 0;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (h) {
                DWORD cb = MAX_PATH;
                QueryFullProcessImageNameW(h, 0, r->path, &cb);
                CloseHandle(h);
            }
            int iconIdx = -1;
            if (r->path[0]) {
                SHFILEINFOW sfi = { 0 };
                if (SHGetFileInfoW(r->path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
                    iconIdx = ImageList_AddIcon(il, sfi.hIcon);
                    DestroyIcon(sfi.hIcon);
                }
            }
            LVITEMW it = { 0 };
            it.mask = LVIF_TEXT | LVIF_PARAM | (iconIdx >= 0 ? LVIF_IMAGE : 0);
            it.iItem = gPickCount; it.lParam = gPickCount; it.iImage = iconIdx;
            it.pszText = r->name;
            int row = ListView_InsertItem(list, &it);
            ListView_SetItemText(list, row, 1, r->path);
            gPickCount++;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

static LRESULT CALLBACK PickProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HWND list = MakeChild(wnd, WC_LISTVIEWW, L"",
                LVS_REPORT | LVS_SINGLESEL | WS_BORDER, 10, 10, 460, 300, IDC_PICKLIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
            LVCOLUMNW c = { 0 }; c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = L"Process"; c.cx = 160; ListView_InsertColumn(list, 0, &c);
            c.pszText = L"Path";    c.cx = 290; ListView_InsertColumn(list, 1, &c);
            HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
            ListView_SetImageList(list, il, LVSIL_SMALL);
            FillProcessList(list, il);
            MakeChild(wnd, L"BUTTON", L"Add", BS_DEFPUSHBUTTON, 300, 320, 80, 26, IDC_PICKOK);
            MakeChild(wnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON, 390, 320, 80, 26, IDC_PICKCANCEL);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_PICKOK) {
                HWND list = GetDlgItem(wnd, IDC_PICKLIST);
                int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    LVITEMW it = { 0 }; it.mask = LVIF_PARAM; it.iItem = sel;
                    ListView_GetItem(list, &it);
                    gPickResult = (int)it.lParam;
                }
                gPickDone = true; DestroyWindow(wnd);
            } else if (LOWORD(wp) == IDC_PICKCANCEL) {
                gPickResult = -1; gPickDone = true; DestroyWindow(wnd);
            }
            return 0;
        case WM_CLOSE: gPickResult = -1; gPickDone = true; DestroyWindow(wnd); return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

bool PickRunningProcess(HWND parent, wchar_t* outName, int nameCch, wchar_t* outPath, int pathCch) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = PickProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"UAC_PickWnd";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&wc); reg = true;
    }
    gPickResult = -1; gPickDone = false;
    HWND wnd = CreateWindowExW(0, L"UAC_PickWnd", L"Pick a running process",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 390, parent, NULL, GetModuleHandleW(NULL), NULL);
    ShowWindow(wnd, SW_SHOW);
    EnableWindow(parent, FALSE);               // modal
    MSG m;
    while (!gPickDone && GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(wnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (gPickResult < 0) return false;
    wcscpy_s(outName, nameCch, gPickRows[gPickResult].name);
    wcscpy_s(outPath, pathCch, gPickRows[gPickResult].path);
    return true;
}
```

- [ ] **Step 2: Wire `IDC_ADD`** in `SettingsProc`'s `WM_COMMAND`:

```c
                case IDC_ADD: {
                    wchar_t name[MAX_NAME], path[MAX_PATH];
                    if (PickRunningProcess(wnd, name, MAX_NAME, path, MAX_PATH)
                        && gNumProfiles < MAX_PROFILES) {
                        EnterCriticalSection(&gHotkeyLock);
                        int idx = gNumProfiles++;
                        memset(&gProfiles[idx], 0, sizeof(gProfiles[idx]));
                        wcscpy_s(gProfiles[idx].ProgramExeName, MAX_NAME, name);
                        wcscpy_s(gProfiles[idx].ProgramPath, MAX_PATH, path);
                        LeaveCriticalSection(&gHotkeyLock);
                        SaveConfig();
                        Job j = { JOB_RELOAD_CONFIG, 0 }; JobQueuePush(j);
                        RefreshList(wnd);
                    }
                    break;
                }
```

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** Settings -> "+ Add from running...". A window lists running processes with icons, names, and full paths. Select one, click Add - a new entry appears in the main list pre-filled with that name and path. Assign it a hotkey and actions; confirm it works.

- [ ] **Step 5: Commit**

```powershell
git add settings_ui.c
git commit -m "feat: process picker with icon, name, and path"
```

---

## Task 18: Install - copy exe and ensure config

**Files:**
- Modify: `install.c` (begin `InstallToUserPrograms`), add `GetInstallPaths` helper

- [ ] **Step 1: Add path helpers and the copy step** in `install.c`:

```c
static void GetInstallDir(wchar_t* out, int cch) {
    wchar_t local[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local);
    swprintf_s(out, cch, L"%s\\Programs\\UniversalAppControl", local);
}
static void GetInstalledExe(wchar_t* out, int cch) {
    wchar_t dir[MAX_PATH]; GetInstallDir(dir, MAX_PATH);
    swprintf_s(out, cch, L"%s\\UniversalAppControl.exe", dir);
}
```

In `InstallToUserPrograms` (replace the stub):

```c
bool InstallToUserPrograms(HWND parent) {
    wchar_t dir[MAX_PATH]; GetInstallDir(dir, MAX_PATH);
    SHCreateDirectoryExW(NULL, dir, NULL);  // creates intermediate dirs

    wchar_t dst[MAX_PATH]; GetInstalledExe(dst, MAX_PATH);
    if (!CopyFileW(GetExePath(), dst, FALSE)) {
        MessageBoxW(parent, L"Failed to copy the executable.", L"Install", MB_OK | MB_ICONERROR);
        return false;
    }

    GetConfigPath();   // ensures %APPDATA%\UniversalAppControl\config.ini dir exists (shared)

    // Start Menu shortcut + startup fixup + relaunch added in Tasks 19-20.
    MessageBoxW(parent, L"Copied to user programs.", L"Install", MB_OK | MB_ICONINFORMATION);
    return true;
}
```

- [ ] **Step 2: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 3: Manual verification.** Settings/tray -> "Install to user programs...". Confirm `%LOCALAPPDATA%\Programs\UniversalAppControl\UniversalAppControl.exe` now exists.

- [ ] **Step 4: Commit**

```powershell
git add install.c
git commit -m "feat: install copies exe to per-user programs folder"
```

---

## Task 19: Install - Start Menu shortcut via COM

**Files:**
- Modify: `install.c` (`CreateStartMenuShortcut`, call it from `InstallToUserPrograms`)

- [ ] **Step 1: Implement the shortcut creation.** Add includes: `#include <shlobj.h>`, `#include <objbase.h>`; `#pragma comment(lib, "Ole32.lib")`.

```c
static bool CreateStartMenuShortcut(const wchar_t* target) {
    wchar_t startMenu[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenu)))
        return false;
    wchar_t lnk[MAX_PATH];
    swprintf_s(lnk, _countof(lnk), L"%s\\UniversalAppControl.lnk", startMenu);

    bool ok = false;
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IShellLinkW* sl = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void**)&sl))) {
        sl->lpVtbl->SetPath(sl, target);
        wchar_t dir[MAX_PATH]; wcscpy_s(dir, _countof(dir), target);
        wchar_t* slash = wcsrchr(dir, L'\\'); if (slash) *slash = 0;
        sl->lpVtbl->SetWorkingDirectory(sl, dir);
        sl->lpVtbl->SetDescription(sl, L"Universal App Control");
        IPersistFile* pf = NULL;
        if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void**)&pf))) {
            ok = SUCCEEDED(pf->lpVtbl->Save(pf, lnk, TRUE));
            pf->lpVtbl->Release(pf);
        }
        sl->lpVtbl->Release(sl);
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return ok;
}
```

Note: the C (not C++) COM call convention uses `obj->lpVtbl->Method(obj, ...)` and `&IID_...`/`&CLSID_...`. These IIDs/CLSIDs are provided by linking `Ole32.lib` and including `<shlobj.h>`; if the linker reports unresolved `IID_IShellLinkW`/`CLSID_ShellLink`, add `#include <initguid.h>` *before* `<shlobj.h>` in this file, or link `uuid.lib` via `#pragma comment(lib, "uuid.lib")`.

- [ ] **Step 2: Call it from `InstallToUserPrograms`** - after the successful `CopyFileW`, before the final message box:

```c
    if (!CreateStartMenuShortcut(dst))
        MessageBoxW(parent, L"Copied, but the Start Menu shortcut could not be created.",
                    L"Install", MB_OK | MB_ICONWARNING);
```

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`. If COM GUIDs are unresolved at link time, apply the `initguid.h`/`uuid.lib` note above and rebuild.

- [ ] **Step 4: Manual verification.** Run Install. Confirm a "UniversalAppControl" entry appears in the Start Menu (search it), and launching it starts the installed copy.

- [ ] **Step 5: Commit**

```powershell
git add install.c
git commit -m "feat: create Start Menu shortcut on install"
```

---

## Task 20: Install - startup fixup, relaunch, and mutex handoff

**Files:**
- Modify: `install.c` (`InstallToUserPrograms` final steps), `Main.c` (single-instance mutex retry at Main.c:779-785)

- [ ] **Step 1: Make the single-instance check tolerate handoff.** In `Main.c`, replace the mutex block (Main.c:779-785) with a short retry so a freshly relaunched installed copy can acquire the name as the old instance exits:

```c
	gMutex = NULL;
	for (int attempt = 0; attempt < 50; attempt++) {   // up to ~5s
		gMutex = CreateMutexW(NULL, FALSE, APPNAME);
		if (GetLastError() != ERROR_ALREADY_EXISTS) break;
		CloseHandle(gMutex); gMutex = NULL;
		Sleep(100);
	}
	if (gMutex == NULL) {
		MsgBox(L"An instance of the program is already running.", APPNAME L" Error", MB_OK | MB_ICONERROR);
		goto Exit;
	}
```

- [ ] **Step 2: Add startup fixup + relaunch** to the end of `InstallToUserPrograms` (replace the final info message box). Needs `GetInstalledExe` (Task 18) and the run-key helpers (Task 9):

```c
    // If startup is enabled, point it at the installed copy.
    if (IsStartupEnabled()) {
        // Re-register with the installed path: temporarily override GetExePath?
        // Simpler: write the value directly here.
        HKEY hk;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
            wchar_t cmd[MAX_PATH + 32];
            swprintf_s(cmd, _countof(cmd), L"\"%s\" --autostart", dst);
            RegSetValueExW(hk, L"UniversalAppControl", 0, REG_SZ, (const BYTE*)cmd,
                           (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
            RegCloseKey(hk);
        }
    }

    // Relaunch the installed copy, then quit this instance.
    ShellExecuteW(NULL, L"open", dst, NULL, NULL, SW_SHOWNORMAL);

    // CRITICAL: the main loop is `while (gIsRunning) {...}` and does NOT honor
    // WM_QUIT, so PostQuitMessage alone will not exit. Tear down exactly like
    // IDM_QUIT: remove the tray icon and clear gIsRunning so the loop ends and
    // the process exits, releasing the single-instance mutex for the new copy.
    extern BOOL gIsRunning;
    extern NOTIFYICONDATA gTrayNotifyIconData;
    Shell_NotifyIconW(NIM_DELETE, &gTrayNotifyIconData);
    gIsRunning = FALSE;
    PostQuitMessage(0);
    return true;
```

Add `extern BOOL gIsRunning;` and `extern NOTIFYICONDATA gTrayNotifyIconData;` to `Main.h` (they are defined in `Main.c` at lines 55 and 57) instead of the local `extern`s above if you prefer them declared once - either compiles. `install.c` already includes `<Windows.h>` for `NOTIFYICONDATA`.

Remove the old "Copied to user programs." success box (the relaunch is the visible result). Keep the earlier shortcut-warning box.

Note: if Install was triggered from the settings window (`IDC_INSTALL`), this same teardown still applies - the WM_COMMAND runs on the main thread, so clearing `gIsRunning` exits the loop after the current message drain.

- [ ] **Step 3: Build**

Run Build. Expected: `Build succeeded`.

- [ ] **Step 4: Manual verification.** From a copy of the exe run outside the install dir, click Install. Verify: the running instance exits, a new instance launches from `%LOCALAPPDATA%\Programs\UniversalAppControl\` (check Task Manager's "Open file location", or that editing settings now writes the same shared `%APPDATA%` config), no "already running" error appears during the handoff, and if startup was enabled the `Run` value now points at the installed path with `--autostart` (`reg query`).

- [ ] **Step 5: Commit**

```powershell
git add install.c Main.c
git commit -m "feat: install relaunches installed copy and hands off single-instance mutex"
```

---

## Task 21: Final pass - version bump, debug-build cleanup, full regression

**Files:**
- Modify: `Main.h` (`VERSION`), optional doc note

- [ ] **Step 1: Bump the version** in `Main.h:5` from `L"1.0.0"` to `L"1.1.0"` to reflect the feature additions.

- [ ] **Step 2: Build Release too** to catch optimization-only warnings:

```powershell
msbuild UniversalAppControl.sln /p:Configuration=Release /p:Platform=x64 /nologo /v:m
```
Expected: `Build succeeded`.

- [ ] **Step 3: Run the full self-test once more**

Run the Self-test command against the Debug build. Expected: `exit=0`, all checks `PASS`.

- [ ] **Step 4: Full manual regression (the spec's verification list):**
  - Bug 1: restart Explorer, tray icon returns.
  - Bug 2: rapid hotkey presses + a slow target; hotkeys never freeze.
  - Add entry via picker, edit hotkey/actions, live reload without restart.
  - Toggle Run-at-startup from both the tray menu and the settings checkbox; registry matches.
  - Open INI folder works.
  - Install copies exe, creates Start Menu shortcut, relaunches installed copy.

- [ ] **Step 5: Commit**

```powershell
git add Main.h
git commit -m "chore: bump version to 1.1.0"
```

---

## Self-Review (completed during planning)

**Spec coverage** - every spec section maps to a task:
- Threading model / Bug 2 -> Tasks 5, 6, 8
- Bug 1 (TaskbarCreated) -> Task 7
- Config location + data model + boss-key removal -> Tasks 2, 3, 4, 8
- Settings UI (Layout A, fields, live reload) -> Tasks 13, 14, 15
- Process picker -> Task 17
- Status indicators -> Task 16
- Startup registration -> Tasks 9, 11
- Open INI folder -> Task 12
- Install (copy, shortcut, startup fixup, relaunch, mutex handoff) -> Tasks 18, 19, 20
- Launch mode (autostart vs manual) -> Task 10
- Tray menu -> Task 11

**Type/name consistency** - shared signatures are declared once in headers and reused: `GetConfigPath`/`GetConfigDir`/`SaveConfig`/`ParseHotkey`/`FormatHotkey` (config.h), `Job`/`JobType`/`JobQueuePush`/`JobQueuePop`/`WorkerInit`/`WorkerThreadProc` (worker.h), `ShowSettingsWindow`/`PickRunningProcess` (settings_ui.h), `IsStartupEnabled`/`SetStartupEnabled`/`OpenConfigFolder`/`InstallToUserPrograms`/`IsExeRunning` (install.h), `GetExePath`/`LoadConfig`/`DispatchHotkey`/`gHotkeyLock` (Main.h). `HandleKeyboardHotkey` is renamed to `DispatchHotkey` in Task 6 and the dead `WM_HOTKEY` reference is removed in Task 13.

**Known follow-ups (out of scope, noted in spec):**
- No worker watchdog for a genuinely hung suspend/resume.
- No settings UI in no-tray mode.
- Live reload (`JOB_RELOAD_CONFIG` -> `ReadConfig`) `memset`s `gProfiles` and rebuilds with `Triggered = false`. If you edit settings while an entry is *currently* hidden/paused, that entry's toggle state inverts (the next press tries to hide-again instead of show) and its `HiddenWindows` list is wiped. `gPausedProcesses` survives the memset and `RestoreWindowsForPofile` re-enumerates when `NumHiddenWindows == 0`, so it partially self-heals, but the toggle inversion is user-visible. Acceptable for now; revisit if it annoys in practice (e.g. preserve per-entry `Triggered`/`HiddenWindows` across reload by matching on exe name).
