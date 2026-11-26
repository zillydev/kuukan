#!/usr/bin/env sh

FONT="${1:-JetBrainsMono Nerd Font}"
MUTED_COLOR="${2:-0xfff38ba8}"
ACTIVE_COLOR="${3:-0xff89b4fa}"

read -r VOLUME MUTED <<EOF
$(osascript -e 'set v to get volume settings' -e '(output volume of v as text) & " " & (output muted of v as text)' 2>/dev/null)
EOF
VOLUME="${VOLUME:-0}"

BT_CACHE="${TMPDIR:-/tmp}/sketchybar_bt_headset"
if [ "$SENDER" != "volume_change" ] || [ ! -f "$BT_CACHE" ]; then
  BT_HEADSET=$(system_profiler SPBluetoothDataType -json -detailLevel basic 2>/dev/null | \
    jq -r '.SPBluetoothDataType[0].device_connected[]? | to_entries[] | select(.value.device_minorType == "Headset" or .value.device_minorType == "Headphones") | .key' 2>/dev/null | head -1)
  printf '%s' "$BT_HEADSET" > "$BT_CACHE"
else
  BT_HEADSET=$(cat "$BT_CACHE" 2>/dev/null)
fi

if [ -n "$BT_HEADSET" ]; then
  ICON="󰋋"
  FONT_SIZE="17.0"
elif [ "$VOLUME" -gt 50 ]; then
  ICON="󰕾"
  FONT_SIZE="20.0"
elif [ "$VOLUME" -gt 0 ]; then
  ICON="󰖀"
  FONT_SIZE="20.0"
else
  ICON="󰝟"
  FONT_SIZE="20.0"
fi

if [ "$MUTED" = "true" ]; then
  sketchybar --set "$NAME" icon="󰝟" icon.color="$MUTED_COLOR" icon.font="$FONT:Medium:$FONT_SIZE"
else
  sketchybar --set "$NAME" icon="$ICON" icon.color="$ACTIVE_COLOR" icon.font="$FONT:Medium:$FONT_SIZE"
fi
