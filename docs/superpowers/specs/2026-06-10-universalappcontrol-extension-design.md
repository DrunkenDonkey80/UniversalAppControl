# UniversalAppControl Extension - Design

Date: 2026-06-10
Status: Approved

## Summary

UniversalAppControl is a Windows tray utility that toggles hide/minimize/pause on
target processes and windows via global hotkeys, driven by an INI config. This
extension fixes two reliability bugs and turns it into a usable general hotkey
manager with a native settings UI, process picker, startup registration, and a
per-user install flow.

Scope decisions made during brainstorming:

- UI is a **native Win32 dialog** (no WebView2, no web frontend). The app stays a
  single self-contained .exe.
- Actions remain the **three existing toggles**: hide/show, minimize/restore,
  pause/resume. No launch/terminate. ("Show/hide Viber" works because Viber stays
  resident.)
- Target matching is **by exe name** (runtime). Full path is captured only for
  display and status.
- The half-built **boss-key / current-window feature is removed**.
- Config moves to **`%APPDATA%\UniversalAppControl\config.ini`** (shared by all
  copies of the exe).

## Goals

1. Tray icon no longer disappears permanently (Bug 1).
2. Hotkeys never stop responding while the app runs (Bug 2).
3. A native settings UI to manage entries, reachable from the tray menu.
4. Add/remove the app from Windows startup (per-user login).
5. Install the app into per-user programs with a Start Menu entry.

## Non-Goals

- Launching or terminating programs via hotkey.
- Full-path-based runtime matching.
- Boss-key / panic-hide / current-window-focus features.
- System-wide (all-users / admin) install.
- A settings UI in no-tray (alternative shell) mode.

---

## Architecture

### Threading model (fixes Bug 1 and Bug 2)

Root cause of both bugs: all work runs on one thread. The low-level keyboard hook
callback (`LowLevelKeyboardProc`) currently does process snapshots, `EnumWindows`,
`IsProcessSuspended` (which suspends/resumes every thread of the target),
suspend/resume, and `Sleep` - all inline. A low-level hook that does not return
within `LowLevelHooksTimeout` (~300 ms) is silently removed by Windows, which
kills the hotkeys. Because that hook runs on the same thread that pumps the tray
window, a stall also freezes the tray.

Restructure into three roles:

- **Hook thread (main thread).** `LowLevelKeyboardProc` does only a cheap lookup
  in the hotkey table (guarded by a critical section), and on a match posts a job
  to the worker and returns `1` immediately. No snapshots, no suspend, no `Sleep`.
- **Worker thread (new).** Owns *all* process/window manipulation and the mutable
  state behind it: `gPausedProcesses` / `gNumPausedProcesses`, each entry's
  `HiddenWindows` / `ForegroundWindow`, and each hotkey's `Triggered` flag. It
  drains a job queue. Job types:
  - `TOGGLE_HOTKEY(index)` - run the existing `HandleKeyboardHotkey` logic
    (`UpdateProcessIDs`, `UpdateWindowProcessIDs`, hide/minimize/suspend or the
    reverse).
  - `RELOAD_CONFIG` - reload profiles and rebuild the hotkey table.
  - `SHUTDOWN` - restore all triggered profiles and exit the thread.
  Because a single thread owns this state, there are no data races. A
  thread-per-press approach would introduce races on the shared globals and is
  explicitly rejected.
- **Tray + UI (main thread).** The tray window and the settings dialog live on the
  main thread's message loop.

Job queue: a fixed-size ring (or simple array) of jobs guarded by a critical
section, with an auto-reset event the worker waits on. Jobs are processed in FIFO
order, so repeated presses of the same hotkey keep the toggle state correct.

**Known limitation (documented, not built unless requested):** the worker keeps
the app responsive, but if a single `NtSuspendProcess`/`NtResumeProcess` genuinely
hangs on one target, that one job stalls the worker. Acceptable for now: the app
itself and its other hotkeys stay responsive only up to the point of a truly hung
op. No watchdog in this scope.

### Bug 1 - tray icon disappears

`SysTrayCallback` does not handle the `TaskbarCreated` broadcast. When explorer.exe
restarts (crash, update, manual restart), Windows broadcasts the registered message
`RegisterWindowMessage(L"TaskbarCreated")`, and every tray app must re-add its icon.
Fix:

