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
| Whole exe after the full dump (2026-10-29, docs/EXE.md "Full dump") | 700 KB | 160 KB (23 %) | 540 KB (77 %) | 0 |
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

7. **Sectors** (user, 2026-10-29: the ghost water is the port misreading which sector Ed is in). Read who sets a perso's
   current sector (SectorInfo +4: the integrators' 4th argument, the env lookup's input) and which of the sector data's
   {head, tail, count} lists the renderer walks for visibility (GLI); export sectors and track the current one in the
   core. Replaces the water's box rules (docs/NOTES.md "Ghost water") and gives the game's own culling (docs/VITA.md).
   **Done 2026-10-30, awaiting a playtest** (docs/NOTES.md "Sectors"): portal crossings every frame (FUN_00451670 from
   GAM_0042c810), the down-ray lookup on teleports (FUN_00451240), the water in the perso's own sector, and drawing from
   the camera's sector with its +0x4C list (FUN_00451d80, list mode 0); the viewer draws from Ed's (the user's choice) by
   default (K: all sectors / Ed's alone). Checked against the
   snapshot's own perso sectors (sectorcheck). Left: moving persos' sectors (per-sector fog and lights: ported 2026-10-30).

## Rendering and lighting
**Ported 2026-10-30** (docs/NOTES.md "Lighting: the original's formula"): the vertex colour formula, the sector lights
(spherical with the per-object cull, parallel, ambient), the sector / env fog, the material coefficients; not
hotspots, MS lights, the underwater tint, the surface waves. The list below is the state before that:
GLI (29 KB) is 0 % read. Everything lighting does now is inferred from the data, not read from the code:
- the radiosity combine (x 255 / 128, overbright x 2)
- which radiosity set is used (A, B or C, and whether they blend)
- sky sectors getting neutral light
- fog taken as the most common per level
- persos lit by a placeholder key light (sector lights and ambient are not looked up)
- the material ambient / diffuse coefficients unused
- additive *_ANU textures drawn as alpha-tested
- no back-to-front sort for blended geometry (done 2026-10-29: tc_viewer_draws sorts them by mesh centre, the
  transparent statics cut into TC_VIEW_PIECE_SIZE pieces at init)

The GLI front end (Light.c, Material.c, Camera.c) is in the exe and readable. The final rasterising is in
GliDX6vr.dll; its `GLI_DRV_*` exports are the contract. Suggested order after the mechanics: sector lights and
ambient for persos (the most visible gap), the radiosity combine scale, blend modes and sorting, then fog.

### Read from GliDX6vr.dll and the exe's GLI (2026-10-29; cut-outs and addressing ported the same day)
GliDX6vr.dll (sha256 `116f3b45…`, same on disc and installed, not packed; REA/Ghidra 12.1.4, evidence
`ev_ee8aa4ff…`, `ev_4e919145…`, `ev_924311d9…`, `ev_fe816fba…`, `ev_dd4e95a7…`) does no lighting: vertices arrive
lit. Per draw it picks a D3D6 texture-stage preset (built once in FUN_10012550, names from its own strings):
| preset | when (texture flags at TextureInfo +8, material alpha at +0x335c8) | colour | alpha | blend |
|---|---|---|---|---|
| OMD (DAT_1003c978) | opaque | texture x diffuse (MODULATE) | - | off |
| ATe (DAT_1003c97c) | texture flags & 0x12 | texture x diffuse | texture | off; alpha test GREATEREQUAL **225** |
| TMT (DAT_1003c960) | flags & 0x8, material alpha 1 | texture x diffuse | texture | SRCALPHA, INVSRCALPHA |
| AT (DAT_1003c970) | flags & 0x20 (not 0x8), material alpha 1 | texture x diffuse | texture | SRCALPHA, **ONE** (additive) |
| TMD / TMA | material alpha < 1 (TMA when flags & 0x1a) | texture x diffuse | diffuse / texture x diffuse | SRCALPHA, INVSRCALPHA |
| AT (DAT_1003c988) | lightmap, single pass | stage 1: **MODULATE2X** (lightmap x stage 0) | - | off |
- So static geometry is never overbright: MODULATE clamps the vertex colour at 1. Only lightmaps get x 2 (our
  255/128 = 1.992 is right for them). Our radiosity x 255/128 is a guess that probably stands in for the missing
  dynamic light (below); where the i16 radiosity becomes the vertex colour is not traced yet.
- Texture address modes per axis from the driver texture's byte +0x155 (1: wrap V, 2: wrap U, 3: both; else clamp).
  The exe sets it from the file name (GLIGLOU_Tex_00440c20 -> GLIGLOU_Tex_0043f220): the stem (an "NZ" suffix
  skipped) ending in TXY / TX / TY gives 3 / 1 / 2, else "_TXY" / "_TX" / "_TY" anywhere, else 0. The same loader
  sets flag 0x12 (cut-out) for an "NZ" stem or "_NZ...", and 0x20 (additive) for "_AD" anywhere, which no level
  material has (strings at 0x4ddfec..0x4de064, read with REA, evidence `ev_e214e63a…`). Checked against the data:
  TY-named textures leave 0..1 along engine u (569 elements, 9 along v), TX along v, so the axes are right in
  engine UVs.
- **Ported:** the exporter sets TC_FLAG_ALPHA_TEST from texture flags 0x12, TC_FLAG_TRANSPARENT from 0x8 (in that
  order; same output as before in all 21 levels: every texture with alpha has one of the two), and
  TC_FLAG_WRAP_U / _V from the name rule (ttlv.py `tiling`); both renderers pick one of four samplers per draw and
  alpha-test at 225/255 (TC_ALPHA_REF; the alpha-to-coverage edge is centred there too). **Open:** ~130 elements
  in the 21 levels have UVs beyond 0..1 on an axis the rule clamps (73 are the LAVAGAIN lava flipbook; MORT, PORTE,
  NG_04, OMBRE ...), which the original does not show smeared, so something about them is unread; the exporter
  repeats such an axis (`fitted_surface`) instead. Find what the exe does with them before clamping those.
- Fog (GLI_DRV_vComputeFogEffect, FUN_1000d8b0): per vertex on the projected vertex's z (view depth, not eye
  distance), parameters {near, far, ?, density at near (/255), density at far (/255), r, g, b}: linear between the two
  densities, constant before near and past far; D3D vertex fog (specular alpha) with FOGCOLOR.
- Dynamic light, exe GLI: the light record is 0x100 bytes (Light.c, GLIGLOU_Light_00443460): +0 on, +8 type
  (Parallel, Spherical, HotSpot, Ambient: console strings), +0xc far, +0x10 near, +0x14 / +0x18 big / little alpha,
  +0x24 matrix (0x58), +0x94 colour rgba (default 0.8), +0xa6 / +0xa7 mode bytes (+0xa7 picks one of three
  accumulators: LAB_00444530 / 004444e0 / 00444470). GLIGLOU_004442c0 applies one light to an element (light
  position into object space); GLIGLOU_00443a10 is the spherical term: full N.L inside near, falling off linearly in
  squared distance to far. Callers: GLIGLOU_004451b0 <- FUN_00446050 <- many GAM draw paths. Persos also carry their
  own light at perso +0x20 ("MS Light", GAM_Commands: "This character has no MS Light"; accessors GAM_004304a0..).
- **Fog, read (2026-10-29):** FUN_004cee60(near, far, third, rgba) -> FUN_004ceec0 builds the driver block {near,
  far, third, density 0 at near, 255 at far, rgba} (0x24 bytes, at the shared state +0x335ac; FUN_004cf030 turns it
  on, per sector from FUN_00451e80). So the fog is linear from none at near to full at far, as ours; the third
  distance is stored but unused by the driver's formula. The difference was the depth: ported (both shaders fog by
  view depth, forward = cam_up x cam_right). The level's visual environment record is {u32, ptr, near, far, third,
  rgb} (Totalski 200 / 400 / 600, light blue; north 80 / 120 / 128, black).
