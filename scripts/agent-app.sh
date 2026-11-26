#!/usr/bin/env bash
# The hidden agent copy: a headless tart macOS VM per checkout, cloned from wm-golden, running this checkout's
# configs and spacesd with the host's binaries (docs/agent-vm.md). Drive it with scripts/ui.
#
#   start [--spacesd DIR] [--keep] [--wait S] [--window]   clone + boot + install + start services (--keep reuses
#       this checkout's VM; --wait S waits for a free VM slot; --window shows it to the user: make dev-guided)
#   sync [--spacesd DIR]             re-stage the configs and binaries and restart the services (seconds)
#   restart                          stop, then start fresh
#   stop [--keep]                    shut the guest down and delete the VM (--keep keeps it for the next start)
#   status | logs [svc] [n] | paths | clean
set -euo pipefail
# shellcheck source=vm/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/vm/lib.sh"

SPACESD_DIR="${SPACESD_DIR:-$ROOT/spacesd}"
DISPLAY_SIZE="${AGENT_DISPLAY:-1512x982}"
MEMORY_MB="${AGENT_MEMORY_MB:-4096}"
# A VM nobody has driven for this long is stopped: a forgotten one holds its memory and one of two slots.
IDLE_MIN="${AGENT_IDLE_MIN:-30}"

# Prints why no VM can start now, or fails when one can.
slot_busy() {
  local vms
  vms="$(running_vms)"
  # A clone taken while the golden image is booted would copy a disk in mid-write.
  if grep -qx "$GOLDEN" <<<"$vms"; then
    echo "$GOLDEN is booted for provisioning (make golden); start when it has shut down, or pass --wait SECONDS"
  elif [[ $(grep -c . <<<"$vms") -ge 2 ]]; then
    echo "two macOS VMs are already running, which is Apple's limit: $(paste -sd';' - <<<"$vms"). Wait for one to stop (start --wait SECONDS does) or ask the user: another session's VM is not yours to stop"
  else
    return 1
  fi
}

stage() {
  # A fresh directory per stage: the guest's virtio-fs view keeps a deleted-and-recreated
  # directory's old (empty) handle, so a payload is never rebuilt in place.
  PAYLOAD="payload-$(date +%s)-$RANDOM"
  local p="$SHARE/$PAYLOAD"
  make -C "$ROOT" -s build
  find "$SHARE" -maxdepth 1 -name 'payload-*' -exec rm -rf {} +
  mkdir -p "$p/bin" "$p/config" "$p/spacesd"
  cp "$(readlink -f "$(command -v yabai)")" "$(readlink -f "$(command -v sketchybar)")" \
    "$(readlink -f "$(command -v skhd)")" "$ROOT/build/guestinput" "$p/bin/"
  for x in sketchybar yabai skhd; do cp -R "$ROOT/$x" "$p/config/$x"; done
  cp "$ROOT/config.default.json" "$p/config.default.json"
  cp "$ROOT/scripts/render-skhdrc.sh" "$p/render-skhdrc.sh"
  cp -R "$ROOT/fixtures" "$p/fixtures"
  # The main checkout's spacesd/bin/spacesd is what launchd runs: building there would deploy it.
  if [[ "$(cd "$SPACESD_DIR" && pwd -P)" != "$(cd "$MAIN/spacesd" && pwd -P)" ]]; then
    make -C "$SPACESD_DIR" -s bin/spacesd >/dev/null
  fi
  [[ -x "$SPACESD_DIR/bin/spacesd" ]] || die "no spacesd build at $SPACESD_DIR/bin/spacesd"
  cp -R "$SPACESD_DIR/bin" "$SPACESD_DIR/scripts" "$p/spacesd/"
  cp "$ROOT/scripts/vm/runner.sh" "$SHARE/"
  cp "$ROOT/scripts/vm/guest-up.sh" "$p/"
  echo "$SPACESD_DIR" >"$STATE/spacesd"
}

up_services() {
  guest_exec 120 <<<"bash \"/Volumes/My Shared Files/agent/$PAYLOAD/guest-up.sh\" $PAYLOAD" || die "services did not start; scripts/agent-app.sh logs"
}

