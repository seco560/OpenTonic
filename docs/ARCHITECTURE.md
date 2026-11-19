# How the engine works, where it is tangled, and how to untangle it

Written 2026-11-18 by Claude at the user's request ("I fear we've created a spaghetti monster"). docs/OVERVIEW.md is the
map (what each file is); this is the analysis: how a frame actually runs, the places where the code knots up, and a
staged plan. Numbers are measured from the tree on that day; judgements are marked as such.

## 1. A frame, top to bottom

```
frontend (Swift/Metal or SDL3)
  held actions, sticks, dt  ->  tc_viewer_update(v, dt)                        core/src/tc_viewer.c
                                  update_frame
                                    debug menu open?            -> tcv_debug_frame          (game held)
                                    scripted picture?           -> count down               (game held)
                                    level loading / pending?    -> tcv_look only
                                    playing                     -> tcv_update_play          tc_viewer_play.c
                                       tcv_look (camera input)
                                       custom slot (the car)?   -> update_custom, ride_tick
                                       step_ai: while tick_time >= 1/30 s:
                                          build tc_main_input (held back by speech / director / scene walk)
                                          tc_main_tick                                       tc_main.c
                                            tc_cast_tick                                     tc_cast.c
                                              run-first persos (lifts, platforms)  step_actor x n
                                              Ed: ed_step -> tc_ed_ai_step (scripts, portals, mechanics)
                                              the other persos                     step_actor x n
                                              moving sectors, water surfaces
                                            take_requests (cover, level change, camera place)
                                          HUD tick, scripted pictures
                                       tcv_update_speech, tcv_update_scene (lines, hint scenes)
                                       tcv_update_camera (follow / director / scripted shots) tc_viewer_camera.c
                                    else free-fly
                                  update_sectors, lighting, portals + transitions, light persos
                                pick_sounds (random clips), recording
  draw list, lights, HUD, speech panel, sounds  <-  tc_viewer_draws / _sounds / _speech_panel ...
```

Two clocks: the game ticks at 30 Hz (TC_ED_AI_TICK) inside step_ai; everything presentational (camera, drawing between
ticks, transitions, mixer gains) runs per host frame. That split is sound and worth keeping.

## 2. The parts and their weight

| layer | where | lines | owns |
|---|---|---|---|
| converter | tools/ttre (Python) | 14,400 | reading the original data; **deciding what runs** (cast.py) |
| script VM | tc_ai.c / tc_ai.h | 3,040 / 670 | the interpreter, the primitives, shared game state (globals) |
| Ed | tc_ed_ai.c, tc_mec_perso.c, tc_main.c | 1,400 | Ed's driver, his mechanics, the tick, the car's slot |
| the cast | tc_cast.c, tc_cast_mec.c | 1,920 | every other perso: activity, mechanics, sounds, spawning |
| physics | tc_mec*.c, tc_collision.c, tc_obstacle.c, tc_sector.c | 2,400 | sweeps, walkers, sectors |
| the viewer | tc_viewer*.c (11 files) + tc_viewer.h | 5,500 | **the game session and the presentation, mixed** |
| menus, saves, HUD, rec | tc_menu.c, tc_save.c, tc_hud.c, tc_rec.c | 2,900 | |
| frontends | Sources/OpenTonic, sdl/src | 4,200 + 5,200 | window, GPU, audio, files, input devices |
| checks | core/tests | 9,500 | the gate (make checkall) |

The core is 25,400 lines of C.

## 3. Where it is tangled (findings)

