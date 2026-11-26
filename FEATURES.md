# kuukan — Features

A macOS desktop environment built around SketchyBar (Catppuccin Mocha), yabai, skhd, and `spacesd`, a native C space-policy daemon, all driven from a single user config file.

> **Maintenance:** this is the index of what kuukan can do now. Add an entry on the feature branch, as the last step before merging a NEW capability, so it lands in the same commit as the feature. Only a substantive, user-facing capability gets one. Group entries under area headings. Each is `- **Name** — one or two lines on what it does for the user. Reach: how a user gets there. Proof: scripts/proofs/<name>.sh`, ending with `→ <spec name>` when it has a design spec in `docs/specs/`.

## Configuration

- **Single user config** — `~/.config/kuukan/config.json` (overlaid on `config.default.json`) configures pinned apps, hotkeys, bar widgets, todo markdown files, palette colours, window manager layout/rules, and optional session-status watching; `make apply` regenerates `skhdrc` and reloads every service. Reach: edit `~/.config/kuukan/config.json` and run `make apply`. Proof: scripts/proofs/user-config.sh

## Spaces

- **Space strip** — numbered space items on the left of the bar, labelled, badged and highlighted by spacesd; clicking one focuses that space. Reach: the bar's left side; click a space item. Proof: scripts/proofs/space-click.sh
- **Space hotkeys** — alt+1…0 focus spaces 2–11, shift+alt+N sends the focused window there, alt+g goes to the pinned space, shift+alt+h/l move a space, alt+n/r/w create, rename and destroy. Reach: the hotkeys. Proof: scripts/proofs/space-focus.sh, scripts/proofs/space-lifecycle.sh
- **Session badges** — a space item turns green while a session on that space is running, and orange with a marker when one has unread output (optional `session_status_file`). Reach: the space items; write a session status file. → spacesd-design
- **Second-display mirroring** — with a second display connected, every general space gets a mirror on it and the two displays always show the same pair; focusing, sending, creating, moving and destroying act on the pair, and unplugging the display folds each mirror's windows back onto its space. Reach: connect a second display; the space hotkeys. → spacesd-design

## Apps

- **Pinned apps** — configurable app buttons on the bar with Dock unread badges (default: Slack, Discord, Beeper) and hotkeys bring the app forward on space 1. Reach: click an app item or press its hotkey. Proof: scripts/proofs/bar-items.sh, scripts/proofs/pinned-app.sh
- **Window layout** — configurable layout (default floating) with gaps and a bar-sized top inset, pinned apps on space 1, alt-drag to move or resize, alt+f maximizes the focused window. Reach: alt+f, alt+drag. Proof: scripts/proofs/window-keys.sh

## Status

- **Clock and calendar** — date and time on the right; clicking shows the month's calendar. Reach: click the clock. Proof: scripts/proofs/bar-popups.sh
- **Battery and volume** — battery and volume icons with a detail popup: volume, output device, battery level and connected Bluetooth headphones' battery. Reach: click the battery or volume item.
- **CPU and RAM** — stacked CPU and RAM percentages. Reach: the bar's right side. Proof: scripts/proofs/bar-items.sh
- **Todos** — configurable checklist widgets read Markdown files (e.g. Obsidian) and show Doing and To-do items in a popup. Reach: click a todo icon left of the notch. Proof: scripts/proofs/bar-popups.sh
- **Theme switcher** — toggles macOS light/dark appearance (through a configurable macOS Shortcut) and recolours the icon. Reach: click the theme item left of the notch.
- **Media** — now-playing popup right of the notch with cover, title, artist and back/play/next controls. Reach: click the media item.
- **Lock and wake** — the bar slides away on screen lock and animates back on unlock.
