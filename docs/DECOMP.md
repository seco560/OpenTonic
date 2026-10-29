# Decompilation status (2026-10-28, updated after the collision port the same day)

Measured with `python3 -m tools.exe.coverage --unread 40`, which can be re-run. A function counts as **documented**
when its address appears in docs/, core/ or the name/signature tables, which means it was read and used. It counts as
**dumped** when only a raw or typed decompile exists, and as **unseen** otherwise. The call graph was cross-checked
with REA (Ghidra 12.1.4, dump sha256 `fea1140e…`): forward graph of GAM_0042b520, evidence
`ev_28f6e65d…`. REA's 107 nodes contain all 101 nodes of index.json's closure. REA snapshot:
`files/out/_decomp_artifacts/rea/TonicTrouble_dump.rea-snapshot`.

## Numbers
| scope | code | documented | dumped | unseen |
|---|---|---|---|---|
| Whole exe (3,652 functions) | 700 KB | 151 KB (22 %) | 116 KB (17 %) | 433 KB (62 %) |
| Mechanics closure (726 functions) | 226 KB | 115 KB (51 %) | 66 KB | 45 KB |
| - DNM integrators + request handlers (4a7-4b9) | 57 KB | 51 KB (89 %) | 2 | 4 |
| - MEC collide-and-move, responses, step-up, re-snap (4b9-4c1) | 20 KB | 15 KB (72 %) | 6 | 0 |
| - Forces / vector math (4c1-4cb) | 32 KB | 25 KB (79 %) | 5 | 2 |
| - Sweep entry, hierarchy walk (43b-43e) | 5 KB | 3 KB | 1 | 1 |
| - **Collision geometry library (460-472)** | **59 KB** | **12 KB (21 %): all of Ed's path** | 46 | 0 |
| - Matrices / transforms (47a-480) | 11 KB | 1 KB | 4 | 6 |

Most of the rest of the exe is not gameplay: SND 31 KB, GLI 29 KB (renderer front end), SNA, GEO, IPT, MNU, FON, FIL,
SAI, VIG, SCR and CMP add up to about 150 KB that the port replaces with its own loaders and backends. Of the 157 KB
in GAM, the parts that still matter for gameplay are the camera, PlayAnim's root motion and other persos' behaviour.

**What this means:** the physics *policy* was read before this session: integrators, forces, the 6-pass loop,
classification, responses, step-up and re-snap. The collision *geometry* was not: `tc_mec_sweep` was our own
swept-sphere code, which is why collision felt "close but vibed". It is now the exe's own algorithm for Ed's spheres
against the level's triangles, proved against the exe under an emulator (below). The rest of the library serves
shapes Ed does not use, except the props' spheres and boxes.

## The collision library (read and ported 2026-10-28)
Decompiles: `files/out/_decomp_artifacts/col/` (all 200 functions of 0x45FD70-0x472000, one file each, from REA's
batch decompile). The path Ed's spheres take, top down:
- **`FUN_0043bff0`** (the sweep MEC calls): two sweeps of the perso's ZDM zones, chosen by `FUN_00420610`
  (list 1 = ZDM). Mask 1 takes the zones whose sphere's collide flags have **8**; mask ~1 takes the others. A zone
  flagged **0x10** is never taken (`FUN_0041fa50`). The flags are the sphere's GameMaterial -> collide material
  (+0xC) -> u16 at +2 (`FUN_004493d0` / `FUN_004497c0` / `FUN_0042f770`): the same field as the triangles' collide
  identifiers (0x8 = the hang spheres).
  - The flag-8 sweep counts only when its contact is a floor (normal z >= 0.93, `DAT_004dc9bc` = 0x3F6E147B) and not
    later than the other sweep's.
  - The hit record: [0] 1 static / 2 perso, [1] own zone type, [2] t, [3..5] normal, [6..8] point, [9..11] the edge's
    direction (edge contacts), +0x78 own shape (3 = sphere), **+0x7A contact kind: 1 face, 2 edge, 0 corner**. The
    old guess in docs/NOTES.md (0 / 3 / 4) was wrong.
  - Own zone type `FUN_0043bf70`: flag 1 -> 1, 2 -> 4, 4 -> 3, 8 -> 2, else -1.
