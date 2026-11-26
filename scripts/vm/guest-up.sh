#!/bin/bash
# Runs inside the agent VM: installs the payload agent-app.sh staged in the share (this checkout's configs,
# the host's yabai/sketchybar/skhd binaries, a spacesd build) and (re)starts the four services.
set -u
P="/Volumes/My Shared Files/agent/${1:?usage: guest-up.sh PAYLOAD_DIR}"
L="$HOME/agent-logs"
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
mkdir -p "$L"

pkill -x sketchybar; pkill -x skhd; pkill -x spacesd; pkill -x yabai
# A config run of the killed bar outlives it and would be taken for the new bar's.
pkill -f "sketchybar/sketchybarrc"
# Windows the golden image reopens at login; the snaps should show only what the services do.
pkill -x TextEdit; pkill -x Terminal
sleep 1

sudo install -m 755 "$P"/bin/* /usr/local/bin/
for x in sketchybar yabai skhd; do
  rm -rf "$HOME/.config/$x"
  cp -R "$P/config/$x" "$HOME/.config/$x"
done
rm -rf "$HOME/Obsidian" && cp -R "$P/fixtures/obsidian" "$HOME/Obsidian"
mkdir -p "$HOME/.config/kuukan"
cp "$P/config.default.json" "$HOME/.config/kuukan/config.default.json"
SD="$HOME/.config/kuukan/spacesd"
rm -rf "$SD" && mkdir -p "$SD" && cp -R "$P/spacesd/bin" "$P/spacesd/scripts" "$SD/"
bash "$P/render-skhdrc.sh" -o "$HOME/.config/skhd/skhdrc"

# TCC checks each binary itself, not the runner that started it. Keyed by signing identifier, not cdhash,
# so a grant survives the host rebuilding the binary.
grant() { # path, services...
  local path=$1 id req svc; shift
  id=$(codesign -dv "$path" 2>&1 | sed -n 's/^Identifier=//p')
  req=$(echo "identifier \"$id\"" | csreq -r - -b /dev/stdout | xxd -p | tr -d '\n')
  for svc in "$@"; do
    # tccd can hold the database lock; without a busy timeout sqlite3 gives up at once.
    sudo sqlite3 -cmd '.timeout 10000' "/Library/Application Support/com.apple.TCC/TCC.db" \
      "INSERT OR REPLACE INTO access (service,client,client_type,auth_value,auth_reason,auth_version,csreq,flags) VALUES ('$svc','$path',1,2,4,1,X'$req',0);" \
      || { echo "TCC grant of $svc to $path failed"; exit 1; }
  done
}
grant /usr/local/bin/yabai kTCCServiceAccessibility
grant /usr/local/bin/skhd kTCCServiceAccessibility kTCCServiceListenEvent kTCCServicePostEvent
grant /usr/local/bin/sketchybar kTCCServiceAccessibility kTCCServiceScreenCapture
grant "$SD/bin/spacesd" kTCCServiceAccessibility kTCCServicePostEvent
grant /usr/local/bin/guestinput kTCCServiceAccessibility kTCCServicePostEvent
sudo killall tccd 2>/dev/null; killall tccd 2>/dev/null
sleep 1

start() { # name, then the command
  local name=$1; shift
  nohup "$@" >"$L/$name.log" 2>&1 &
}

start yabai yabai
for _ in $(seq 1 50); do [ -S "/tmp/yabai_$USER.socket" ] && [ -S "/tmp/yabai-sa_$USER.socket" ] && break; sleep 0.2; done
[ -S "/tmp/yabai-sa_$USER.socket" ] || { echo "yabai: scripting addition socket missing"; tail -20 "$L/yabai.log"; exit 1; }
start spacesd "$SD/bin/spacesd"
start skhd skhd
start sketchybar sketchybar
for _ in $(seq 1 50); do sketchybar --query bar >/dev/null 2>&1 && break; sleep 0.2; done
sketchybar --query bar >/dev/null 2>&1 || { echo "sketchybar did not come up"; tail -20 "$L/sketchybar.log"; exit 1; }
# The bar answers before its config has finished adding items: ready means this bar's sketchybarrc
# has started and exited.
bar=$(pgrep -x sketchybar)
rc_running() { pgrep -P "$bar" -f sketchybarrc >/dev/null; }
for _ in $(seq 1 25); do rc_running && break; sleep 0.2; done
for _ in $(seq 1 150); do rc_running || break; sleep 0.2; done
rc_running && { echo "sketchybarrc still running after 30s"; exit 1; }
# Snaps should show only the bar: hide the desktop widgets, and clear the "App Background Activity" banner
# BTM posts for the runner at each clone's login (dismissing it does not mark it notified, so golden can't).
if [ "$(defaults read com.apple.WindowManager StandardHideWidgets 2>/dev/null)" != 1 ]; then
  defaults write com.apple.WindowManager StandardHideWidgets -int 1 && killall WindowManager
fi
guestinput clear-notifications >/dev/null || exit 1
# A headless VM's pointer rests at the top-left corner, where it reveals the autohidden menu bar;
# last, since restarting WindowManager shows it again.
guestinput move 512 500
for svc in yabai spacesd skhd sketchybar; do
  pgrep -x "$svc" >/dev/null && echo "$svc running" || { echo "$svc NOT running"; tail -20 "$L/$svc.log"; exit 1; }
done
