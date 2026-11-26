# kuukan

A macOS desktop environment combining **SketchyBar**, **yabai**, **skhd**, and **`spacesd`** — a native C99 space-policy daemon — configured from a single JSON file (`~/.config/kuukan/config.json`).

## Highlights

- **`spacesd` (C99 daemon)** — observes Mission Control directly through SkyLight and pushes diffed updates to stock SketchyBar over its Mach CLI protocol:
  - Sub-10 ms space highlight updates with zero shell forks on space switches.
  - UUID-keyed persistent space names that follow spaces across creates, moves, and destroys.
  - Second-display space mirroring: every general space on display 1 pairs with a mirror on display 2, switches stay locked in sync via yabai's scripting addition without stealing focus, and unplugging drains mirror windows back to display 1.
  - Race-free pinned-app switching (space 1) and live Dock unread badges polled via Accessibility.
  - Optional session status badges (running/unread highlights) watched via `kqueue` from a JSON status file.
- **SketchyBar status bar** (Catppuccin Mocha by default) — space strip, pinned app buttons with unread badges, clock + month calendar popup, battery + volume + Bluetooth headphones battery popup (including Sony RFCOMM V2 battery reads), CPU/RAM monitor, Markdown checklist todo popups, light/dark appearance switcher, media controls popup, and lock/unlock slide animation.
- **Single user config** — customize pinned apps, hotkeys, widgets, todo files, colours, and window manager settings in `~/.config/kuukan/config.json` and apply with `make apply`.

## Architecture

```text
  ~/.config/kuukan/config.json (overlaid on config.default.json)
        │
        ├──► sketchybarrc     creates bar items & popups, invokes plugins
        ├──► yabairc          window layout, gaps, pinned & floating rules
        ├──► render-skhdrc.sh generates ~/.config/kuukan/skhdrc for skhd
        └──► spacesd (C99)    reads pinned apps, palette & session status path
               ├── SkyLight + NSWorkspace  ◄── space, window & display events
               ├── Mach CLI wire           ──► stock SketchyBar (space.* & app.*)
               ├── Unix socket + SA socket ──► stock yabai & Dock scripting addition
               └── Accessibility           ──► pinned-app focus & Dock badges
```

See [docs/specs/2026-08-30-spacesd-design.md](docs/specs/2026-08-30-spacesd-design.md) for the full `spacesd` architecture and invariants.

## Prerequisites

- **macOS on Apple Silicon** with Command Line Tools (`xcode-select --install`) and SIP disabled for yabai's scripting addition ([yabai wiki](https://github.com/koekeishiya/yabai/wiki/Disabling-System-Integrity-Protection)).
- **Mission Control**: enable *Displays have separate Spaces* and disable *Automatically rearrange Spaces based on most recent use* in System Settings → Desktop & Dock.
- **Core tools** (Homebrew):
  ```sh
  brew install koekeishiya/formulae/yabai
  brew install koekeishiya/formulae/skhd
  brew install FelixKratz/formulae/sketchybar
  brew install jq
  brew install --cask font-jetbrains-mono-nerd-font
  ```
