# How Open Tonic fits together

A one-page map, written 2026-11-11 for the reader who wants the whole picture before the details. Every part has its own
longer document; they are named as they come up. What works and what does not is in docs/STATUS.md. How a frame runs,
where the code is tangled and a plan to untangle it: docs/ARCHITECTURE.md (2026-11-18).

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
| | CAST, CMCS, WAYS | which characters run their scripts, their physics cards and collide sets, the waypoints |
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
| `tc_main.c` | the main character (the exe's main-character list): Ed, his physics and scripts, and the game's tick around him (the run-first persos, Ed, the rest). The cast steps Ed by his **brain** (`tc_brain.h`: his driver); another character's brain (custom gameplay's car) can hold his slot. Who the player plays now (Ed, a chameleon, the car) is asked in one place, `tc_game_main` |
| `tc_mec.c`, `tc_mec_walk.c` | the physics, one "ID card" type at a time: walking and falling, flying, the ski board |
| `tc_cast.c`, `tc_cast_mec.c` | every other character: who is awake, their scripts, their physics, the moving sectors |
| `tc_collision.c`, `tc_obstacle.c` | the static world's triangles; the characters' own collision, moved with them |
| `tc_sector.c` | which sector someone is in, its water, what is drawn from it |
| `tc_cam.c` | the camera that follows Ed |
| `tc_speech.c`, `tc_transition.c` | speech panels drawn in the game's font; the screen wipes |
| `tc_rec.c`, `tc_tape.c` | our recordings and replays; the original game's demo tapes |
| `tc_debug.c` | the debug menu (F4): the powers and save values, who uses them, switches |
| `tc_player.c` | the old "simple Ed": a plain walker with no scripts, kept as a fallback |
| `tc_game*.c` | the game session (`tc_game.h`, since 2026-11-19): one level being played, its rules apart from how it is shown. The world, Ed and the cast, the game's ticks, custom gameplay's frame, the HUD's state, the scripts' pictures, the sounds and their random picks (`tc_game.c`); the lines and hint scenes (`_speech`); screen transitions, level changes, arrivals (`_portals`); a save of it (`tc_save.c`). The host gives it the player's input and the camera (`tc_game_input`) and takes what to show, and the camera moves it asks for |
| `tc_viewer*.c` | the presentation, using a `tc_game` (the public side is `tc_viewer.h`). One module in nine files, sharing the private `tc_viewer_internal.h`: `tc_viewer.c` (init, the frame, settings), `_play` (input mapping, the game's frame, Ed's drawing between its ticks, the simple controller), `_camera`, `_speech` (the text window), `_portals` (overlays, the game's camera requests), `_light`, `_draw` (the draw list), `_tape`, `_debug`. What one part lends another is `tcv_*` |
| `tc_prof.c` | the profiling hook: where a frame's time goes, zone by zone, when the host lends it a clock (`tc_prof.h`; off by default) |
| `custom/tcx_*.c` | custom gameplay, ours and not the game's (the toy car and Ed's ride: docs/ENGINE.md "Custom gameplay"), reached only through `custom/tcx.h`; `make CUSTOM=0` builds the core without it |

## One frame

