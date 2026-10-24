# Changelog

## 1.3.1
- Tray menu: "Copy config to clipboard" and "Paste config from clipboard..." to move the config between PCs. Paste asks first, keeps the old config as `UniversalAppControl.ini.bak`, restores hidden apps and reloads without a restart.

## 1.3.0
- Auto-update from GitHub releases: checks a minute after start and then daily; "Check for updates" in the tray menu forces a check.
- When an update is downloaded the tray icon gets a red "!" badge, and "Restart to update" appears in the tray menu and the Settings window.
- Built with VS 2026; GitHub Actions builds every push and publishes a release for `v*` tags.
