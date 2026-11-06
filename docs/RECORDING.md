# Recording and replaying play runs

A **run** is one `.ttrec`: it starts when a level is opened from a menu, goes on through the portals, and ends on the way
back to a menu (Cmd-L, Ctrl+L, Start) or on quitting. The next level opened from a menu is a new run. Nothing to switch on: a bug you only notice afterwards is already
recorded. Code: `core/include/tonic/tc_rec.h`, `core/src/tc_rec.c`, `sdl/src/record.c`, `Sources/OpenTonic/Recording.swift`.
Why it exists: docs/FINDINGS.md item 4.

## Using it

| | |
|---|---|
| Where | `~/Library/Application Support/OpenTonic/Recordings` (macOS, both builds), `~/.local/share/OpenTonic/Recordings` (Linux) |
| Other folder, or off | `TONIC_RECORDINGS=/some/folder`, `TONIC_RECORDINGS=off` |
| Mark a moment | **F9**, or the pad's touchpad: "look at this". The title counts them |
| Watch or delete one | **Replays** on the level select (R, or Square): the list, newest first, with Play and Delete |
| Watching one | locked: the game's keys, the mouse and the pad do nothing. **P** or Cross pauses: mouse, arrows, right stick and wheel move the camera round Ed, and resuming puts it back (a level's scripted shot, as in the outro, keeps its camera). Esc or Start goes back to the list, as its end does; Cmd-L / Ctrl+L to the level select |
| Straight from the command line | `OpenTonic --replay [file]` (the newest without a file) |
| What's in one | `core/build/replay info FILE` |
| What happened | `core/build/replay timeline FILE`: levels, marks, settings, behaviours, hit points, deaths, sectors, story values |
| The trail as a table | `core/build/replay csv FILE`, for plots |
| Does it still play the same | `core/build/replay check [-d DIR] FILE...` |
| Keep one as a regression test | copy it to `files/recordings/`: `make -C core replaycheck` runs them all |

`make -C core replay` builds the tool, and it runs from `core/`. Size: about 37 MB an hour, measured on a 60 s demo run
at 60 frames a second. Nothing deletes old recordings. A file holds inputs and numbers, nothing of the game's data.

## The Replays menu

It took the place of the old Demo (Ed's autopilot, removed 2026-11-06: the user found him poor, stumbling into walls).
`sdl/src/replays.c`, `Sources/OpenTonic/ReplaysScreen.swift`; the row text is the core's, `tc_replay_summarize`.

- A row: its levels, then date, length, marks, and "did not end cleanly" for a crash or a kill.
- **Delete is permanent** (not the Trash) and asks first: in the row in the SDL build, a dialog in the Swift one.
- A run ends on the way to a menu, so nothing in the list is being written.
- Keys: up / down, Return plays, Delete or Backspace deletes, Esc back. SDL's pad: Cross plays, Square deletes, Circle
  back. The Swift build's level select and this menu have no pad control, as before.
- Rows are read from the whole of every file, so a very large folder will open slowly (not measured).
- Files from the autopilot's days are listed, deletable, not playable: their inputs came from code that is gone.
  `replay check` skips them.

## What is recorded

**To run it again:**
- per frame, tc_viewer_update's dt and the inputs it read: held actions, mouse, both sticks (after the dead zones);
- the settings doors, in the order they were called (`tc_rec_door_id`);
- when the frame was drawn: the camera's sight test reads the last draw list and the camera feeds Ed's AI, so a replay
  draws where the run drew, and frames without a draw (minimised) stay that way;
- per level: its name, edition, seed and portal carry.

**To tell how a later build differs:**
- a trail about 30 times a second (`tc_rec_sample`): Ed, the camera, his behaviour, state, clip, hit points, lives,
  sectors, clocks, random streams, hashes of his dsgvars, the cast and the globals;
- every actor's place, behaviour, state, hit points and activity every 2 s and at marks;
- sounds, and the story values and lives when they change;
- every 30th draw, hashes of the draw list and of the frame's values, kept apart so a rendering change reads as one;
- the data's identity: converter stamps, and hashes of the files without their stamp and paths, so re-exporting an
  unchanged level doesn't look like new data.

**To explain it:** notes (frontend, build, start time, os, cpu), marks, and how the run ended.

## How it behaves

- **A crash leaves a usable file.** Records are only appended, and a frame's input is flushed before the frame runs, so
  the frame that crashed is in the file and `replay check` runs into the same crash under a debugger. `replay info` tells a
  crash ("ended inside a level") from quitting ("closed").
- **Levels are segments.** A portal makes a new viewer and a new segment in the same file. Each segment starts from its recorded
  seed and carry, so drift in one level doesn't spoil the next, and `check` compares the level change a run ended with
  against the next segment's carry.
- **Drift is reported, not just failed.** `check` gives the first difference (frame, time, field, both values), how many
  records differ, and how far Ed got from the recorded trail. Some drift is an intended fix. A change of data is said
  separately. Exit 0 exact, 1 drifted, 2 couldn't run.
- **Missing level:** `check` skips that segment; a playback goes back to the list and says so.
- A playback isn't recorded. Its inputs are locked in the frontends, and the core's replay puts each frame's own inputs in
  place of anything else that reaches the viewer (`replaycheck` tests that with stray input between every frame).
- Files from before runs (2026-11-06): one per app launch, so unrelated levels can share a file. They play straight through.

## Verified, and not

Headless (`make -C core replaycheck`, plus one-off runs):
- bit-exact on retail sud0, Land, north and SE sud0, outro, with uneven dts, hitches, doors, pause, undrawn frames;
- a real portal, sud0 -> cock01 (Ed placed in front of it at a fixed frame, in the run and in its replay), then 300 frames
  of the next level from the recorded carry;
- a file cut in half opens and replays exact up to the cut; a changed dt is caught within a few frames;
- a recording made with the Swift build's core objects replays exact in the `make` build's tool (sud0, Land; 3,600
  frames each, headless harness);
- with no recorder attached, and with the autopilot gone, portalcheck and castcheck print what the build before them
  printed but for a timing line; viewcheck and camcheck too, less the autopilot's own lines;
- the SDL menu's list, hit tests, confirm and delete, as code: `replays.c` with only its drawing stubbed, on a scratch
  folder: order, junk files, nothing deleted without the yes, only the chosen file deleted;
- one file per run, as code: `record.c` headless on a scratch folder: a portal stays in the file, each run from a menu gets
  its own, two in the same second don't overwrite (`_2`), a run without a level leaves nothing.

**Not verified:**
- The user has run the Swift build's menu and playback ("look decent", 2026-11-06), which found the two faults fixed that day
  (input ended a replay, one file held everything until quitting). The fixes themselves have not run in an app. The SDL
  build has never run here: syntax-checked only, as this Mac has no glslangValidator to link it. Whether the user's own
  runs replay exact has not been checked with `replay check`.
- **Mac against Linux:** unmeasured. sinf and friends come from each system's libm.
- Recordings made before a format change: `TC_REC_VERSION` refuses them. Samples can grow without that.

## Open

- Nothing prunes the folder but the menu's Delete.
- Marks have no text yet: tc_recorder_mark takes one, the frontends pass none.
