# shellcheck shell=bash
# Sourced by scripts/agent-app.sh, scripts/ui and scripts/prove: where this checkout's agent VM lives
# and how to run a command in it.
ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
MAIN="$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")"
ID="$(printf %s "$ROOT" | shasum | cut -c1-8)"
# proj ps and proj stop find agent copies under /tmp/devkit-*/ (app.pid, root).
STATE="/tmp/devkit-kuukan$ID"
SHARE="$STATE/share"
VM="wm-$ID"
GOLDEN="${AGENT_GOLDEN:-wm-golden}"

die() { echo "agent: $*" >&2; exit 1; }

# The version of the golden image's provisioning this checkout would apply.
golden_stamp() { shasum "$ROOT/scripts/vm/provision-golden.sh" | cut -c1-12; }

# running_vms: one line per running tart VM, "NAME (checkout, idle Nm)" for an agent copy.
running_vms() {
  local name state
  tart list --format json | python3 -I -c 'import json, sys; [print(v["Name"]) for v in json.load(sys.stdin) if v["State"] == "running"]' |
    while read -r name; do
      state="/tmp/devkit-kuukan${name#wm-}"
      if [[ -f "$state/root" ]]; then
        echo "$name ($(cat "$state/root"), idle $((($(date +%s) - $(stat -f %m "$state/used" 2>/dev/null || date +%s)) / 60))m)"
      else
        echo "$name"
      fi
    done
}

vm_running() { [[ -f "$STATE/app.pid" ]] && kill -0 "$(cat "$STATE/app.pid")" 2>/dev/null; }

# guest_exec TIMEOUT_S < script: runs the script in the guest as the logged-in user, prints its output,
# returns its exit code (124 on timeout).
guest_exec() {
  local timeout=${1:-60} id i
  id="c$(date +%s)$RANDOM"
  # Every command counts as use: a VM nobody drives for a while is stopped (agent-app.sh reap).
  : >"$STATE/used"
  { echo "#timeout $timeout"; cat; } >"$SHARE/q/$id.tmp" && mv "$SHARE/q/$id.tmp" "$SHARE/q/$id.sh"
  # The runner ends the job at the timeout; the margin lets its answer cross the share.
  for ((i = 0; i < (timeout + 5) * 5; i++)); do
    [[ -e "$SHARE/out/$id.done" ]] && break
    sleep 0.2
  done
  cat "$SHARE/out/$id.txt" 2>/dev/null
  if [[ -e "$SHARE/out/$id.done" ]]; then
    local rc
    rc="$(cat "$SHARE/out/$id.done")"
    rm -f "$SHARE/out/$id.txt" "$SHARE/out/$id.done"
    return "$rc"
  fi
  echo "agent: no reply from the guest within ${timeout}s" >&2
  return 124
}
