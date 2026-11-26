#!/usr/bin/env bash
# Clicking a todo widget opens its popup with the fixture checklist's open items and none of the done
# ones, and clicking the clock opens this month's calendar.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

open_popup() {
  scripts/ui bar-click "$1"
  scripts/ui until "sketchybar --query $1 | jq -e '.popup.drawing == \"on\"'" >/dev/null || { echo "$1: the popup did not open"; exit 1; }
}
todos() { # item, fixture checklist
  local shown line
  open_popup "$1"
  shown="$(scripts/ui run "for i in 0 1 2 3 4 5 6 7 8 9; do sketchybar --query $1.item.\$i | jq -r 'select(.geometry.drawing == \"on\") | .label.value'; done")"
  while IFS= read -r line; do
    grep -qxF "$line" <<<"$shown" || { echo "$1: \"$line\" is not in the popup, which shows: $(tr '\n' '|' <<<"$shown")"; exit 1; }
  done < <(sed -n 's/^- \[ \] //p' "$2")
  while IFS= read -r line; do
    if grep -qxF "$line" <<<"$shown"; then echo "$1: the done item \"$line\" is in the popup"; exit 1; fi
  done < <(sed -n 's/^- \[x\] //p' "$2")
  echo "ok $1 shows $(grep -c . <<<"$shown") open items"
}

todos todo.work "fixtures/obsidian/Work/Todos.md"
scripts/ui snap --bar proof-popup-todo
todos todo.personal "fixtures/obsidian/Personal/Todos.md"

open_popup clock
scripts/ui until 'test "$(sketchybar --query cal.0 | jq -r .label.value)" = "$(date "+%B %Y")"' || { echo "clock: the calendar is not this month's"; exit 1; }
echo "ok clock shows this month"
scripts/ui snap --bar proof-popup-clock