- Register the message id once at startup.
- In `SysTrayCallback`, on receiving that message, re-add the icon
  (`Shell_NotifyIconW(NIM_ADD, &gTrayNotifyIconData)`).

Moving heavy work off the main thread (above) additionally removes the *transient*
tray unresponsiveness. Both fixes are needed.

---

## Configuration

### Location and format

- Config path: `%APPDATA%\UniversalAppControl\config.ini` (resolved via
  `SHGetKnownFolderPath`/`FOLDERID_RoamingAppData` or `%APPDATA%`). Create the
  directory on first run if missing.
- `ReadConfig` changes from "ini next to the module" to this fixed per-user path.
  All copies of the exe (download folder, installed folder) share this one file.
- Format stays INI (`GetPrivateProfileString` / `WritePrivateProfile*`), so any
  existing config concepts carry over.

### Data model changes (`Main.h`)

- `PROFILE_CONFIG`:
  - Keep: `ProgramExeName` (match key), `HideEnabled`, `PauseEnabled`,
    `MinimizeEnabled`, `WindowNames` / `NumWindows`, runtime fields
    (`ProcessID`, `HiddenWindows`, `NumHiddenWindows`, `ForegroundWindow`),
    `HotKey` / `HotKeyModifiers`.
  - Add: `wchar_t ProgramPath[MAX_PATH]` - captured by the picker, used only for
    display and status. Not used for matching.
- `CONFIG`: remove `BossHotKey`, `BossHotKeyModifiers`, `BossSections`,
  `NumBossSections`. Keep `Debug`, `TrayIcon`.
- Remove `PRIFILE_ID_BOSS`, `PROFILE_ID_CURRENT`, and the dead branches in
  `HandleKeyboardHotkey` (Main.c:621-628) plus the boss/current hotkey
  registration calls in `ReadConfig`.

### INI schema (per entry / `[section]`)

```
[Viber]
Hotkey=Ctrl+Alt+V
ProgramExeName=Viber.exe
ProgramPath=C:\Users\me\AppData\Local\Viber\Viber.exe
Hide=true
Minimize=false
Pause=true
WindowNames=         ; optional, '|'-separated, as today
```

`[general]` keeps `Debug` and `TrayIcon`. Boss keys are dropped from the schema.

### Live reload

When the settings UI saves, it writes the INI and posts a `RELOAD_CONFIG` job to
the worker. The worker reloads profiles and rebuilds the hotkey table while holding
the critical section the hook uses for lookups, so changes take effect without a
restart and without racing the hook thread. No process restart required.

---

## Settings UI (native Win32)

### Entry point and launch mode

- Run-key registration uses an `--autostart` argument.
- Launched **with** `--autostart` (login): tray icon only, no window.
- Launched **without** it (user double-clicks the exe): the settings window opens
  immediately, in addition to the tray icon.

### Tray context menu

Right-click the tray icon shows a `TrackPopupMenu` with:

