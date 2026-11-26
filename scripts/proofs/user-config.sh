#!/usr/bin/env bash
# User config (~/.config/kuukan/config.json) overrides bar widgets, pinned apps, space colours, and hotkeys.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

scripts/ui run 'cat >~/.config/kuukan/config.json <<'\''EOF'\''
{
  "bar": {
    "colors": {
      "mauve": "0xff89b4fa"
    }
  },
  "pinned_apps": [
    { "id": "Notes", "app": "Notes", "icon": "󰠮", "bar": true, "hotkey": "alt - n" }
  ],
  "widgets": {
    "cpu_ram": { "enabled": false },
    "todos": [
      { "id": "todo.custom", "icon": "󰄱", "file": "~/Obsidian/Personal/Todos.md" }
    ]
  },
  "hotkeys": {
    "focus_pinned": "alt - p",
    "create_space": ""
  }
}
EOF'

scripts/agent-app.sh sync

# Disabled widget is absent; custom pinned app and custom todo widget are drawn.
if scripts/ui bar --query cpu_percent >/dev/null 2>&1; then
  echo "cpu_percent still present when widgets.cpu_ram.enabled=false"
  exit 1
fi
echo "ok cpu_ram disabled"

for item in app.Notes todo.custom; do
  scripts/ui bar --query "$item" | grep -q '"drawing": "on"' || { echo "custom item $item missing"; exit 1; }
  echo "ok $item drawn"
done
if scripts/ui bar --query app.Slack >/dev/null 2>&1; then
  echo "default app.Slack still present after pinned_apps override"
  exit 1
fi
echo "ok default pinned_apps replaced"

# Focus space 2 (alt+1): spacesd highlights it with the custom bar.colors.mauve colour.
scripts/ui key alt+1
scripts/ui until 'test "$(yabai -m query --spaces --space | jq .index)" = 2' >/dev/null
scripts/ui until 'sketchybar --query space.2 | jq -e ".geometry.background.color == \"0xff89b4fa\""' >/dev/null \
  || { echo "space.2 did not pick up custom mauve colour: $(scripts/ui bar --query space.2)"; exit 1; }
echo "ok spacesd uses custom palette"

# Rendered skhdrc picks up custom focus_pinned binding (alt - p -> space 1).
scripts/ui key alt+p
scripts/ui until 'test "$(yabai -m query --spaces --space | jq .index)" = 1' >/dev/null \
  || { echo "custom alt+p hotkey did not focus space 1"; exit 1; }
echo "ok custom hotkey alt+p -> space 1"

scripts/ui snap --bar proof-user-config
