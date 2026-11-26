#!/usr/bin/env sh

if [ "$SENDER" = "mouse.exited.global" ]; then
  sketchybar --set battery popup.drawing=off
  exit 0
fi

read -r PERCENTAGE CHARGING <<EOF
$(pmset -g batt | awk '
  /AC Power/ { charging = 1 }
  match($0, /[0-9]+%/) && pct == "" { pct = substr($0, RSTART, RLENGTH - 1) }
  END { print (pct == "" ? "-" : pct), (charging + 0) }
')
EOF

if [ "$PERCENTAGE" = "-" ] || [ -z "$PERCENTAGE" ]; then
  exit 0
fi

if [ "$CHARGING" -gt 0 ]; then
  ICON="󰂄"
  COLOR="0xffa6e3a1"
elif [ "$PERCENTAGE" -ge 90 ]; then
  COLOR="0xffa6e3a1"
  ICON="󰁹"
elif [ "$PERCENTAGE" -ge 70 ]; then
  COLOR="0xfff9e2af"
  ICON="󰂁"
elif [ "$PERCENTAGE" -ge 40 ]; then
  COLOR="0xfffab387"
  ICON="󰁿"
elif [ "$PERCENTAGE" -ge 10 ]; then
  COLOR="0xfff38ba8"
  ICON="󰁻"
else
  COLOR="0xfff38ba8"
  ICON="󰂎"
fi

sketchybar --set "$NAME" icon="$ICON" icon.color="$COLOR" label="${PERCENTAGE}%"

# Refresh detail popup only while it is visible (click_script handles opening).
if [ "$(sketchybar --query battery 2>/dev/null | jq -r '.popup.drawing')" = "on" ]; then
  ~/.config/sketchybar/plugins/status_detail.sh &
fi
