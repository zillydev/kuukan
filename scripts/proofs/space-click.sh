#!/usr/bin/env bash
# Clicking a space item on the bar focuses that space, and the highlight follows: the pointer path
# through sketchybar's click script to spacesd.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

on() { scripts/ui until "test \"\$(yabai -m query --spaces --space | jq .index)\" = $1" || { echo "$2: not on space $1"; exit 1; }; }
colour() { scripts/ui run "sketchybar --query space.$1 | jq -r .geometry.background.color"; }

scripts/ui run 'while [ "$(yabai -m query --spaces | jq length)" -lt 4 ]; do yabai -m space --create || exit 1; done'
for n in 3 4 2; do
  scripts/ui bar-click "space.$n"
  on "$n" "click space.$n"
  echo "ok click space.$n -> space $n"
done
# Selected now, so its background must differ from an unselected item's, and swap when another is clicked.
scripts/ui until 'test "$(sketchybar --query space.2 | jq -r .geometry.background.color)" != "$(sketchybar --query space.3 | jq -r .geometry.background.color)"' >/dev/null
selected="$(colour 2)"
scripts/ui bar-click space.3
on 3 "click space.3"
scripts/ui until "test \"\$(sketchybar --query space.3 | jq -r .geometry.background.color)\" = $selected && test \"\$(sketchybar --query space.2 | jq -r .geometry.background.color)\" != $selected" \
  || { echo "the highlight did not move to space.3"; exit 1; }
echo "ok highlight follows the click"
scripts/ui move 512 400
