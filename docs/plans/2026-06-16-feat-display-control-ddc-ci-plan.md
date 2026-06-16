---
title: Display Control via DDC/CI (foreground-driven monitor presets)
type: feat
date: 2026-06-16
---

# ✨ Display Control via DDC/CI

> Source brainstorm: `docs/brainstorms/2026-06-16-display-control-brainstorm.md`

## Overview

Add automatic monitor adjustment driven by the foreground application.
Each existing app **profile** gains an optional reference to a named **display
preset** (brightness + contrast + color temperature). When a profile's target
app comes to the foreground, the app applies that preset — via DDC/CI — to the
physical monitor the app's window is on (e.g. raise brightness for a game).
When no app with a preset is in front, a designated **Default** preset is
applied as the baseline. A **"Capture current monitor settings"** button reads
live monitor values into a preset, and the Default preset auto-fills from the
current monitor on first run if empty.

This is foreground-driven (not hotkey-driven) and reuses the existing
worker-thread job queue so the slow (~50 ms/call) DDC/CI work never blocks the
UI or the keyboard hook.

## Problem Statement

Today `UniversalAppControl` reacts only to hotkeys and only manipulates windows
(Hide / Minimize / Pause). There is no awareness of *which* app is in front over
time, and no control over monitor hardware. Users who want "brighten the screen
while my game is focused, dim it back afterward" must do it manually through the
monitor OSD. The app already owns the per-app *profile* concept and a foreground
detection helper, so this is a natural extension.

Constraints discovered during research:
- DDC/CI calls are **slow** (`SetMonitorBrightness` ≈ 50 ms) and must stay off
  the UI thread and the LL keyboard hook path. → route through worker thread.
- Many monitors **partially or don't** implement MCCS; calls can fail or be
  undefined. → capability probe + skip + warn-once in UI.
- **Color temperature is a discrete enum** (`MC_COLOR_TEMPERATURE`: 4000K…
  11500K), not a continuous value; supported values vary per monitor.
  Brightness/contrast are continuous (min..max). → store color temp as the enum,
  brightness/contrast as 0–100 percent mapped to each monitor's min..max.
- Laptop internal panels generally don't support DDC/CI (need WMI instead) →
  out of scope; treated as "unsupported, warn once".

## Proposed Solution

1. New self-contained module **`display.c` / `display.h`** wrapping the Win32
   monitor-configuration API (`Dxva2.lib`): enumerate physical monitors for a
   window, probe capabilities, read current values (capture), apply a preset,
   and restore.
2. New data types: `DISPLAY_PRESET` (name + brightness/contrast/colortemp, each
   independently "unset" = leave unchanged) and a `gPresets[]` table. Extend
   `PROFILE_CONFIG` with a `DisplayPreset` name reference.
3. **Throttled foreground polling** added to the existing main message loop
   (~400 ms). On a *stable* foreground change it computes the desired target
   (monitor + preset) and pushes a coalescing `JOB_APPLY_DISPLAY` job.
4. Worker thread gains a `JOB_APPLY_DISPLAY` case that *reconciles* the monitor
   to the desired state (restoring the previously-changed monitor to Default if
   the active monitor changed).
5. INI persistence: `[preset:Name]` sections + `DisplayPreset=` per profile +
   `[general] DisplayControl=` master toggle + `DefaultPreset=`.
6. Settings UI additions: a preset picker (combo) on the profile form, a small
   preset editor (brightness/contrast/colortemp fields + **Capture** button),
   and a once-per-session warning listing monitors that can't be controlled.
7. On exit, restore each touched monitor to the values snapshotted at startup.

## Technical Approach

### Architecture

```
┌────────────────────┐    foreground poll (~400ms, main loop)
│  wWinMain msg loop  │───────────────┐
└────────────────────┘               │ computes desired {HMONITOR, presetName}
        │ PeekMessage / hook          ▼
        │                    ┌───────────────────────┐
        │  JOB_APPLY_DISPLAY │  gDesiredDisplay (lock)│  coalesced target
        ▼  (push)            └───────────────────────┘
┌────────────────────┐                 ▲ read
│   Worker thread     │─────────────────┘
│  WorkerThreadProc   │   reconcile -> display.c (DDC/CI, ~50ms/call)
└────────────────────┘
        │
        ▼
   display.c  ── Dxva2.lib: GetPhysicalMonitorsFromHMONITOR,
                 GetMonitorCapabilities, Get/SetMonitorBrightness,
                 Get/SetMonitorContrast, Get/SetMonitorColorTemperature
```

