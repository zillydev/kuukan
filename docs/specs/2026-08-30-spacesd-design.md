# spacesd — native space-policy daemon

Status: built and live; every stage done except G6's recorded close-out, one unplug/replug verification (§11). Language: C99. Code: `spacesd/` in this repo.

## 1. Summary

spacesd is a single launchd-supervised daemon that owns all space policy: space names, primary/mirror pairing across displays, the selected-space highlight, optional session badges, app badges, and focus/create/destroy/move/rename/send/app coordination. It observes the window server directly through SkyLight, optionally watches a session-status file through kqueue, reads pinned apps and colours from `~/.config/kuukan/config.json`, issues actions to yabai over its socket and to yabai's scripting addition over the SA socket, and pushes rendering to stock SketchyBar over the CLI wire protocol.

**Goals:** event-driven everything (no polling loops on any user-visible path); highlight ≤ 10 ms behind the space switch; badge updates ≤ 50 ms behind a session-status write; mirror corrections that can never undo a user action; space identity keyed by UUID so index shifts stop requiring bookkeeping; zero forks on any hot path; stock upstream yabai and SketchyBar, no forks of either.

**Non-goals:** window tiling (yabai's job), drawing pixels (SketchyBar's job), replacing the scripting addition, multi-user, creating bar items (sketchybarrc creates them; spacesd sets properties of `space.N` and the configured pinned `app.*` items).

## 2. Context and measured problems

Established empirically in the 2026-08-23/30 sessions, against the shell pipeline spacesd replaced:

| Problem | Root cause | Mitigation before spacesd |
|---|---|---|
| Highlight lag was 89 ms avg | shell renderer forked per event; 2 yabai queries per pass | C `mach_helper` helper → 8.6 ms, but coupled to sketchybar's undocumented env serialization (forced the brew pin) and split-brain with the script via `/tmp/sketchybar_space_rest` |
| Badges up to 2 s stale | `spaces_render.sh` polled on a 2 s timer | none |
| 1-in-5 double-tap misfire | `mirror_focus.sh` was async shell racing other shells; settle-sleep + mkdir lock + staleness guards could not cancel an in-flight correction | narrowed, not fixed |
| 90-line destroy, `rebuild_mirror.sh` existing at all | names and pairs keyed by mission-control index, which shifts on create/destroy/move | optimized (600 ms → 40 ms at 20 spaces) but conceptually unchanged |
| Helper unsupervised | started with bare `&` | launchd agent + self-re-registration |

Hard constraints verified: yabai signals can only fork/exec (`event_signal.c`), so no external process can get push events *from yabai*, but SkyLight pushes to any client, which is how sketchybar itself learns of space changes. yabai requires "Displays have separate Spaces". Mission Control transitions cost 27–700 ms wall time; nothing in this design changes that floor.

## 3. Architecture

```
  SkyLight notifications ────┐             ┌─ mach msg (CLI wire) ──► SketchyBar (stock): paints space.N and app.* items
  (SLSRegisterNotifyProc     │             │
   1327/1328 + NSWorkspace   ├─► spacesd ──┼─ unix socket ──────────► yabai (stock): focus/create/move/display
   fallback, CGDisplay cb)   │  one        │   [4-byte len][argv\0…]
  kqueue: session-status.json│  CFRunLoop, ├─ SA socket ────────────► yabai scripting addition in Dock.app:
  timers: 5 s sweep, 1 s Dock│  all state  │   [int16 len][op][args]   space switch/destroy, window move/order
   badge poll (bg queue) ────┘             └─ Accessibility ────────► app activation (app command), Dock badges
                ▲
   control socket: spacesd -m focus/send/app/create/destroy/move/rename/mirror/repaint/status/axprompt/version
                   (skhd, sketchybarrc click scripts, prompt scripts, user)
```

Division of danger: yabai's scripting addition does the window-server mutation inside Dock.app, and SketchyBar draws. spacesd observes through SkyLight and acts through those two: the yabai socket for focus-type actions, and the SA socket directly for operations that must not steal focus or that must target a space by id rather than by yabai's index (§7, §8). It also uses the documented Accessibility API to activate apps, which needs the Accessibility grant (`status` reports `ax=0|1`).

## 4. Interfaces

