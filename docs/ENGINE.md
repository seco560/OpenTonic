# The engine's entity loop: persos, the frame, the passes

How the original runs its characters ("persos") every frame, read from the exe, and how the port follows it. This is
the model the port's world is built after (docs/DIRECTION.md): one kind of entity, one loop, in the exe's order. Keep
this file rewritten, not appended: what is read, what the port does, what is open. Started 2026-11-04.

Marking: **read** = read from the exe or the data (function addresses given); **port** = the port's own choice;
**open** = not read yet.

## A perso
Every character, prop, trigger and manager is a perso: a SuperObject of type 4 in the dynamic world, whose data has
- **std game** (perso +4): active (+0x18), the frame mark (+0x19), dead (+0x17), hit points (+0x14 / +0x15), the
  re-init flag (+0x20), run mechanics (+0x2A), animate without mechanics (+0x2B), the squared activation radius
  (+0x2C, -1 always), "visit my children" (+0x31), the last frame it ran (+0x10, against the frame counter
  DAT_008bd088). **read**
- **brain**: its AI model (behaviours, reflexes, dsgvars), docs/NOTES.md "AI interpreter".
- **3D data** (+0): states, animations, the object list, the shadow (docs/NOTES.md "Level structure").
- **dynamics**: the mechanics by ID card type (docs/NOTES.md "MEC physics", docs/DECOMP.md).
- **collide set and zones**: ZDM (its own collision), ZDD / ZDE (detection), ZDR (what others collide with).
- **sector info**: the sector it is in (docs/NOTES.md "Sectors").

Ed is not special in the exe: he is the perso on the main-character list. Everything that acts on him (a lever, a pit,
the game flow) is another perso's script using these parts.

## The frame (main loop, unanalysed code around 0x428a69; disassembled from the dump with capstone)
In order, in normal play (`[0x4ec728]` not 0; else 0x434680 instead of the two passes) **read**:
1. 0x4832f0, 0x41cd30(DAT_008bd094), 0x41cc90: **open** (timers / input?).
2. **Always awake:** every perso on the list 0x8c172c (`FUN_0043bce0`) gets its frame mark, std game +0x19 := 1
   (`GAM_00423da0`, 0x428a8f), whatever sector the main character is in.
