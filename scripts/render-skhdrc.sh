#!/usr/bin/env bash
# Generates an skhdrc from config.default.json overlaid with ~/.config/kuukan/config.json (or KUUKAN_CONFIG).
# Usage: render-skhdrc.sh [--default] [-o OUTFILE]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
DEFAULT_CFG="$SCRIPT_DIR/../config.default.json"
[[ -f "$DEFAULT_CFG" ]] || DEFAULT_CFG="$HOME/.config/kuukan/config.default.json"

use_default=""
out_file=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --default) use_default=1; shift ;;
    -o) out_file="$2"; shift 2 ;;
    *) echo "usage: render-skhdrc.sh [--default] [-o OUTFILE]" >&2; exit 1 ;;
  esac
done

USER_CFG="${KUUKAN_CONFIG:-$HOME/.config/kuukan/config.json}"
if [[ -z "$use_default" && -f "$USER_CFG" ]]; then
  CFG="$(jq -s '.[0] * .[1]' "$DEFAULT_CFG" "$USER_CFG")"
else
  CFG="$(cat "$DEFAULT_CFG")"
fi

# shellcheck disable=SC2088 # emitted literally into skhdrc for skhd to expand at runtime
SPACESD='~/.config/kuukan/spacesd/bin/spacesd'
# shellcheck disable=SC2088
PROMPTS='~/.config/kuukan/spacesd/scripts'

render() {
  printf '# Generated from kuukan config — edit ~/.config/kuukan/config.json and run `make apply`.\n\n'

  local bl
  bl="$(jq -r '.hotkeys.blacklist // [] | .[] | "    \"" + . + "\""' <<<"$CFG")"
  if [[ -n "$bl" ]]; then
    printf '.blacklist [\n%s\n]\n\n' "$bl"
  fi

  local focus_pinned send_pinned focus_mod send_mod maximize move_left move_right create_sp rename_sp destroy_sp restart_yabai
  {
    IFS= read -r focus_pinned
    IFS= read -r send_pinned
    IFS= read -r focus_mod
    IFS= read -r send_mod
    IFS= read -r maximize
    IFS= read -r move_left
    IFS= read -r move_right
    IFS= read -r create_sp
    IFS= read -r rename_sp
    IFS= read -r destroy_sp
    IFS= read -r restart_yabai
  } < <(jq -r '
    .hotkeys | (
      (.focus_pinned // ""),
      (.send_pinned // ""),
      (.focus_space_mod // ""),
      (.send_space_mod // ""),
      (.maximize // ""),
      (.move_space_left // ""),
      (.move_space_right // ""),
      (.create_space // ""),
      (.rename_space // ""),
      (.destroy_space // ""),
      (.restart_yabai // "")
    )
  ' <<<"$CFG")

  printf '# ===== Pinned space & apps =====\n'
  [[ -n "$focus_pinned" ]] && printf '%s : yabai -m space --focus 1\n' "$focus_pinned"
  local hk app
  while IFS=$'\t' read -r hk app; do
    [[ -n "$hk" && -n "$app" ]] && printf '%s : %s -m app "%s"\n' "$hk" "$SPACESD" "$app"
  done < <(jq -r '.pinned_apps // [] | .[] | select((.hotkey // "") != "" and (.app // "") != "") | [.hotkey, .app] | @tsv' <<<"$CFG")
  printf '\n'

  if [[ -n "$focus_mod" ]]; then
    printf '# ===== Focus spaces (syncs mirror on secondary display) =====\n'
    local i key
    for i in 1 2 3 4 5 6 7 8 9 10; do
      key=$((i % 10))
      printf '%s - %d : %s -m focus %d\n' "$focus_mod" "$key" "$SPACESD" "$((i + 1))"
    done
    printf '\n'
  fi

  if [[ -n "$send_mod" ]]; then
    printf '# ===== Move window to space (toggle: primary -> mirror) =====\n'
    local i key
    for i in 1 2 3 4 5 6 7 8 9 10; do
      key=$((i % 10))
      printf '%s - %d : %s -m send %d\n' "$send_mod" "$key" "$SPACESD" "$((i + 1))"
    done
    printf '\n'
  fi

  printf '# ===== Windows & spaces =====\n'
  [[ -n "$maximize" ]] && printf '%s : ~/.config/yabai/maximize.sh\n' "$maximize"
  [[ -n "$send_pinned" ]] && printf '%s : app=$(yabai -m query --windows --window | jq -r '\''.app'\''); yabai -m window --space 1; yabai -m space --focus 1; open -a "$app"\n' "$send_pinned"
  [[ -n "$move_left" ]] && printf '%s : %s -m move left\n' "$move_left" "$SPACESD"
  [[ -n "$move_right" ]] && printf '%s : %s -m move right\n' "$move_right" "$SPACESD"
  [[ -n "$create_sp" ]] && printf '%s : %s/create_prompt.sh\n' "$create_sp" "$PROMPTS"
  [[ -n "$rename_sp" ]] && printf '%s : %s/rename_prompt.sh\n' "$rename_sp" "$PROMPTS"
  [[ -n "$destroy_sp" ]] && printf '%s : %s -m destroy\n' "$destroy_sp" "$SPACESD"
  [[ -n "$restart_yabai" ]] && printf '%s : yabai --restart-service\n' "$restart_yabai"
}

if [[ -n "$out_file" ]]; then
  mkdir -p "$(dirname "$out_file")"
  tmp="${out_file}.tmp.$$"
  render >"$tmp"
  mv -f "$tmp" "$out_file"
else
  render
fi