- **Settings…** - opens the settings window.
- **Run at Windows startup** - checkable; reflects and toggles the Run-key entry.
- **Install to user programs…** - runs the install flow.
- **Open INI folder** - opens Explorer at `%APPDATA%\UniversalAppControl\`,
  selecting the ini.
- **Quit**.

(Left/middle click behavior: open Settings rather than the current quit prompt.)

### Settings window - Layout A (master-detail)

A single modeless dialog owned by the main thread (pumped in the main message loop
with `IsDialogMessage`). One window, no per-entry pop-ups.

- **Left:** a list (ListView, report mode) of entries showing icon + name, hotkey,
  and live status.
- **Right:** fields for the selected entry:
  - Name (exe name; the match key).
  - Path + status line (captured path, with present/missing and running/not-running
    indicators).
  - Hotkey - a capture control (type the combo; parsed into modifiers + vk).
  - Action checkboxes: **Hide/show**, **Minimize/restore**, **Pause/resume**.
- **Buttons:** `+ Add from running…`, `Remove`.
- **Checkbox:** `Run at Windows startup`.
- **Button:** `Open INI folder`.
- **Button:** `Install to user programs…`.

Edits apply live: changing a field updates the in-memory model, writes the INI, and
posts `RELOAD_CONFIG`.

### Process picker (`+ Add from running…`)

A modal dialog listing current processes via `CreateToolhelp32Snapshot`
(`TH32CS_SNAPPROCESS`), each row showing:

- **Icon** - extracted from the process exe (`ExtractIconEx` / `SHGetFileInfo` on
  the full path).
- **Name** - `szExeFile`.
- **Full path** - `QueryFullProcessImageNameW` (needs
  `PROCESS_QUERY_LIMITED_INFORMATION`).

Selecting a process creates a new entry pre-filled with name (match key) and path
(display/status). The user then assigns a hotkey and actions.

### Status indicator

Per entry, computed for display:

- **Running / not running** - is any process with that exe name currently alive.
- **File present / missing** - if `ProgramPath` was captured, whether that path
  exists on disk.

This satisfies the "detect if it's there or missing" requirement without making
runtime matching path-dependent.

### No-tray mode

When `TrayIcon` is false (alternative shell / no Explorer), there is no tray menu,
so the settings UI is unreachable and the app is config-file-driven. Documented as
a known limitation.

---

## Startup registration

- Registry: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value name
  `UniversalAppControl`, data = `"<exe path>" --autostart`.
- The tray menu item and the settings checkbox both read from / write to this one
  registry value (single source of truth).
- "Enabled" = value exists and points at a valid exe path.

---

## Install to user programs

Triggered by the tray menu / settings button. Steps:

1. **Copy exe** to `%LOCALAPPDATA%\Programs\UniversalAppControl\UniversalAppControl.exe`
   (create the directory if missing; `CopyFileW`).
2. **Ensure config exists** at `%APPDATA%\UniversalAppControl\config.ini`. Because
   config is in the shared AppData location, no copy into the program folder is
   needed - the installed copy reads the same file. Create a default if absent.
3. **Start Menu shortcut** at
   `%APPDATA%\Microsoft\Windows\Start Menu\Programs\UniversalAppControl.lnk`,
   pointing at the installed exe, created via COM (`IShellLinkW` + `IPersistFile`,
   `CoInitialize`/`CoCreateInstance`).
4. **Startup path fixup** - if "Run at startup" is enabled, rewrite the Run-key
   value to the installed exe path (with `--autostart`).
5. **Relaunch installed copy** - `ShellExecuteW`/`CreateProcessW` the installed exe
   (normal/UI mode), then quit this instance. The single-instance mutex is handed
   off cleanly: the relaunched instance retries `CreateMutexW`/`OpenMutexW` for a
   short bounded period so it does not trip the "already running" guard during the
   brief overlap while the old instance exits.

---

## Files affected

- `Main.h` - data model changes (`ProgramPath`, remove boss-key fields/IDs),
  new declarations (worker, job queue, UI, install, startup helpers).
- `Main.c` - threading restructure, `TaskbarCreated` handling, config path change,
  tray menu, remove boss-key code.
- New: `settings_ui.c` / `.h` - settings dialog, process picker, status.
- New: `install.c` / `.h` - startup registry, install flow, Start Menu shortcut,
  open-ini-folder.
- `resource.h` + `.rc` - dialog templates and menu resources.
- `UniversalAppControl.vcxproj` / `.filters` - add new sources; link `Ole32`,
  `Shell32`, `Comctl32`, `Advapi32` as needed.

(Splitting UI and install into their own translation units keeps `Main.c` focused;
it has already grown large.)

## Testing and verification

The two bugs cannot be unit-tested (they need explorer crashes / hook timeouts).
Verification is manual + analytical:

- **Bug 1:** restart explorer.exe (Task Manager) and confirm the tray icon
  reappears.
- **Bug 2:** confirm `LowLevelKeyboardProc` does no heavy work (code review),
  then exercise rapid hotkey presses and a deliberately slow target; hotkeys keep
  working.
- **UI / config / startup / install:** run the app, add an entry via the picker,
  verify the INI in AppData, verify live reload (no restart), toggle the Run key
  and confirm the registry value, run Install and confirm the copied exe, Start
  Menu shortcut, and relaunch.

## Open risks

- COM shortcut creation and the install relaunch/mutex handoff are the fiddliest
  pieces; they need careful manual testing.
- Worker-thread stall on a genuinely hung suspend/resume is out of scope (no
  watchdog) - revisit if it occurs in practice.
