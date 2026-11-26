#!/usr/bin/env sh

BAR_HEIGHT="${1:-39}"
NOTCH_WIDTH="${2:-200}"

lock() {
  sketchybar --bar y_offset="-$BAR_HEIGHT" \
                   margin=-200 \
                   notch_width=0 \
                   blur_radius=0 \
                   color=0x00000000
}

unlock() {
  sketchybar --animate sin 25 \
             --bar y_offset=0 \
                   notch_width="$NOTCH_WIDTH" \
                   margin=0 \
                   corner_radius=20 \
                   corner_radius=20 \
                   corner_radius=20 \
                   corner_radius=0 \
                   color=0x00000000
}

case "$SENDER" in
  "lock") lock ;;
  "unlock") unlock ;;
esac
