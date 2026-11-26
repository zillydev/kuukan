# The agent VM

Every checkout's hidden agent copy (`scripts/agent-app.sh`) is a clone of the tart VM `wm-golden`: macOS 27.0 with SIP off and `-arm64e_preview_abi`, so yabai's scripting addition loads in its Dock just like on the host.

## Rebuilding wm-golden from nothing

Only needed if `wm-golden` is gone (`tart list`). About 45 minutes, mostly downloads; nothing appears on the host's screen.

1. `brew install cirruslabs/cli/tart`.
2. `tart clone ghcr.io/cirruslabs/macos-golden-gate-vanilla:27.0 wm-golden` (29 GB download; user `admin`, password `admin`). Use the guest version yabai supports: 27.2 changes the Dock.
3. SIP, in recovery: `tart run wm-golden --recovery --no-graphics --vnc-experimental` prints a `vnc://` URL (never drop `--no-graphics`: it opens Screen Sharing on the host). Drive it with vncdotool (`pip install vncdotool` in a venv; `vncdo -s 127.0.0.1::PORT -p PASSWORD capture x.png`, `move X Y click 1`, `type`, `key enter`), screenshotting before each click: Options → Continue → Utilities → Terminal → `csrutil disable`, `y`, password `admin`, then `shutdown -h now`. Keep VNC connections few: the VZ VNC server crashes tart on some connects.
4. The mailbox, once by hand: boot `tart run wm-golden --no-graphics --vnc-experimental --dir="agent:/tmp/devkit-kuukan-golden/share"` with `scripts/vm/runner.sh` copied into that share, open Terminal from the Dock over VNC and run `bash "/Volumes/My Shared Files/agent/runner.sh"`. Then, through the mailbox (`source scripts/vm/lib.sh; SHARE=/tmp/devkit-kuukan-golden/share; guest_exec 60 <<<'…'`):
   - `echo admin | sudo -S nvram boot-args=-arm64e_preview_abi`
   - TCC Accessibility and Screen Recording for `/bin/bash` (the runner's process; see `provision-golden.sh` for the sqlite insert)
   - then `sudo shutdown -h now` from inside the guest (a host `tart stop` can lose the boot-args).
5. `make golden`: everything else (LaunchAgent for the runner, passwordless sudo, fonts, Command Line Tools for `python3`, menu bar and Dock autohide, input grants). It is idempotent; rerun it after changing it. It needs one of the two VM slots, takes about half a minute on a provisioned image, and leaves the script's version in the guest's `~/.agent-golden`, which every clone carries and `scripts/agent-app.sh start` compares with its checkout's.

## Checking it

`make agent && make proofs` from any checkout: the proofs pass on a healthy golden. `scripts/ui snap` and look at the PNG.
