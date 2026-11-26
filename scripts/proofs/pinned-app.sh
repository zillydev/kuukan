#!/usr/bin/env bash
# A pinned app's hotkey brings its window on space 1 forward from another space.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

scripts/ui run 'cat >~/.config/kuukan/config.json <<'\''EOF'\''
{
  "pinned_apps": [
    { "id": "TextEdit", "app": "TextEdit", "icon": "󰈙", "bar": true, "hotkey": "alt - t", "yabai_rule": "^TextEdit$" }
  ]
}
EOF'
scripts/agent-app.sh sync

scripts/ui run 'open -a TextEdit'
scripts/ui until --timeout 30 'yabai -m query --windows | jq -e "any(.[]; .app == \"TextEdit\" and .space == 1)"' >/dev/null \
  || { echo "TextEdit window did not land on space 1"; exit 1; }
scripts/ui key alt+1
scripts/ui until 'test "$(yabai -m query --spaces --space | jq .index)" = 2' >/dev/null

scripts/ui key alt+t
scripts/ui until 'yabai -m query --windows --window | jq -e ".app == \"TextEdit\" and .space == 1"' >/dev/null \
  || { echo "alt+t: focused $(scripts/ui run 'yabai -m query --windows --window | jq -c "{app,space}"'); $(scripts/ui logs spacesd 5)"; exit 1; }
scripts/ui logs spacesd 20 | grep -q 'app: TextEdit (wid=' || { echo "spacesd did not find TextEdit's window"; exit 1; }
echo "ok alt+t -> TextEdit focused on space 1"