### 3.1 "Who is the player" has three answers
- Ed is not a cast perso: he is a *driver* (tc_ed_ai) with his own step, his own mechanics entry (tc_mec_perso,
  bridged into the cast's integrators by tc_cast_main_mechanics), his own animation set (tc_level.ed.anims, drawn by the
  viewer, not by the cast's families), his own sector tracker, and an actor record the cast attaches to him every tick
  (tc_cast_attach_main).
- The **car** takes "the main character's slot" through tc_character_ops (tc_main.h): a native brain on host frames.
- The skier is the player through `main_alias` (CAST), a second id that means Ed.
- Since today the **chameleons** take it through `tc_ai_globals.main_character` / tc_cast_character, which is the
  exe's model (PROC 4, one perso id on the main-character list).

Four mechanisms for one concept, each with its own places to consult (camera target, sector, MainActor, input). This is
the knot most other knots tie into. *Judgement:* the chameleon model (a perso id) is the right one; the others should
fold into it.

### 3.2 The viewer is the game session
`tc_viewer` (203-line struct) holds the camera, the draw lists, menus, debug, tapes, **and** game rules: hint scenes
(scene_said, scene_walk, update_scene's cover), speech holding Ed, the HUD tick, portal stand-ins (simple_portals),
level carry-over, saves (tc_save.c includes tc_viewer_internal.h), random sound picks. The viewer files reach into
`p->main.driver` / `p->main.player` 115 times. A rule that belongs to the game (who may talk, when a scene ends) is
found in a presentation file, and the next fix lands beside it.

### 3.3 The converter decides behaviour by name
cast.py holds 15 name sets (MAY_FORCE_MAIN, MAY_FORCE_MAIN_MODELS, MAY_NOT_FORCE_MAIN, SAFE_MAIN_FORCES, SCENE_SPEAKERS,
ZONES_HELD, LEVEL_CHANGERS_LEFT_OUT, PROMPT_ANY_BUTTON ...) and level.py its story gates. They were stopgaps for missing
features: a perso whose force on Ed would strand him is kept off him. The cost shows today: the chameleons (an Ed
behaviour never implemented) and the talking beans were silently blocked for weeks; the blocks hid the gap instead of
reporting it. The 2026-11-17 "safe forces" rule (allow what hands Ed back) was the right direction.

### 3.4 Per-family shortcuts for per-perso data
A cast *family* is exported once, from its first perso: states, animations (right: they are the family's), but also the
mechanics bodies and the ZDE hit zones (wrong: they live in each perso's collide set). Found today: sud0's tomato family
took ZOR_CoulisDeTomate's (none), so no tomato could be hit or hurt Ed, and every tomato glided through walls. Fixed by
per-perso overrides (CMEC own rows, own ZDE tail), but the pattern (one perso stands for its kind) may hide elsewhere:
the ZDR zones (PZDR) are per perso already; cards and transitions are per family by the data.

### 3.5 Two ways to run the same thing
Some persos are reduced to data at export (PORT portals, TRIG Totalski obstacles, LAIS light persos, ski_portals) and
run by host code, others run their own scripts in the cast; the boundary moved several times (the arches since
2026-11-13). The simple controller (tc_player, I key) keeps a second Ed with its own portal path (simple_portals,
walk_in clips). Each duplicate is a place where a fix has to be made twice or silently isn't.

### 3.6 tc_ai_perso is everyone's notebook
162 lines: the VM's state, host-fed inputs (positions, contacts, distances), outputs the host must collect
(change_level, cover_requested, sounds, launch_channel), port policy (removal_guard, keep_off_main, frozen,
read_only_for_cast), diagnostics. `main_actor == 0` doubles as "this is Ed" in a dozen protections.

### 3.7 File formats grow by appending
Optional tails read while `r->offset < r->size` (CMEC now has five), "older files: 0 there" branches, sentinel values
that change meaning (CMEC own radius: 0 meant none until today, now a point). It works because every level is
re-exported on a converter change, but each tail is order-dependent and unversioned.

### 3.8 Frontends duplicate
Swift and SDL each implement input mapping, audio voices, menus wiring and file handling (4,200 / 5,200 lines). The SDL
build cannot be fully built here (no glslangValidator), so half the duplicate is checked by syntax only.

### 3.9 Debugging is rebuilt every session
Each session writes the same scratch probes (place Ed, press a button, print a perso's behaviour/state/HP: today
hitprobe, sceneprobe, champrobe, hintprobe, cardprobe) and then throws them away. Since 2026-11-19: core/tests/probe.c does what they did.

## 4. A plan to rein it in

Each stage is one change set, checked by `make -C core checkall`, `make CUSTOM=0 checkall` and a replay of the user's
recordings with frame hashes (docs/RECORDING.md); stages 1-3 change nothing a player sees.

1. **A probe tool** (small, first): core/tests/probe.c with subcommands (`probe LEVEL place X Y Z press action watch
   ID ...`), kept in the tree. Pays for itself in every later stage.
   *Done 2026-11-19:* `core/build/probe` (docs/OVERVIEW.md "The probe"): a script of commands on a level or from a
   moment of a recording; levels followed through portals; `probecheck` in the gate runs every command once.
2. **Split the game session out of the viewer.** A `tc_game` (game rules: the tick, speech, hint scenes, HUD state, level
   changes, saves, sound picks) that tc_viewer *uses*; tc_viewer keeps camera, drawing, menus, debug, tapes. No
   behaviour change; the 115 reaches into the driver go through a handful of tc_game calls. tc_save stops including the
   viewer's internals.
3. **One main character.** `tc_cast` owns "the main character = a perso id" (today's main_character). Ed becomes a cast
   actor whose brain is his driver (his mechanics already go through the cast's integrators); the skier's alias folds
   in; the car becomes a cast actor with a native brain (its tc_character_ops become a brain vtable). Camera, sector,
   input and MainActor read one place. This is the character-interface plan the user set on 2026-11-13.
4. **Per-perso data at export, by construction.** One exporter function per perso's collide set (ZDM, ZDE, ZDR, the
   +0x36 sphere), families only for states, cards and animations. Remove the "first perso" lookups.
5. **Allow-lists become a report.** Let every perso force Ed by default; castcheck lists the forces that lead to a
   behaviour of Ed's whose primitives are not ported (by walking his model), so a gap shows as a failing line, not as
   a silent block. Keep a very short deny-list with a reason per entry.
6. **Versioned chunks.** Each chunk starts with a version word; readers switch on it instead of probing tails.
7. **Thinner frontends.** Input mapping and the audio voice logic into core (tc_mixer is already shared); frontends keep
   devices, GPU and files.

*Judgement on order:* 1 and 2 are cheap and make 3 safe; 3 is the big one and unblocks the user's planned playable
characters; 4-6 can go in any order between features.

## 5. What is in good shape
- The exe-first method: subsystems read from the exe and documented with addresses (ENGINE.md, CAMERA.md) — fixes
  today (PROC 4, random sound picks, per-perso collide sets) were each a matter of reading what the data already says.
- The layering core / frontends: the core has no platform code.
- The gate: 17 checks, 42 s, run before and after every change.
- Recordings that replay bit-exact: the best regression tool the project has.
