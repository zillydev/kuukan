---
name: explore-app
description: Use when the user asks to explore, fuzz or bug-hunt the app, to "use it like a user" to find what is broken, or after a release is cut. Walks every feature in FEATURES.md in the project's hidden agent copy, runs the proofs, pushes on the edge states around each feature, and files what breaks as TODOS.md items. It finds and files; it never fixes or merges.
---

# Explore the app for bugs

Shared skill: identical in every own project, from `workbench/skills/explore-app`, updated with `proj sync`.

This is the outer loop on top of the project's harness: use the app the way its users do, across every feature, and turn what breaks into queue items. Fixing them is ordinary work from `TODOS.md`, one item per branch, verified by the user before any merge.

## Before you start

- The harness is in the project's `AGENTS.md` (definition of done): a hidden agent copy and its driver (`scripts/agent-app.sh` and `scripts/ui` on the Tauri kit). With no hidden copy, stop and say so. Never explore in a window the user sees or on their data.
- Explore `main` as it is: `git worktree add --detach worktrees/explore-<date> main`, and start that worktree's own agent copy. Other sessions may be in the main tree.
- `FEATURES.md` is the map. Each entry says what the feature does, how a user reaches it (`Reach:`) and which proof shows it works (`Proof:`). An entry without a `Reach:` line is reached however its text suggests; note the gap in the report.

## The pass

1. Run every proof (`make proofs`, or `scripts/prove`). Each failing proof is a finding: read its log and screenshot, and re-run that one proof once to rule out a flake.
2. Walk each area of `FEATURES.md`, then push on it the way people do by accident:
   - **Amounts:** no data, one item, hundreds. Use the `scripts/seed` scenarios; when none fits, say which state you needed in the finding.
   - **Input:** very long text and names, emoji, right-to-left text, multi-line paste, leading and trailing spaces.
   - **Size:** `scripts/ui resize` narrow (600×500) and wide. Look for clipping, overflow, text running under controls, and nothing scrolling.
   - **Speed and repetition:** double clicks, switching items rapidly, a shortcut pressed twice, Escape halfway through an action.
   - **Keyboard only:** Tab order, Enter and Escape in every dialog, every shortcut `FEATURES.md` names.
   - **Neighbours:** after using one feature, check that the next still behaves (selection, scroll position or filters leaking between views).
3. After every step, `scripts/ui snap` and look at the PNG, check `scripts/ui logs` for console errors and `scripts/agent-app.sh logs` for backend errors and panics.
4. Before filing, reproduce each finding a second time from a fresh copy (`scripts/agent-app.sh restart`, with the seed it needs).

## Filing

- Look in `proj todo` and in `git log` first; don't file what is open or already fixed.
- One item per bug, filed with `proj todo bug "<text>"` in the user's style: what you did, what you saw, what you expected, the seed scenario, and a screenshot. Copy the screenshot to the main checkout's `screenshots/explore-<date>-<n>.png` and reference it as `ref: screenshots/…`, since the agent copy's state dir is wiped on the next start.
- A design opinion is not a bug. List suggestions in the report, not in `TODOS.md`.
- A failure the harness causes (a click that lands wrong, a copy that can't reach a state) is a harness gap: report it as one, not as an app bug.

## Finish

Stop the agent copy, remove the worktree (`git worktree remove`), and report: the proofs' results, the items filed per area with their screenshot paths, harness gaps, suggestions, and what could not be explored (native menus, drag and drop, anything the project's `AGENTS.md` lists as out of the agent copy's reach).
