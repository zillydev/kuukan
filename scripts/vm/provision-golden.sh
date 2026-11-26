#!/usr/bin/env bash
# Idempotent setup of wm-golden, the VM every agent copy clones, after the recovery-mode steps in
# docs/agent-vm.md. Passwordless sudo is for yabairc's `sudo yabai --load-sa`.
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
VM="$GOLDEN"
STATE="/tmp/devkit-kuukan-golden"
SHARE="$STATE/share"

tart get "$VM" >/dev/null 2>&1 || die "no $VM VM; see docs/agent-vm.md"
vms="$(running_vms)"
if grep -qx "$VM" <<<"$vms"; then die "$VM is already running: another make golden is in progress"; fi
[[ $(grep -c . <<<"$vms") -lt 2 ]] || die "two macOS VMs are already running ($(paste -sd';' - <<<"$vms")); provisioning needs one of the two slots"
mkdir -p "$SHARE/q" "$SHARE/out" "$SHARE/fonts"
rm -f "$SHARE"/q/* "$SHARE"/out/*
cp "$ROOT/scripts/vm/runner.sh" "$SHARE/"
cp ~/Library/Fonts/JetBrainsMonoNerdFont-*.ttf "$SHARE/fonts/"
cat >"$SHARE/dev.zilly.agent-runner.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>dev.zilly.agent-runner</string>
<key>ProgramArguments</key><array><string>/bin/bash</string><string>-c</string>
<string>until [ -f "/Volumes/My Shared Files/agent/runner.sh" ]; do sleep 1; done; exec /bin/bash "/Volumes/My Shared Files/agent/runner.sh"</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
<key>LimitLoadToSessionType</key><string>Aqua</string>
</dict></plist>
EOF

nohup tart run "$VM" --no-graphics --dir="agent:$SHARE" >"$STATE/tart.log" 2>&1 &
pid=$!
for ((i = 0; i < 180; i++)); do
  kill -0 $pid 2>/dev/null || die "tart exited: $(tail -3 "$STATE/tart.log")"
  echo true >"$SHARE/q/ping.sh"; sleep 1
  [[ -e "$SHARE/out/ping.done" ]] && break
done
((i < 180)) || die "the guest runner never answered"
rm -f "$SHARE"/out/ping.* "$SHARE"/q/ping.*

guest_exec 1800 <<'EOF'
set -e
S="/Volumes/My Shared Files/agent"
echo admin | sudo -S sh -c 'echo "admin ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/agent && chmod 440 /etc/sudoers.d/agent'
sudo mkdir -p /usr/local/bin
mkdir -p ~/Library/Fonts && cp "$S"/fonts/*.ttf ~/Library/Fonts/
# The host's copies are quarantined downloads, and macOS does not activate a quarantined font.
xattr -dr com.apple.quarantine ~/Library/Fonts
cp "$S/dev.zilly.agent-runner.plist" ~/Library/LaunchAgents/
# The runner is /bin/bash under launchd, so TCC checks bash: input posting joins the earlier grants.
REQ=$(cd /tmp && codesign -d -r- /bin/bash 2>&1 | sed -nE 's/^#? *designated => //p' | csreq -r - -b /dev/stdout | xxd -p | tr -d '\n')
for svc in kTCCServiceAccessibility kTCCServiceScreenCapture kTCCServicePostEvent kTCCServiceListenEvent; do
  sudo sqlite3 "/Library/Application Support/com.apple.TCC/TCC.db" \
    "INSERT OR REPLACE INTO access (service,client,client_type,auth_value,auth_reason,auth_version,csreq,flags) VALUES ('$svc','/bin/bash',1,2,4,1,X'$REQ',0);"
done
sudo pmset -a sleep 0 displaysleep 0 disksleep 0
defaults -currentHost write com.apple.screensaver idleTime 0
defaults write NSGlobalDomain NSQuitAlwaysKeepsWindows -bool false
defaults write com.apple.loginwindow TALLogoutSavesState -bool false
# As on the host: the bar replaces the menu bar (macOS 27 reads controlcenter's key, at login), the Dock hides.
defaults write NSGlobalDomain _HIHideMenuBar -bool true
defaults write com.apple.controlcenter AutoHideMenuBarOption -int 0
defaults write com.apple.controlcenter HasAttemptedMenuBarWorkflowMigration -int 1
defaults write com.apple.dock autohide -bool true
# clock.sh runs python3; without the Command Line Tools /usr/bin/python3 is a stub that pops an install dialog.
if ! xcode-select -p >/dev/null 2>&1; then
  touch /tmp/.com.apple.dt.CommandLineTools.installondemand.in-progress
  label=$(softwareupdate -l 2>/dev/null | sed -n 's/^\* Label: \(Command Line Tools.*\)$/\1/p' | tail -1)
  [ -n "$label" ] || { echo "no Command Line Tools in softwareupdate -l"; exit 1; }
  sudo softwareupdate -i "$label" --agree-to-license
  rm -f /tmp/.com.apple.dt.CommandLineTools.installondemand.in-progress
fi
/usr/bin/python3 -c 'print("python3 ok")'
pkill -x TextEdit || true; pkill -x Terminal || true; pkill -f "Install Command Line Developer Tools" || true
rm -rf ~/Library/Saved\ Application\ State/*.savedState
defaults write com.apple.Terminal NSQuitAlwaysKeepsWindows -bool false
nvram boot-args; csrutil status
sudo sqlite3 "/Library/Application Support/com.apple.TCC/TCC.db" "select service,client from access where client='/bin/bash'"
EOF
# Every clone carries the version of this script it was provisioned by; agent-app.sh start compares it.
guest_exec 15 <<<"echo $(golden_stamp) >~/.agent-golden"
guest_exec 15 <<<'(sleep 1; sudo shutdown -h now) >/dev/null 2>&1 &' >/dev/null || true
for ((i = 0; i < 90; i++)); do kill -0 $pid 2>/dev/null || break; sleep 1; done
kill -0 $pid 2>/dev/null && tart stop "$VM"
rm -rf "$STATE"
echo "provisioned $VM"
