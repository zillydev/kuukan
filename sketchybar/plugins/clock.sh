#!/usr/bin/env bash

if [[ "$SENDER" == "mouse.exited.global" ]]; then
  sketchybar --set clock popup.drawing=off
  exit 0
fi

NOW_LABEL=$(date '+%a %b %d %H:%M')
YM=$(date '+%Y-%m')
CAL_CACHE="${TMPDIR:-/tmp}/sketchybar_cal_ym"

# Redraw calendar rows only on startup/forced update or month rollover.
if [[ "$SENDER" == "routine" && -f "$CAL_CACHE" && "$(cat "$CAL_CACHE")" == "$YM" ]]; then
  sketchybar --set "$NAME" label="$NOW_LABEL"
  exit 0
fi

args=(--set "$NAME" label="$NOW_LABEL")
i=0
while IFS= read -r line; do
  if ((i <= 7)) && [[ -n "$line" ]]; then
    args+=(--set "cal.$i" drawing=on label="$line")
    i=$((i + 1))
  fi
done < <(cal -h | awk '
  NR == 1 { gsub(/^[[:space:]]+|[[:space:]]+$/, ""); print; next }
  NR == 2 { sub(/[[:space:]]+$/, ""); print; next }
  /[^[:space:]]/ {
    row = ""
    for (c = 0; c < 7; c++) {
      cell = substr($0, c * 3 + 1, 2)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", cell)
      row = row (c ? " " : "") (cell == "" ? " ." : sprintf("%2s", cell))
    }
    print row
  }
')

while ((i <= 7)); do
  args+=(--set "cal.$i" drawing=off)
  i=$((i + 1))
done

sketchybar "${args[@]}"
printf '%s' "$YM" > "$CAL_CACHE"
