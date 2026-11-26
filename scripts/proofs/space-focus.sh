#!/usr/bin/env bash
# alt+1 / alt+2 go through the guest's skhd to spacesd, which focuses the space via yabai's scripting
# addition, and alt+g goes to the pinned space: the whole hotkey path on a real Dock.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

on() { scripts/ui until "test \"\$(yabai -m query --spaces --space | jq .index)\" = $1" || { echo "$2: not on space $1"; exit 1; }; }

scripts/ui run 'while [ "$(yabai -m query --spaces | jq length)" -lt 3 ]; do yabai -m space --create || exit 1; done'
for step in "alt+1 2" "alt+2 3" "alt+1 2" "alt+g 1" "alt+2 3"; do
  read -r chord want <<<"$step"
  scripts/ui key "$chord"
  on "$want" "$chord"
  echo "ok $chord -> space $want"
done
