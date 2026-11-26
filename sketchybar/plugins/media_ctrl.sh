#!/usr/bin/env bash

COVER_FILE="${TMPDIR:-/tmp}/sketchybar_cover.jpg"
MEDIA_APP="${1:-YouTube Music}"

next() { media-control next-track; }
back() { media-control previous-track; }
play() { media-control toggle-play-pause; }

update() {
  local data title artist album playing artwork display play_icon label_icon tmp
  data=$(media-control get --now 2>/dev/null)

  if [[ -z "$data" || "$data" == "(null)" ]]; then
    sketchybar --set media.anchor drawing=off
    return
  fi

  {
    IFS= read -r title
    IFS= read -r artist
    IFS= read -r album
    IFS= read -r playing
    IFS= read -r artwork
  } < <(jq -r '(.title // ""), (.artist // ""), (.album // ""), ((.playing // false) | tostring), (.artworkData // "")' <<<"$data")

  if [[ -z "$title" ]]; then
    sketchybar --set media.anchor drawing=off
    return
  fi

  display="$title - $artist"
  if ((${#display} > 25)); then
    display="${display:0:22}..."
  fi

  if [[ "$playing" == "true" ]]; then
    play_icon="󰏤"
    label_icon="󰎈"
  else
    play_icon="󰐊"
    label_icon="󰏤"
  fi

  if [[ -n "$artwork" ]]; then
    tmp="${COVER_FILE}.tmp.$$"
    if base64 --decode <<<"$artwork" > "$tmp" 2>/dev/null; then
      mv -f "$tmp" "$COVER_FILE"
    else
      rm -f "$tmp"
    fi
  fi

  local args=(--set media.anchor drawing=on label="$label_icon $display")
  if [[ -z "$artist" ]]; then
    args+=(--set media.title label="$title"
           --set media.album label="Podcast"
           --set media.artist label="$album")
  else
    args+=(--set media.title label="$title"
           --set media.album label="$album"
           --set media.artist label="$artist")
  fi

  args+=(--set media.play icon="$play_icon"
         --set media.cover background.image="$COVER_FILE"
                           background.color=0x00000000)

  sketchybar "${args[@]}"
}

mouse_clicked() {
  case "$NAME" in
    "media.next") next ;;
    "media.back") back ;;
    "media.play") play ;;
    "media.cover")
      [[ -n "$MEDIA_APP" ]] && open -a "$MEDIA_APP"
      sketchybar --set media.anchor popup.drawing=off
      return
      ;;
    *) return ;;
  esac
  sleep 0.3
  update
}

case "$SENDER" in
  "mouse.clicked") mouse_clicked ;;
  "mouse.exited.global") sketchybar --set media.anchor popup.drawing=off ;;
  "routine"|"forced") update ;;
  *) update ;;
esac