3. The main character (DAT_008c1720's first node): 0x449350, 0x452900 (his sector info: **open**), then
   **`GAM_00428490`(his sector)**: marks the persos of that sector (sector +0x1C) that are active and whose
   `GLIGLOU_Tex_00441070` byte is <= DAT_008c175a, and through `GAM_00428400` those of each sector on its activity
   list (+0x64). Unmarked ones get `GAM_0041ee20` (**open**: put to sleep?).
4. 0x425e80, 0x482d80 **open**; the frame's counters reset (0x8c1ca4, 0x8c1cbc, 0x8c1cc4, 0x8c1ccc).
5. **Run first: `GAM_004287b0`.** When the list 0x8c1738 is not empty (its count, 0x8c1740), each perso on it that is
   marked, active and not yet run this frame runs (`GAM_00428070`); one that is not gets `GAM_00416be0` (**open**).
6. **The dynamic world: `GAM_00428550`(DAT_008c1e38, 0, 1)**, recursive in tree order. A perso runs
   (`GAM_00428070`) when marked, active and not yet run this frame (+0x10 != the frame counter), so the run-first ones
   are not run twice; children are visited when the parent ran or has +0x31.
7. 0x424c10, 0x429d00, 0x429520 **open**; 0x445710(time x constant) **open**.
8. **The world perso:** 0x42ea30(DAT_008c1744), World's SuperObject. **open** (what it does with it).

The second path at 0x428bb0 (another engine mode) only marks (steps 3's `GAM_00428490`). **open**: which mode.

## One perso's frame: GAM_00428070 (read; the parts the port has are cross-referenced)
Skipped if already run this frame. Then, roughly: the "dead" handling (+0x26: `GAM_00428010`), `GAM_0041f2a0`,
`FUN_0043cc20` when +0x2B (animate) is set, the frame stamp; the main character takes a short path when
`DAT_008bc740` is set (a cutscene mode: animation only). Otherwise:
- without mechanics (+0x2A clear): `GAM_0042aaf0`, `GAM_00417810`, **the AI** (`GAM_PersoFrame_0042b520`: the
  brain, gated by the activation radius);
- with mechanics: `GAM_0042aaf0`, the animation (`GAM_PlayAnim_00415bb0`, `GAM_PlayAnim_Update_00415c00`: the
  animation events, root motion), the MS way (`GAM_00427830`), **the AI**, then the animation's advance
  (`GAM_PlayAnim_Advance_00415fb0`, `GAM_PlayAnim_004161b0`);
- then `GAM_0041df30` (with the console's display flags), `GAM_0042df50`, `GAM_004309a0`, `GAM_0042f3c0`, and **the
  sector walk** (`GAM_0042c810`: through the portal faces from the last position, docs/NOTES.md "Sectors") for persos
  with mechanics.
The mechanics themselves (the integrators, the collide-and-move) run inside the AI step's requests and
`GAM_PersoFrame_0042b520`: docs/DECOMP.md.

## The data: the engine's lists, from the level's .gpt (read)
One stream read in `GAM_Initeng_0040de70` (0x40e259) fills the engine globals 0x8c1714..0x8c175c; the fix-up loops
right after (0x40e30b, 0x40e358) link the nodes ({SuperObject, next, prev, list}). In every level the block's lists
sit at gpt + 0x4d90 (after the 3 perso-in-fix records): `LevelData.engine_lists` (tools/ttre/level.py) reads them.
| global | what | in the data |
|---|---|---|
| 0x8c1720 | the main character list (n = 1; read again right after the block) | Ed / the skier |
| 0x8c172c | **always awake** | the camera, World, the cheat-code reader, HEG_Inventaire, FRB_MoreBees, NBD_Feuque, the lightning / weather managers, a few level managers (north's drakkar, auto-guard; skymap's cloud hole) |
| 0x8c1738 | **run first** (count 0x8c1740) | what others stand on or push: platforms, lifts, bridges, the Saturn rings, sud0's arena blocks and the boss mushroom's platforms, the cavedoc crate BEM_Caisse1 |
| 0x8c1744 | the world perso (World) | handed to 0x42ea30 every frame |
Counts (always / first): bar 5 / 0, BigBoss 5 / 1, carota 6 / 4, cavedoc 6 / 6, cock01 8 / 13, death 4 / 0, Land 9 / 21,
LeCanyon 8 / 1, mammyon 3 / 0, marmite 7 / 6, north 12 / 13, outro 4 / 0, pharmmap 3 / 0, pyr_fou 3 / 0, pyramide 7 / 4,
skymap 8 / 0, SpyCarota 3 / 0, spynorth 3 / 0, sud0 9 / 11, Totalski 8 / 0, Transit 5 / 0. Some persos are listed twice
(the camera, World): harmless, the frame stamp runs each once.

**Why "run first":** the riders (Ed, the crate's pusher) collide with these persos' collision where they are this
frame, so their mechanics come before everyone else's. The port has no moving collision yet (below), but the order is
the hook for it.

## The port today
- **tc_cast (core/src/tc_cast.c)** is the loop for the level's persos: the always-awake list and the sector /
  activity marks decide who is awake (.ttlv CAST flags 4 and 8, tools/ttre/cast.py). Each tick (tc_viewer step_ai)
  runs the run-first persos (`tc_cast_step_first`), then Ed (tc_ed_ai), then the rest (`tc_cast_step_rest`), as the
  exe's frame does (**read**; within each pass in level perso order: **port**, the exe walks the list's order, then the
  tree's). An actor's sector follows it through the portals when its mechanics move it (`tc_sector_move`, GAM_0042c810).
- **Ed** is stepped by tc_ed_ai between the two passes (**port**: in the exe he is one perso of the tree walk).
  Folding him into the same loop is the direction (docs/DIRECTION.md, point 2).
- **Perso collision (2026-11-05):** each perso's collision is its own (.ttlv PCOL, tc_obstacle.h), no longer baked
  into the static world. Cast actors pose theirs after each step (state animation, position, tilt); the others stand
  at their frame-0 pose. Ed's sweeps meet them by the exe's rules and tests (below); `FUNC 54` and riding work.
- **The cast's mechanics** (tc_cast_mec.c, .ttlv CMEC): card types 6 (MoveLift), 7 (pushed and floating things), and
  since 2026-11-05 (evening) the walkers 0 / 4 / 12 (below, "Walkers"). Types 9, 10, 3, 8 still turn in place (**port**).
- **Activity (read, ported):** a perso's AI runs only while the main actor is within its activation radius (std game
  +0x2C squared, -1 always: `GAM_PersoFrame_0042b520` compares it with DAT_008bc744, the squared distance
  `GAM_0042a680` sets), and only in an awake sector (the marks above). Its **mechanics run whatever the radius**
  (`GAM_MecFrame_0042b2b0` after the AI, gated by +0x2A only): a perso out of range keeps what its animation's root
  motion does. tc_cast step_actor: `think` = the radius test, mechanics after it.
- Left out of the cast (**port**): the camera and its directors, the cheat-code reader, the arches, the persos the
  exporter reduced to data (portals and walk-in passages: .ttai PORT; Totalski's obstacles: TRIG; the light managers:
  LAIS). Each is a stopgap to fold back into the cast, one subsystem at a time (docs/DIRECTION.md).

## Moving collision, read (2026-11-05; all **read** unless marked)
The port's plan for it is at the end of this section.

**Who collides with whom: `MEC_SweepPass_0043c830`.** Both sweeps of the mover's ZDM (the flag-8 one and the rest)
first test the persos in its sector's character list and in those of every sector on its +0x58 list (not itself),
through `FUN_0043c640`, but only when the mover's own collide set byte **+0x35** is 1. Then the sector's static trees
(`FUN_0043c410`). Per other perso (`FUN_0043c640`):
- it must be active and not dead (`GAM_00423d20`: std game +0x18, not +0x17), and have no collide set or one with
  +0x35 set;
- a bounding-volume pretest (`FUN_00464650`, the SuperObjects' +0x34);
- **priority**: collide set byte **+0x34**, 0 without a collide set. The mover's ZDM meets the other's geometry only
  when the other's priority <= the mover's;
- the other's **geometry** = each channel object (p3d +0x164 list, type 0xB) whose physical object has a ZDR collide
  object (`FUN_0047a750(3, po)`), at the channel SuperObject's world matrix (+0x24) (`FUN_0043c770`). This is what the
  exporter has baked at frame 0 so far;
- **ZDR against ZDR**: the mover's own ZDR zones (its collide set list 3, active in its state) against the other's
  (`FUN_0043c7d0`), when the mover has any. Ed has none;
- hits on a perso are recorded with obstacle type 1 and the hit SuperObject (the channel); `MEC_SweepZones_0043bff0`
  turns them into contact kind 2 with the perso's dynamics.
- The flag-8 sweep (the hand point) throws away its hit when the nearest is a perso whose collide set has
  **+0x37 floor_ok** = 0 (`GAM_CollSetFloorOk_00431730`).

**In the obstacle's axes: `FUN_00464240`.** The mover's start and end matrices go into the obstacle's axes (the
inverse of its matrix; a uniform scale only, `FUN_0047c8d0`: radii are divided by it), the tests run there, and hit
point and normal come back through the obstacle's matrix. Tests by obstacle element type (`FUN_004656d0`; points
`FUN_00470a40`):
- 1 triangles: as the static world (`FUN_00465b60`, `FUN_0046ff70`; docs/DECOMP.md).
- 7 **spheres** (`FUN_0046d600`): the mover's sphere centre swept against the obstacle sphere's centre with radius
  R + r (`FUN_00469500`, the same test as a triangle corner). Normal = (centre - C) / (R + r); point = centre +
  (C - centre) r / (R + r). Kind 3. A point (r = 0) the same (`FUN_004704c0`), own shape 0.
- 8 **boxes** (axis-aligned in the obstacle's axes; `FUN_0046d8a0` -> `FUN_004679d0`): the box grown by r on every
  side (so its corners are square, not rounded). The start must be outside it (region code `FUN_004663e0`: bits
  1 / 2 x below / above, 4 / 8 y, 0x10 / 0x20 z), and the start's and end's codes must share no bit. Then, for each
  face the start is outside of (x-, x+, y-, y+, z-, z+), the crossing t and point, inside the face's rectangle
  (`FUN_004664c0` / `00466520` / `00466460`, bounds inclusive): the first one found is the hit. Normal = the face's
  axis (constants 0x4e1ca0). The point is the centre at t minus r n. Kind 4. A point the same without growing
  (`FUN_00466600`).

**Contacts with a perso: `MEC_004bd689`** (from `MEC_CollideMove_004be1bd` when the contact kind is 2; the sphere
mover's twin is `MEC_004bdac7`):
- obstacle == the current carrier: classified as usual, plus report **0x30000**. In the carrier-follow collide (flags 0,
  first pass) such a contact ends the move at once with no contact: the carried move is taken whole.
- else, when the mover's walk card byte **+0x3C** is set, the obstacle **carries** (std game **+0x31** of its first
  perso ancestor, `GAM_0042bc67`), the normal is within the card's steep slope (n . up >= +0x20) and the touching
  part's zone type is 1 or 2: the ground response (`MEC_RespondGround_004bb904`), the record kept as ground and as
  "other" (+0x1C4), report **0x30001** (ground) or **0x30002** (steep), target pushed out by 0.01 n.
- else classified as usual; a step-up (0x40) onto a carrier adds 0x30000.

**Riding is reparenting** (`DNM_IntegrateWalk_004aa6af`, the type 0 / 4 integrator; type 11 and 12 do the same):
- After the collide-and-move, report & 0x30000 == 0x30000 and the obstacle is not the carrier:
  `SPO_SetParent_00483430(so, obstacle)`. The perso becomes a child of the channel SuperObject it stands on.
- At the end of the integrator, with a carrier, the local matrix is recomputed from the world one (local =
  carrier world^-1 x world).
- **Every frame first** (`DNM_FollowCarrier_004a83d2` -> `FUN_004a7c25`): the world matrix = the carrier chain's
  product x the local one; the position follows (`FUN_004a7899`). The move from last frame's position is collided
  (flags 0; a 0x20 "blocked" result puts him back and carries nothing; no contact restores the reports). The carried
  motion is kept for the leaving.
- **Leaving** (`DNM_LeftCarrier_004a8388`): when neither this frame's nor last frame's report has 0x30000, a sweep
  0.2 down along up (`FUN_004c2ba0`) must still find the carrier, else he is detached (`FUN_004c2ac1`: back to the
  dynamic world) and **velocity += carried x 0.666666** (`DAT_004d3034`), unless the carrier's std game +0x26 is 3.
- Standing still (no velocity, no turn) skips the collide: the report becomes 0 (keep_reports clear) or & 3 (set).
  So 0x30000 is lost while standing, but the parent link stays and he keeps moving with the carrier.
- **FUNC 54** (`Func_GetFather`, the port's StandingOnPerso): with report_seen & 0x30000 == 0x30000, the first perso
  ancestor of the perso's parent. Else none.

**Card type 6, the lift** (`DNM_RequestType6_004b11ea`; its integrator only shifts the snapshots). Driven by
**META 28 `Action_MoveLift`(from, to, time)** (`META_28_004a0990` / `META_28Cont_004a09c0`): ProposalVecA = from,
ProposalVecD = to, proposal [0] = T. The first frame queues mode **14**, then mode **15** until velocity_snap is zero.
- Mode 14: s = 0, x = 0, the lag rate 0, the position = A, "moving" (velocity_snap.x = 1).
- Mode 15, L = |B - A|: the lag `FUN_004c9afb`({x, rate, T/2, L}, dt): x += (L - x) / (T/2) dt. While s < 0.6321 L
  (1 - 1/e): s = x, elapsed = T/2. After: s = ((elapsed - T/2) / (T/2 e) + 0.6321) L, elapsed += dt. So the lift
  covers 63 % of the way in T/2, slowing exponentially, then the rest at that speed, arriving at T. Position = A +
  (B - A) / L x s. At s >= L: at B, stopped (velocity_snap = 0: the meta-action ends).
- The position goes straight into the perso's matrix. Velocity = dt x the step (stored, not used for riding).
- State (dynamics +0x2B0 s, +0x2B4 elapsed, +0x2BC x, +0x2C8 rate). Constants 0.5, 0.6321, 2.718 (0x4d2f38..40).

**Card type 7, floating and pushed things** (`DNM_RequestType7_004b17d1`, `DNM_IntegrateType7_004acb49`, forces
`DNM_Type7Forces_004c831a`). Card: +4 mass, +0xC min speed, +0x10 min turn, +0x14 max speed, +0x18 max turn,
+0x1C gravity factor g7, +0x20 damping, +0x24 stiffness.
- Requests: 3 / 4 turn by [0xD] (clamped to min / max turn), 5 / 0x12 turn to an angle; **12** wanted = the move
  direction x speed (clamped) in the perso's axes, **0x14** the same in world axes plus a turn towards it, 0x15 a spring
  towards ProposalVecA (only with g7 = 0), 0x17 an orbit. Each move request sets dynamics +0x70 = -1.
- Forces: a pending velocity (PROC 35) replaces the velocity (z := 0). With g7 = 0: no motion of its own (the plane
  spring when +0x70 >= 0). With g7 != 0, **a damped vertical spring under the load**: load = the masses of the persos
  riding it (`FUN_004c32f6`, its children), M = mass + load, g' = g7 x g. When +0x70 < 0 the rest height latches:
  rest = z + M g' / k, +0x70 = 0, dynamics flag 0x20000. Then vz += (k (rest - z) - M g' - damping vz) dt / M. A
  rider sinks it to a new rest and it rises when he leaves.
- **Tilt**, only above mass 125 (`DAT_004e77fc`): when the first rider is more than 0.5 off centre (squared distance
  > 0.25), a torque -(200 + load) g' (dy, -dx, 0) tilts it towards him, against a restoring -25000 x its tilt and a
  damping -3250 x its tilt rate (rotation vectors added as vectors, `FUN_004b73db`; `FUN_004b7c6f` takes their x, y).
  The exe also adds the angular velocity itself to the torque before scaling by dt / mass (kept as read).
- With flag 0x20000 the integrator steps in **0.005 s** substeps. Then a collide-and-move of its own ZDM (flags 4;
  most have none), and the full rotation from its axis-angle.

**Data, measured (2026-11-05, all 12 playable levels with perso collision):** about 300 persos carry collision; the
card types of their states are mostly 6 (146 states) and 7 (142), then 0 (69), 4, 10, 9. Only about 50 move their
collision by animation; the platforms, lifts and the crate move by mechanics. 74 are on the run-first list. 231 are
in the cast.
- **Port bug found:** cast.py leaves out every perso calling PROC 6 (meant for the skier's start, which deactivates
  the fix Ed). That drops cavedoc's 11 BEM_PFQuiSeDesagrege ("the platform that falls apart") platforms, whose B3
  deactivates themselves and whose B0 / B2 test `InPersoContext(MainActor, StandingOnPerso) == Me`.

**Built (2026-11-05), headless only; nothing seen in the app yet.**
- **Data** (tools/ttre/ttlv.py `perso_collision`, cast.py `mech_chunk`): .ttlv **PCOL** holds every perso's own
  collision in its objects' axes (triangles, spheres, boxes, each with its material), with the collide set's +0x34 /
  +0x35 / +0x37 and std game +0x31. COLL holds the static world only. .ttlv **CMEC** holds the cast families' state
  cards and each actor's own sweep sphere (+0x36 / +0x30). Old exports without PCOL keep their baked collision.
- **Core:** `tc_obstacle.c` poses each obstacle (cast actors after every step, the others once). `tc_mec_sweep`
  tests the obstacles in their axes before the static world, with the rules above. The perso contact classification
  and riding live in tc_mec_walk.c (`respond_perso`, `tc_mec_walk_carry`, `ride`). FUNC 54, COND 37, META 28 and the
  NormSpeed field are in tc_ai.c. Card types 6 and 7 for the cast are in tc_cast_mec.c.
- **Also read and ported on the way:** the **ground press** (`DNM_WalkForces_004c41c5`, DAT_004d3084 = -0.04): grounded
  by the step before but not the last, the wanted motion gets 0.04 into the ground. Standing still shifts the report
  (Ed's dynamics +0x38C, keep_reports, is 0 in the data). So a grounded perso touches its ground every other step,
  and on a perso report_seen keeps 0x30000. Before, the port froze the contacts while standing. All checks pass with it.
  mechcheck's 0.45 step is no longer slowed (the step-up wants last step's ground, which the press keeps). Also ported:
  FUNC 88 / 100 / 101 run as OP 24's perso (they used the asker's axes).
- **Checks:** `tools/exe/oracle.py point_sphere sphere_box`: `FUN_00469500` and `FUN_004679d0` against the core, 100,000
  of 100,000 random cases agree each. viewcheck `ride_scenario` (cavedoc, needs PCOL): Ed dropped on SDO_PFCaveDoc1
  lands on it, rides it (437 of 480 frames), FUNC 54 names it, MoveLift takes it 45 units with him on it. castcheck on
  all 21 levels (254 ok) with the larger cast. A scratch probe pushes the cavedoc crate from its four sides: it slides
  with Ed and stops (at the ledge, at a wall).
- **Port choices and gaps** (marked in the code):
  - The pretest per obstacle part is a bounding sphere (the exe's `FUN_00464650` is not read).
  - Persos outside the cast stay at their frame-0 pose, in their snapshot sector.
  - Riding is the type 0 / 4 integrator's. The exe's types 11 and 12 ride too. Any other card lets go of the carrier.
  - Leaving keeps 2/3 of the carried velocity always (the exe skips that when the carrier's std game +0x26 is 3;
    not exported).
  - The type 7 forces take g = 9.81 (every level's air) instead of looking the environment up.
  - Type 7 mode 0x17 (orbit) is not ported. Card types 0 / 4 / 9 / 10 / 3 / 8 / 12 for the cast still turn in
    place (the rolling rocks, the doors with card 0, the guards).
  - The type 7 sweep body is the collide set's single sphere only (the crate). Its contacts push it out 0.01 each
    and nothing pulls it down (gravity factor 0), so it creeps up a little where the floor is uneven: the exe's own
    algorithm, kept.
  - **Special Edition** data: its collide-set and std-game bytes past +0x2A are not read, so every SE perso obstacle is
    met by all (priority 0, floor_ok, carries), and the borrowed retail Ed keeps retail Ed's collide set (127, meets).
  - The static world's raycasts (camera, footsteps lookup, FUNC 162, tc_player) no longer see perso geometry.
    Footsteps on a perso take the ground contact's floor sound instead.
  - `MEC_004bd689`'s inverted steep flag (above) is kept as read.
- **Next:** the cast's walkers (types 0 / 4 with their ZDM sets and root motion), types 9 / 10 (the rolling rocks),
  the ZDE zones (COND 18-29), the ZDR-against-ZDR test, persos' obstacles for the camera's sight.

## Ways and waypoints (2026-11-05 evening; all **read** unless marked)
**Data.** A waypoint is 0x1C bytes: u32, f32 position[3] (in its father's axes when it has one), f32 radius (+0x10: where
the GoTo meta-actions arrive), u32, ptr father SuperObject (+0x14) (`AI_0048dcf0` gives its world position). Scripts
hold them as node type 29 and dsgvars of type 8. A perso's ways hang off perso **+0x1C** (the MS_Way): a list of nodes
{next, prev, list, way, u8 at (+0x10), u8 link step (+0x11), u8 from (+0x12, 0xFF none), u8 saved step (+0x13)}; the
scripts' way number (node type 23) is the n-th node (`GAM_004257f0`). A way = {ptr waypoints[n], ptr links[n - 1], u8 n,
u8 at, u8 from}. A link (0x50) has its type at +0x38 (0 straight, 1, 2 Bezier, 3 circle arc) and speed type +0x3C;
**every link of the 21 retail levels is type 0**, so the port exports no links.
**Primitives** (PROC 251-253 `PROC_251Etc_0048c0b0`, META 21-26, FUNC 3 / 11 / 12, COND 66):
- Proc_ReInitWay (`AI_0048d540`): at 0, from none, step 0, saved none. ReInitWayBack (`AI_0048d5a0`): at n - 1.
  InitWayWithWp(way, wp) (`AI_0048d620`): at the first match among waypoints 0 .. n - 2 (the last is never matched).
- **The go-to core** (`FUN_0049f580` via `FUN_0049f4e0`): this frame's mode 12 requests are dropped (`FUN_0047a4a0`);
  within the radius (`FUN_0049f530`, <=) it is there (4); else the unit direction goes into proposal [7..9] and mode
  0x14 is queued (the speed [10] stays the script's: SetSpeed / SetMoveDirection).
- Action_GoToWP (META 21, `0x49fc50`): to the waypoint, its own radius. Action_GoOverWay (22) / GoOverWayBack (24) /
  GoToNextWPOfWay (25, 26 back) (`0x49fd40(perso, go_on, back)`): to the way's current waypoint; there, `AI_0048d390`
  (on: from = at, at + 1; at the end back to the start and done) or `AI_0048d460` (back: down to 0, then to the end
  and done); 22 / 24 go on, 25 / 26 stop at the next waypoint. GoOverWay3D (23, `0x49fe90`) is not read and unused.
- Func_GetWPAbsolutePosition = FUNC 3 (the port had it as a perso's position before), Func_DistanceToWP = FUNC 11
  (from OP 24's perso), Func_CloserWP = FUNC 12 (the nearest of waypoints 0 .. n - 2), Cond_IsValidWayPoint = COND 66.
- **Mode 0x14** for types 0 / 12 (`DNM_RequestWalk_004ae392`): the direction's horizontal part times its own length
  (as read: the sqrt multiplies) times the capped speed, and a turn towards it (`FUN_004ae280`) within the card's
  min / max turn. Type 7's (`DNM_RequestType7_004b17d1`) was already ported.
**Port:** .ttlv **WAYS** (tools/ttre/ai.py `ways_chunk`, `waypoints`: one table per level, numbered from 1 in a fixed
order so the .ttlv and the .ttai agree; dsgvars of type 8 hold the number); tc_level.h `tc_ways`; tc_ai.c (`go_to`,
`go_over_way`, the cursor in tc_ai_perso.way_cursor). A father-relative waypoint uses its live father's position and
heading (**port**: the exe's straight link keeps a copy of its end point; 4 such waypoints in the 21 levels).
Check: viewcheck `guide_scenario` (cavedoc's ruby BNE_PFGuide: all 10 waypoints in order, 37 s at speed 12, Ed riding).
Uses: 1143 waypoint nodes, 273 GoToWP, 59 GoOverWay (the rubies, north's boat and drakkar, the see-saw platforms, the
guards' patrols, Suzy, the mummies, the haricot).

## Walkers: card types 0, 4, 12 for the cast (2026-11-05 evening)
- **Data:** CMEC's last part (tools/ttre/cast.py `walk_block`): per family, from its first cast perso, its root motion
  and ZDM bodies, as Ed's .ttai BODY + ZDMF. A collide set with +0x36 and radius 0 (north's guards) sweeps **a point**
  at its origin (`MEC_SweepSphere_0043d480` with r = 0: **read**); ai.zdm_sets exported nothing for those before.
- **Types 0 / 4** (tc_cast_mec.c `walk_step`) run as Ed's walk (tc_ed_ai.c `walk`): riding, the requests (mode 12, mode
  0x14, the air modes) or the animation's root motion, then `tc_mec_walk_step`.
- **Type 12** (`DNM_IntegrateType12_004a9d20`, **read**): it collides only while its report has no ground, and its forces
  (`FUN_004c3e50`) cancel gravity and the old velocity on a grippy ground: a grounded type 12 perso goes where its
  requests take it, at its height, through walls. **A perf shortcut of the original** (NPCs walking without collision
  tests). Port: `glide_step` (no collision at all: the snapshot's report is not exported, so it starts grounded), also
  the stand-in for a type 0 / 4 state without a body. Suzy and the skating fridge guards (cock01) are type 12.
- Measured (a scratch probe: Ed put 4 away from each walker, 4 s): north's guard walks 9, sud0's boxing mushrooms 4-11,
  pyramide's dentures chase (53 in 4 s), cock01's skaters skate 30-54, Totalski's stalactite traps fall and shatter.
  Not yet seen in the app.

## Shooting (2026-11-05 night; all **read** unless marked)
**Ed's shot: META 11 `Action_ExecuteActionAndGenerateObject`(perso P, state S, vector V)** (`META_SpawnInState_0049f190`,
continuation 0x49ef70 = ChangeStateAndWait's). Ed fires in B29 (stick mode), B45 (flying), B69 (the blowgun out: Action
sets his dsg27), B74, B105, while Shoot is held, his inventory counts some P, and FUNC 162's wall ahead is further than
3.2 (FUNC 13 > 10). When Ed may go to S (`GAM_00437600`), P's model is instanced from the engine's **spawnable list**
(gpt after num_always: {head, tail, n} of {next, prev, list, u32 count, ptr template perso}; `GAM_Always_004259a0`) at
Ed's matrix; while the last instance is out it is reused (Ed's perso +0x30); Ed goes to S; the instance's
ProposalVecA := V (`AI_00488070`). Ed's projectile is `LOL_Actor1` (gen_abeille, the bees): V = (0, -dsg32, 0), 60 u/s.
- **The bee:** card type 10 (+8 gravity factor 0; `DNM_IntegrateType10_004adfc3`: no request -> the forces
  `FUN_004c709a` keep the velocity). B2 probes the wall along its velocity (FUNC 162), B0 ends it at that wall, after
  dsg0 ms, or on a ZDE hit (COND 28: its zone 1, type 2, meets a type 1 zone that is not Ed's, COND 24); state 1, gone
  300 ms later. PROC 287-289 are empty in retail.
- **The hits are ZDE zones** (COND 18-29 `COND_18Etc_00497ef0`; `GAM_CollectNearby_0041fa70`): a perso's collide set
  +4 (list 2), active per state (+0x14); a zone's type = its elements' collide material flags (`GAM_0041f060`,
  `CPA_004493d0`: flags & mask == mask). Ed: zone 5 his body (type 1) in nearly every state, type 2 attack boxes in front
  of him in his stick swings. The guards (R0): `CollectNearby(list, 1, type 2)`, `Proc_ListSortByFamily` (a filter:
  `AI_0048fe40`), then hurt (FUNC 36 `Func_SubAndGetHitPoints`) or downed (B6).
- **FUNC 162 / FUNC 13** (`FUNC_162_0049c690`, `FUNC_GetPositionEtc_0049a140` case 0xd): the probe starts at the perso's
  origin + the offset (world axes), goes along the direction in its axes, and returns the hit **in world axes**; FUNC 13
  is the **squared** distance from the perso to a point. The port had both wrong (a local point, a length), which hid
  each other in Ed's uses (B29 fired with the wall further than 10 instead of 3.2, B82 climbed within 2.25 instead of 1.5)
  and broke the bee's wall test. Fixed; north / cavedoc / Land's hang runs are unchanged.
**Port** (tools/ttre: `LevelData.spawnables`, `perso_ids`, ai.py `zde_block`; core: tc_ai.c ZDE section, tc_cast.c
`cast_spawn` / `cast_each`, tc_cast_mec.c `projectile_step`):
- The templates are cast entries with flag SPAWN (64), ids after the level's persos (the scripts' perso nodes name them:
  Ed's PERSO@0x9379a was unresolved before), out until instanced, never drawn where the data places them.
- **One instance per template** (**port**; the list's count, e.g. 16 for the pink balls, is not used): Ed reuses his bee
  as the exe does; other generators share it.
- Ed's shots start at his mouth, (0, -0.5, 1.7) in his axes (**port**, the user saw them leave his feet: the exe
  instances at his matrix, whose origin is his feet, and the template's own matrix is the identity; the offset the
  original uses, if any, is not found).
- The launch vector becomes the instance's velocity at once (**inferred**: its scripts read Velocity right away;
  `GAM_Always` is not read). Type 10's ballistic forces with thrust are not ported (gravity x the factor instead), nor its
  own collide-and-move (the bee's scripts end it at its probed wall).
- ZDE tests (**port**: the exe's `FUN_004631c0` is not read): spheres exact, sphere / box by the nearest point of the box
  in its axes, box / box by their bounds in each other's axes; zones on animated channels ("modules", COND 19-21 / 23 /
  29, PROC 85) are taken as the perso's own; PROC 86 not ported. A list dsgvar keeps up to 8 persos (tc_ai_list).
- **The inventory is a stub** (the user's choice): Ed always has one of everything (FUNC 95 1, FUNC 96 / 97 give it
  without taking it), so he shoots as often as his scripts let him; others hold nothing, as before. The exe's inventory is
  the save values by the object model's slot (perso +0x34 -> u16 +2: the bee's is slot 0; FRB_MoreBees refills it).
- Checks: viewcheck `shoot_scenario` (north: the blowgun out, two bees, guard 46 downed, B6). Scratch probes: Ed fires
  every 0.67 s holding Shoot, the bee bursts at the wall 8 ahead. Not checked: the stick's hits (a probe at a guard: the
  guard struck first and knocked Ed back, B102), the bees' damage to other enemies, how the bee looks.