Key decision: the poll thread never calls DDC/CI. It only writes a small
`gDesiredDisplay` struct (under a lock) and pings the worker. The worker reads
the *latest* desired state and reconciles — this naturally coalesces rapid
alt-tabbing (intermediate targets are simply overwritten before the worker runs).

### New / changed files

| File | Change |
|------|--------|
| `display.h` *(new)* | Public API for the DDC/CI module |
| `display.c` *(new)* | Implementation; links `Dxva2.lib` |
| `Main.h` | Add `DISPLAY_PRESET`, `MAX_PRESETS`, extend `PROFILE_CONFIG`, globals |
| `Main.c` | Foreground polling in msg loop; desired-state struct; exit restore |
| `worker.h` | Add `JOB_APPLY_DISPLAY` to `JobType` |
| `worker.c` | Handle `JOB_APPLY_DISPLAY` → reconcile via display.c |
| `config.c` | Load/save `[preset:*]` sections, `DisplayPreset`, `[general]` keys; skip preset sections in profile parse |
| `config.h` | Declarations for preset load/save helpers |
| `settings_ui.c` | Preset combo + editor + Capture button + unsupported warning |
| `ui_ids.h` | New control IDs (`IDC_PRESET`, `IDC_BRIGHT`, `IDC_CONTRAST`, `IDC_COLORTEMP`, `IDC_CAPTURE`, `IDC_DISPLAYCTL`) |
| `UniversalAppControl.vcxproj` / `.filters` | Add `display.c`/`display.h`; add `Dxva2.lib` |

### Data structures (`Main.h`)

```c
#define MAX_PRESETS 16
#define PRESET_UNSET (-1)   // a field left untouched by the preset

typedef struct _DISPLAY_PRESET {
    wchar_t Name[MAX_NAME];
    int Brightness;   // 0..100 percent, or PRESET_UNSET
    int Contrast;     // 0..100 percent, or PRESET_UNSET
    int ColorTemp;    // MC_COLOR_TEMPERATURE enum value, or PRESET_UNSET
} DISPLAY_PRESET;

extern DISPLAY_PRESET gPresets[MAX_PRESETS];
extern int  gNumPresets;
extern BOOL gDisplayControlEnabled;          // [general] DisplayControl
extern wchar_t gDefaultPresetName[MAX_NAME]; // [general] DefaultPreset (default L"Default")

// PROFILE_CONFIG gains:
//   wchar_t DisplayPreset[MAX_NAME];  // empty = no display rule (=> Default applies)
```

### Display module API (`display.h`)

```c
// display.h
#pragma once
#include <Windows.h>
#include <stdbool.h>
#include "Main.h"

typedef struct {
    wchar_t  deviceId[128];   // stable-ish id (PHYSICAL_MONITOR description + index)
    HANDLE   handle;          // physical monitor handle (transient; reopened on use)
    DWORD    caps;            // MC_CAPS_* bitmask from GetMonitorCapabilities
    // snapshot of original values for restore-on-exit:
    DWORD bMin,bCur,bMax, cMin,cCur,cMax; DWORD origColorTemp;
    bool   hasSnapshot;
} DisplayMonitor;

// Enumerate physical monitors for the monitor that window `hwnd` lives on.
int  DisplayMonitorsForWindow(HWND hwnd, DisplayMonitor* out, int maxOut);

// Probe all monitors; fill names of unsupported ones for the UI warning.
int  DisplayListUnsupported(wchar_t names[][128], int maxOut);

// Read current values from the primary/target monitor into a preset (Capture).
bool DisplayCaptureCurrent(HWND hwnd, DISPLAY_PRESET* out);

// Apply a preset to the monitor(s) for `hwnd`; snapshots originals on first touch.
bool DisplayApplyPreset(HWND hwnd, const DISPLAY_PRESET* preset);

// Restore monitor(s) for `hwnd` (or all touched) to snapshotted originals.
void DisplayRestoreAll(void);
```