- **Vertex colour, read (2026-10-29, not ported):** per object, FUN_00446050: a float rgba accumulator per vertex
  (shared state +0x1fbf0) is zeroed, or seeded from the baked radiosity when draw flag 0x1000 is set
  (GLIGLOU_00445270 -> FUN_0044d690: i16 x 1/256, constant 0x4d2710); GLIGLOU_004451b0 adds the lights of the list
  at 0x90baa0 (count 0x90bc30): type 1 Parallel GLIGLOU_00444fc0, 2 Spherical 004442c0, 3 HotSpot 00444630 (range
  test 00444550), 4 Ambient 00444f60 (into the global ambient G, shared state +0x33470); then the element's material
  (FUN_00445b50, visual material +4 ambient, +0x14 diffuse: add = ambient + G x diffuse, mul = diffuse) and the
  conversion (FUN_00448e60 / 00448cf0 / 00448fb0, same formula): byte = clamp(512 x (acc x diffuse + add), 0, 255)
  (the float-bits trick with 1.5 x 2^14, constant 0x4d26b0). For a baked vertex with no lights:
  **light = min(1, (R / 256 x diffuse + ambient) x 512 / 255)** per channel, then MODULATE with the texture.
  Materials: diffuse 0.7 nearly everywhere; ambient 0.1 (sud0, Totalski, LeCanyon) or 0 (north, pyramide, Land).
  Mean baked light, ours (clamp(R) / 128) vs this: sud0 0.27 / 0.39, Totalski 0.48 / 0.45, LeCanyon 0.51 / 0.47,
  Land 0.60 / 0.46, north 0.62 / 0.41, pyramide 0.93 / 0.63; ours goes to 2x, the exe clamps at 1.
