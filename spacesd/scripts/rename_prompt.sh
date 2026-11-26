#!/usr/bin/env sh
# Default alt-r binding. Cancel/Esc and empty OK both no-op; clear a name with `spacesd -m rename ""`.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPACESD="${SPACESD:-$SCRIPT_DIR/../bin/spacesd}"
OSASCRIPT="${OSASCRIPT:-/usr/bin/osascript}"
if ! NAME=$("$OSASCRIPT" -e 'Tell application "System Events" to display dialog "Rename space:" default answer ""' -e 'text returned of result' 2>/dev/null); then
  exit 0
fi
[ -n "$NAME" ] && exec "$SPACESD" -m rename "$NAME"
exit 0
