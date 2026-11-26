#!/usr/bin/env bash
# scripts/live.sh on a throwaway repo, with stand-ins for the commands that touch the desktop, and the
# spacesd makefile's refusal to build the binary launchd runs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
bad=0
ok() { printf 'ok    %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; bad=1; }
# expect NAME PATTERN... : every pattern is in $out, and a pattern starting with ! is not.
expect() {
  local name=$1 p miss=""
  shift
  for p in "$@"; do
    if [[ "$p" == !* ]]; then ! grep -qE -- "${p#!}" <<<"$out" || miss+=" has:${p#!}"; else grep -qE -- "$p" <<<"$out" || miss+=" lacks:$p"; fi
  done
  if [[ -z "$miss" ]]; then ok "$name"; else fail "$name ($miss)"$'\n'"$out"; fi
}
# Stand-ins for the commands that would touch the desktop: each logs its call and fails when named by FAIL.
mkdir -p "$T/bin" "$T/agents" "$T/cfg"
for c in make launchctl yabai sketchybar skhd; do printf '#!/bin/sh\necho "$0 $*" >>"%s/ran"\n[ "$(basename "$0")" != "${FAIL:-}" ]\n' "$T" >"$T/bin/$c"; chmod +x "$T/bin/$c"; done
real() { out="$(PATH="$T/bin:$PATH" LIVE_MAIN="$T/repo" LIVE_AGENTS="$T/agents" LIVE_CONFIG_DIR="$T/cfg" "$ROOT/scripts/live.sh" "$@" 2>&1)"; }
live() { out="$(LIVE_MAIN="$T/repo" LIVE_DRY=1 LIVE_AGENTS="$T/agents" LIVE_CONFIG_DIR="$T/cfg" "$ROOT/scripts/live.sh" "$@" 2>&1)"; }
g() { git -C "$T/repo" "$@"; }
commit() { g add -A && g -c user.name=t -c user.email=t@example.com commit -q --no-gpg-sign -m "$1"; }

mkdir -p "$T/repo"/{sketchybar/plugins,yabai,skhd,spacesd/src,spacesd/launchd,docs}
git -C "$T" init -q -b main repo
for f in sketchybar/sketchybarrc sketchybar/plugins/appearance.swift yabai/yabairc yabai/maximize.sh skhd/skhdrc \
  spacesd/src/spacesd.c docs/notes.md; do echo one >"$T/repo/$f"; done
echo '<string>@SPACESD_BIN@</string>' >"$T/repo/spacesd/launchd/dev.zilly.spacesd.plist.in"
echo /worktrees/ >"$T/repo/.gitignore"
commit base
branch() { # name, files it changes
  local name=$1 f
  shift
  g checkout -q -b "$name" main
  for f in "$@"; do echo "$name" >>"$T/repo/$f"; done
  commit "$name"
  g checkout -q main
}
branch bar sketchybar/sketchybarrc
branch daemon spacesd/src/spacesd.c
branch helper sketchybar/plugins/appearance.swift
branch wm yabai/yabairc skhd/skhdrc
branch plist spacesd/launchd/dev.zilly.spacesd.plist.in
branch docs docs/notes.md
# A branch that is checked out in a worktree, as every feature branch is.
g worktree add -q "$T/repo/worktrees/held" -b held main
echo held >"$T/repo/worktrees/held/sketchybar/sketchybarrc"
git -C "$T/repo/worktrees/held" add -A
git -C "$T/repo/worktrees/held" -c user.name=t -c user.email=t@example.com commit -q --no-gpg-sign -m held

live try held
expect "dry try: lists the steps" 'would: sketchybar --reload' '!launchctl' '!yabai'
[[ "$(g branch --show-current)" == main && ! -e "$T/repo/.git/live-commit" ]] && ok "dry try: leaves the checkout alone" || fail "dry try: leaves the checkout alone"
real try held
expect "try: a branch a worktree holds" 'sketchybar --reload' 'Return with: make -C .* back'
[[ "$(g rev-parse HEAD)" == "$(g rev-parse held)" && -z "$(g branch --show-current)" ]] && ok "try: detached at the branch" || fail "try: detached at the branch"
live back
expect "dry back: reloads the bar again" 'would: sketchybar --reload'
real back
expect "back: on main" 'back on main'
[[ "$(g branch --show-current)" == main ]] && ok "back: the checkout is on main" || fail "back: the checkout is on main"
rm -f "$T/repo/.git/live-commit"

live try daemon
expect "try: daemon source" 'would: make -C .*/spacesd bin/spacesd DEPLOY=1' 'would: launchctl kickstart -k gui/[0-9]+/dev.zilly.spacesd' '!sketchybar --reload'
live try helper
expect "try: a plugin helper" 'would: make -C .*/repo build' 'would: sketchybar --reload'
live try wm
expect "try: yabairc and skhdrc" 'would: yabai --restart-service' 'would: skhd --reload' '!sketchybar'
live try plist
expect "try: the launchd plist" 'would: render .*dev.zilly.spacesd.plist.in' 'would: launchctl bootout' 'would: launchctl bootstrap gui/[0-9]+ ' '!kickstart'
real try plist
grep -q "<string>$T/repo/spacesd/bin/spacesd</string>" "$T/agents/dev.zilly.spacesd.plist" \
  && ok "plist: @SPACESD_BIN@ rendered to checkout path" || fail "plist: @SPACESD_BIN@ not rendered"
real back
rm -f "$T/repo/.git/live-commit"
live try docs
expect "try: nothing the desktop runs" 'nothing on the desktop changes' '!would:'

live deploy
expect "deploy: no record of the last one applies everything" 'would: make -C .*/repo build' 'would: make -C .*/spacesd bin/spacesd' 'would: yabai --restart-service' 'would: sketchybar --reload'
g rev-parse HEAD >"$T/repo/.git/live-commit"
g -c user.name=t -c user.email=t@example.com merge -q --no-gpg-sign --squash daemon >/dev/null 2>&1 && commit "merge daemon"
live deploy
expect "deploy: after a merge, only what it changed" 'would: make -C .*/spacesd bin/spacesd DEPLOY=1' 'would: launchctl kickstart' '!yabai' '!sketchybar --reload'

echo dirty >>"$T/repo/yabai/yabairc"
if live try bar; then fail "try: refuses a dirty live checkout"; else expect "try: refuses a dirty live checkout" 'uncommitted changes'; fi
g checkout -q -- yabai/yabairc
if live try nope; then fail "try: unknown branch"; else expect "try: unknown branch" 'no branch or commit nope'; fi
if live try; then fail "try: needs a branch"; else expect "try: needs a branch" 'usage: make -C'; fi

# A step that fails stops the rest, and back undoes what ran.
g branch -q both main && g checkout -q both && echo both >"$T/repo/spacesd/src/spacesd.c" && echo both >"$T/repo/sketchybar/sketchybarrc" && commit both && g checkout -q main
rm -f "$T/repo/.git/live-commit"
if FAIL=make real try both; then fail "try: a failed build stops"; else expect "try: a failed build stops" 'applying it failed' 'make -C .* back' '!kickstart' '!sketchybar --reload'; fi
: >"$T/ran"
real back
out="$(cat "$T/ran")"
expect "back: after a failed try, undoes the whole branch" 'make -C .*/spacesd bin/spacesd DEPLOY=1' 'launchctl kickstart' 'sketchybar --reload'
real try docs
real back
[[ "$(cat "$T/repo/.git/live-commit")" == "$(g rev-parse main)" ]] && ok "the record follows the checkout" || fail "the record follows the checkout"

# Named as the live binary, this checkout's spacesd must refuse to build without DEPLOY=1.
if out="$(make -C "$ROOT/spacesd" -B bin/spacesd LIVE_BIN="$ROOT/spacesd/bin/spacesd" 2>&1)"; then
  fail "makefile: built the live binary without DEPLOY=1"
else
  expect "makefile: refuses the live binary without DEPLOY=1" 'launchd runs' '!clang'
fi

exit $bad