Internals use, per Microsoft docs (header `highlevelmonitorconfigurationapi.h`,
`physicalmonitorenumerationapi.h`, lib `Dxva2.lib`):
`MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)` →
`GetNumberOfPhysicalMonitorsFromHMONITOR` → `GetPhysicalMonitorsFromHMONITOR` →
`GetMonitorCapabilities` (gate each set call on `MC_CAPS_BRIGHTNESS` /
`MC_CAPS_CONTRAST` / `MC_CAPS_COLOR_TEMPERATURE`) → `Get*`/`Set*` →
`DestroyPhysicalMonitors`. Brightness/contrast percent map:
`raw = min + (max-min)*pct/100`.

### Foreground polling (Main.c msg loop)

```c
// in the while(gIsRunning) loop, after MsgWaitForMultipleObjects(...):
static DWORD lastPoll = 0;
if (gDisplayControlEnabled && GetTickCount() - lastPoll >= 400) {
    lastPoll = GetTickCount();
    HWND fg = GetForegroundWindow();
    // resolve fg -> exe -> matching profile -> preset name (or Default)
    // debounce: require the same fg exe seen twice before committing
    // if desired {monitorKey, presetName} changed -> store gDesiredDisplay + push job
    Job j = { JOB_APPLY_DISPLAY, 0 };
    JobQueuePush(j);
}
```

### Worker reconcile (worker.c)

```c
case JOB_APPLY_DISPLAY: {
    EnterCriticalSection(&gHotkeyLock);
    DesiredDisplay d = gDesiredDisplay;   // copy latest (coalesced)
    LeaveCriticalSection(&gHotkeyLock);
    // if active monitor changed, restore previous monitor to Default first
    // then apply d.preset to d.hwnd's monitor (skip if already applied)
    break;
}
```

### INI schema

```ini
[general]
Debug=false
DisplayControl=true
DefaultPreset=Default

[preset:Default]
Brightness=40
Contrast=50
ColorTemp=6500          ; MC_COLOR_TEMPERATURE; omit/blank => unset

[preset:Game]
Brightness=90
Contrast=75
; ColorTemp omitted => leave unchanged

[notepad.exe_0]
Hotkey=Ctrl+Alt+V
ProgramExeName=notepad.exe
Hide=false
Minimize=false
Pause=false
DisplayPreset=Game      ; empty/absent => Default applies when focused
```

`config.c` must **skip sections beginning with `preset:`** in the profile parse
loop (currently every non-`general` section becomes a profile).

### Concurrency

- `gPresets`, `gProfiles`, `gDesiredDisplay`, `gDisplayControlEnabled` are read by
  the worker and written by UI/config/poll → guard with the existing
  `gHotkeyLock` critical section (same pattern as `RebuildHotkeys`).
- Physical monitor handles are **transient**: opened and `DestroyPhysicalMonitors`'d
  within a single worker apply call — never cached across calls (avoids stale
  handles after monitor unplug). Capability/snapshot keyed by `deviceId` string.

## Implementation Phases

### Phase 1: Display module foundation
- Create `display.h` / `display.c`; add to `.vcxproj`, `.filters`, link `Dxva2.lib`.
- Implement enumerate-for-window, capability probe, capture, apply, restore.
- Standalone manual test: temp hotkey or `selftest.c` hook to apply a hardcoded
  preset to the focused monitor and restore.
- **Success:** brightness/contrast/colortemp change on a DDC/CI-capable monitor;
  unsupported monitors are detected without crashing.

### Phase 2: Data model + persistence
- Add `DISPLAY_PRESET`, `gPresets`, `gNumPresets`, `DisplayPreset` field,
  `[general]` toggles to `Main.h`.
- `config.c`: parse/skip `[preset:*]`, load `DisplayPreset`, `DisplayControl`,
  `DefaultPreset`; `SaveConfig` writes presets + new keys. Auto-fill empty
  Default from current monitor on first run.