**4a. SkyLight in (events + queries).** `SLSMainConnectionID()` once; `SLSRegisterNotifyProc(cb, 1327/1328, NULL)` for space changes (sketchybar `sketchybar.c:223-226`); public `NSWorkspaceActiveSpaceDidChangeNotification` + `ActiveDisplayDidChange` registered in a small `.m` shim as belt-and-braces (pattern: sketchybar `workspace.m`); `CGDisplayRegisterReconfigurationCallback` (public) for display add/remove; `NSWorkspaceDidWakeNotification` for post-sleep resync. Events are only delivered under `RunApplicationEventLoop`; a bare `CFRunLoopRun` never receives them. Queries: `SLSCopyManagedDisplaySpaces` (per-display ordered space list with `id64`/`uuid`: UUID, display, and MC index by position in one call), `SLSManagedDisplayGetCurrentSpace` (visible space per display), `SLSSpaceGetType` (skip fullscreen type 4). Window→space: `SLSWindowQueryWindows`/iterator APIs with the suitability tag constants copied verbatim from sketchybar `app_windows.c`; owner app name via `SLSGetWindowOwner` pid → `NSRunningApplication.localizedName`. These calls are unprivileged.

**4b. SketchyBar out (rendering).** The CLI wire protocol: NUL-separated args with trailing NUL, sent as one Mach OOL descriptor to bootstrap name `git.felix.sketchybar` (BAR_NAME env respected). This is the protocol the `sketchybar` binary speaks, the stable surface; the `mach_helper` env-blob format is not used. spacesd builds the NUL-separated buffer directly, so labels with spaces or quotes need no quoting. Sends are fire-and-forget; on send failure, mark bar-down, retry with 1 s backoff, full repaint on first success. A full resync also sends `--trigger space_change`, because sketchybar recomputes each space item's display only in its space-change handler. Re-check sketchybar's `mach.c` before upgrading it.

**4c. yabai out (actions).** Unix socket `/tmp/yabai_$USER.socket`. Wire (verified from `yabai.c:55-100`): `[int32 length][args NUL-separated][trailing NUL]` where length counts everything after the prefix; then `shutdown(SHUT_WR)`; read reply until EOF; a reply starting with `FAILURE_MESSAGE` `"\x07"` is an error. One connection per command. spacesd sends mutations (`space --focus N`, `space --create`, `space --move N`, `space N --display D`, `window W --space N`), never queries (SLS answers those). yabai resolves `N` through its own space list, which lags a display change, so every index-based call is preceded by `yabai_agrees(i)`: `query --spaces --space N` must return the expected UUID (retried 8 × 100 ms), else the action is refused. On `ECONNREFUSED`/`ENOENT` (yabai restarting) the action fails, is logged, and is not queued.

**4d. ccm in (badges).** `~/.config/claude-code-manager/session-status.json`, watched via kqueue on the containing directory (ccm writes via rename, so the watch is on the dir and re-armed after each event). Parsed with vendored `jsmn.h`. Consumed per session: `state` (only `"unread"` and `"running"` count) and `window` (CG window id, the same id space as `SLSWindowIteratorGetWindowID`, so the join to spaces needs no yabai). Malformed or missing file ⇒ all badge counts zero, logged once per change.

**4e. Scripting addition out (Dock-side operations).** `/tmp/yabai-sa_$USER.socket`, framed `[int16 len][op][payload]`, written without reading a reply. Opcodes used: `0x02` space focus (`u64 sid`) as a pure display-content switch with no focus, cursor or animation change; `0x04` space destroy (`u64 sid`); `0x10` window order; `0x13` window to space (`u64 sid`, `u32 wid`, `SLSMoveWindowsToManagedSpace` in the Dock). Addressing by sid avoids yabai's index lag. This couples spacesd to yabai's SA opcode enum: recheck it on every yabai upgrade. Destroy keeps yabai's guards (`SLSSpaceGetType == 0`, never a display's last space) and verifies the space is gone; the SA socket briefly disappears while yabai reloads, so destroy waits up to 2 s for it.

**4f. Accessibility out (app activation, Dock badges).** On macOS 27 the window-server front-process calls put a window on top without activating its app, so `app` activates through AX (`AXFrontmost`, `AXMain`, `AXRaise` in `sls_window_ax_focus`). The Dock's icon badges are read from its AX tree (`sls_dock_badges`), because `lsappinfo` stopped reporting `StatusLabel` on macOS 27. Both need the Accessibility grant; the binary is signed with the self-signed `spacesd-selfsign` certificate so the grant survives rebuilds (`scripts/setup-codesign.sh`), and `-m axprompt` re-registers it. The grant is tied to the binary's path.

