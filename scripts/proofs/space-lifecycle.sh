#!/usr/bin/env bash
# A named space is created, moved left and right with shift+alt+h/l (its name travels with it) and
# destroyed with alt+w. The name dialogs of alt+n/alt+r are out of the VM's reach; create is what they run.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

count() { scripts/ui run 'yabai -m query --spaces | jq length'; }
on() { scripts/ui until "test \"\$(yabai -m query --spaces --space | jq .index)\" = $1" || { echo "$2: not on space $1"; exit 1; }; }
named() { scripts/ui until "sketchybar --query space.$1 | jq -e '.icon.value | endswith(\" proofspace\")'" >/dev/null || { echo "$2: space.$1 does not carry the name"; exit 1; }; }

scripts/ui run 'while [ "$(yabai -m query --spaces | jq length)" -lt 3 ]; do yabai -m space --create || exit 1; done'
n="$(count)"
scripts/ui spacesd create proofspace
last=$((n + 1))
on "$last" create
named "$last" create
echo "ok create -> space $last, named"

scripts/ui key shift+alt+h
on "$n" "shift+alt+h"
named "$n" "shift+alt+h"
echo "ok shift+alt+h -> space $n, name moved with it"
scripts/ui key shift+alt+l
on "$last" "shift+alt+l"
named "$last" "shift+alt+l"
echo "ok shift+alt+l -> space $last"

scripts/ui key alt+w
scripts/ui until "test \"\$(yabai -m query --spaces | jq length)\" = $n" || { echo "alt+w: still $(count) spaces"; exit 1; }
scripts/ui until '! spacesd -m status | grep -q proofspace' || { echo "alt+w: the name outlived the space"; exit 1; }
echo "ok alt+w -> $n spaces, name pruned"
