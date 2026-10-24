# Typing the exe (plan)

Status: **steps 1 and 2 done, step 3 under way (2026-10-24)**: 82 types, 145 prototypes, 295 AI primitives from 9
dispatch tables, 30 globals (docs/NOTES.md "Typing pass, step 1 / step 2", "Flight, read with types", "Swimming").
Goal: decompiles that read `dyn->ground.normal` instead of `*(float *)(param_1 + 0xd8)`, so the remaining mechanics
(flight, swimming, zones, the camera, other persos) are read rather than re-derived, and the guesses in the core can be
checked. Decided with the user: type the **real** Ghidra project (`files/out/_decomp_artifacts/ghidra`), from a script
that can be re-run on a fresh import.

## Running it
```
export GHIDRA_INSTALL_DIR=/opt/homebrew/opt/ghidra/libexec
clang -target i386-pc-windows-msvc -fsyntax-only tools/exe/types_check.c        # offsets, at compile time
$GHIDRA_INSTALL_DIR/support/analyzeHeadless files/out/_decomp_artifacts/ghidra TonicTrouble -process -noanalysis \
  -scriptPath tools/ghidra -postScript ApplyTypes.java tools/exe files/out/_decomp_artifacts/typed
```
- Add `-readOnly` for a dry run (nothing saved); write the output elsewhere to compare.
- ApplyTypes parses `types.h` into a scratch type manager with the program's data organisation. It checks the layout
  against every `AT()` / `SIZE()` of `types_check.c` and **stops before changing anything** on a difference.
- It then replaces /TT, applies the prototypes, labels the globals (float constants only when memory holds the expected
  value) and writes one typed decompile per listed function, plus `layout.txt`.
- Re-running is idempotent: a second run gives identical output, with no renames and no doubled comments.
- `tools/exe/tables.tsv`: dispatch tables whose entries share one prototype (the AI primitives). Only targets still
  named `FUN_x` / `AI_x` are retyped; others (folded stubs shared with other code) get a comment. Their decompiles go to
  `typed/ai/`.
- Prototype names replace Ghidra's / modmap's default names (`FUN_x`, `MEC_x`, `GAM_PlayAnim_x`) and keep the address
  (`MEC_CollideMove_004be1bd`). Hand names are kept.
- **Before adding a prototype, read the argument count from the asm:**
  `-readOnly -postScript ArgCheck.java out.txt <addr>...` (callers' `ADD ESP`, the callee's `RET n`). Ghidra's guesses
  merge the pushes of the next call; a function called through a pointer needs its call site read (CallArgs.java).
- An untyped helper between pushes can still confuse the caller's stack: type it, even with `undefined4` arguments.
- **Check a getter's callers before giving it a struct type.** The linker folded identical one-liners: `*(p + 8)`
  (GEN_Get8_00431e20, 80 callers) and `*(float *)(p + 0x10)` serve many structs. Step 1 had typed them as dynam
  accessors; fixed 2026-10-24 (Callers listing grouped by module, or a look at two unrelated callers).
- Backup of the untyped project: `files/out/_decomp_artifacts/ghidra_backup_2026-10-24_pre_types`.