- **Lights per draw** (the sectors themselves: docs/NOTES.md "Sectors", 2026-10-30): drawing a sector (FUN_00451d80) clears the list, pushes the sector's lights (FUN_00452410:
  sector data +0x34 always; +0x28 when the draw flags and the sector's flags have 0x8000), draws the sector
  (FUN_00451e80), then the sectors of one of its lists +0x4c / +0x58 / +0x64 (mode DAT_004fe730) with the same
  lights: the visibility lists (BUGS.md "Level Sectors"). So the lights depend on the sector being viewed from.
  Census: +0x34 is nearly empty (Totalski 1 hotspot, north 2 spherical, none in sud0 / LeCanyon / bar / pyramide);
  +0x28 holds the many (Totalski 360 spherical + 5 hotspot + 7 ambient 0.8 grey, north 140, pyramide 382; sud0 and
  LeCanyon none): probably the lights for persos (flag 0x8000). Static geometry then gets ~no lights: the formula
  above holds for it without sectors. **Open:** lightmapped levels (bar, death): every material has ambient 0, so
  the formula would give their vertex colour 0 (then x lightmap x 2: black), which the game doesn't show: their
  accumulator is seeded some other way (lights? ISI +0, "not a radiosity" there), unread.
- **The frame, read (2026-10-29; investigation only, nothing ported).** The draw routine is unanalysed code ending
  at 0x428e9a (no Ghidra function; REA xref 0x428e45, disassembled with llvm-objdump -D): picks the sector to draw
  from (mode byte 0x8c1764: 0 = GAM_00424940(1), entry 1 of the table at 0x8c1704, presumably the main character's
  sector), sets the visibility-list mode (FUN_00451d70, byte 0x8c1765), then FUN_00452c70(camera, viewport,
  context 0x8bd0e8, sector, mask = *0x8bd080) -> FUN_00451d80 (lights pushed, sector drawn, its listed sectors
  drawn, lights cleared), then GAM_004149f0 / 00414910 / 00414330 / 00414170 / 00413fc0 / 00415020 and
  FUN_004568e0 (other passes, mask 0xf7fbfff7 / 0xffffffff). The mask 0x8bd080 is the console's display flags
  (GAM_Basic_0040f850: case 0 sets all bits or none, case 3 toggles 0x8000, inverted): all bits in normal play, so
  the sector's +0x28 list does light static geometry too. Its value isn't in the dump (zero there, as 0x8c1764).
- **Per object:** the scene walk (FUN_00483510, recursive) pushes an object's attached light (SuperObject +0x30,
  GAM_00437810; null on every static object in sud0 / Totalski / north / bar / pyramide) for it and its children;
  FUN_00483820 masks with the object's draw flags (SO +0x2c, GAM_004307d0; bits 0x1000 and 0x8000 are set on every
  IPO and sector in 9 levels, so they don't separate anything). IPOs (type 0xd / 0x15) draw through
  GLIGLOU_00442840: LOD by distance, then the radiosity of the IPO's runtime buffer (IPO +4), which FUN_00449860
  fills from the ISI set DAT_004fe5f0, blending towards DAT_004fe5f8 over time (the day / night change).
  Characters are drawn inside the sector pass, under the same lights.
- **Lights:** ambient (GLIGLOU_00444f60) adds its colour to G with no range test; light byte +0xa6 = a spherical
  light without N.L (GLIGLOU_00444100); +0xa7: 0 colour and alpha, 1 alpha only, 2 colour only. Per-character MS
  lights (perso +0x20 -> +0x2c) are lights characters carry (north: two orange spherical, near 1, far 30: torches);
  elsewhere one black zero-range hotspot per level.
