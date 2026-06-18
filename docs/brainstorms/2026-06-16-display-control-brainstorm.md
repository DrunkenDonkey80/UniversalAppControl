---
date: 2026-06-16
topic: display-control
---

# Display Control via DDC/CI

## What We're Building

Automatic monitor adjustment driven by the foreground application. Each existing
app **profile** gains an optional reference to a named **display preset**. When a
profile's target app comes to the foreground, the app applies that preset to the
physical monitor the app's window is on (e.g. raise brightness for a game). When
no monitored app is in front, a designated **Default** preset is applied as the
baseline.

Monitor control uses the Windows monitor-configuration APIs over DDC/CI. A preset
captures **brightness, contrast, and color temperature**. A "Capture current
monitor settings" button reads the live values into a preset, making setup fast.

## Why This Approach

- **Extend profiles, don't add a parallel system.** The profile is already the
  per-app unit of behavior (Hide/Minimize/Pause). Adding an optional display
  preset keeps one mental model and one config file.
- **Named presets over per-profile raw values.** Define "Game" / "Default" once;
  profiles just pick one. Less duplication, easier to tune.
- **Polling in the existing message loop** (Approach A) over a WinEvent hook or
  hybrid. The main loop already wakes ~every 5 ms via `MsgWaitForMultipleObjects`;
  a throttled foreground check (~300-500 ms) detects app switches and pushes an
  apply-preset **job** to the existing worker thread. No new global hook; slow
  DDC/CI calls stay off the UI thread; trivial to debounce.

## Key Decisions

- **Integration:** Display settings are an optional field on each `PROFILE_CONFIG`
  (a preset name reference), applied automatically on foreground change — not a
  separate rules list and not tied to the manual hotkey toggle.
- **Preset contents:** brightness + contrast + color temperature (high-level /
  common VCP codes — `0x10`, `0x12`, `0x14`). Avoids the flakiest VCP features.
- **Baseline behavior:** a designated **Default** preset is applied whenever no
  monitored app is in the foreground. If Default is empty, auto-fill it from the
  current monitor values on first run.
- **Capture button:** UI control reads live monitor values (brightness/contrast/
  color temp) into the preset being edited.
- **Multi-monitor:** apply the preset only to the monitor the app's window is on
  (`MonitorFromWindow` → physical monitor), not all displays.
- **Detection:** poll the foreground window in the existing loop, throttled;
  apply work runs through the worker job queue (DDC/CI is slow, ~tens of ms/call).
- **Unsupported monitors:** detect DDC/CI capability and **warn once in the UI**
  (when configuring a preset) showing which monitors can't be controlled; act
  only on capable displays.

## Open Questions

- **Restore scope on focus loss:** when a profile's app loses focus, do we restore
  Default on *that* monitor only, or re-evaluate all monitors? (Likely: re-apply
  Default to the monitor that was changed.)
- **App spanning / moving monitors:** if the app's window moves to another monitor
  while focused, should the preset follow it? (Safety poll could catch this.)
- **Color temperature representation:** store as MCCS color-preset index (VCP
  `0x14`, e.g. 6500K) vs. Kelvin? Capability varies by monitor — confirm during
  planning.
- **Debounce window + poll interval:** exact values (e.g. 400 ms poll, ignore
  switches shorter than X) to avoid hammering DDC/CI on rapid alt-tabbing.
- **Persistence:** new INI schema for presets (a `[preset:Name]` section) and the
  profile's preset reference + Default selection.
- **Capability probe cost:** probing at startup vs. lazily when the settings UI
  opens.

## Next Steps
→ `/workflows-plan` for implementation details
