#!/usr/bin/env sh

read -r VOLUME MUTED <<EOF
$(osascript -e 'set v to get volume settings' -e '(output volume of v as text) & " " & (output muted of v as text)' 2>/dev/null)
EOF

DEVICE=$(system_profiler SPAudioDataType 2>/dev/null | awk '
  /^[[:space:]]+[^:]+:[[:space:]]*$/ { dev = $0; sub(/^[[:space:]]+/, "", dev); sub(/:[[:space:]]*$/, "", dev) }
  /Default Output Device: Yes/ { print dev; exit }
')

BT_BATT=$(~/.config/sketchybar/plugins/bt_battery 2>/dev/null | jq -r '.[0].battery // empty')
BATT_STR=""
if [ -n "$BT_BATT" ]; then
  BATT_STR=" 󰁹${BT_BATT}%"
fi

if [ "$MUTED" = "true" ]; then
  VOL_LABEL="Muted ($DEVICE)$BATT_STR"
else
  VOL_LABEL="${VOLUME}% $DEVICE$BATT_STR"
fi

read -r PERCENTAGE CHARGING REMAINING <<EOF
$(pmset -g batt | awk '
  /AC Power/ { charging = 1 }
  match($0, /[0-9]+%/) && pct == "" { pct = substr($0, RSTART, RLENGTH) }
  match($0, /[0-9]+:[0-9]+/) && rem == "" { rem = substr($0, RSTART, RLENGTH) }
  END { print (pct == "" ? "-" : pct), (charging + 0), rem }
')
EOF

if [ "$PERCENTAGE" = "-" ]; then
  PERCENTAGE=""
fi
if [ "$CHARGING" -gt 0 ]; then
  BATT_LABEL="${PERCENTAGE:+${PERCENTAGE} }Charging${REMAINING:+ ($REMAINING remaining)}"
else
  BATT_LABEL="${PERCENTAGE:-N/A}${REMAINING:+ ($REMAINING remaining)}"
fi

sketchybar --set status.volume label="$VOL_LABEL" \
           --set status.battery label="$BATT_LABEL"