- **Materials decide most of the look:** character parts are mostly ambient 0.87 (some 0.36, 0.1), so under the
  formula most characters are flat at full texture brightness (2 x 0.87 caps at 1; lights only add), the 0.36 ones
  start at 0.72 and get shaded. Statics are ambient 0.1 (sud0, Totalski) or 0 (north, death, bar).
- **Still unexplained:** skies (ambient 0.1 / 0, radiosity ~0: the formula makes them dark, the game shows them lit;
  sud0 has no lights at all), and the lightmapped levels (bar: ambient 0, no readable radiosity, its only light a
  darkening spherical light, colour -0.3, near 0.5, far 64: the formula gives black). Both point at an input not
  read yet: what the IPO's runtime radiosity buffer holds for them (bar's ISI +0 is "not a radiosity" to our
  exporter), or a draw path for them that skips the lighting.
- **Lightmapped triangles, solved (2026-10-29):** GliDX6vr.dll's vertex setup for a triangle with a lightmap
  (LAB_100037a0, chosen in xSendElementTIToClip when the element has per-triangle lightmaps and the texture has no
  0x1a flags) stores 0xFFFFFFFF as the D3D diffuse colour of all three vertices (`orl $-1, %ecx`; the plain one,
  LAB_10003640, copies the computed colour). So a lightmapped triangle is texture x lightmap x 2 whatever the lights
  and materials say (bar's darkening light and ambient-0 materials don't matter): what the port does already.