## Step 1 result (2026-10-24)
- The check passed. The typed decompiles read the way docs/NOTES.md describes them:
  - **DNM_WalkForces_004c41c5:** weight `-dyn->mass * card->gravity_factor * env->gravity` along `env->up`;
    `grip = ground.own_mat.grip * ground.obstacle_mat.grip`; `wanted x grip^4`.
  - **DNM_IntegrateWalk_004aa6af:** env lookup, forces, `v + F dt / m + work->wanted`, collide-and-move, the re-snap
    with `card->cos_slope` / `card->resnap_distance` on "no ground now and last frame, ground before".
  - **The flight and ski code:** reads exactly the card fields the port names (`lag_*`, `wing_area`, `polar_set`,
    `env->density`; `probe_cos`, `hold_down`, `edge_grip`, the floor's `obstacle_mat.steer`).
  - **PlayAnim:** `p3d->frame += g_timer_ms / 1000 x p3d->frame_rate`.
- The corrections it brought are listed in docs/NOTES.md "Typing pass, step 1" (perso +8 is a wrapper, the
  integrators' 4th argument is the sector, be1bd / bedd6 were swapped, Dynamics +0x14 is a frame, the card casts ...).
- Typed so far: the per-frame dispatch, the 13 handler pairs, collide-and-move with classify and the ground / wall /
  ceiling responses, re-snap, env lookup, the walk / ski / fly forces and their helpers, PlayAnim's clock, the dynam's
  request queue and histories.

## Approach
- **`tools/exe/types.h`**: the engine's structs as C, one place, with the source of every field (the function that
  reads or writes it, or the data that shows it). Unknown gaps as `uint8_t _pad_XX[n]`, so offsets stay exact. Only
  what we know; it grows as functions are read.
- **`tools/exe/signatures.tsv`**: `address <TAB> C prototype` for the functions we have touched (MEC integrators and
  helpers, the AI dispatch handlers, PlayAnim, the transition module, SND / VIG entry points ...), next to the names in
  `names.tsv` / `ai_names.tsv`.
- **`tools/ghidra/ApplyTypes.java`**: parses `types.h` with Ghidra's C parser into the project's data type manager,
  applies the prototypes from `signatures.tsv` (typed parameters make the decompiler propagate the struct types
  through the bodies), labels the known globals (engine state 0x8bcb00, the transition block 0x51b040, the procedure /
  condition / function tables ...), then re-exports the decompiles of every listed function to
  `files/out/_decomp_artifacts/typed/` (git-ignored, like the rest of the decompiles).
- Re-runnable: `analyzeHeadless files/out/_decomp_artifacts/ghidra TonicTrouble -process -noanalysis -scriptPath
  tools/ghidra -postScript ApplyTypes.java tools/exe/types.h tools/exe/signatures.tsv`.

## The structs, from what the notes already hold
| struct | known fields (docs/NOTES.md, tools/ttre/level.py, ai.py, core) |
|---|---|
| SuperObject | type, data (perso / sector / IPO), children, matrix |
| Perso | +0 Perso3dData, +4 StdGame, +8 dynamics, +0xC brain, collide set ... |
| Perso3dData | +0 initial state, +0xC / +0x10 object lists, +0x14 family, matrix |
| StdGame | +0 family, +4 model, +8 instance (name tables), +0x2A/+0x2B activation, +0x2C radius^2 |
| Brain / Mind / Intelligence / DsgMem / DsgDef | mind +0 model, +4 / +8 normal / reflex intelligence, +0xC dsg memory; intelligence +8 current behaviour; dsg def {default buffer, infos, size, count}, info {offset, type} |
| AI model / behaviour / script / node | behaviour lists {ptr, count}, 12-byte entries {scripts, schedule, n}; node {param, type, depth} |
| Family / State / transitions | state +0xC anim ref, +0x10 transitions, +0x1C prohibited, +0x28 next, +0x30 card, +0x3A repeats, +0x3B speed, +0x3C flags |
| AnimationReference, AnimEvent, A3D | anim ref +4 frames, +8 events, +0xC n_events, +0x28 A3D; event 20 bytes {type, value, frame, ...}; A3D header and packed arrays (level.py) |
| Dynamics (MEC) | **typed** (types.h Dynamics; +0x104 / +0x120 are ground.obstacle_mat / own_mat grip, +0xD8 ground.normal) |
| ID cards, types 0-12 | sizes (MEC_hidcard_004b7d04); type 0 / 2 / 4 / 5 fields as decoded |
| Mechanics material, GameMaterial | {visual, mechanics, sound, collide}; mechanics 0x1C bytes: grip, rebound ... |
| Collide sets, ZDx zones | **typed** (CollSet, ZoneEntry, ZoneActivation, GeoCollide; exe list numbers 0 ZDD, 1 ZDM, 2 ZDE, 3 ZDR) |
| Waypoint | {u32, vec3 position, radius, ...} |
| Sound: bank, event, resource, the global table | as tools/ttre/sfx.py |
| Screen transitions | block 0x51b040, effect, variant, GLI sprite (+0x58 value, +0x5c alpha mode) |
| Engine | state byte 0x8bcb00, level names 0x8bcc05, transition flags 0x4ec748..50, fix.gpt read order (GAM_Initeng_0040d700) |

## Order
1. ~~`types.h` + `ApplyTypes.java` with the perso chain, dynamics, state and anim reference; the MEC functions already
   decoded as the first signatures. Check: their typed decompiles must read the way docs/NOTES.md describes them.~~
   Done 2026-10-24 (above).
2. ~~Next~~ done 2026-10-24:
   - **The card accessors**: dead code (no reference anywhere), used as layout evidence only.
   - **The step-up MEC_004bb06e** and the count-only helpers: typed and named.
   - **Collide sets and ZDx zones, the sweep and its hit record**: typed.
   - **The AI interpreter**: node, value, behaviour, action, dsgvar types; tools/exe/tables.tsv applies one prototype
     per dispatch table (node handlers, keywords, conditions, functions, operators, procedures, meta-actions,
     fields) and names the implementations from ai_names.tsv.
   - Left for later: sound, transitions, waypoints, the engine globals (type them when a feature needs them).
3. In progress (2026-10-24): **flight** read (the physics matched; the pose head first, the air density 1.3, ported:
   NOTES "Flight, read with types"), **swimming and water** read and ported (NOTES "Swimming": water planes, swim
   physics, kinematic type 7, the water zones the scripts test; .ttai v8), **ZDx zones** read (NOTES "ZDx zones": list
   numbering, COND 13-15, the point-in-zone test; COND 13 ported for portals, .ttai v9), **footsteps** by floor
   material (NOTES "Footsteps", .ttai v10). Next: the camera (NOTES "The camera, first look": a plan). The original order:
   Then read, with types, what the next features need, in this order: **flight** (types 5: why it feels off),
   **swimming and water** (which card / zone; exporting the water volumes), **zones** (COND_13 and ZDx: the holes,
   Doc's house, enemy hits), **footsteps by material**, the **camera**.

## Cost and effect on the core
About one to two sessions: the knowledge is mostly written down already; the Ghidra runs cost time, not tokens; the
typed decompiles are read only for the features above. The core keeps its own structs; expected changes are
corrections where it guessed (ChangeStateByDirection, the zone radii, flight, footsteps) and named constants for Ed's
dsgvars and states (`ED_DSG_ARRIVAL_MODE` for dsg134, `ED_STATE_PORTAL_IN` for 228) in one header. No rewrite; the
frontends do not change.
