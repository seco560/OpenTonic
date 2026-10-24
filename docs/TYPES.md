# Typing the exe (plan)

Status: planned 2026-10-25, not started. Goal: decompiles that read `dyn->contact.normal` instead of
`*(float *)(param_1 + 0xd8)`, so the remaining mechanics (flight, swimming, zones, the camera, other persos) are read
rather than re-derived, and the guesses in the core can be checked. Decided with the user: type the **real** Ghidra
project (`files/out/_decomp_artifacts/ghidra`), from a script that can be re-run on a fresh import.

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
| Dynamics (MEC) | +0x38 velocity, +0x54 position, +0x98 mass, +0xB8 handler mode, +0xC0 / +0xC4 contact reports, +0xCC ground contact (+0xC normal), +0xD8 carried motion, +0x104 own grip, +0x120 floor grip, +0x146, +0x2B0.. flight controls, +0x2F4 / +0x310 pending velocity, +0x314 rotation request (PROC_242), +0x38C keep-reports |
| ID cards, types 0-12 | sizes (MEC_hidcard_004b7d04); type 0 / 2 / 4 / 5 fields as decoded |
| Mechanics material, GameMaterial | {visual, mechanics, sound, collide}; mechanics 0x1C bytes: grip, rebound ... |
| Collide sets, ZDx zones | CollSet {zdm, zdd, zde, zdr}; spheres; zone lists by state |
| Waypoint | {u32, vec3 position, radius, ...} |
| Sound: bank, event, resource, the global table | as tools/ttre/sfx.py |
| Screen transitions | block 0x51b040, effect, variant, GLI sprite (+0x58 value, +0x5c alpha mode) |
| Engine | state byte 0x8bcb00, level names 0x8bcc05, transition flags 0x4ec748..50, fix.gpt read order (GAM_Initeng_0040d700) |

## Order
1. `types.h` + `ApplyTypes.java` with the perso chain, dynamics, state and anim reference; the MEC functions already
   decoded (walk, ski, fly, collide-and-move, forces) as the first signatures. Check: their typed decompiles must read
   the way docs/NOTES.md describes them (the notes are the test).
2. The rest of the table above; the AI dispatch handlers' prototypes (`(perso, node) -> next node`).
3. Then read, with types, what the next features need, in this order: **flight** (types 5: why it feels off),
   **swimming and water** (which card / zone; exporting the water volumes), **zones** (COND_13 and ZDx: the holes,
   Doc's house, enemy hits), **footsteps by material**, the **camera**.

## Cost and effect on the core
About one to two sessions: the knowledge is mostly written down already; the Ghidra runs cost time, not tokens; the
typed decompiles are read only for the features above. The core keeps its own structs; expected changes are
corrections where it guessed (ChangeStateByDirection, the zone radii, flight, footsteps) and named constants for Ed's
dsgvars and states (`ED_DSG_ARRIVAL_MODE` for dsg134, `ED_STATE_PORTAL_IN` for 228) in one header. No rewrite; the
frontends do not change.