- `FUN_0043c830` -> **`FUN_0043c410`** walks the super-object tree. For each node with a bounding volume it runs
  `FUN_00464650` first. IPOs give their collide object directly; persos (types 0xD, 0x15) through
  `FUN_00451780` / `FUN_00451790` / `FUN_0041ea50`. Each pair goes to `FUN_004645c0` (mask 0x1FFFFFF) -> the
  context at **0x50D840** -> **`FUN_00464440`**.
- `FUN_00464240` (setup) moves both matrices into the obstacle's axes and sets context +0x8E9 = `FUN_0046b9e0`(own
  object): 1 when the own object is spheres only. **That is Ed: `FUN_00464440` then hands over to `FUN_004657f0`.**
  The general path (own triangles, the obstacle's edge and vertex lists `FUN_0046bd60` / `FUN_0046c880`, mask
  families 0xF / 0xF0 / 0x1800 ...) serves other shapes.
- `FUN_004657f0` -> `FUN_004656d0` per own sphere element, by obstacle element type:
  - 1 (triangles) -> `FUN_00465520` -> per sphere and triangle `FUN_00464c80` -> **`FUN_00465b60`**
  - 2 -> `FUN_00464e50`, 7 (sphere) -> `FUN_0046d740`, 8 (box) -> `FUN_0046d9e0`. **Not ported**: props' spheres
    and boxes are still triangulated by the exporter.
  - With an octree (+0x5C0) `FUN_004650b0` picks the triangles; same test.
- **`FUN_00465b60`** = `tc_mec_sphere_tri`: the face first, only when moving into its front (d . n < 0; touching
  counts: 0 <= distance <= -d . n; the plane point inside by `FUN_00465870`, cross products on the normal's largest
  axis). Else the three edges a-b, a-c, b-c (`FUN_0046a220`, swept capsule, the earliest; no side test). Else the
  three corners (`FUN_00469500`). A sphere that already overlaps an edge or corner does not hit it (the smaller root
  must be >= 0). `FUN_00464c80` skips everything when the sphere does not move, and keeps a contact only when
  earlier than the best so far (`DAT_0050e140`, reset to 2 by `FUN_00460210`).
- The stored face normals (element +0x10) equal the winding's normals: checked on bar, Land, sud0 and Totalski, all
  10,986 triangles (6 degenerate in bar). So the core's computed normals are the exe's, and **faces are one-sided**.
- Step-up (`MEC_004bbc4c`): ground last frame, **own zone type 1** (a flag 1 sphere), a step height, and **an edge
  contact or a wall within the lean**. `MEC_004bb06e` accepts the probe's floor at t = 0 and does not check the lift's
  way. `FUN_004ca170` = the contact point moved onto the start's horizontal plane.

**Proof:** `tools/bin/oracle-venv/bin/python -m tools.exe.oracle` runs the dump's `FUN_00465b60` under Unicorn and
`tc_mec_sphere_tri` on the same random spheres and triangles. Face, edge and corner hits are aimed for, along with
touching starts and moves exactly along an edge. With the x87 in single precision (as Direct3D sets it) and the
core built with `-ffp-contract=off`: **200,000 of 200,000 cases agree** (seed 7; outcomes: 37,247 face, 41,451 edge,
7,114 corner, 114,188 none). With the x87 at its power-on 64-bit precision, 99.5 % agree; the rest are touching
cases that rounding decides.

## Ledge hang, found and ported (2026-10-28, second pass)
The user: in the original Ed grabs ledges too high to land on with his feet. The "hang spheres" do nothing (right:
see below). How the exe does it:
- **Ed's zone 9 is a point** (collide element type 5, Points), 1.3 ahead and 2.0 up, its vertex's material flag 8. It
  is active in about 220 states: running, jumping, falling, hanging. `FUN_00470b40` sweeps the zone's vertices as
  moving points; `FUN_0046ff70` (= `tc_mec_point_tri`) tests a point against a triangle: the face only, moving into
  its front, strictly in front of the plane (`FUN_004683b0`, `FUN_00467d40`). Its flag 8 puts it in
  `FUN_0043bff0`'s floors-only sweep: it can only land on a flat top.
- **The material conditions read Ed's own part.** `COND_GroundMaterialIsEtc_004985e0` takes the report block
  (dynamics +0xB0, `AI_DynReport_00491e40`) +0x8C / +0x108 = the ground / wall record's +0x70, the *own* material.
  `FUN_00497890` tests `(identifier & mask) != 0`. So `GroundMaterialIs(8)` = "the hand point is on a ledge" ->
  reflexes R0 / R25 / R26 -> B19 -> state 46 -> B82 (hanging). The level's own collide identifiers (the obstacle
  side, e.g. the 0x8 hang spheres on Land) are read by no condition: the CLID chunk is gone.
