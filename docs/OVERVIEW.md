# How Open Tonic fits together

A one-page map, written 2026-11-11 for the reader who wants the whole picture before the details. Every part has its own
longer document; they are named as they come up. What works and what does not is in docs/STATUS.md.

## Three layers

```
your copy of the game            tools/ttre (Python)             core (C11)                  frontends
files/sources/GAMEDATA    ->     the converter            ->     libtoniccore         ->     Sources/OpenTonic (Swift, Metal)
                                 writes files/out/               no platform code            sdl/ (C, SDL3)
```

1. **The converter (tools/ttre, run by tools/convert_files.sh).** Reads the original level files and writes, per level,
   a `.ttlv` (what is drawn and collided with) and a `.ttai` (the scripts and their data), plus textures, music, sounds
   and the intro. Nothing of the original's formats reaches the game: if the core needs something, the converter exports
   it. A level is redone when the converter's code has changed (each file carries a stamp).
2. **The core (core/).** Loads those files and runs the game: scripts, physics, camera, what to draw. It opens no window,
   reads no key and plays no sound.
3. **The frontends.** Each owns a window, the GPU, files, audio and the menus. Per frame it hands the core the held
   actions and the time passed, then draws the list the core gives back and plays the sounds it names. Both frontends do
   the same things twice, so logic belongs in the core (docs/VIEWER_CORE.md).

The exe is the reference throughout: tools/exe and tools/ghidra hold the decompile index and the names (docs/EXE.md,
docs/DECOMP.md, docs/TYPES.md).

## What the converter writes

| file | chunk | what |
|---|---|---|
| `.ttlv` | TEXS, MESH, PRSO | textures, the level's geometry, the characters' models and animations |
| | COLL, FLOR, PCOL | the static collision, the floors, each character's own collision |
| | SECT | the sectors: their portals, what each sees, their water, who and what is in them |
| | CAST, CMEC, WAYS | which characters run their scripts, their physics cards, the waypoints |
| | LITE, CHLT, LAIS, LMAP | lights, character lights, the light managers, lightmaps |
| | SPCH, EDPL, EDIT | speech lines, Ed's start places, the custom layer (our own additions) |
| `.ttai` | SCPT, STAT, CARD, BODY | the scripts, the states (which animation, which physics card), Ed's body spheres |
| | PORT, PLAC, TRIG, ZONE | portals and walk-in passages, places, triggers: characters the converter reduced to data |
| | SNDS, FSND, TRAN | sound events, the screen transitions |

Where the files are made: `level.py` (geometry, sectors, story gates), `cast.py` (who is in the cast and what they may do
to Ed), `ai.py` (scripts), `ttlv.py` (the format), `custom.py` (our additions), `se.py` (the Special Edition),
`import_gltf.py` (levels from other games), `progress.py` (the survey of who uses each power and save value: one file
for all levels, `progress.ttpg`, shown by the debug menu).

## The core, file by file

| file | what it is |
|---|---|
| `tc_level.c` | reads a `.ttlv` |
| `tc_ai.c` | the script interpreter: runs one character's behaviours, as the exe does. Also holds the game's shared state (powers, save values, lives) |
| `tc_ed_ai.c` | Ed: his scripts joined to his physics |
| `tc_mec.c`, `tc_mec_walk.c` | the physics, one "ID card" type at a time: walking and falling, flying, the ski board |
| `tc_cast.c`, `tc_cast_mec.c` | every other character: who is awake, their scripts, their physics, the moving sectors |
| `tc_collision.c`, `tc_obstacle.c` | the static world's triangles; the characters' own collision, moved with them |
| `tc_sector.c` | which sector someone is in, its water, what is drawn from it |
| `tc_cam.c` | the camera that follows Ed |
| `tc_speech.c`, `tc_transition.c` | speech panels drawn in the game's font; the screen wipes |
| `tc_rec.c`, `tc_tape.c` | our recordings and replays; the original game's demo tapes |
| `tc_debug.c` | the debug menu (F4): the powers and save values, who uses them, switches |
| `tc_player.c` | the old "simple Ed": a plain walker with no scripts, kept as a fallback |
| `tc_viewer.c` | everything else, tied together: input, the frame, portals, lighting, the draw list. The biggest file, and the one a split is planned for |

## One frame

`tc_viewer_update(dt)`, then the frontend draws:

1. Input is read (and written to the recording, if one runs).
2. The game steps in **ticks of 1/30 s**, as the original, however fast the screen is. In each tick:
   the characters others stand on (lifts, platforms) move first; then Ed (his scripts decide, his physics move him); then
   everyone else. This is the exe's order (docs/ENGINE.md).
3. What is drawn between ticks is blended from the last two, so the picture is smooth at any frame rate.
4. Speech and hint scenes, then the camera.
5. Ed's sector, the lighting, the portals and level changes.
6. The core builds the draw list: the sectors Ed's sector sees, the characters, the water, sorted for blending.

A character is awake when its sector is Ed's or on his sector's list; it thinks only while Ed is inside its radius; its
physics run either way.

## What is real and what is a stand-in

The exe has one kind of thing, the perso, and one loop. The port grew the other way round: a viewer, then a simple Ed,
then Ed's real scripts, then the cast. So some of the game is still data the converter prepared instead of a character
running its script:

- portals, walk-in passages and Totalski's obstacles (`.ttai` PORT, TRIG);
- the light managers (LAIS);
- the camera (ours, not the original's camera character) and its directors;
- story progress: the powers start all on, four of sud0's five gates are left out at export, the inventory always has one
  of everything, saving is skipped.

The direction is to fold each back into the cast, one subsystem at a time (docs/DIRECTION.md).

## How it is checked

`make -C core checkall` is the gate: thirteen headless checks on the real levels (walking, skiing, portals, sectors, the
camera, the cast, replays, the debug menu). They prove the build has not changed against what was measured before; they do not prove it
looks or feels right. Only a playtest does.

## Where to read more

| about | file |
|---|---|
| what works, what is broken | docs/STATUS.md, docs/BUGS.md |
| where the project is going | docs/DIRECTION.md |
| the exe's loop and each subsystem as read | docs/ENGINE.md |
| the camera | docs/CAMERA.md |
| portals and level changes | docs/PORTALS.md |
| recordings | docs/RECORDING.md |
| the Special Edition | docs/SE.md |
| building and installing | docs/BUILD.md, docs/SDL3.md |
| the long history, contradictions included | docs/NOTES.md |
