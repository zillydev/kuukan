#!/usr/bin/env sh

SHORTCUT="${1:-Toggle Appearance}"
DARK_COLOR="${2:-0xffcba6f7}"
LIGHT_COLOR="${3:-0xfff9e2af}"

# Toggle light/dark on left click via a macOS Shortcut (avoids Automation TCC grants on ad-hoc sketchybar).
if [ "$BUTTON" = "left" ] && [ -n "$SHORTCUT" ]; then
  # Bound runtime to ~8s so a wedged `shortcuts run` never stalls sketchybar's per-item queue.
  shortcuts run "$SHORTCUT" &
  p=$!
  i=0; while kill -0 "$p" 2>/dev/null && [ "$i" -lt 80 ]; do sleep 0.1; i=$((i+1)); done
  kill -0 "$p" 2>/dev/null && kill "$p" 2>/dev/null
  sleep 0.2
fi

# Read effective appearance via the compiled Swift helper (accurate under Auto, needs no TCC grant).
if [ "$(~/.config/sketchybar/plugins/appearance)" = "Dark" ]; then
  sketchybar --set "$NAME" icon="󰖔" icon.color="$DARK_COLOR"
else
  sketchybar --set "$NAME" icon="󰖙" icon.color="$LIGHT_COLOR"
fi
