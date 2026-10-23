# Portals, screen transitions and the loading screen (plan)

Status: step 1 done 2026-10-24 (.ttai v6: portals, places, transitions; core loader reads it); steps 2-5 open. Supersedes the "Level portals" guesses in docs/NOTES.md (session 10): Ed's
`dsg136` is an arrival **heading**, not a destination index, and Land's red arches teleport **within** Land.

## What the game does (decoded)

### Departure: the arch captures Ed, Ed's own scripts do the rest
Every arch (family `gen_arche`) wakes when Ed is within 4 (`PROC_18(Me, 4)`, the activation radius already used for
Totalski's trees) and, if `Ed.dsg134 == 0`, sets `Ed.dsg99 = arch`, `Ed.dsg100 = 0`, `Ed.dsg136 = arrival heading`
and forces Ed's normal behaviour **B66** and reflex **R4** (`PROC_237` / `PROC_238`). From there it is Ed's script
(identical in every level):

| Ed | does |
|---|---|
| B66 | a lead-in by his current state (ChangeStateAndWait 29 / 182 / 38 / 127, then state 0), then B68 |
| B68 | `dsg134 = 2` (= "step out of a portal" on arrival); on the ground `ChangeStateByDirection(dsg99, (0, -4.5, 0), walk-start 9, backward 2)`: walk to 4.5 in front of the arch; `TurnTowardOverFrames(dsg99, 1)`; `dsg0 = 20`; **`ChangeStateAndWait(228)` = stepping into the portal**; **`GameAction2`** (start the cover transition); B106. Holding / releasing Shoot skips the walk. |
| B106 | `If GameState63` (cover finished, screen black): `dsg103 = 1`, B102 (wait) |

The arch watches `Ed.dsg103 == 1` and then moves him. While Ed walks in, the arch points the camera at its
waypoint (`dsg2`; camera perso dsg39/44/89-93).

### Portal kinds (all 21 levels surveyed)
Red vs blue is not a flag: it is what the portal does when it fires. **Red** = `ChangeLevel` / `PROC_29` (level
change: loading screen), **blue** = `PROC_8` (teleport within the level: cover and reveal only). The families:
| Family / model | where | capture | fires on |
|---|---|---|---|
| `gen_arche/DAV_Archesec2a` (blue) | almost every level (sud0 11, cavedoc 12, carota 10, Land 4, ...) | `Ed.dsg134 == 0`: walk-in (B66, R4), arrival heading in own dsg5 | `Ed.dsg103 == 1`: wait 250 ms, teleport, B67 |
| `gen_arche/YAS_ArcheAMoi` (blue) | pyramide | the same, heading in dsg6 | the same |
| `gencanyon/FRB_Gen_ArcheCanyon`, `genplainenord/KEV_ChangementSousMap` (blue zone teleports) | LeCanyon, north, marmite, cock01, BigBoss | within `dsg8` (canyon) or the activation radius: cover at once, B15 (wait) | screen covered: heading, B67, teleport |
| `gen_arche/ZOR_ArcheSud`, `HEG_ArcheMagicMush` (red) | sud0 | walk-in | `Ed.dsg103 == 1`: `ChangeLevel` (by own dsg0: 1 totalski, 2 cavedoc, 3 spycarota, 4 spynorth, 5 mammyon, 6 bar, 7 pyr_fou, 8 pharmmap) |
| `gen_arche/SDO_ArcheRetourneDansSud` (red) | every world's start and end | walk-in | `Ed.dsg103 == 1`: `PROC_29('sud0', ...)` by own dsg1 (a byte), heading per destination |
Per level after step 1: sud0 20, pyramide 15, cavedoc 14, carota 12, marmite 8, north 8, cock01 6, LeCanyon 6,
Land 5, Transit 2, BigBoss 1; none in the cutscene-only levels (bar, death, mammyon, outro, pharmmap, pyr_fou, SpyCarota,
spynorth; `tc_level_is_cutscene`). Gap: Totalski and skymap are played as the skier, a level perso, and
`portals()` only runs for the fix Ed, so their way back to sud0 (`GIZ_ArcheRetourneDansSud0`, `NBD_ArcheGagneSkymap`)
is not exported yet.
Left out (they don't start the walk-in or the cover): `GenTransitionSubMap` (zone test COND_13), `ChangeMapDebut/Fin`
(falling off Land -> bigboss), canyon's retry warp, Doc and the training spies, the game-over screen.

### Destinations
| Arch | where | on `Ed.dsg103 == 1` |
|---|---|---|
| `ZOR_ArcheSud` (sud0, the hub) | by its `dsg0` 1-8 | `ChangeLevel` totalski, cavedoc, spycarota, spynorth, mammyon, bar, pyr_fou, pharmmap |
| `HEG_ArcheMagicMush` (sud0) | gated on collectibles (FUNC_93 counters) | `ChangeLevel('skymap')` |
| `DAV_Archesec2a` (Land x4, sud0's FRB_Section1) | in-level | wait 250 ms; `PROC_8(Ed, PositionOf(dsg0), camera, PositionOf(dsg1), dsg3)` = teleport Ed and the camera to the arch's two waypoints; force Ed's arrival behaviour (BEHAVIOUR@0x690004) |
| `ArcheRetourSUD` (Land) and the other levels' exits (`GIZ_ArcheRetourneDansSud0`, ...) | by its `dsg1` | `Ed.dsg136 = heading` (335, 175, 92, ...); `PROC_29('sud0', n, 'heg_positioned<place>', 'heg_positioncam<place>')` = change level and place Ed / the camera at named objects (`n` is read but unused on PC) |

Exe: `ChangeLevel` = PROC 7 -> `GAM_Basic_004105f0` (store the name, engine state 7); `PROC_29` -> `GAM_Basic_00410640`
(the same plus the two names, `GAM_Basic_0040f670`); `PROC_8` -> engine state 0x1B (teleport); `PROC_242(deg)` =
`FUN_0048ba00`: the perso's rotation about Z = deg x pi/180 - pi/2 (wrapped), i.e. set the heading.
Not portals but the same mechanism: `GenTransitionSubMap` persos (Land, zone-triggered PROC_8 teleports via
`GameAction2` / `GameState63`), `ChangeMapDebut/Fin` (Land -> bigboss), Ed's B59 (`ChangeLevel('land')` when
dying), Doc's `PROC_29('transit', ...)` (the training levels).

### Arrival
Ed lives in the fix, so his dsgvars survive the level change. The new level starts him in **B63**: with
`dsg134 == 2` it sets his heading (`PROC_242(dsg136)`) and plays **state 229 (stepping out of the portal)**; with
`dsg134 == 1` it puts him at a saved position (`dsg139`, camera `dsg140`). Then normal play (B79). The engine starts
the **reveal** transition after every level load (`GAM_Initeng_0040de70`) and after a PROC_8 teleport
(`GAM_00410ca0`).

### Screen transitions (data in fix.gpt, drawn as a viewport sprite)
- **Cover**: `GameAction2` (`GAM_00429070`) -> `FUN_004cb380`. **Reveal**: `GAM_00429010` -> `FUN_004cb4b0`. Each
  frame `GAM_004290b0` / `GAM_00429040` step them (`FUN_004cb400` / `FUN_004cb530`) with the frame's ms. When the
  cover is done, `GameState63` (`GAM_004290a0`) turns true and the viewport is cleared to black every frame until
  the reveal starts.
- **Configuration**: a 0x30-byte block read from **fix.gpt at 0x12a8** (`FUN_004cb350`, the last read of the fix
  loader `GAM_Initeng_0040d700`) into `DAT_0051b040`: {flags, RND channel, kind, threshold 15, range 20, effect A,
  effect B} for the cover, then the same for the reveal. Random r in [0, 20] (`FUN_00450070`): r > 15 -> effect B
  (about 24 %), else A. Each effect = {variant list, count, sprite, start, end, current, speed, ...}; a random variant
  (`FUN_004cb980`) is put on the sprite. All times: value += speed x ms / 16.
- **Effect A (`FUN_004cb600`), 20 variants** `Textures/Effects/Alpha1..20` (256x256, RGB black, the pattern in
  alpha: frosted glass, ripples, swirls, blobs): a full-viewport black quad with **alpha test `alpha <= t`**
  (GliDX6 `GLI_DRV_fn_vDrawRectToViewPort`: mode 2 = D3DRENDERSTATE_ALPHAFUNC LESSEQUAL, ALPHAREF from the value);
  cover t 0 -> 1 at 0.01 per 16 ms (1.6 s), reveal 1 -> 0. Black spreads along the mask's gradient.
- **Effect B (`FUN_004cb710`), 2 variants** `Effects/Rotate1` (Ubi Soft logo) and `Rotate2` (Tonic Trouble logo):
  the texture on a square centred on the viewport (corners at 0.5 +- r (cos, sin)(a - k pi/2), viewport units),
  spinning by 0.1 rad per 16 ms; cover: r from 0 to 50 with speed 0.01 + 0.003 per 16 ms per 16 ms (about 3 s),
  reveal: r from 50 down to 0.01, speed -0.55 + 0.003 (decelerating), spin -0.1. No alpha test (plain alpha).
- Open: whether the driver also alpha-blends effect A (soft gradient edge) or cuts hard; plan a shader tunable and
  let the user compare with the original.

### Loading screen
The VIG module shows the level's vignette (Vignette.cnt, `files/out/Vignette/Level/<Level>.png`) while the level
loads. Ours loads in a fraction of a second, so per the user: **the vignette for 3 s with a progress bar and
"Loading <level>"**.

## Design
Same split as the rest: the converter reduces other persos' scripts to data (like `ai.proximity_triggers`), the
core runs Ed and sequences everything, the frontends draw.

### Converter (`tools/ttre/ai.py`, .ttai v6, appended tables as v4/v5)
- **Portals**: per arch-like perso of the level (pattern-matched as above, not by family name alone): position and
  facing (its 3x4), capture radius (4), arrival heading, B66 / R4 indices (resolved through the refs), and the
  action: `teleport` {Ed waypoint position, camera waypoint position, delay 250 ms, Ed's arrival behaviour index},
  `level` {name}, `level_at` {name, place, camera place, heading}. One record per destination when an arch switches on
  its own dsgvar (ZOR_ArcheSud: read the instance's `dsg0`). Gated arches (MagicMush) exported with the gate flag
  set; first version: open.
- **Places**: every perso instance name of the level with its position and heading (for `PROC_29` arrivals), plus Ed's
  start (already in the .ttlv).
- The transition block's parameters are fix data, the same for every level: exported once into each .ttai (small)
  so the core has no magic numbers.

### Core
- `tc_ai.c`: `GameAction2` (PROC 2) and `GameState63` (COND 63) through a host flag (`p->transition_request`,
  `p->screen_covered`); `PROC_242` (heading request); `ChangeStateByDirection` (META 12) and
  `TurnTowardOverFrames` (META 19), decoded in one Ghidra batch (they are stubs today: they finish at once), first
  version: walk to the target point in the given state, then face it.
- `tc_ed_ai.c`: portal triggers next to the proximity triggers: capture (the arch's B0 above), watch `dsg103`,
  report the portal's action to the host. Export/import of Ed's dsgvar block so it survives the level change (then
  B63's arrival branch runs by itself).
- New `tc_transition.h/.c`: the exe's effect logic (start cover / reveal with a seeded RNG, step by dt in ms/16
  units, outputs: kind, variant, threshold or angle + radius, done).
- `tc_viewer`: a phase machine **play -> cover -> (teleport: black 250 ms -> move Ed and the camera -> reveal) |
  (level: loading screen 3 s -> host loads -> reveal) -> play**. New API: `tc_viewer_overlay()` (what to draw on top:
  none / mask / logo / black / loading {vignette name, progress, level name}), `tc_viewer_pending_level()` (name +
  a carry blob: Ed's dsgvars, arrival place), and `tc_viewer_desc.carry` for the next level, which then starts with
  the reveal. The orbit camera looks at the arch while Ed walks in (stand-in for the scripted camera).

### Frontends (Swift/Metal and SDL3, the same work twice, small)
- Load the 22 effect textures once (`Textures/Effects/*.png`, already in the app bundle).
- One overlay pass: mask quad (alpha test against the threshold, black), logo quad (rotated square, alpha), black,
  or the loading screen (vignette scaled to fit, bar, text; SDL: debug font, Swift: a text texture or an AppKit
  label).
- Level switch without the picker: on `pending_level`, load the next .ttlv/.ttai/textures/music and init the viewer
  with the carry. Vignette names differ in case from level names (`Cock01.png` / `cock01`): match case-insensitively.

## Steps
1. **Done.** Converter: `ai.portals()` (static evaluation of each perso's scripts against its own dsgvars, flattened
   into capture / fire op lists), `ai.places()` (every level perso by instance name), `ai.transition_config()`; .ttai
   v6; `python3 -m tools.ttre level-ttai <level> ...` prints the portal table. Core: `tc_ai_model` gains portals,
   names, places, transitions (`tc_ai_portal`, `TC_AI_POP_*`).
2. Core primitives (PROC 2, COND 63, PROC 242, META 12 / 19) + `aicheck` script `AI_SCRIPT=portal`: Ed walks into
   sud0's bar arch, plays 228, the cover runs, `dsg103` is set.
3. `tc_transition` + viewer phases + carry; extend `viewcheck`: sud0 -> bar (pending level, then init with the carry:
   Ed plays 229 facing the arrival heading), Land's in-level arch (teleport, reveal). `make -C core check aicheck
   mechcheck crashcheck viewcheck` stay green.
4. SDL frontend (the user tests on Linux), then the Swift app (the user tests on the Mac).
5. Docs: this file -> done, NOTES "Level portals" corrected, core/README backend notes.

## Totalski's finish (looked at 2026-10-24, not implemented)
In the original, after the striped finish flags Ed steps off the board and walks into the exit arch on his own.
Found so far: the finish persos `SYM_Gen_Arrivee` (110, -929, -3093) and `NBD_Arrivee_Et_Retry` (86, -873, -3102)
(zone tests COND_13 on the skier, set `Ed.dsg148 = 3`, its dsg40 = 1 "next" / 0 "retry" and its arrival waypoints
dsg35/36); the skier's **B19** = the finish: dsg43/44 = 1, 500 ms, **state 321** (stepping off), move direction
(0, -11, 0), B13; the exit arch `GIZ_ArcheRetourneDansSud0` (31, -983, -3102) and `SYM_Arche_Retry` (37, -835, -3102)
act on the skier via COND_13. Crude plan: export the fix Ed too where the player is another perso (a second Ed record
in the .ttlv, `<level>_ed.ttai`), force B19 near either finish perso, swap to regular Ed after state 321, let him walk
to the exit arch (or just hand control back).

## Not in scope (later)
`GenTransitionSubMap` zone teleports (COND_13 zone test), the scripted camera moves, portal sounds (PROC_118),
the collectible gate, death -> Land (B59), Doc's training levels (`transit`), saved-position arrivals (`dsg134 == 1`).