cmd_start() {
  local keep="" wait_s=0 busy waited=0 stamp graphics="--no-graphics"
  while [[ $# -gt 0 ]]; do
    case $1 in
      --spacesd) SPACESD_DIR="$(cd "$2" && pwd)"; shift 2 ;;
      --keep) keep=1; shift ;;
      --wait) wait_s=$2; shift 2 ;;
      --window) graphics=""; shift ;;
      *) die "unknown option $1" ;;
    esac
  done
  # A window on the user's screen is theirs to open, so a coding agent's shell is turned away.
  if [[ -z "$graphics" && -n "${CLAUDECODE:-}${ANTIGRAVITY_AGENT:-}${ANTIGRAVITY_CONVERSATION_ID:-}${CODEX_SANDBOX:-}${CODEX_CI:-}${OPENCODE:-}${CURSOR_AGENT:-}${T3CODE_THREAD_ID:-}" ]]; then
    die "start --window opens a window on the user's screen; give them: make -C $ROOT dev-guided"
  fi
  vm_running && { echo "agent: $VM already running (scripts/agent-app.sh status)"; return 0; }
  command -v tart >/dev/null || die "tart is not installed (brew install cirruslabs/cli/tart)"
  tart get "$GOLDEN" >/dev/null 2>&1 || die "no $GOLDEN VM; see docs/agent-vm.md"
  while busy="$(slot_busy)"; do
    ((waited < wait_s)) || die "$busy"
    sleep 5
    waited=$((waited + 5))
  done

  mkdir -p "$SHARE/q" "$SHARE/out" "$STATE/shots"
  rm -f "$SHARE"/q/* "$SHARE"/out/*
  echo "$ROOT" >"$STATE/root"
  if [[ -z "$keep" ]] || ! tart get "$VM" >/dev/null 2>&1; then
    tart delete "$VM" >/dev/null 2>&1 || true
    tart clone "$GOLDEN" "$VM"
    tart set "$VM" --display "$DISPLAY_SIZE" --memory "$MEMORY_MB"
  fi
  stage
  # --no-graphics with no VNC: the VZ VNC server crashes tart.
  # shellcheck disable=SC2086
  nohup tart run "$VM" $graphics --dir="agent:$SHARE" >"$STATE/tart.log" 2>&1 &
  echo $! >"$STATE/app.pid"
  echo "$MEMORY_MB" >"$STATE/app.mem"
  : >"$STATE/used"
  # A window is the user's to close: they drive it by hand, which the idle clock cannot see.
  if ((IDLE_MIN > 0)) && [[ -n "$graphics" ]]; then
    nohup "$ROOT/scripts/agent-app.sh" reap >>"$STATE/reaper.log" 2>&1 &
    echo $! >"$STATE/reaper.pid"
  fi
  local i
  for ((i = 0; i < 180; i++)); do
    vm_running || die "tart exited: $(tail -3 "$STATE/tart.log")"
    echo true >"$SHARE/q/ping.sh"
    sleep 1
    [[ -e "$SHARE/out/ping.done" ]] && break
  done
  rm -f "$SHARE"/out/ping.* "$SHARE"/q/ping.*
  ((i < 180)) || die "the guest runner never answered; is $GOLDEN provisioned (make golden)?"
  stamp="$(guest_exec 10 <<<'cat ~/.agent-golden 2>/dev/null' || true)"
  [[ "$stamp" == "$(golden_stamp)" ]] || echo "agent: $GOLDEN was provisioned by another scripts/vm/provision-golden.sh (${stamp:-no stamp}; this checkout's is $(golden_stamp)). The image is shared by every checkout: make golden from the checkout whose version it should be." >&2
  up_services
  echo "agent: $VM up ($ROOT, spacesd from $SPACESD_DIR)"
  [[ -n "$graphics" ]] || echo "agent: this checkout's desktop is in the tart window. Close it with: make -C $ROOT stop"
}

cmd_sync() {
  vm_running || die "not running (scripts/agent-app.sh start)"
  [[ "${1:-}" == --spacesd ]] && SPACESD_DIR="$(cd "$2" && pwd)" || SPACESD_DIR="$(cat "$STATE/spacesd" 2>/dev/null || echo "$SPACESD_DIR")"
  stage
  up_services
}

cmd_stop() {
  local reaper
  reaper="$(cat "$STATE/reaper.pid" 2>/dev/null || true)"
  [[ -z "$reaper" || "$reaper" == "$$" ]] || kill "$reaper" 2>/dev/null || true
  if vm_running; then
    guest_exec 15 <<<'(sleep 1; sudo shutdown -h now) >/dev/null 2>&1 &' >/dev/null || true
    local pid i
    pid="$(cat "$STATE/app.pid")"
    for ((i = 0; i < 60; i++)); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
    kill -0 "$pid" 2>/dev/null && { tart stop "$VM" --timeout 10 >/dev/null 2>&1 || kill "$pid"; }
  fi
  rm -f "$STATE/app.pid" "$STATE/app.mem" "$STATE/reaper.pid"
  [[ "${1:-}" == --keep ]] || tart delete "$VM" >/dev/null 2>&1 || true
  echo "agent: stopped $VM"
}

cmd_reap() {
  local idle
  while vm_running; do
    sleep 60
    idle=$(($(date +%s) - $(stat -f %m "$STATE/used" 2>/dev/null || date +%s)))
    if ((idle >= IDLE_MIN * 60)); then
      echo "$(date '+%F %T') not driven for $((idle / 60))m: stopping $VM"
      cmd_stop
      return
    fi
  done
}

cmd_status() {
  echo "VMs running: $(running_vms | paste -sd';' -)"
  if vm_running; then
    echo "running: $VM (pid $(cat "$STATE/app.pid")), checkout $ROOT, spacesd $(cat "$STATE/spacesd" 2>/dev/null)"
    guest_exec 10 <<<'for s in yabai spacesd skhd sketchybar; do pgrep -x $s >/dev/null && echo "  $s up" || echo "  $s DOWN"; done'
  else
    echo "stopped: $VM"
  fi
}

cmd_logs() {
  local svc=${1:-} n=${2:-40}
  vm_running || die "not running"
  if [[ -n "$svc" ]]; then
    guest_exec 10 <<<"tail -n $n ~/agent-logs/$svc.log"
  else
    guest_exec 10 <<<"for f in ~/agent-logs/*.log; do echo \"== \$f\"; tail -n $n \"\$f\"; done"
  fi
}

case ${1:-} in
  start) shift; cmd_start "$@" ;;
  sync) shift; cmd_sync "$@" ;;
  restart) shift; cmd_stop; cmd_start "$@" ;;
  stop) shift; cmd_stop "$@" ;;
  status) cmd_status ;;
  reap) cmd_reap ;;
  logs) shift; cmd_logs "$@" ;;
  paths) echo "vm=$VM state=$STATE share=$SHARE shots=$STATE/shots golden=$GOLDEN" ;;
  clean) cmd_stop; rm -rf "$STATE" ;;
  *) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