- **Success:** round-trips presets + profile references through the INI; presets
  are never mistaken for profiles.

### Phase 3: Foreground polling + worker reconcile
- Add `JOB_APPLY_DISPLAY` (`worker.h`), `gDesiredDisplay` struct, reconcile case.
- Add throttled, debounced foreground poll to the main loop.
- Track active monitor/preset; restore previous monitor to Default on switch.
- Restore-all on exit (snapshot originals at first touch).
- **Success:** focusing a Game-preset app brightens its monitor within ~1 s;
  alt-tabbing away restores Default; moving the window to another monitor moves
  the effect; rapid alt-tab does not queue/stutter (coalesced).

### Phase 4: Settings UI
- Profile form: **Display preset** combo (None/Default/…custom).
- Preset editor area: Brightness, Contrast, Color-temp fields + **Capture
  current monitor settings** button + add/remove preset.
- Master **"Control monitor (DDC/CI)"** checkbox (`DisplayControl`).
- **Warn-once** dialog listing monitors that can't be controlled, shown the first
  time the preset editor is opened in a session.
- **Success:** users can create/capture/assign presets and toggle the feature
  entirely from the UI; existing profiles with no preset behave exactly as before.

## Alternative Approaches Considered

- **WinEvent hook (`EVENT_SYSTEM_FOREGROUND`)** instead of polling — rejected for
  v1 (brainstorm): more lifecycle/hook management; debounce still needed anyway.
  Polling reuses the existing loop with minimal code.
- **Per-profile raw display values** instead of named presets — rejected:
  duplication and harder tuning; named presets chosen in brainstorm.
- **Low-level VCP (`SetVCPFeature`) for everything** — rejected for v1: high-level
  API covers brightness/contrast/colortemp and is simpler; VCP can be a later
  extension (e.g. input-source switching).
- **Apply to all monitors** — rejected: brainstorm chose "monitor where the app's
  window is".

## Acceptance Criteria

### Functional Requirements
- [x] A profile can be assigned a named display preset; leaving it unassigned
      keeps the profile's current behavior unchanged.
- [x] Presets store brightness (0–100%), contrast (0–100%), color temperature
      (enum), each independently optional ("unset" = leave unchanged).
- [x] Focusing an app whose profile has a preset applies it to that app's monitor
      within ~1 second.
- [x] When no preset-bearing app is focused, the Default preset is applied.
- [x] "Capture current monitor settings" fills a preset from the live monitor.
- [ ] If Default is empty on first run, it auto-fills from the current monitor. (deferred — can be added later)
- [x] Effect targets only the monitor the app's window is on.
- [x] A global toggle disables all display control (default off until configured).
- [x] On exit, every monitor the app changed is restored to its startup values.

### Non-Functional Requirements
- [x] No DDC/CI call runs on the UI thread or LL keyboard hook path (worker only).
- [x] Rapid alt-tabbing coalesces to at most the latest desired state (gDesiredDisplay overwrite).
- [x] Unsupported / unplugged monitors never crash or hang the app; failures are
      skipped and (for unsupported) surfaced once in the UI.
- [x] Preset `[preset:*]` sections are never parsed as profiles.

### Quality Gates
- [ ] Manual test matrix: DDC/CI-capable external monitor, laptop panel
      (unsupported), dual-monitor move, monitor unplug while active, rapid
      alt-tab, app exit restore.  **(requires real hardware — do before releasing)**
- [x] Builds clean in the existing MSVC project with `Dxva2.lib` linked.
- [ ] AI-generated DDC/CI error handling reviewed by a human (validate on real hardware).

## Success Metrics
- Brightness visibly changes on focus within ~1 s on supported monitors.
- Zero UI/keyboard latency regression (hook + settings remain responsive).
- No stuck/incorrect monitor state after exit across the test matrix.

## Dependencies & Prerequisites
- `Dxva2.lib` / `Dxva2.dll` (present on Windows Vista+; already targeted).
- Headers: `highlevelmonitorconfigurationapi.h`, `physicalmonitorenumerationapi.h`,
  `lowlevelmonitorconfigurationapi.h` (only if VCP added later).
