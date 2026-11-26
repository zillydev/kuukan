#!/usr/bin/env sh

CFG="${KUUKAN_CONFIG:-$HOME/.config/kuukan/config.json}"
if [ -f "$CFG" ]; then
  BAR_HEIGHT=$(jq -r '.bar.height // 39' "$CFG" 2>/dev/null)
fi
BAR_HEIGHT="${BAR_HEIGHT:-39}"

WIN=$(yabai -m query --windows --window 2>/dev/null)
[ -n "$WIN" ] || exit 0

DISPLAY_IDX=$(printf '%s' "$WIN" | jq -r '.display // empty')
[ -n "$DISPLAY_IDX" ] || exit 0

DISPLAY=$(yabai -m query --displays --display "$DISPLAY_IDX" 2>/dev/null)
[ -n "$DISPLAY" ] || exit 0

read -r NEW_X NEW_Y NEW_W NEW_H <<EOF
$(jq -rn --argjson w "$WIN" --argjson d "$DISPLAY" --argjson bar "$BAR_HEIGHT" \
  '[($w.frame.x | round), (($d.frame.y + $bar) | round), ($w.frame.w | round), (($d.frame.h - $bar) | round)] | @tsv')
EOF

[ -n "$NEW_W" ] && [ -n "$NEW_H" ] || exit 0
yabai -m window --resize "abs:${NEW_W}:${NEW_H}"
yabai -m window --move "abs:${NEW_X}:${NEW_Y}"
