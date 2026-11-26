# Changelog

What changed in each release, written for people using kuukan. Build-time detail is left out; the commit history has it.

> **Maintenance:** add entries under **Unreleased** on the feature branch, alongside the `FEATURES.md` update. At release time that heading becomes the version.

## Unreleased

- A pinned app's hotkey (alt+d for Discord) brings its window forward again when macOS registers the app's launch without a process id; it used to only activate the app without switching to space 1.
- Single user config file (`~/.config/kuukan/config.json`, overlaid on `config.default.json`) for pinned apps, hotkeys, bar widgets, todo lists, colours, and window manager settings, applied with `make apply`.
- Portable installation (`make install`) that works from any clone path, generates `skhdrc` and the LaunchAgent plist, and removes hardcoded local paths.
- `make try BRANCH=<branch>`, `make back` and `make deploy` put a branch on the live desktop, return to `main`, and bring the desktop up to `main` after a merge. Each restarts only what the change touches (helpers, spacesd, yabai, the bar) and works from any shell.
- `make dev-guided` opens a branch's whole desktop (bar, hotkeys, spacesd) in a VM window, so a change can be tried by hand without switching the live desktop. Keys pressed in that window go to the VM.
