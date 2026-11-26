#!/usr/bin/env bash
# The bar loads in the agent VM with its widgets drawn and spacesd painting the space strip.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

for item in clock cpu_percent ram_percent volume app.Slack app.Discord app.Beeper theme todo.work todo.personal media.anchor; do
  scripts/ui bar --query "$item" | grep -q '"drawing": "on"' || { echo "item $item missing or not drawing"; exit 1; }
  echo "ok $item"
done
# spacesd paints the space items; space.2 is the first general space (space 1 is the pinned one, no item).
scripts/ui until 'sketchybar --query space.2 | jq -e ".icon.value == \"1\" and .geometry.drawing == \"on\""' >/dev/null
echo "ok space.2 (spacesd)"