- **Which baked set, read:** GAM_Initeng_0040de70 reads, as the last values of the level's .gpt stream
  (SNA_0045ac50: sequential reads, pointers relocated), the current / next / target set (0x4fe5f0 / 5f4 / 5f8, a
  byte each), a flag (5fc), the set count (600), two u32 and the blend progress and rate (60c, 610). They are the
  last 21 bytes of the decrypted .gpt: the count is 3 for sud0 and Land and 1 elsewhere (so the reading is right),
  and **every level starts at set 0**. Nothing else in the exe writes them (only FUN_00449860's own blend), so as far
  as read the game never leaves set 0.
- **Skies, narrowed:** raw sky radiosity per set (mean, max): sud0 -13 / 25, 178 / 393, 32 / 51; Land 135, (biased),
  59; skymap 204; north 32 / 259; Totalski 0; death 2. Under the formula with the sky materials' ambient (0.1 or 0):
  sud0 set 0 and Totalski, north, death come out dark, Land and skymap lit. Lights only from the sector the frame is
  drawn from (the neighbour draw FUN_004521b0 pushes only lights attached to objects, null on statics): Totalski's 7
  ambient lights (0.8) sit in sct12v..15v, sct27v, sctfinish, sctfleche03 (everything full bright while the frame is
  drawn from those), north's one in sctciel. Open: how the original looks there (the user can check in Wine).
- **sud0's day / night cycle, found (2026-10-29).** User: only sud0 passes time (starts on set 1, then 2, 0, ...);
  Land's other sets are leftovers (it uses set 0). The set-change function is unanalysed code at 0x449ad0
  (`SetLightSet(n, duration, absolute)`; found by searching the raw exe for 0x4fe5f8 / 0x4fe610, disassembled with
  llvm-objdump -D): refused while a change runs (0x4fe5fc); duration 0 switches at once (to n if absolute, else n
  sets on); otherwise target = n or current + n, next = current + 1, rate = sets to go / duration per tick of the
  game clock 0x8bd090 (ms), flag 0x4fe5fc set; FUN_00449860 then blends IPO radiosity current -> next (lerp of the
  i16 values) and steps on at progress 1 (next = (next + 1) % count) until it reaches the target. Its only caller is
  AI procedure 283 (PROC_283_0048ca60, g_ai_procedures +0x8d8: args set, duration, absolute). In sud0 one perso
  calls it: d.persos()[325] (family 18, no geometry, an invisible manager), dsg3 = 90000, dsg4 = 180000, dsg5 = 1:
  B1 (once): PROC_283(RandomInt(0, 3), 0, 1), start the hold timer, -> B0; B0: when TimeElapsed(dsg0, dsg1):
  dsg2 = RandomInt(10000, 60000), PROC_283(1, dsg2, 0) (one set on over 10-60 s), wait dsg2 + 500, dsg1 =
  RandomInt(dsg3, dsg4) (hold 90-180 s), dsg0 = GetTime. So: a random start set, then forever one set on every
  hold + transition. **Correction (2026-10-29, later):** Land calls it too: perso 225 (genLand / YBA_LightMaker /
  YBA_Lighter, no geometry) is **lightning**: B0 waits RandomInt(2000, 5000) ms, then B1 forever: when
  TimeElapsed(dsg3, dsg0): dsg2 = RandomInt(1, 3), SetLightSet(1, 0, 1), wait RandomInt(50, 70), SetLightSet(2, 0,
  1), wait RandomInt(50, 150), the flash pair again if dsg2 > 1 (and if dsg2 > 2), SetLightSet(1, 1000, 0) (set 2 ->
  0 over 1 s), wait 1500, dsg0 = RandomInt(5000, 15000). Land's set 1 (the "biased" one, raw ~25600) is the white
  flash. No other level (all 21 scanned, `ai.light_persos`). The user had forgotten the lightning and asked for it.
- **RandomInt (FUN_00450070), read:** min + t x (max - min) / (n - 1), t from a table of 0..n-1 shuffled at start
  (FUN_0044ff50: srand(GetTickCount), swaps; FUN_0044ffe0 subtracts the minimum, n - 1 = the largest), stepping one
  entry per call per channel. So max comes up only for the table's last entry: in effect [min, max). The interpreter
  had max inclusive (hi - lo + 1), which gave sud0's RandomInt(0, 3) start set a 1-in-4 chance of a set 3 that doesn't
  exist; fixed in tc_ai.c (it changes every RandomInt of Ed's AI slightly too).
- **Ported (2026-10-29): both run from their own scripts.** The .ttlv's LAIS chunk holds the SCPT block (nodes,
  behaviours, dsgvars) of every perso calling PROC_283; the viewer runs them with the AI interpreter (models without
  states: scripts only) on the game clock (real time, stopped by P), and applies their SetLightSet calls as 0x449ad0
  does (refused while a change runs; duration 0 = at once; else set by set at sets to go / duration). The shaders
  blend the shown set and the next (both frontends). Difference left: the exe lerps the raw i16 radiosity, then
  clamps; ours lerps the clamped bytes (the same wherever both ends are within 0..255; Land's flash set is far above,
  so its 1 s fade starts dimming later in the exe than in ours). Number keys hold a set; 0 follows the scripts
  again.
- **User observations (2026-10-29, the original in Wine):** Totalski's sky is bright ("inundated with fog": its fog
  is light blue, near 200 / far 400, so the sky dome is fully fogged whatever its light); some spots on the slope
  are very dark in the port and lit in the original (matches the ambient-light sectors sct12v..15v, sct27v,
  sctfinish, sctfleche03: sector lights, not ported); north's sky is dark with the stars shining through (matches
  the formula: ambient 0, radiosity ~32); characters look a little dull in the port, the portals most of all
  (matches: their materials, ambient mostly 0.87, make them flat at full brightness in the original). The original
  hides imperfections with a fixed camera; the user keeps the free camera and wants it improved (the camera was one
  of the game's weaknesses).
- **Lights, census and corrections (2026-10-30, read, not ported).** At most 21 lights in one sector (Totalski sct29;
  pyramide 14, pharmmap 11, north 3; sud0, LeCanyon, cock01, mammyon none), all with +0xa4 = 1 and +0xa6 / +0xa7 = 0;
  no flag limits a light to characters (nothing reads +0xa5), so the sector's lights reach static geometry too. Odd
  data: north has darkening lights (colour -10, e.g. near 3 / far 8) and +7 ones (near 32.3 / far 35.2); Totalski
  pairs a +5 and a -5 light at the same place and range in 13 sectors (they cancel). **Per-object cull:**
  GLIGLOU_00443940 zeroes the object's bounding radius (state +0x3c, asm 0x44395c) before testing, so a light reaches
  an object only when the object's bounding-sphere centre (state +0x30, FUN_00445d50: geometric +0x34 through the
  object matrix) is within the light's far range; then per vertex: full N.L inside near, x (far^2 - d^2) / (far^2 -
  near^2) out to far (GLIGLOU_00443a10 / 00443c50). **Correction:** Totalski's 7 ambient lights all sit in virtual
  sectors (sct12v..15v, sct27v, sctfinish, sctfleche03: no portals lead in, no floors), and the listed-sector draw
  (FUN_004521b0) pushes no sector lights, so as read they never apply. The dark spots on the slope are more likely the
  missing ambient floor: the formula never goes below 2 x ambient (0.2 there), the port's clamp(R) / 128 goes to 0.
