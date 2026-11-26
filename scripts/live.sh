#!/usr/bin/env bash
# Moves the live desktop, which runs the main checkout, to a commit and applies what that changes.
# The user's to run (make try BRANCH=<branch>, make back, make deploy), never an agent's.
set -euo pipefail

MAIN="${LIVE_MAIN:-$(dirname "$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --path-format=absolute --git-common-dir)")}"
# The commit the desktop was last brought up to, so each run applies only what changed since.
STAMP="$(git -C "$MAIN" rev-parse --path-format=absolute --git-common-dir)/live-commit"
SERVICE="gui/$(id -u)/dev.zilly.spacesd"
AGENTS="${LIVE_AGENTS:-$HOME/Library/LaunchAgents}"
KUUKAN_CFG_DIR="${LIVE_CONFIG_DIR:-$HOME/.config/kuukan}"

usage() {
  cat >&2 <<'EOF'
live.sh try BRANCH   check BRANCH out in the main checkout (detached) and apply it
live.sh back         return the main checkout to main and apply that
live.sh deploy       apply the main checkout as it stands (after a merge)
Applying rebuilds a changed helper, rebuilds and restarts a changed spacesd, restarts yabai for a changed
yabairc, and reloads skhd and the bar for their changed configs. LIVE_DRY=1 prints those steps and changes nothing, the checkout included.
EOF
  exit 1
}
die() { echo "live: $*" >&2; exit 1; }
record() { [[ -n "${LIVE_DRY:-}" ]] || echo "$1" >"$STAMP"; }
run() { if [[ -n "${LIVE_DRY:-}" ]]; then echo "would: $*"; else echo "+ $*"; "$@"; fi; }

render_plist() {
  local commit=$1
  local src="$MAIN/spacesd/launchd/dev.zilly.spacesd.plist.in"
  local dst="$AGENTS/dev.zilly.spacesd.plist"
  if git -C "$MAIN" cat-file -e "$commit:spacesd/launchd/dev.zilly.spacesd.plist.in" 2>/dev/null; then
    if [[ -n "${LIVE_DRY:-}" ]]; then
      echo "would: render $src -> $dst"
    else
      echo "+ render $src -> $dst"
      mkdir -p "$AGENTS"
      sed "s|@SPACESD_BIN@|$MAIN/spacesd/bin/spacesd|g" "$src" >"$dst"
    fi
  else
    run cp "$MAIN/spacesd/launchd/dev.zilly.spacesd.plist" "$dst"
  fi
}

clean() {
  local dirty
  dirty="$(git -C "$MAIN" status --porcelain --untracked-files=no)"
  [[ -z "$dirty" ]] || die "$MAIN has uncommitted changes, so someone is working in the live checkout:"$'\n'"$dirty"
}

applied() {
  local sha
  sha="$(cat "$STAMP" 2>/dev/null)" && git -C "$MAIN" cat-file -e "$sha^{commit}" 2>/dev/null && echo "$sha"
}

# apply OLD NEW: brings the desktop from commit OLD to the checked-out NEW; an empty OLD applies everything.
# It stops at the first step that fails, leaving the record of what the desktop is on unchanged.
apply() {
  local old=$1 new=$2 changed did=""
  if [[ -n "$old" ]]; then changed="$(git -C "$MAIN" diff --name-only "$old" "$new")"; else changed="$(git -C "$MAIN" ls-files)"; fi
  has() { grep -qE "$1" <<<"$changed"; }

  if [[ -z "${LIVE_DRY:-}" ]]; then
    mkdir -p "$KUUKAN_CFG_DIR"
    ln -sfn "$MAIN/spacesd" "$KUUKAN_CFG_DIR/spacesd"
    [[ -f "$MAIN/config.default.json" ]] && ln -sfn "$MAIN/config.default.json" "$KUUKAN_CFG_DIR/config.default.json"
  fi

  if has '^sketchybar/plugins/[^/]+\.(swift|plist)$'; then
    run make -C "$MAIN" build || return 1
    if has '^sketchybar/plugins/bt_battery\.'; then echo "live: bt_battery was rebuilt; macOS may ask for its Bluetooth access again"; fi
    did=1
  fi
  if has '^spacesd/(src/|vendor/|makefile$)'; then
    run make -C "$MAIN/spacesd" bin/spacesd DEPLOY=1 || return 1
  fi
  # kickstart does not re-read a plist, so a changed one is unloaded and loaded again.
  if has '^spacesd/launchd/'; then
    render_plist "$new" || return 1
    run launchctl bootout "$SERVICE" || true
    if [[ -z "${LIVE_DRY:-}" ]]; then
      local i=0
      while launchctl print "$SERVICE" >/dev/null 2>&1 && ((i < 30)); do
        sleep 0.1
        i=$((i + 1))
      done
    fi
    run launchctl bootstrap "${SERVICE%/*}" "$AGENTS/dev.zilly.spacesd.plist" || return 1
    did=1
  elif has '^spacesd/(src/|vendor/|makefile$)'; then
    run launchctl kickstart -k "$SERVICE" || return 1
    did=1
  fi
  if has '^(yabai/yabairc|config\.default\.json)$'; then run yabai --restart-service || return 1; did=1; fi
  if has '^(sketchybar/|config\.default\.json$)'; then run sketchybar --reload || return 1; did=1; fi
  # skhd watches ~/.config/skhd/skhdrc; regenerate if linked to kuukan's rendered skhdrc, then reload.
  if has '^(skhd/|config\.default\.json$|scripts/render-skhdrc\.sh$)'; then
    if [[ -x "$MAIN/scripts/render-skhdrc.sh" && -f "$MAIN/config.default.json" ]]; then
      run "$MAIN/scripts/render-skhdrc.sh" -o "$KUUKAN_CFG_DIR/skhdrc" || return 1
    fi
    run skhd --reload || return 1
    did=1
  fi
  [[ -n "$did" ]] || echo "live: nothing on the desktop changes between ${old:0:7} and ${new:0:7}"
  record "$new"
}

# move TARGET [--detach]: checks TARGET out in the main checkout and applies the difference.
move() {
  local old new
  clean
  old="$(applied || git -C "$MAIN" rev-parse HEAD)"
  new="$(git -C "$MAIN" rev-parse "$1^{commit}")"
  [[ -n "${LIVE_DRY:-}" ]] || git -C "$MAIN" checkout -q "$@"
  if ! apply "$old" "$new"; then
    # Half applied counts as applied, so the way back undoes all of it.
    record "$new"
    die "the live checkout is on $(git -C "$MAIN" describe --all --always HEAD) but applying it failed; fix that, or return with: make -C $MAIN back"
  fi
}

case ${1:-} in
  try)
    [[ -n "${2:-}" ]] || die "usage: make -C $MAIN try BRANCH=<branch>"
    sha="$(git -C "$MAIN" rev-parse -q --verify "$2^{commit}")" || die "no branch or commit $2"
    move "$sha" --detach
    [[ -n "${LIVE_DRY:-}" ]] || echo "live: the desktop is on $2 (${sha:0:7}). Return with: make -C $MAIN back"
    ;;
  back)
    move main
    [[ -n "${LIVE_DRY:-}" ]] || echo "live: the desktop is back on main"
    ;;
  deploy)
    clean
    apply "$(applied || true)" "$(git -C "$MAIN" rev-parse HEAD)" || die "applying main failed; fix that and run it again"
    ;;
  *) usage ;;
esac
