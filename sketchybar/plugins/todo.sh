#!/usr/bin/env bash
# Usage: todo.sh [FILE]. The bar item name comes from sketchybar's $NAME.

ITEM="${NAME:-todo}"
TODO_FILE="${1:-}"
DOING_MAX=5
TODO_MAX=5
ROW_HEIGHT=22
POPUP_PADDING=16

if [[ "$SENDER" == "mouse.exited.global" ]]; then
  sketchybar --set "$ITEM" popup.drawing=off
  exit 0
fi

if [[ ! -f "$TODO_FILE" ]]; then
  args=(--set "$ITEM" icon.color=0xff585b70 popup.height=40
        --set "$ITEM.section.doing" drawing=off
        --set "$ITEM.section.todo" drawing=off
        --set "$ITEM.summary" drawing=off)
  for i in 0 1 2 3 4 5 6 7 8 9; do
    args+=(--set "$ITEM.item.$i" drawing=off)
  done
  sketchybar "${args[@]}"
  exit 0
fi

doing=()
todos=()
total_done=0

# Single-pass Markdown parse: pre-heading tasks belong to To-do/Completed.
while IFS=$'\t' read -r kind text; do
  case "$kind" in
    D) doing+=("$text") ;;
    T) todos+=("$text") ;;
    C) total_done="$text" ;;
  esac
done < <(awk -v dmax="$DOING_MAX" -v tmax="$TODO_MAX" '
  function clean(s) {
    sub(/^[[:space:]]*- \[ \] /, "", s)
    while (match(s, /\[[^]]+\]\([^)]+\)/)) {
      seg = substr(s, RSTART, RLENGTH)
      sub(/^\[/, "", seg)
      sub(/\]\([^)]+\)$/, "", seg)
      s = substr(s, 1, RSTART - 1) seg substr(s, RSTART + RLENGTH)
    }
    return s
  }
  BEGIN { sec = "" }
  /^##[[:space:]]/ {
    sec = $0
    sub(/^##[[:space:]]+/, "", sec)
    sub(/[[:space:]]+$/, "", sec)
    next
  }
  /^[[:space:]]*- \[ \]/ {
    if (sec == "Doing" && dc < dmax) { print "D\t" clean($0); dc++ }
    else if ((sec == "" || sec == "To-do") && tc < tmax) { print "T\t" clean($0); tc++ }
  }
  /^[[:space:]]*- \[x\]/ {
    if (sec == "" || sec == "Completed") done++
  }
  END { printf "C\t%d\n", done + 0 }
' "$TODO_FILE")

args=()
visible=()

if ((${#doing[@]} > 0)); then
  visible+=("$ITEM.section.doing")
  for ((i = 0; i < ${#doing[@]}; i++)); do
    args+=(--set "$ITEM.item.$i" label="${doing[$i]}")
    visible+=("$ITEM.item.$i")
  done
else
  args+=(--set "$ITEM.section.doing" drawing=off)
fi
for ((i = ${#doing[@]}; i < DOING_MAX; i++)); do
  args+=(--set "$ITEM.item.$i" drawing=off)
done

if ((${#todos[@]} > 0)); then
  visible+=("$ITEM.section.todo")
  for ((i = 0; i < ${#todos[@]}; i++)); do
    slot=$((5 + i))
    args+=(--set "$ITEM.item.$slot" label="${todos[$i]}")
    visible+=("$ITEM.item.$slot")
  done
else
  args+=(--set "$ITEM.section.todo" drawing=off)
fi
for ((i = ${#todos[@]}; i < TODO_MAX; i++)); do
  args+=(--set "$ITEM.item.$((5 + i))" drawing=off)
done

visible+=("$ITEM.summary")
total_rows=${#visible[@]}
top_y=$(( (total_rows - 1) * ROW_HEIGHT / 2 ))
for ((idx = 0; idx < total_rows; idx++)); do
  y=$(( top_y - idx * ROW_HEIGHT ))
  args+=(--set "${visible[$idx]}" drawing=on "y_offset=$y")
done

total_pending=$((${#doing[@]} + ${#todos[@]}))
icon_color="0xff585b70"
((total_pending > 0)) && icon_color="0xffa6e3a1"
popup_height=$((total_rows * ROW_HEIGHT + POPUP_PADDING))

args+=(--set "$ITEM.summary" label="$total_pending pending · $total_done done"
       --set "$ITEM" "icon.color=$icon_color" "popup.height=$popup_height")

sketchybar "${args[@]}"
