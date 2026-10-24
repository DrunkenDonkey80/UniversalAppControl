# Changelog

## 1.3.3
- Minimize/restore hotkeys: apps like Pi Hub (Tauri) no longer just flash on the taskbar instead of coming to front, and no longer lose their maximized state when they were minimized by other means. Restores now work like AutoHotkey's WinActivate, and invisible helper windows are no longer minimized/restored.

## 1.3.2
- Minimize/restore hotkeys: a maximized window (e.g. Viber) now always comes back maximized and in front. Previously a quick double press, or restoring the window from the taskbar in between, could un-maximize it or only flash its taskbar button.

## 1.3.1
- Tray menu: "Copy config to clipboard" and "Paste config from clipboard..." to move the config between PCs. Paste asks first, keeps the old config as `UniversalAppControl.ini.bak`, restores hidden apps and reloads without a restart.

## 1.3.0
- Auto-update from GitHub releases: checks a minute after start and then daily; "Check for updates" in the tray menu forces a check.
- When an update is downloaded the tray icon gets a red "!" badge, and "Restart to update" appears in the tray menu and the Settings window.
- Built with VS 2026; GitHub Actions builds every push and publishes a release for `v*` tags.