- **Hanging** (card 11, `FUN_004adb4d`; forces `FUN_004c6993`): while the hand is on the ledge the grip cancels
  last frame's velocity and `FUN_004a7b71` presses the body 0.0125 into the ground every frame, so the hand touches
  the ledge again each frame; R10 lets go 200 ms after `GroundMaterialIs(8)` fails (the ledge ends). Not ported:
  `FUN_004c6993`'s alignment (`FUN_0043cf80` -> `FUN_00463940`, 2.3 KB, unread: it finds the ledge's edge near card
  +0x14 and turns Ed square to it).
- **Climbing up** (B43, state 153) uses **card type 8** (`FUN_004ac62b`, request `FUN_004af04e`): no forces at all
  (`FUN_004c4aa4` empties the list), the velocity is the request's (mode 12: direction x speed in world axes; the port
  takes the root motion, (0, -0.85, +2.28) over the climb), no ground projection, no gravity: Ed rises onto the ledge.
  Before, the port ran type 8 as a walk: the projection on the ground removed the lift.
- **Checks:** `AI_SCRIPT=hang` / `shimmy` with `AI_START` 3.5 from a ledge (aicheck; the camera now follows
  AI_START's heading): Land `125.88,35.44,-32.54,-7` (a 1.5-wide ledge: the shimmy falls off its ends, as it should)
  and sud0 `-40.00,32.75,-2.60,-31`: grab at 2.18 s, hold, shimmy 3.2 units at the same height, climb onto the top
  (-0.02 against the ledge's -0.08).

## Jumps, air control and root motion (2026-10-28, third pass)
Read from the exe; how each was found is noted so it can be checked:
- **The take-off is an animation event.** `FUN_004366c0` (called by the PlayAnim update `FUN_00415c00`) walks the
  playing animation's events up to the current frame; `FUN_00436910` case 1 (event type 1) pushes a mechanics
  request {mode = the event's value, the state's card}. Ed's jump state 43 has {type 1, mode 7, frame 0}: on the
  air card (`FUN_004b0f2a`) mode 7 zeroes the velocity and wants ProposalVecA in Ed's axes, the vector B85 stored a
  moment before (8 x stick ahead, 1.3 x 12 up). 16 of Ed's states carry such events (mode 7 on air cards: 43, 58,
  110, 202, 257; mode 1 on swim cards; mode 10 on ski and swim cards). Exported as the .ttai MECE chunk; the port
  queues them like the AI's SetMechMode* (`queue_mech_request`, tc_ai.c).
- **ProposalVecA only stores** (`AI_00488070`); the proposal lives in the dynamics and keeps its values (the port no
  longer clears it each frame, nor on StopMotion, which in the exe zeroes the speeds only). ProposalVecD
  (`AI_004881d0`) turns the stored vector half way to the new one, keeping its length; a zero vector stays zero.
- **Air control is the script's own.** B89 (jumping) and the fall behaviours set the velocity every tick with PROC 35
  (`PROC_DynamicsOnEtc_00488cd0` case 0x23 = dynamics +0x38): horizontal (6 or 7 + 0.1 / 0.3 x dsg105) x stick along
  the stick's direction in Ed's axes, after TurnBySpeed towards it; vertical 0.95 x vz + 0.1 x dsg105 (dsg105 counts
  the ticks the jump button is held, up to 10). Against a wall it uses ProposalVecD instead (no velocity set). The
  port skipped PROC 35: Ed flew straight. The Velocity field reads dynamics +0x25C (`FIELD_VelocityGet_004a6820`).
- **Root motion is found**: `FUN_0042a8d0` (each animation update) sums the frame vectors between the last and the
  current animation position (`FUN_004170d0`) / the tick's milliseconds, puts direction and length in the proposal's
  move direction and speed and pushes mode 12; the frame's rotation becomes a mode 3 turn. The port's "root motion x
  frame rate as mode 12" is that; on the air card mode 12 means nothing (the port used to apply it in the air).
- **The scripts assume a fixed tick.** B89's per-tick 0.95 and +0.1 x dsg105 against gravity (9.81 x 3.5 x tick):
  holding jump with a direction settles vz at about -2.8 u/s with 30 ticks per second (a slow glide), +8.6 u/s with 60
  (Ed keeps rising). aicheck at 30 / 60 / 120 fps, steering jump: apex +2.15 / +2.59 / +6.46. The exe's mechanics
  constants (1/30, 30) point the same way, and the user confirms the game ran at 30 fps (a 60 fps patch breaks the
  jumps). **Done:** Ed's AI and mechanics step in ticks of `TC_ED_AI_TICK` (1/30 s, tc_ed_ai.h); tc_viewer
  accumulates time, steps whole ticks and draws Ed between the last two (position linearly, rotation by quaternion,
  the animation phase within a clip); the headless checks step at 30 by default (aicheck's AI_FPS overrides).
- **Event counters** (`FUN_00436560` / `FUN_004366c0` / `FUN_00436820` / `FUN_00436600`): each animation event has
  a counter (event +0xB start, +0xC reload; 0xFF stays). Ed's jump event is {0, 0xFF}: once per jump, not on every
  loop of state 43 (re-firing it gave a second take-off against a wall); sounds use {0, 0xFF}, {0, 1}, {0, 3} and
  {0xFF, 0} (fired only when a script changes the state). Exported in SNDS and MECE; the port keeps the counters.
- At 30 ticks a tapped running jump rises about 3.1 and clears 2.5-2.9 ledges; the hang tests use north's 3.43 ledge
  (`AI_SCRIPT=hang|shimmy AI_START=43.23,20.38,-43.49,-77 aicheck files/out/ttlv north`): grab at 2.17 s, shimmy,
  climb.
- **Open:** pyramide's wander run at 30 ticks has Ed's middle jitter through a downward-facing overhang for half a
  second (7 front crossings, (-48.8, 179.0, 57.8)); likely the step-up lifting him under it (the exe has no lift
  check). To be read, not guessed.

## Own materials (2026-10-28, second pass)
Every response multiplies the obstacle's mechanics material with the touching part's own (record +0x34 x +0x50
rebound, +0x38 x +0x54 grip; no material = 1, `DAT_004dc9c4..d4`). Ed's parts: feet none (1, 1); body spheres
rebound 0.85, grip 0; some lower-body spheres grip 0.02; zones 19, 20 rebound 0. The port had taken them as 1. Effect:
a head-on hit with the body bounces Ed back slightly (1.15 x the normal speed instead of 1); a ground contact made by a
body sphere has no grip. **The edge riding of the first pass is gone with it** (that was the port's grip 1): Ed walks
up what his feet meet first, about 0.45 high, and stops at 0.5 (mechcheck).

The wall slide itself (`MEC_004bbc4c`) is faithful, checked against the instructions: tangent, then x and y times
1 - softening(vx^2 + vx^2, 36) (0x4bc0e0 loads velocity.x four times: the original's own slip), z untouched.

**What changed in play, first pass** (wander stress test, 60 s per level, old against new):
- 19 of 21 levels run the same or within centimetres.
- pyr_fou loses its 2,622 blocked frames and spynorth almost all of its 1,353. Both start areas trapped Ed between
  surfaces; the new sweep has no tunnelling on any level (no front-face crossing on any path).
- Spheres flagged 0x11 (zones 4, 6, 18; below Ed's feet in sets 2 and 7) no longer collide.
- Steps: a walking or running Ed (sets 0 and 1: only the feet have flag 1) steps up when his feet meet the wall. From
  about 0.5 to 0.7 high, his middle sphere meets the step's top edge first. That edge contact counts as ground, he
  rides up it, his feet reach the face and he steps up. **Retracted in the second pass:** the body spheres have no
  grip (own materials, below), so the exe does not do this; the user saw the hop and confirmed it is not the game.

## Where the port is not the exe (mechanics)
Each item is marked in the code or in docs/NOTES.md.
1. **Prop spheres and boxes** (element types 7, 8 on persos) are triangulated by the exporter (80-face icospheres,
   boxes). The exe tests them as shapes (`FUN_0046d740`, `FUN_0046d9e0`); points against them `FUN_00470590`,
   `FUN_00470700`. Needs a .ttlv chunk for the shapes and their materials.
2. **Depenetration** before each move (tc_mec_walk.c) stays, though not in the exe. Without it the wander test gets
   more stalls (Land, sud0) and two front-face tunnels on the ski levels, so the port still creates overlaps the exe
   does not (sphere sets swapping on state changes, the respawn and re-snap paths). Remove it once those are found.
3. **Hang alignment**: `FUN_004c6993`'s turn towards the ledge's edge (`FUN_00463940`) is not ported.
4. **Perso obstacles**: the flag-8 sweep's exception for perso obstacles (collide set +0x37) and the moving-frame
   second pass (`DAT_008bcaec`) are not ported; the port's persos are static triangles.
5. **Fixed tick**: done (30 Hz, third pass above). Dynamics +0x38C is open.
6. **Root motion**: found (FUN_0042a8d0); the port's equivalent is kept.
7. **Card 11 (hang)**: gravity in the air (x dt^2, negligible) is not applied; see 3 for the alignment.
8. **Skier**: the three stopgaps (ski_unstick, slope_cos 70 deg, ski_unpin) were removed on 2026-10-28 at the user's
   request (in the original he slides on anything; with the exe's collision he does not stick: wander on Totalski and
   skymap, no stalls). The wall stumble is not found.
9. **Other card types**: types 9 and 10 (forces FUN_004c5aa0 and FUN_004c709a, dumped, 8.5 KB together) and the
   fn1 handlers of types 1, 3, 6, 8 and 12 are not read. They matter for other persos and moving platforms.
10. **ANIM_TIME_SCALE 0.25** for non-Ed persos contradicts the animation clock as read (the user matched it against
   the original running in Wine).
11. **Momentum at the jump's apex (deliberate, user's choice 2026-10-29).** Ed's jump reflex R26 (entered through R1,
   which stamps dsg123 at take-off) runs `PROC_35(0, 0, 0)`, state 44, B90 when he starts falling (`PersoStatus69`) or
   500 ms in (rule 9), and after 750 ms (rule 5). The fall's air control (B90 rule 7) sets the velocity again only
   while the stick is past dsg81 (0.25), so with the keys released on the way up the exe drops him straight down
   from the apex. The port keeps the horizontal speed for a reflex's zero in the air (tc_ai.c, PROC 35). B19's zeros
   (the hang grab) are a normal behaviour's and stay. `FUNC_164` (`FUN_0049b350` case 4) returns the byte at
   `0x8C175C`, the **control type** (`InitControlType` / `ChangeControlType`, saved at `0x8F33B4`; `0x8C175B` is
   the camera type); `PROC_293` (`0x48CC00`) toggles it. The port runs type 0 (camera-relative). Type 1 is
   Ed-relative and smooths the air control through dsg154 / dsg155, which carries momentum on keys too. It is not
   ported.

## Plan, in order (mechanics first)
1. **The user checks in the game**: the ledge hang (jump at a ledge too high to step on), shimmy, climb; steps
   (up to about 0.45 now); head-on wall hits (the 0.85 rebound).
2. **Prop shapes** (1): the sphere and box tests, oracle-checked like `FUN_00465b60`, and a .ttlv chunk for them.
3. **Oracle the responses**: MEC_004bb904 / bbc4c / bc20a and the forces (FUN_004c41c5) are pure math too; the same
   harness can check them against tc_mec_walk.
4. **Jump and air control** (5), read with the typed project: this has the biggest effect on how Ed feels.
5. **Hang alignment** (`FUN_00463940`), then the depenetration (2).
6. **Skier** (8): the state-change trace of the original under Wine (docs/NOTES.md "Sliding stages"). Wine is not
   installed on this Mac; the user's Linux box has it.

## Rendering and lighting
GLI (29 KB) is 0 % read. Everything lighting does now is inferred from the data, not read from the code:
- the radiosity combine (x 255 / 128, overbright x 2)
- which radiosity set is used (A, B or C, and whether they blend)
- sky sectors getting neutral light
- fog taken as the most common per level
- persos lit by a placeholder key light (sector lights and ambient are not looked up)
- the material ambient / diffuse coefficients unused
- additive *_ANU textures drawn as alpha-tested
- no back-to-front sort for blended geometry

The GLI front end (Light.c, Material.c, Camera.c) is in the exe and readable. The final rasterising is in
GliDX6vr.dll; its `GLI_DRV_*` exports are the contract. Suggested order after the mechanics: sector lights and
ambient for persos (the most visible gap), the radiosity combine scale, blend modes and sorting, then fog.