- **Optional**:
  - [`media-control`](https://github.com/ungive/media-control) for the now-playing media widget.
  - A macOS Shortcut named `Toggle Appearance` (one action: *Set Appearance → Toggle*) for the theme toggle widget.
  - A session-status JSON producer at `session_status_file` (`{"sessions": {"id": {"state": "running"|"unread", "window": <cg_wid>}}}`) for space session badges.

## Installation

1. **Clone anywhere:**
   ```sh
   git clone https://github.com/zillydev/kuukan.git
   cd kuukan
   ```

2. **Create a self-signed code-signing certificate for `spacesd`** (keeps the Accessibility grant valid across rebuilds):
   ```sh
   spacesd/scripts/setup-codesign.sh
   ```

3. **Build helpers and `spacesd`:**
   ```sh
   make build
   make -C spacesd bin/spacesd
   ```

4. **Install symlinks, default config, generated `skhdrc`, and LaunchAgent plist:**
   ```sh
   make install
   ```
   This links `~/.config/sketchybar`, `~/.config/yabai`, `~/.config/kuukan/spacesd`, and `~/.config/kuukan/config.default.json`, creates `~/.config/kuukan/config.json` (if not already present), renders `~/.config/kuukan/skhdrc` (linked at `~/.config/skhd/skhdrc`), and renders `~/Library/LaunchAgents/dev.zilly.spacesd.plist` with your checkout's path.

5. **Start the services:**
   ```sh
   launchctl bootstrap "gui/$(id -u)" ~/Library/LaunchAgents/dev.zilly.spacesd.plist
   yabai --start-service
   skhd --start-service
   brew services start sketchybar
   ```

6. **Grant permissions:**
   - **Accessibility** (System Settings → Privacy & Security → Accessibility): allow `yabai`, `skhd`, and `spacesd` (run `~/.config/kuukan/spacesd/bin/spacesd -m axprompt` to trigger the prompt).
   - **Bluetooth**: macOS prompts on first run of `bt_battery` when opening the battery/volume popup.

## Configuration

Edit **`~/.config/kuukan/config.json`** and run:

```sh
make apply
```

`~/.config/kuukan/config.json` is deep-merged on top of [`config.default.json`](config.default.json) (objects merge recursively; arrays replace the default array), so you only need to specify the keys you want to override.

### Example `~/.config/kuukan/config.json`

```json
{
  "bar": {
    "font": "JetBrainsMono Nerd Font",
    "height": 39,
    "notch_width": 200,
    "colors": {
      "mauve": "0xffcba6f7"
    }
  },
  "pinned_apps": [
    { "id": "Slack",   "app": "Slack",   "icon": "󰒱", "bar": true,  "hotkey": "alt - s", "yabai_rule": "^Slack$" },
    { "id": "Discord", "app": "Discord", "icon": "󰙯", "bar": true,  "hotkey": "alt - d", "yabai_rule": "^Discord$" },
    { "id": "Mail",    "app": "Mail",    "icon": "󰇮", "bar": true,  "hotkey": "alt - m", "yabai_rule": "^Mail$" },
    { "id": "Music",   "app": "Music",   "icon": "",  "bar": false, "hotkey": "alt - a", "yabai_rule": "^Music$" }
  ],
  "widgets": {
    "clock": { "enabled": true },
    "battery": { "enabled": true },
    "volume": { "enabled": true },
    "cpu_ram": { "enabled": true },
    "theme": { "enabled": true, "shortcut": "Toggle Appearance" },
    "media": { "enabled": true, "app": "Music" },
    "todos": [
      { "id": "todo.personal", "icon": "󰋜", "file": "~/Notes/Personal/Todos.md" },
      { "id": "todo.work",     "icon": "󰃖", "file": "~/Notes/Work/Todos.md" }
    ],
    "lock_animation": { "enabled": true }
  },
  "hotkeys": {
    "focus_pinned": "alt - g",
    "maximize": "alt - f"
  },
  "session_status_file": null
}
```

### Todo file format

Each entry in `widgets.todos` reads a Markdown checklist with optional `## Doing`, `## To-do`, and `## Completed` headings (`- [ ]` open items, `- [x]` completed items). Set `"todos": []` to disable the todo widget group.

## Default Keybindings

| Shortcut | Action |
| --- | --- |
| `alt + 1` … `0` | Focus general spaces 1–10 (Mission Control spaces 2–11) and sync display 2 mirror |
| `shift + alt + 1` … `0` | Send focused window to space 1–10 (toggle primary ↔ mirror if already there) |
| `alt + g` / `shift + alt + g` | Focus pinned space (space 1) / send focused window to space 1 |
| `alt + s / d / b / a / v` | Bring pinned app forward on space 1 (configurable in `pinned_apps`) |
| `alt + n` / `alt + r` / `alt + w` | Create named space / rename current space / destroy current space |
| `shift + alt + h` / `l` | Move current space (and its mirror) left / right |
| `alt + f` | Maximize focused window vertically below the bar |
| `alt + left-click drag` | Move window under cursor |
| `alt + right-click drag` | Resize window under cursor (two-finger click-drag on trackpad) |
| `ctrl + alt + cmd + r` | Restart yabai |

## Development & Testing

- **`make test`** — runs lint checks (`config.default.json` validation, `skhd/skhdrc` sync check, `bash -n`, `shellcheck`), builds helpers, compiles all `spacesd` sources with `-Wall -Wextra -Werror`, runs `spacesd` C unit tests, and runs `scripts/live_test.sh`.
- **Headless macOS VM tests (`make agent` / `make proofs`)** — optional end-to-end verification in a headless [Tart](https://github.com/cirruslabs/tart) macOS VM running real `yabai` + scripting addition + `spacesd` + `skhd` + `sketchybar` without touching your host desktop. See [docs/agent-vm.md](docs/agent-vm.md).

## License

MIT — see [LICENSE](LICENSE). Vendors [jsmn](https://github.com/zserge/jsmn) (MIT). Built on [yabai](https://github.com/koekeishiya/yabai), [SketchyBar](https://github.com/FelixKratz/SketchyBar), [skhd](https://github.com/koekeishiya/skhd), and [Catppuccin Mocha](https://github.com/catppuccin/catppuccin).
