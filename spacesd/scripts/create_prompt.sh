#!/usr/bin/env sh
# UI prompt stays outside the daemon (spec §7). Default alt-n binding.
# osascript exits non-zero on Cancel/Esc (-128), which aborts; empty OK creates an unnamed space.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPACESD="${SPACESD:-$SCRIPT_DIR/../bin/spacesd}"
OSASCRIPT="${OSASCRIPT:-/usr/bin/osascript}"
if ! NAME=$("$OSASCRIPT" -e 'Tell application "System Events" to display dialog "Space name:" default answer ""' -e 'text returned of result' 2>/dev/null); then
  exit 0
fi
if [ -n "$NAME" ]; then exec "$SPACESD" -m create "$NAME"; else exec "$SPACESD" -m create; fi