## 5. Data model

In-memory, rebuilt from SLS at startup, on wake, and on any topology event; between events, updated incrementally.

- **Space table:** rows keyed by space UUID: `{uuid, cgs_id (id64), display, mc_index, is_visible, type, pair}`. `mc_index` is derived by position from `SLSCopyManagedDisplaySpaces` at each rebuild.
- **Pairs:** `primary_uuid ↔ secondary_uuid`. D1 spaces with `mc_index > PINNED` zipped against D2 spaces in order; empty when displays < 2. `PINNED = 1` (space 1 is the messaging space; excluded from numbering, naming, pairing, destroy and move). Pairs are persisted to `~/.config/spacesd/mirrors` (uuid pairs), so after an unplug/replug the leftovers macOS restores can be recognised as mirrors; the file is not rewritten while a connect or reconfiguration is in progress.
- **Mirror window snapshot:** which windows sit on each mirror and its primary, recorded on each settled sync with two displays, so a disconnect can drain mirror windows home and a reconnect can return a window to its mirror.
- **Names:** `~/.config/spacesd/names`, `uuid=name` lines, written atomically (tmp+rename) on rename/create/destroy. Secondary spaces never get entries; they inherit the primary's name at render time. If the file is absent at startup and `~/.config/sketchybar/space_names` exists, its `index=name` lines are resolved to UUIDs once. Destroyed-space entries are pruned at write time.
- **Windows:** `{wid, space_uuid, pid, app_name}` from the iterator sweep; refreshed on space/topology events and on a 5 s idle timer (windows move without space events firing).
- **Session badges:** per-space `{unread_count, running_count}` = join of ccm sessions on `window` → windows table. Sessions with unknown windows are dropped.
- **App badges:** for named spaces whose name is a running app, the Dock icon's badge text, refreshed on the 5 s sweep. For configured `pinned_apps` with `bar: true` (`app.<id>` items), the Dock badges are polled every 1 s on a background queue (no push channel exists for StatusLabel changes) and matched in `papp.c`.

## 6. Rendering contract

