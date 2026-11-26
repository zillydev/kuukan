#!/bin/bash
# The guest end of the mailbox (the host cannot reach the guest by network), run at login by the LaunchAgent
# dev.zilly.agent-runner: q/<id>.sh is run, its output goes to out/<id>.txt and its exit code to out/<id>.done.
D="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$D/q" "$D/out"
# Job control puts each job in its own process group, so one that overruns is ended with its children
# and the next command still gets an answer. The limit is the job's first line, "#timeout SECONDS".
set -m
while true; do
  for f in "$D"/q/*.sh; do
    [ -e "$f" ] || continue
    id=$(basename "$f" .sh)
    mv "$f" "$D/q/$id.run"
    limit=$(sed -n '1s/^#timeout \([0-9][0-9]*\)$/\1/p' "$D/q/$id.run")
    bash "$D/q/$id.run" >"$D/out/$id.txt" 2>&1 &
    job=$!
    (sleep "${limit:-600}"; touch "$D/q/$id.late"; kill -TERM -- "-$job"; sleep 2; kill -KILL -- "-$job") >/dev/null 2>&1 &
    dog=$!
    wait "$job"
    rc=$?
    if [ -e "$D/q/$id.late" ]; then
      rc=124
      echo "runner: stopped after ${limit:-600}s" >>"$D/out/$id.txt"
    else
      kill -TERM -- "-$dog" 2>/dev/null
    fi
    echo $rc >"$D/out/$id.done"
    rm -f "$D/q/$id.run" "$D/q/$id.late"
  done
  sleep 0.2
done
