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
                                    tc_game_begin_frame (sounds cleared, a fresh Action noted)  tc_game.c
                                    tc_game_picture_hold?       -> count down               (game held)
                                    level loading / pending?    -> tcv_look only
                                    playing                     -> tcv_update_play          tc_viewer_play.c
                                       tcv_look (camera input), game_input (tc_game_input: player, camera, listener)
                                       tc_game_custom_frame (the car holds the slot?)       tc_game.c
                                       step_ai -> tc_game_step: while tick_time >= 1/30 s:  tc_game.c
                                          build tc_main_input (held back by speech / director / scene walk)
                                          tc_main_tick                                       tc_main.c
                                            tc_cast_tick                                     tc_cast.c
                                              run-first persos (lifts, platforms)  step_actor x n
                                              Ed, by his brain: ed_step -> tc_ed_ai_step (scripts, portals, mechanics)
                                              the other persos                     step_actor x n
                                              moving sectors, water surfaces
                                            take_requests (cover, level change, camera place)
                                          HUD tick, scripted pictures
                                       ... then Ed's pose between the ticks (viewer: ed_pose, ed_clip)
                                       tc_game_update_speech, tc_game_update_scene          tc_game_speech.c
                                       tcv_update_camera (follow / director / scripted shots) tc_viewer_camera.c
                                    else free-fly
                                  update_sectors, lighting,
                                  tcv_update_portals -> tc_game_update_portals, camera requests  tc_game_portals.c
                                  light persos
                                tc_game_pick_sounds (random clips), recording
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
fold into it. *Since 2026-11-19 (stage 3, first part):* the alias is one test (`tc_cast_is_main_id`), Ed's step and
the car are one kind of thing (a brain, `tc_brain.h`), and the host asks one place who the player plays
(`tc_game_main`). The car is not a cast actor yet (stage 3 below).

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
reporting it. The 2026-11-17 "safe forces" rule (allow what hands Ed back) was the right direction. *Since 2026-11-19
(stage 5):* the force lists are gone; every perso may force the player and forcecheck reports the ones that keep him.