spacesd owns properties of the existing items `space.2`…`space.22` and configured `app.<id>` items; sketchybarrc creates them. For each existing space with `mc_index > PINNED`, display number `N = primary.mc_index − PINNED` (mirrors borrow their primary's `N` and name):

- label: `"N"`, or `"N name"`; append ` 󰍡` when the app badge is `•`, or ` (X)` for a numeric badge; append ` 󰚩` when `unread > 0`.
- colors (`colors.h`): visible ⇒ bg `0xffa6e3a1` if running>0 else `0xffcba6f7`, fg `0xff1e1e2e`; else unread>0 ⇒ bg `0xff313244` fg `0xfffab387`; else running>0 ⇒ fg `0xffa6e3a1`; else fg `0xffcdd6f4`. `background.drawing=on` always.
- Non-existent spaces' items are left untouched (they are `drawing`-hidden placeholders).

Renders are **diffed**: spacesd keeps the last-pushed value per item property and sends only changes, in one batched message per event. A full repaint (diff cache cleared) happens on startup, the `repaint` command, bar-send recovery and wake. sketchybarrc's last line runs `spacesd -m repaint`.

## 7. Actions and control protocol

Control socket `~/.config/spacesd/sock` (unix stream, same length-prefixed framing as yabai). The same binary is the client: `spacesd -m <cmd…>`. One request per connection; requests over 4096 bytes are rejected.

- `focus N` — resolve display number `N` to its pair; switch the near display first, then the far one (§8). skhd's `alt-N` and the space items' click scripts call this.
- `send N` — move the focused window to space `N`'s primary, or to its mirror when the window is already on the primary. skhd's `shift-alt-N`.
- `app <name>` — bring a pinned app's window forward on the pinned space: z-order top through SA `0x10`, then AX activation, re-claimed until the app is front and its window on top (about 450 ms, condition-gated). Without Accessibility it falls back to the window-server calls (on top, not focused). skhd's `alt-s/d/b/a/v` and the `app.*` click scripts.
- `create [name]` — yabai create on D1 (UUID diff finds it), optional name stored by UUID, focus it; with two displays, create and move the D2 mirror. The name prompt is `scripts/create_prompt.sh`, outside the daemon.
- `destroy` — resolve the current space to its primary; refuse `mc_index ≤ PINNED`; drain the mirror's windows onto the primary; focus a neighbour; destroy mirror then primary by sid through SA `0x04`; prune the name.
- `move left|right` — swap the primary with its neighbour (names travel with UUIDs), mirror follows.
- `rename <name>` — store by UUID. Prompt: `scripts/rename_prompt.sh`.
- `mirror connect|disconnect` — the mirror lifecycle, run automatically from the display reconfiguration callback (§9); the command exists for manual recovery.
- `repaint`, `status` (space table, pairs, badges, `ax=`), `axprompt` (re-register for Accessibility), `version`.

## 8. Mirror correction

With two displays, the visible pair must stay matched. On a space-change event spacesd updates visibility and renders (the ≤ 10 ms highlight path), then `mirror_check` compares the two visible spaces. If they are not a pair, it switches the *other* display to the pair of the space on the active display through SA `0x02`. That opcode changes only what the far display shows, never input focus, so a correction cannot undo a user action and needs no settle delay, generation counter or cursor warp. Recursion terminates on state: the switch emits an SLS event, the next check sees a matched pair and does nothing. Both displays sync 49–72 ms after `alt-N`.

`focus` uses the Dock switch (`yabai -m space --focus`) only on the half of a pair that has windows, and the raw SA switch for the other. A Dock switch to an empty space makes macOS restore focus to the most recently used app anywhere and pull that app's display to its space. Any new switch path must keep this invariant.

Acceptance (gate G5): 20 single switches → 100 % land + sync; 10 rapid double-taps (2nd within 0.3 s) → 100 % land on the second target, sync within 1.5 s; no correction ever moves the display the user last acted on.

## 9. Robustness

- **launchd:** `launchd/dev.zilly.spacesd.plist.in` (rendered by `make install` / `scripts/live.sh` with the checkout's `spacesd/bin/spacesd` path), `RunAtLoad` + `KeepAlive`, `ThrottleInterval 5`, logs to `/tmp/spacesd.{out,err}.log`. Crash ⇒ restart ⇒ rebuild from SLS ⇒ full repaint. A changed plist needs `bootout`/`bootstrap`; `kickstart -k` does not re-read it.
- **SketchyBar restart/reload:** send failure flips bar-down + backoff; sketchybarrc's last line calls `repaint`. Both paths are idempotent.
- **yabai restart:** observation is unaffected (SLS). yabai-socket actions fail visibly and are not queued; SA actions wait up to 2 s for the SA socket.
- **Display changes:** reconfiguration callbacks are debounced 1.5 s trailing and sequence-gated, so a plug-then-power-on flap (connect → remove → connect within ~10 s) is one decision; a 2→2 event re-runs an idempotent connect. On disconnect, every mirror is drained onto its primary and destroyed only once confirmed empty, else deferred. After a connect, `sweep_orphan_mirrors` destroys at most one empty, unpaired, surplus D2 space per settled pass, never during a connect or reconfiguration and never one with windows.
- **Sleep/wake:** full rebuild + repaint on `DidWake`.
- **Single display:** pairing empty, correction dormant, everything else identical; the daily-driver case never depends on mirror code paths.
- **Logging:** stderr with timestamps; every yabai and SA action, every correction, every rebuild and layout snapshot around disconnects; near-zero output in steady state.

## 10. Layout and build

```
spacesd/
  src/spacesd.c        entry point, control server+client, event plumbing
  src/model.c/.h       pure data structures, pairing, formatting, neighbor selection
  src/state.c/.h       live SLS/yabai state tables, names persistence, Dock badge refresh
  src/render.c/.h      diffed space-item rendering and pinned-app badge poller
  src/mirror.c/.h      multi-display mirroring, sync/resync, display reconfig lifecycle
  src/actions.c/.h     control command handlers (focus, send, app, create, destroy, move, rename, status)
  src/skylight.c/.h    SLS externs + notification/query/window-iterator wrappers, AX focus and Dock badges
  src/workspace.m/.h   NSWorkspace fallbacks + wake + app-name-for-pid
  src/bar.c/.h         sketchybar wire sender
  src/yabai_ipc.c/.h   yabai socket client, SA socket sender, and jsmn query parsers
  src/ccm.c/.h         kqueue watcher + jsmn parse
  src/config.c/.h      JSON config loader (pinned apps, palette, session status path)
  src/papp.c/.h        pinned-app badge matching
  src/colors.h         space and pinned-app palette and colour policy
  vendor/jsmn.h
  tests/test.c         unit tests (make test)
  scripts/             create/rename prompts, setup-codesign.sh
  launchd/dev.zilly.spacesd.plist.in
  makefile             clang -std=c99 -O2 -Wall -Wextra -Werror; signs with spacesd-selfsign when present
```

launchd runs `spacesd/bin/spacesd` from this repo's main checkout, so building there deploys: the makefile refuses to build the path the installed LaunchAgent plist names without `DEPLOY=1`, which `scripts/live.sh` (`make try`, `back`, `deploy`) passes. Feature work builds and runs in a worktree's agent VM instead (`AGENTS.md`). Every private-API usage carries a comment citing the reference file/line it was copied from, so macOS-drift fixes can be diffed against upstream mechanically.

## 11. Staged migration

Each stage was gated, with the previous behaviour left on disk until its gate passed.

- **G1 Skeleton.** Event loop + SLS/NSWorkspace shims + control socket + launchd. Done.
- **G2 Model.** Space table, windows, names migration, ccm ingest; `status` agrees with yabai's queries, badge latency < 50 ms. Done.
- **G3 Shadow.** Full render computed and diff-logged, nothing pushed, checked against the bar over a day of use. Done.
- **G4 Rendering cutover.** Push enabled, the old renderer and helper disabled; highlight ≤ 10 ms behind the switch (baseline 8.6 ms), badges < 50 ms, visuals identical by `sketchybar --query` sweep, bar reload and spacesd kill self-heal. Done.
- **G5 Actions cutover.** skhd and click scripts point at spacesd; §8 acceptance; create/destroy/rename/move round-trips keep names. Done.
- **G6 Mirror lifecycle.** connect/disconnect ported off CGDisplay callbacks with the persisted pairs file, and hardened against replug failures seen in daily use. Open: the recorded gate, plug/unplug ×3 with no duplicate spaces, windows merged home, pairs and names correct, and the double-tap test with two displays.
- **G7 Cleanup.** Superseded scripts, the helper and its LaunchAgent, `space_mirror*` files and the old names file deleted. sketchybar unpinned (coupling is CLI-wire only). Done.

Rollback for a cutover is a config-pointer change (skhd and sketchybarrc lines) back to the archived implementation.

## 12. Risks

- **SLS drift on macOS majors.** Shared with stock sketchybar/yabai; mitigated by the NSWorkspace fallback for events, cited-source comments for mechanical re-diffing, and both upstreams as the early warning. Accepted: this machine already runs on private API everywhere.
- **SA opcode drift.** yabai renumbering its scripting-addition opcodes breaks focus correction, destroy, send and `app` silently (the SA socket gives no reply). Recheck the enum on every yabai upgrade.
- **Notification codes 1327/1328 change meaning.** The NSWorkspace fallback covers space changes at slightly higher latency; discrepancies between the two sources are logged.
- **ccm format change.** The parser is defensive; badges degrade to zero, never crash.
- **Iterator tag constants wrong on some window class.** Worst case a badge maps to no space; mitigated by copying yabai's constants.
- **Accessibility grant lost** (binary moved, cert lost). `app` degrades to on-top-not-focused and Dock badges stop; `status` shows `ax=0`.
- **Mission Control floor.** Unchanged and out of scope.

## 13. Resolved decisions

Language C99 (toolchain stability + verbatim reference reuse). Names keyed by UUID at `~/.config/spacesd/names`, migrated once from the old index-keyed file. Item creation stays in sketchybarrc. Name prompts stay in thin scripts outside the daemon. Mirror correction goes through the SA's focus-free space switch, immediately, with no settle timer. Pairs are persisted so post-replug leftovers can be recognised. App badges come from the Dock's AX tree. Control socket framing mirrors yabai's. The `mach_helper` mechanism is retired.
