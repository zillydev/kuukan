#!/usr/bin/env sh
TOTAL=$(sysctl -n hw.memsize)
PAGE_SIZE=$(sysctl -n hw.pagesize)
PERCENT=$(vm_stat | awk -v total="$TOTAL" -v page="$PAGE_SIZE" '
  /Pages active/                 { gsub(/\./, "", $3); used += $3 }
  /Pages wired down/             { gsub(/\./, "", $4); used += $4 }
  /Pages occupied by compressor/ { gsub(/\./, "", $5); used += $5 }
  END { printf "%.0f", (used * page / total) * 100 }
')
sketchybar --set ram_percent label="${PERCENT}%"
