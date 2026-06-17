# DDC/CI Display Control Tasks

## Phase 1: Display module foundation
- [x] P1.1 Write display.h (public API)
- [x] P1.2 Write display.c (DDC/CI implementation: enumerate, probe, capture, apply, restore)
- [x] P1.3 Add display.c/h to .vcxproj and .filters; add Dxva2.lib
- [x] P1.4 Verify build compiles cleanly with new module

## Phase 2: Data model + persistence
- [x] P2.1 Extend Main.h: DISPLAY_PRESET struct, MAX_PRESETS, globals, PROFILE_CONFIG.DisplayPreset
- [x] P2.2 config.c: skip [preset:*] sections in profile parse; load DisplayPreset, DisplayControl, DefaultPreset
- [x] P2.3 config.c SaveConfig: write [preset:*] sections and new [general] keys
- [x] P2.4 Build verified clean

## Phase 3: Foreground polling + worker reconcile
- [x] P3.1 worker.h: add JOB_APPLY_DISPLAY to JobType; add DesiredDisplay struct
- [x] P3.2 Main.c: add gDesiredDisplay global + throttled foreground poll in message loop
- [x] P3.3 worker.c: JOB_APPLY_DISPLAY case; reconcile monitor state via display.c
- [x] P3.4 Main.c: restore-all on exit
- [x] P3.5 Build compiles clean (exe locked = expected, app running)

## Phase 4: Settings UI
- [x] P4.1 ui_ids.h: add new control IDs
- [x] P4.2 settings_ui.c: widen window; add DisplayControl checkbox; preset combo on profile form
- [x] P4.3 settings_ui.c: preset editor (brightness/contrast/colortemp + Capture button)
- [x] P4.4 settings_ui.c: warn-once dialog for unsupported monitors
- [x] P4.5 LoadSelectionToFields/ApplyFieldsToSelection: handle DisplayPreset field
- [x] P4.6 Build compiles clean

## Phase 5: Final
- [x] P5.1 Final build verification (no new compiler errors)
- [x] P5.2 Commit + push
