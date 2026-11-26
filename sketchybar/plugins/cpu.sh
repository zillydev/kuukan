#!/usr/bin/env sh
CORE_COUNT=$(sysctl -n machdep.cpu.thread_count)
PERCENT=$(ps -eo pcpu= | awk -v cores="$CORE_COUNT" '{sum += $1} END {printf "%.0f", sum / cores}')
sketchybar --set cpu_percent label="${PERCENT}%"
