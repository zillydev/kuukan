#!/usr/bin/env bash
# alt+f stretches the focused window from under the bar to the bottom of the screen, keeping its
# column, and shift+alt+2 sends it to the second general space.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

win() { scripts/ui run "yabai -m query --windows | jq -rc '.[] | select(.app == \"TextEdit\") | $1'"; }

scripts/ui run 'while [ "$(yabai -m query --spaces | jq length)" -lt 3 ]; do yabai -m space --create || exit 1; done'
scripts/ui key alt+1
scripts/ui until 'test "$(yabai -m query --spaces --space | jq .index)" = 2'
scripts/ui run 'open -a TextEdit'
scripts/ui until --timeout 30 'yabai -m query --windows | jq -e "any(.[]; .app == \"TextEdit\" and .space == 2 and .\"has-focus\")"' >/dev/null
before="$(win '"\(.frame.x) \(.frame.w)"')"

scripts/ui key alt+f
scripts/ui until 'yabai -m query --windows | jq -e --argjson h "$(yabai -m query --displays --display | jq .frame.h)" "any(.[]; .app == \"TextEdit\" and .frame.y == 39 and .frame.h == \$h - 39)"' >/dev/null \
  || { echo "alt+f: frame is $(win '.frame')"; exit 1; }
[[ "$(win '"\(.frame.x) \(.frame.w)"')" == "$before" ]] || { echo "alt+f moved the window sideways: $before -> $(win '"\(.frame.x) \(.frame.w)"')"; exit 1; }
echo "ok alt+f -> full height under the bar"

scripts/ui key shift+alt+2
scripts/ui until 'yabai -m query --windows | jq -e "any(.[]; .app == \"TextEdit\" and .space == 3)"' >/dev/null || { echo "shift+alt+2: window on space $(win .space)"; exit 1; }
echo "ok shift+alt+2 -> window on space 3"