- DDC/CI must be enabled in the monitor's OSD (user-side; document this).

## Risk Analysis & Mitigation
| Risk | Impact | Mitigation |
|------|--------|-----------|
| Monitor ignores/garbles MCCS writes | Wrong/undefined display state | Probe `GetMonitorCapabilities`; only set supported features; warn-once |
| 50 ms/call latency stacks up | Sluggishness / flicker | Worker-only; coalesce via `gDesiredDisplay`; skip if already applied |
| Stale physical-monitor handle after unplug | Crash / failed calls | Never cache handles; reopen per apply; re-probe on `WM_DISPLAYCHANGE` |
| Color-temp enum unsupported / different steps | Color jump or no-op | Store enum; skip if monitor lacks `MC_CAPS_COLOR_TEMPERATURE` |
| App spans two monitors | Ambiguous target | `MONITOR_DEFAULTTONEAREST` (largest-area monitor) |
| Crash before exit-restore | Monitor left dimmed/bright | Document; consider Phase 5 periodic `SaveCurrentMonitorSettings` opt-in (deferred) |

## Future Considerations
- Low-level VCP extension: input-source switching (`0x60`), color presets (`0x14`).
- Per-monitor values within a preset (brainstorm alternative).
- Smooth brightness fades instead of instant set.
- WMI brightness path for laptop internal panels.

## Documentation Plan
- README: new "Display control (DDC/CI)" section — how presets/Default work,
  the global toggle, the OSD DDC/CI prerequisite, and unsupported-monitor notes.
- Update settings screenshot.

## References & Research

### Internal References
- Profile model: `Main.h:40` (`PROFILE_CONFIG`), globals `Main.c:31`
- Config load/parse (section loop to special-case presets): `Main.c:216`
  (`ReadConfig`), profile fields `Main.c:247-258`
- Config save: `config.c:91` (`SaveConfig`)
- Foreground helpers: `Main.c:489` (`GetForegroundWindowProcessID`),
  `gWindowInfo` exe resolution `Main.c:386` (`enumWindowCallback`)
- Worker job queue / dispatch: `worker.c:44` (`WorkerThreadProc`),
  `worker.h:5` (`JobType`)
- Main message loop (poll insertion point): `Main.c:966`
- Exit restore block (mirror for monitors): `Main.c:993`
- Settings UI form + apply/save: `settings_ui.c` (`CreateControls`,
  `LoadSelectionToFields`, `ApplyFieldsToSelection`); control IDs `ui_ids.h`

### External References
- Using the High-Level Monitor Configuration Functions —
  https://learn.microsoft.com/en-us/windows/win32/monitor/using-the-high-level-monitor-configuration-functions
- Monitor Configuration Functions (index) —
  https://learn.microsoft.com/en-us/windows/win32/monitor/monitor-configuration-functions
- `GetPhysicalMonitorsFromHMONITOR` (PHYSICAL_MONITOR, Dxva2.lib) —
  https://learn.microsoft.com/en-us/previous-versions/ms775216(v=vs.85)
- `SetMonitorBrightness` (≈50 ms, MC_CAPS_BRIGHTNESS) —
  https://learn.microsoft.com/en-us/windows/win32/api/highlevelmonitorconfigurationapi/nf-highlevelmonitorconfigurationapi-setmonitorbrightness
- VCP code reference (future low-level work) —
  https://github.com/Pink-o/monitor-control/blob/main/docs/DDC-CI-Reference.md

### Related Work
- Brainstorm: `docs/brainstorms/2026-06-16-display-control-brainstorm.md`
- Prior commit (loop/hook groundwork this builds on): `c44ee75`

## Open Questions (resolved defaults, confirm during build)
- **Restore on exit**: restore to startup snapshot (chosen) vs. apply Default.
- **No-preset app**: applies Default baseline (chosen) vs. leave-as-is.
- **Color temp storage**: MCCS enum value (chosen) vs. Kelvin string.
- **Poll/debounce**: 400 ms poll, require 2 consecutive same-foreground reads.
- **Capability probe timing**: lazy on worker apply + on `WM_DISPLAYCHANGE`.
```