`tc_viewer_update(dt)`, then the frontend draws (the steps marked "game" are `tc_game`'s; docs/ARCHITECTURE.md §1):

1. Input is read (and written to the recording, if one runs).
2. (game) The game steps in **ticks of 1/30 s**, as the original, however fast the screen is. In each tick:
   the characters others stand on (lifts, platforms) move first; then Ed (his scripts decide, his physics move him); then
   everyone else. This is the exe's order (docs/ENGINE.md).
3. What is drawn between ticks is blended from the last two, so the picture is smooth at any frame rate.
4. (game) Speech and hint scenes; then the camera.
5. Ed's sector, the lighting; (game) the portals and level changes, then the camera moves they ask for.
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

`make -C core checkall` is the gate: eighteen headless checks on the real levels (walking, skiing, portals, sectors, the
camera, the cast, the cast's forces on Ed, replays, the debug menu, the HUD, the menus, sound, the probe's smoke run). They prove the build has not changed against what was measured before; they do not prove it
looks or feels right. Only a playtest does.

**forcecheck (2026-11-19, `core/tests/forcecheck.c`; docs/ARCHITECTURE.md stage 5).** Every cast perso may force Ed,
as in the exe; this is what keeps that safe. On every level with a cast (8 at a time), each perso whose scripts force
the player is met in a fresh level, Ed in front of it pressing Action; one that takes him must let him go (he runs
again, or it changed the level, or it handed the player to a chameleon that moves). A perso that keeps him fails the
gate: a gap to port, or a line in `tools/ttre/cast.py` KEPT_OFF_MAIN with its reason. `FORCE_ONLY=id` traces one
(every change of Ed's behaviour), `FORCE_VERBOSE` lists them all. Each level's `_cast.txt` shows what its persos force
("forces main B15 R4").

**The probe (2026-11-19, `core/tests/probe.c`; docs/ARCHITECTURE.md stage 1).** For looking into one thing, not for
the gate: a level played headless under a script of commands given on the command line or in a file, printing a line
whenever a watched perso's behaviours, state, hit points or activity change. It replaces the scratch probes each
session used to write and throw away. `make -C core probe`, run from `core/`:

```
build/probe sud0 near HEG_TomateKamikaze1 3 watch 64 watch ed aim 64 pulse shoot 0.66 run 5 skipped 64
build/probe sud0 watch game portal 2 run 14             # through the arch into bar: the level change is followed
build/probe -r ~/Library/.../Recordings/FILE.ttrec 90 watch ed run 5   # a recording up to 90 s, then the commands
build/probe -s ~/Library/.../Saves/NAME.ttsv watch ed run 5   # a saved game, opened as the menus open it
build/probe -f my.probe                                 # words from a file, '#' comments (tests/probecheck.probe)
```

Persos are named by id, instance name or family name (from `LEVEL_cast.txt`), or `ed`; `game` watches speech, the
camera's target, the main character, the transition phase, the powers; `camera` the eye, its angles, field of view
and shots. The commands: place Ed (`place`, `near`, `at`, `portal`, `car` beside custom gameplay's car, `face`, `aim`), drive him (`hold`, `tap`, `pulse`, `stick`, `free`), run (`run`, `frames`, `until P B S`),
print (`watch`, `every`, `show`, `info`, `list`, `portals`, `cars`, `skipped`), set (`power`, `value`, `hp`, `setdsg`, `dsg`
to read). The full list is the head of probe.c. It drives the viewer as a frontend does (60 frames a second, a draw
each frame, the HUD, a 16:9 window), so what it shows is what a play session does with those inputs; it bypasses
nothing but the window. `make -C core probecheck` (in the gate) runs every command once on sud0, so it keeps working.
Its first big use (2026-11-19, docs/ARCHITECTURE.md stage 2): the same scripts on the build before a change and after,
the outputs compared line by line.

`make -C core perfcheck` is not in the gate: it times the core's own work per frame (`tc_prof.h`), headless, on sud0,
LibertyCity2001 (walking and free-fly) and the biggest SSX course, and prints the draws and triangles handed to the GPU.
Compare its numbers on one machine, before and after a change. First run (2026-11-13, MacBook): the core takes under
0.2 ms a frame on all of them; what grows with a level is what it hands the GPU. Levels without sectors (the imported
ones) handed it everything every frame (LibertyCity2001: some 3200 draws and 4.1 million triangles; the SSX course 830
thousand); since then culling (`tc_viewer_set_aspect`: bounding spheres against the view, wide statics cut into cells)
takes LibertyCity2001 walking to 1.4 million and the SSX ride to 540 thousand. What is left there is mostly the cars:
some 94 thousand triangles each (a converter-side simplification would be the next step). Levels with sectors are not
culled (the original's own visibility); viewcheck proves on LibertyCity2001 that nothing in view is left out.

**Occlusion culling (2026-11-14, `core/src/tc_viewer_occlusion.c`, `tc_viewer_set_occlusion`; F5 in the SDL build).**
Culling by the view still handed the GPU everything inside it, hidden or not: measured on a software render, the
draws with at least one visible pixel held 2 % of the triangles handed over when walking in LibertyCity2001, 21 % on
the SSX ride. So, on levels without sectors, each frame the core now draws the nearest big triangles of the opaque
statics (some 1,500 of them, largest first, nearest sets first) into a 256-pixel-wide depth picture on the CPU, and
leaves out every draw (and every cell of a wide static) whose bounding box is wholly behind it. perfcheck on the
Celeron J4125, triangles handed to the GPU per frame: LibertyCity2001 walking 1.34 million -> some 130 thousand, the
SSX ride 535 thousand -> some 110 thousand; free-fly over the city barely changes (from the air little hides
anything; the cars' 94 thousand triangles each remain the cost there). It costs the core 1 to 2.5 ms a frame on the
Celeron. viewcheck measures what it changes of the picture against a 960 x 540 software render of each frame: 5
pixels of 52 million (hairline cracks between the city's walls, which the small depth picture cannot hold). Levels
with sectors are untouched. Not done yet, next in line if needed: backface culling (the pipelines draw both faces),
drawing the opaque draws front to back, a SIMD rasteriser.

**The cars (2026-11-14).** Each of LibertyCity2001's cars is some 94 thousand triangles. Two rules, both for
custom-gameplay cars only: one more than `TC_VIEW_CAR_HIDE_HEIGHT` (100) below the camera is not drawn, and one more
than `TC_VIEW_CAR_FAR` (40) away is drawn as its far model: `tools/ttre/vehicle.py` `simplify()` (vertex clustering,
8 cm cubes: some 15 thousand triangles), stored as extra parts with `TCX_PART_FAR` in their object. perfcheck, the
cars' triangles per frame: walking 48 thousand -> 8 thousand, free-fly 599 thousand -> 69 thousand. The SDL build's
antialiasing now starts at 2x (8x before).

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