### 3.4 Per-family shortcuts for per-perso data
A cast *family* is exported once, from its first perso: states, animations (right: they are the family's), but also the
mechanics bodies and the ZDE hit zones (wrong: they live in each perso's collide set). Found today: sud0's tomato family
took ZOR_CoulisDeTomate's (none), so no tomato could be hit or hurt Ed, and every tomato glided through walls. Fixed by
per-perso overrides (CMEC own rows, own ZDE tail), but the pattern (one perso stands for its kind) may hide elsewhere:
the ZDR zones (PZDR) are per perso already; cards and transitions are per family by the data. *Since 2026-11-19 (stage
4):* a perso's whole collide set is its own by construction (.ttlv CMCS).

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
re-exported on a converter change, but each tail is order-dependent and unversioned. *Since 2026-11-19:* CMEC's tails
are gone (stage 4: CMCS is read whole, required with a CAST); no reader probes for bytes left any more (stage 6 below).

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
   *Done 2026-11-19:* `core/include/tonic/tc_game.h`, `core/src/tc_game.c`, `_speech.c`, `_portals.c`. The game
   session holds the world, Ed, the cast, the tick, the lines and hint scenes, the pictures, the HUD's state, the
   transitions and level changes (phase, carry, rng), the sounds and their picks, what a save needs; tc_viewer holds a
   `tc_game *game` and calls it once or twice a frame (§1's order kept). What the game needs of the viewer comes in
   as `tc_game_input` (the player, the camera, the listener) and `tc_game_view` (the camera settings a carry keeps);
   what it asks of the camera goes out as `tc_game_camera_request` (PROC 8 / 29 aims, a save's yaw). Moved to the
   viewer: Ed's pose between ticks (`ed_pose`) and the ski camera flag. tc_save.c includes tc_game.h only. The
   viewer's presentation files (camera, draw, light, tape) still *read* the session's fields (`v->game->main`,
   `->cast`); they no longer write its rules. Proof of no change: the gate and CUSTOM=0; the user's clean recordings
   exact (four segments, ~70,000 frames); nine probe scripts (hint scenes in bar, spycarota, spynorth with their level
   changes, the SE outro's director, Totalski, a tomato fight and sud0's arches, the car, two saved games) and the
   demo tapes print the same lines, every 0.1 s, on the build before and after.
3. **One main character.** `tc_cast` owns "the main character = a perso id" (today's main_character). Ed becomes a cast
   actor whose brain is his driver (his mechanics already go through the cast's integrators); the skier's alias folds
   in; the car becomes a cast actor with a native brain (its tc_character_ops become a brain vtable). Camera, sector,
   input and MainActor read one place. This is the character-interface plan the user set on 2026-11-13.
   *First part done 2026-11-19*, in three steps, each proven unchanged on its own:
   - **3a, the skier's alias folds in.** `tc_cast_is_main_id(c, id)`: Ed's own id or the fix Ed's that stands for him
     (CAST main_alias). The lookups that repeated the test (find, the sound emitter and its placing, the drawn position,
     the camera's shot target) ask it.
   - **3b, one place to ask who the player plays.** `tc_game_main(g)`: ED, PERSO (a chameleon: its actor) or CHARACTER
     (the car, Ed riding), with its place and its sector. The camera, the light, the drawn sector and the title bar ask
     it; the viewer no longer reads `tc_cast_character` or the alias to find the player (it still asks tc_main whether
     Ed rides, to draw him at the car's seat, and reads main_alias as the ski levels' mark for the ski camera). Ed's sector without a driver (the
     simple controller's) moved from the viewer into tc_main (`tc_main_track_sector`, `tc_main_sector`).
     `tc_viewer_ed_sector` is `tc_viewer_main_sector`; `tc_main_is_ed` (which was false only for the car, not for a
     chameleon) is `tc_main_riding`.
   - **3c, a brain.** `tc_brain.h`: one table for what moves a character other than its own scripts in the cast: a tick
     and a sector (on the game's ticks, in the cast's walk) and a frame, camera target, status and abort (on the host's
     frames, holding Ed's slot). Ed's actor in the cast carries his (his driver; riding the car, the asleep one that
     carries his AI record) and `tc_cast_tick` steps him by it at his place in the walk, where it used to be handed a
     `tc_cast_main_ops` each tick; the car's `tc_character_ops` are a brain too, held by tc_main's slot.
   Proof of no change, after each step: the gate and CUSTOM=0 (after 3c), the Swift build, the user's recording of
   2026-11-19 14:40 exact (six segments, ~107,000 frames: Totalski, sud0, cavedoc, SpyCarota, carota), and fifteen probe
   scripts printing the same lines every 0.1 s on the build before and after: stage 2's nine (hint scenes, the SE outro,
   sud0's tomatoes and arches, the car, a saved game twice) plus two chameleons (sud0's marsh tomato and Pyrex: Ed hands
   the player over and takes it back), Totalski and skymap (the skier's alias), and two moments of that recording.
   *Not done, and why:* **the car as a cast actor.** Liberty City, its level, has no CAST, so no cast exists there. Giving
   it one changes Ed's own physics on that level (with a cast his walkers go through the cast's integrators,
   `tc_cast_main_mechanics`, not the shared step) and his scripts' globals, unless "a cast exists" and "the level's cast
   runs" are separated first. And with the car's id on the main-character list, MainActor in the scripts would name a
   perso with no AI record: a rule is needed first (MainActor = its rider while the main character has no scripts).
   Both are behaviour questions, not refactoring; they wait for a stage of their own, with a playtest of the car.
4. **Per-perso data at export, by construction.** One exporter function per perso's collide set (ZDM, ZDE, ZDR, the
   +0x36 sphere), families only for states, cards and animations. Remove the "first perso" lookups.
   *Done 2026-11-19*, in two steps:
   - **4a, the format.** .ttlv CMEC became **CMCS** (a new tag: chunks.py's rule for a changed layout; the core refuses
     a level with a CAST and no CMCS as stale, "re-run the conversion"). Families carry their cards and root motion
     (cast.py `roots_block`); each perso's collide set is one block (`collide_block`: the +0x36 sphere, +0x34 / +0x35,
     the ZDM bodies per state, the ZDE zones), the persos that share one sharing its block; each entry names its own.
     Core: `tc_cast_collide` (tc_level.h), `tc_cast_entry.collide` (never NULL: `tc_cast_no_collide`); the cast's
     mechanics read it (tc_cast_mec.c), the actor's model borrows its ZDE zones. This step still filled the bodies from
     the family's stand-in, to prove the format alone: every cast perso's mechanics data (cards, root motion, bodies,
     zones, sphere, priority, family index) dumped from all 32 levels (retail and SE) was identical, old core on old
     files against new on new; 23 probe scripts identical, the user's recording exact, both gates.
   - **4b, each perso's own bodies.** The stand-in lookup (`zdm_owner`) is gone. What changed (dumps compared): 31
     persos of 6 levels lose a borrowed walking body in walker states (pyramide's 17 bonuses, carota's 5 bean teeth,
     sud0's generated mushrooms, the tomato coulis and EclairFin, BigBoss's tower and two platforms, marmite's guard
     MAD_GardeKaisse: the port glides them, `glide_step`), and Land's stone SDO_MPierreCave1 sits on its own sphere.
     The exe sweeps each perso's own set (`MEC_SweepZones_0043bff0`: `so->perso->collset`); one with no ZDM zones meets
     nothing. Probes: those two moved, everything else identical.
5. **Allow-lists become a report.** Let every perso force Ed by default; castcheck lists the forces that lead to a
   behaviour of Ed's whose primitives are not ported (by walking his model), so a gap shows as a failing line, not as
   a silent block. Keep a very short deny-list with a reason per entry.
   *Done 2026-11-19.* MAY_FORCE_MAIN, MAY_FORCE_MAIN_MODELS, MAY_NOT_FORCE_MAIN and SAFE_MAIN_FORCES are gone: every
   retail cast perso may force the player, write his dsgvars, place him and take the main character, as in the exe
   (86 persos of 15 levels had been kept off him). `KEPT_OFF_MAIN` (cast.py) holds one: marmite's magnet switch
   (leaning it to let go is not ported). The level's `_cast.txt` shows what each perso forces ("forces main B15 R4").
   The report is **forcecheck** (core/tests/forcecheck.c, in the gate, every level at once): each perso whose scripts
   force the player, in a fresh level, Ed in front of it pressing Action; if it takes him, the player gets past what
   holds him (Jump for lines, pictures and the director; Action, Jump), and Ed must run (or the level change it asked
   for, or the chameleon it gave him to, must go). Not "by walking his model": the interpreter knows a primitive is
   unported only when it runs one, and a force strands Ed through the scene, not only through a primitive. 2026-11-19:
   401 forcers, 169 take Ed from in front of them and all let him go; 231 need a story step, a zone or a timer first
   (not judged); death and outro are not played. On the way it found and fixed: the main actor's GoTo walks asked for
   speed 0 (tc_ai.c `go_along`: the exe feeds it from the state's root motion, GAM_0042a8d0; sud0's walk-in director,
   pyramide's 12 sarcophagi, the skier's finish all stood); the test's own blind spots (a spy waits to be seen: the
   draw list; the mushroom boss is met past its spy: forcecheck's `approaches`). castcheck's lever test keeps the
   levers and machines. *Not a time-out:* docs/BUGS.md asked whether the cutscene reveals may hold Ed "with a
   time-out so none can strand him"; they hold him now, as the exe's do, with no time-out (a port choice for the user).
6. **Versioned chunks.** Each chunk starts with a version word; readers switch on it instead of probing tails.
   *Status 2026-11-19:* the tails are gone (stage 4's CMCS, read whole); chunks.py's rule stands instead of a version
   word: a changed layout takes a new tag, and the stamp re-exports. Left: four chunks an older export lacked, read by
   name with a fallback (.ttai ZDES, STRT, STDG; .ttlv PCOL), some legitimately absent (STRT is the ski levels' only;
   imported levels have no PCOL).
7. **Thinner frontends.** Input mapping and the audio voice logic into core (tc_mixer is already shared); frontends keep
   devices, GPU and files.

*Judgement on order:* 1 and 2 are cheap and make 3 safe; 3 is the big one and unblocks the user's planned playable
characters; 4-6 can go in any order between features.

## 5. The Special Edition: a plan (user, 2026-11-19)
The user's direction: one engine, both editions' levels, and in time the full Ed in the SE levels instead of a donor.
The core already is one engine (edition branches only in saves, recordings, a debug HUD, the demo tape, drawing); the
split is in the converter (about 30 `d.se` branches). "Merging" means the converter brings the SE's data into the same
model as retail's, so the core needs no SE cases.

**What the SE export leaves out today** (each a `d.se` branch in tools/ttre):
- which persos run: only the items and save points (SE_ITEM_FAMILIES, SE_SAVE_MODELS) and the outro's actors;
- collide sets: none (no ZDE zones, no ZDM bodies). Read 2026-11-19 with retail's layout: the zone lists come out
  plausible (SE Ed 17 ZDE zones, retail Ed 18; items one each), the tail +0x30..+0x37 does not (the sphere's size reads
  as a denormal): that tail's layout is to be read in the SE exe (MAID3DVR.EXE keeps its COL_* names);
- root motion (an SE animation frame's vector, not read), waypoints (WAYS), the activation radius (retail added
  SetActivity: an SE perso has none), animation events, the SE's own sound tables;
- script references: a family, a sector, a water surface, a waypoint, a font resolve to nothing on SE levels;
- the primitives: mapped (tools/exe/se_ai_map.tsv, every SE primitive has a retail counterpart); the pairs matched by
  order alone are the least sure.

**Order:** (1) collide sets: `collide_block` is the one place since stage 4; (2) root motion and waypoints; (3) the
script references; (4) sounds and animation events; (5) let the SE persos run, level by level, judged by forcecheck
and castcheck (the report of stage 5, not a list of names).

**The full Ed instead of the donor.** Today an SE level borrows the player from the retail level of its name (EDPL and
.ttai, its level-bound chunks emptied: docs/SE.md), with the SE persos numbered from SE_ID_BASE. Measured 2026-11-19:
Ed's exported scripts differ between bar, sud0 and north in 254 of 49,277 words, all of them the level's numbering of
the fix persos. So:
1. Ed exported once from the fix, the fix persos named by role (the camera, the cheat reader) and bound at load; the
   level-bound parts (PORT, TRIG, PLAC, ZONE, FSND) from each level. Retail uses it too; SE_ID_BASE goes.
2. The SE levels' own data for him: portals (none today: no level changes), footsteps by floor (FSND).
3. What the SE scripts ask of SE Ed: they force his behaviours, read his dsgvars and test his ZDE zones by number,
   and retail Ed's differ (17 zones against 18). A map from SE Ed to retail Ed: behaviours by name (the SE's level
   scripts are text, .dsc, with names), zones by shape and place. forcecheck reports what is left unmatched.
4. SE Totalski: the SE has no skier (SE Ed skis himself, Proc_SkiTurnLeft / Right); retail Ed does not ski. The skier
   as a character a level asks for (stage 3's playable characters), not a donor.
1 and 2 are mechanical and help retail too; 3 is most of the work; 4 waits for the playable characters.

## 6. What is in good shape
- The exe-first method: subsystems read from the exe and documented with addresses (ENGINE.md, CAMERA.md) — fixes
  today (PROC 4, random sound picks, per-perso collide sets) were each a matter of reading what the data already says.
- The layering core / frontends: the core has no platform code.
- The gate: 18 checks, about 75 s since forcecheck (2026-11-19), run before and after every change.
- Recordings that replay bit-exact: the best regression tool the project has.
