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
- **The cast's mechanics** (tc_cast_mec.c, .ttlv CMEC): card types 6 (MoveLift), 7 (pushed and floating things), the
  walkers 0 / 4 / 12 (below, "Walkers") and the projectiles 10 ("Perso motion"). Types 8 and 9 still only turn in place
  (**port**); 3 is the camera, outside the cast.
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
  - Type 7 mode 0x17 (orbit) is not ported.
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
  `GAM_Always` is not read). Type 10 itself: "Perso motion" below. The bee (gravity factor 0) has no sweep of its own:
  its scripts end it at its probed wall.
- ZDE tests (**port**: the exe's `FUN_004631c0` is not read): spheres exact, sphere / box by the nearest point of the box
  in its axes, box / box by their bounds in each other's axes; zones on animated channels ("modules", COND 19-21 / 23 /
  29, PROC 85) are taken as the perso's own; PROC 86 not ported. A list dsgvar keeps up to 8 persos (tc_ai_list).
- **The inventory is a stub** (the user's choice): Ed always has one of everything (FUNC 95 1, FUNC 96 / 97 give it
  without taking it), so he shoots as often as his scripts let him; others hold nothing, as before. The exe's inventory is
  the save values by the object model's slot (perso +0x34 -> u16 +2: the bee's is slot 0; FRB_MoreBees refills it).
- Checks: viewcheck `shoot_scenario` (north: the blowgun out, two bees, guard 46 downed, B6). Scratch probes: Ed fires
  every 0.67 s holding Shoot, the bee bursts at the wall 8 ahead. Not checked: the stick's hits (a probe at a guard: the
  guard struck first and knocked Ed back, B102), the bees' damage to other enemies, how the bee looks.

## Perso motion (2026-11-06; all **read** unless marked)
What moves a perso, top down. A script does not move anything itself: it fills the **proposal** and queues **requests**,
and the state's card type turns those into motion.

**Before the AI, the animation (`GAM_0042a8d0`, from `GAM_PlayAnim_Update_00415c00`).** A playing animation with root
motion this frame writes the proposal's direction [7..9] (unit) and speed [10] (its length, units per second:
`GAM_004170d0` scales the frame's displacement by 1 / (ms x 0.001)) and queues mode 12. That is the default way a perso
walks. A zero row writes nothing and queues nothing, so the old speed stays.

**The go-to core (`FUN_0049f580`) keeps that speed.** It drops this frame's mode 12, overwrites only the direction, and
queues 0x14. So a chaser goes at its run cycle's pace and **the scripts set no speed** (sud0's boxer: no SetSpeed anywhere).
The port had never fed [10] for the cast: every Pursuit had speed 0, and turned on the spot. `root_to_proposal` (tc_cast.c,
before `tc_ai_step_parts`); Ed keeps his own path.

**Meta-actions** (`META_2Etc_0048f4c0` evaluates the arguments once into per-slot storage by the packed signature; the
continuation then runs every frame). Until now these fell to `default:` and finished at once, so the scripts ran on as if
the action had happened: a perso "pursued" and stood still.
| META | name (SE exe) | start | every frame |
|---|---|---|---|
| 0 | IncrementalTurn2(deg, ms) | rate = deg x pi / 180 / (\|ms\| x 0.001) | until elapsed >= ms: mode 3 at the rate |
| 1 | IncrementalTurn(deg, frames) | rate = deg / frames x pi / 180 (as read: per frame, used as rad/s) | frames left: mode 3 |
| 2 / 3 | Pursuit(perso, r), DeltaPursuit(perso, r, x, y, z) | | go-to the perso / the point in its axes |
| 14 / 20 | RunAway(perso, r), RunAwayWP(wp, r) | | go-to reversed (`0x49fac0`), done at distance >= r (`0x49fa10`) |
| 27 | GoToPosition(point, r) | | go-to |
| 5 / 6 | WaitEndOfAction / WaitEndOfAnim | | until 3dData +0x161 (ended: last repeat, no next state) / +0x162 (end reached this advance, cleared each advance) |
| 15-17 | IncrementalTurnPosition / DeltaTurnPerso2 / TurnPerso2 (.., ms) | delta = the short way to the target; rate = delta / \|ms\| | ms left: angle += rate x ms, mode 5 |
| 18 / 19 | IncrementalDeltaTurnPerso / TurnPerso (.., frames) | rate = delta / frames | frames left: angle += rate, mode 5 |
Mode 3 is a turn rate through the card's max turn (`DNM_RequestWalk_004ae392`; type 4's air request takes it as it is); mode
5 is an absolute angle at once, unclamped ((angle - position's angle) / dt). PROC 247 / 248 (`PROC_TurnTowardPointEtc_0048be20`)
are mode 5 to a point in a perso's axes / to a perso; FUNC 1 the signed degrees from its front to a perso.
**Bugs on the way:** `heading_set` was never applied on any card with mechanics (a walker told to face something did
not), and `perso_frame` knows only portals, so META 19 and PROC 247 did nothing against a cast perso (`perso_point`:
portals first, as a ghost's heading says nothing, then the live perso).

**Also:** COND 15 (`GAM_AnyZoneHolds_004202c0`: any ZDD zone of a holds b's origin; the most-skipped primitive, 43,000
in 5 s across the levels; **port**: all exported zones count, active or not, and zones on channel objects are not exported),
COND 51 (`COND_50Etc_00498820`, walkers only), FUNC 44 (`FUNC_GetTimeEtc_0049b040`), PROC 16 (`GAM_00428ea0`: back to the
dynamic world; the port clears the carrier, so a contact can take it again next frame, as in the exe).

**Type 10** (`DNM_RequestType10_004b252e`, `FUN_004c709a`). The port read modes 0x13 / 0x16 as a velocity. They are not:
ProposalVecA is a **target point**, [0] the apex height of 0x13 and the speed of 0x16. pyramide's cat paw lobbed by its
position flew 776 units in 5 s.
- **0x13, the lob** (handler 3): g = card +8 x the environment's; x, y = the target along the ground and up; h = max([0], y).
  h > 0: vy = sqrt(2 g h), t = 1 - y / h, vx = g x / vy (t <= 0) or g x / ((sqrt t + 1) vy). h <= 0, y < 0: vy = 0,
  vx = sqrt(g) x / sqrt(-2 y). g = 0 or [0] < 0: no velocity.
- **0x15** (0xb): the velocity = ProposalVecA in its axes, with one frame's gravity.
- **0x16, homing** (0xc): straight at the target the first frame, then the velocity turns 0.2 of the angle a frame, at speed [0].
- **None:** the velocity kept, with gravity when the factor is set.
- **Port:** g = 9.81 x the factor (every level's air). With a gravity factor it sweeps its own sphere (`own_sweep`) and stops
  on the ground; no factor (the bee) no sweep. **Not ported:** card +0xC (turning it to its velocity), +0x10 (the velocity
  turning towards gravity over time), `FUN_004c8860`.

**Measured** (`make movecheck`: Ed 3 away from each of about 520 drawn persos with mechanics, 5 s, 12 levels; before ->
after): moved 37 -> 52, turned 10 -> 47. Most of the rest are meant to stand (pickups, spikes, crates) or are behind an
activation radius or a trigger: this counts motion, not correctness. Scenarios in movecheck: sud0's boxer (perso 7) runs Ed
down at top speed 8.6 and strikes, then goes home by its way; pyramide's paw lobs onto him (within 1.5) and stays within 16 of
its start. Special Edition levels: castcheck passes on all 11, but movecheck visits none there (their persos do not run
mechanics), so this barely reaches them. **Nothing here has been seen in the app.** viewcheck's shoot_scenario: guard 46
used to walk away from Ed with its back turned; it now turns and closes 6 -> 2.8 in 0.5 s, so the test fires after 0.1 s
(a change in the test's timing; the assertions are the same).

**Next for motion:** card types 8 (pyr_dentier, patcat2; `DNM_RequestType8_004af04e` read, forces not) and 9 (the rolling
stones, gen_pierres), PROC 12-15 (a perso parented to a perso), PROC 116 / 117 / 109 (**moved sectors** over time: read as far
as `GAM_0042a350` / `GAM_0042a270`; these are probably what opens the story gates that `GATES_OPEN` removes at export).

## Speech (2026-11-06; all **read** unless marked)
The exe's `GAM_LipsSync.c`. A character's line is one object carrying its voice, its mouth shapes, its text and cues.
**Data.** Per level, encrypted as the snapshots: `.lsy` {name[255], u32 address}: a dialogue's name and the **param of the
script's type 21 node** that names it (`SNA_snasyn_0045c910` writes the object's pointer there); `.bls` the objects
(`SNA_snasyn_0045ca20`): frames, rate, sound, channels (+0x18, n +0x22), phonemes (3 bytes a frame, +8), flags text (+0x4C) and
voice (+0x4D), lines (+0x24 / +0x28 / +0x2C), the window (+0x30..+0x3C, percent: top, bottom, left, right; 75, 95, 5, 95), the
backdrop (LIPSYNC.TGA, 8x8, blue at 53 %), events {frame, state} (+0x5C / +0x60). 246 objects in 21 levels, each file read to
its last byte. `game.dsb` holds the text ("NewString" records, 539 strings); 245 of 246 are found (`grogh001` has a voice only).
**Font.** FontLips in fix.sna (the record 0x2C before its name): +0x14 flags, +0x18 n (126), +0x1C the list of {u16 code, f32
baseline shift, ptr glyph}, +0x20 spacing 0.015; a glyph {ptr quad, f32 step, f32 advance}, the quad's UVs in
`MotorTxt/typo2DCnz.tga`. Tools: tools/ttre/speech.py, .ttlv SPCH.
**Voices.** The value's high half is the bank (0x540000 = English/Bnk_84). The low half no longer matches the bank's order in 96
dialogues; the resource's file name carries the dialogue's number (TXTDIA139 = A_G0139.apm), which decides (`voice_of`). All 246
resolve, 163 files: events.tsv kind `voice`, VOICE_SOUND (8192) + rank. The frontends decode one when it is said (**port**:
everything else is decoded at load) and stop it on TC_AI_VOICE_STOP (8191).
**Running.** META 7 (`META_7_0049efc0`): not speaking, `GAM_004180a0` sets 3dData +0x1A0, the speaker (`DAT_004ec6bc`), game mode 2
(`DAT_008bd079`); the continuation waits for +0x1A0 to clear. Mode 2 (`GAM_0042cce0`) only swaps the debug and pause keys for the
line's. **The game runs on meanwhile.** With text on, **only the key ends a line** (`DAT_008bc3bc`); it stays when voice and text
are done. Held `DAT_008bc3b4` / `b8` scroll, once the text is out (`DAT_004ec6c0`). PROC 104 Proc_StopSpeech ends one.
**Text.** `GAM_00417e90`: every `^` to a space, greedy lines of width <= 8.2 (`FON_004ce110`) broken at spaces, the first at 0.8, 0.3
apart. More than five lines start at -0.75 and rise 0.007 a frame to -0.7 + 0.3 n: a credit roll. `FON_004cdd10`: centred.
**Port** (core/src/tc_speech.c, tc_viewer.c `update_speech`): the core lays the text out and draws it on the CPU, the game's font
at 1152 x 192 (`tc_viewer_speech_panel`), which the frontends show as one quad; so the panel can be checked headless as a picture.
- The window is the 4:3 frame's, centred on wider screens. The window's scale (2 font units high, isotropic) is **inferred**: five
  lines fit it; `FON_004cd680`'s camera is not read.
- Dismiss: a fresh Jump, Action or Shoot; scroll: held Back / Forward. A line with no text ends with its voice. The exe's key's binding
  is not read.
- **Ed is held while a line is on** (`speech_held`, until the key is let go). The exe's scripts hold him (`ForceBehaviour(Ed, B15)`), which
  the cast may not do (MAY_FORCE_MAIN: he could be parked for good), so he stands, without the scripts' listening pose.
- A line waits for the one on show (the exe would leave the first speaker waiting for ever).
- Lines heard unprompted (40 s standing, no story flags): BigBoss's taunt, cavedoc's Agent, cock01's guard, Transit's flight lesson, the
  outro. **Not done:** the mouth shapes (`GAM_00417a10`, called from `GAM_PlayAnim_00415500`: the phonemes pick the nearest entry of
  the 3D data's table, `GAM_cineinfo_00426f10`, by their three bytes, for the listed channels; needs that table exported and a channel's
  object swapped in the draw). Special Edition speech (its levels have no SPCH: lines end at once).

## Walkable scenes (2026-11-06; the user's plan of 2026-11-02, part built; **port** unless marked)
Six levels are hint scenes ("Astuce"): bar, mammyon, pharmmap, pyr_fou, SpyCarota, spynorth. **Read:** a speaker (PAT_Barman_Astuce01 ...)
drives it: camera shots, one line (Speak, chosen by its dsg2), GameAction2, then `ChangeLevel`; PAT_Ed_Map_Astuce is **the scene's own Ed**,
acted on by forces, and the real Ed is switched off (the "double Ed"); NBD_Appui...Start skips it on Start.
- **The double Ed is gone:** cast.py SCENE_ED: neither drawn (PRSO) nor met (PCOL). The speaker's references to it fall on a ghost.
- **Arrival:** where the scene's Ed stood, facing the speaker (SPCH `arrival`), and back there after a death. Ed's own place in the data
  is not meant for it (mammyon: 70 from the speaker). It holds him in all six.
- **The speaker** joins the cast (cast.py SCENE_SPEAKERS, TC_CAST_TALK) and sleeps until a fresh Action within TC_CAST_TALK_REACH (7, along
  the ground: the bar's counter keeps Ed 5.8 off, the mammoth stands 23 above him). Its META 29 waits are cut to 250 ms (`brisk`):
  they pace camera shots the port does not play. Its cover and level change go through Ed's events.
- **The arches now lead into the scene** (tc_viewer.c; an export without SPCH passes them by, as before). The Start selector stays out.
- A speaker left in an empty behaviour after its line waits for the scene's Ed to force it on (SpyCarota's B9): `update_scene` covers and goes
  to the level it would have (tc_level_after_cutscene).
- **Checks:** viewcheck scene_scenario (bar, whole flow). A scratch steering probe took all six through it: walk to within reach, Action, the
  line and voice, Jump, the next level; Ed lost no hit points (in mammyon he drops to z -25 on the way). mammyon's first line comes 18 s after
  Action: the mammoth walks in along its waypoint, then turns to where the scene's Ed stood.
- **The prompt** "Press Action to talk" (`tc_viewer_talk_prompt`, `tc_speech_render_prompt`): the game's font in a small box in the text window's
  place, within TC_CAST_TALK_REACH of a waiting speaker. It is the same panel, so the frontends needed nothing. The text is English and names the
  action, not the key.
- **Open:** the exit place (the user's plan has Ed walk out; here the cover follows the line), the scenes' camera
  shots (`SetCameraTarget`, dsg writes: refused by read_only_for_cast), Ed's own side of the conversation. `dsg2` restarts each visit: the same
  hint every time. The arrival is against a wall in pyr_fou and spynorth, so the camera swings to the open side and faces Ed.

## Audio and UI triggers met in the scripts (2026-11-06)
Counts over the cast's scripts, all retail levels. **Works:** the sound requests (PROC 145-148: 1,585 uses, 64 families,
flat, within TC_CAST_HEARING), animation sound events, DisplayVignetteDuring (PROC 210), transparency (PROC 203-205), speech
(META 7, above).
| primitive | uses | what it is | status |
|---|---|---|---|
| PROC 142 AddObjectInInventory | 344 | the inventory (`0x419310`) | **no-op**, with FUNC 95's stub (Ed has one of everything) |
| PROC 153 / 154 / 155 Display / Show / EraseString | 133 / 0 / 72 | HUD text slots (20, `DAT_008a36c0`), font text | **skipped** |
| PROC 149-152 DisplayChrono etc. | 47 | boss life bars (`GAM_00418c00`: "FixHitPoints"), the chrono | **skipped**; 152 runs every frame |
| PROC 201 / 202 (and COND 90 TestPower, 82 uses, which the interpreter has) | 21 | Ed's powers | PROC 201 / 202 **skipped** |
| PROC 118 SaveGame | 21 | | **skipped** |
| PROC 126-128 PersoLight | 77 | a light carried by a perso (lightning, glows) | **skipped**, purely visual |
| PROC 212-214 particles | 8 | | **skipped**, purely visual |
| PROC 165 / 173 / 182 / 192 / 199 | 134 | camera speed, angle, shake, orientation | **skipped** (the camera is the port's own) |
| PROC 241 (c2boxer) | 2 | under a distance, something on a channel (`0x48b7d0`, not read): 750 hits in 5 s | **skipped**, not understood |

## Game progress (2026-11-11; **read** unless marked)
What the scripts read and write as progress, surveyed over every AI model of the 21 retail levels (a scratch survey of
the primitives below; 525 distinct uses). The state is small:
- **Powers:** the u16 at `*DAT_004ec6d4`. `Cond_TestPower(m)` (COND 90, `0x419680`) = (powers & m) == m;
  `Proc_AddPower` (PROC 202, `GAM_00419650`) |= m; `Proc_SubPower` (PROC 201, `0x419660`) &= ~m; PROC 200
  (`GAM_00419670`) := m. `GAM_004196a0`(0..3) adds 0x7F / 0x80 / 0x100 / 0x200. The scripts use the masks 7, 8, 0x70,
  0x80, 0x100, 0x200. **0x80 is flying** (cavedoc's J2P_GelePouvoirVoler takes it inside a zone and gives it back;
  LeCanyon's gencanyon gives and takes the wings). What 7, 8, 0x70, 0x100 and 0x200 are: **open** (Ed's scripts test
  each).
- **Who gives them:** cavedoc's robot (rob-val FRB_RoboBne B11) gives **7**; sud0's Doc (docfou) gives 8, 0x70, 0x80,
  0x100 and 0x200; the cheat-code reader gives any.
- **Save values:** 31 i16 (FUNC 93 / PROC 143 / 144). Slot 0: the bees (docfou, gen_IncreaseBees). Slots 1-6: read by
  sud0's Doc and by Suzy, written by no script with a constant slot (the inventory writes save values by the object's
  slot, perso +0x34: Doc's six things, **inferred**). Odd slots 7-23: read by sud0's gen_arche (HEG_ArcheMagicMush adds
  them up), written only by the cheat codes in script. Even slots 8-28: read by genplainesud. Who writes those two
  rows in normal play is **open** (not a script with a constant slot).
- **sud0's gates** (the sectors `GATES_OPEN` removes at export): `sctporte`, the wall across the river, drops 50 by
  `Proc_TranslateLocalSector` (PROC 117) in Suzy's (HEG_LaBelleSuzy) first schedule **when Ed has power 7**; without
  it she moves `sctfeuillage3` instead and stays. `sctporte1` / `sctporte2`: the tomato cannon (HEG_TomateCanon), PROC
  117 over 1000 ms. `sctportedroite` / `sctportegauche`: the mummies and ZOR_GenDemoPyramide, `Proc_RotateSector`
  (PROC 109) by 80 degrees.
- Uses, all levels: TestPower 607 rows (before merging the same model across levels), LevelIs 393, SaveValue 348,
  AddPower 264, SetSaveValue 197, GameAction2 156, SubPower 135, ChangeLevel 112, RotateSector 98, TranslateLocalSector
  79, IncSaveValue 74, AddObjectInInventory 40, SaveGame 24, TranslateSector 24.

**Port today.** Three stand-ins fake progress, and they depend on each other: the powers start all on
(TC_AI_POWERS_ALL; tc_ai.h), cast.py `freeze_story_removals` turns the removals under a power or save-value test into
freezes, and level.py `GATES_OPEN` leaves the gate sectors out (all but the river wall, below). Since 2026-11-11 the powers are real state in
tc_ai_globals (the exe's test, PROC 200-202), and the powers, the lives and the save values go through a level change
(tc_viewer_carry.has_progress; from a menu a level starts afresh; replaycheck's portal run checks it). The powers are
not in the recordings' hash. The inventory is a stub.

**Odd in the data (2026-11-11, from the survey below):** LeCanyon's YAS_GestionDuChrono and NBD_TransRetry call
IncSaveValue(33), past the 31 values the port keeps (it ignores the call; FUNC 93 gives -1 out of range, as read). What the
exe's `GAM_00419620` does with 33 is **open**. LeCanyon's SDO_ActiveLesAiles gives 7 as well as 0x80, and FRB_Gen_ArcheCanyon
(BigBoss, cock01, LeCanyon) takes both.

**Moving sectors (2026-11-11; read unless marked).** The exe keeps a table of the sectors its scripts move (`DAT_005212f4`,
0x48 a record, made on a sector's first order: `GAM_0042a000`). `Proc_TranslateSector`(sector, to, ms) (PROC 116,
`GAM_0042a350`) and `Proc_TranslateLocalSector`(sector, by, ms) (117, `GAM_0042a270`) set a target and a velocity
(`GAM_0042a1c0`: the way / ms, per ms; no time: the whole way as the velocity). Each frame after the persos
(`GAM_00429d00`): while the target is ahead along the velocity the sector moves ms x velocity, then it is put on the target
(so with no time it is far past for one frame, then there). `Proc_RotateSector`(sector, axis, pivot, degrees, ms) (109,
`GAM_0042a100`, `GAM_00429e80`): the angle / ms a frame until the angle is reached, x -> R (x - pivot) + pivot
(`GAM_00429b40`, `GAM_004298d0`). Both move the sector's SuperObject and its children, and the persos of its character
list that are flagged or stand on it (`GAM_004299e0`, `GAM_004297a0`).
**Port:** script nodes of type 27 are the sector's index in the .ttlv SECT (ai.py `resolve`); the orders go through
tc_ai_globals.sector_orders to tc_cast's moving sectors (tc_cast.c `move_sectors`, 16 at most), stepped each tick as the
exe's frame; the sector's collision triangles leave the static grid and follow its matrix (`tc_collision_move_sector`); the
viewer draws its ranges at that matrix (tc_viewer.c `add_mesh`). **Not done / not verified:** the persos in a moved sector
do not move with it; the turn's sense (`FUN_004a9588`'s axis-angle is not read: taken as right-handed); nothing is drawn
between ticks (30 steps a second); the sector's own floors and portals (tc_sector) stay where they were; the camera's and
the other raycasts see the moved triangles (they are the static world's).
**sud0's river wall** is exported again (level.py STORY_GATES keeps the other four out): castcheck `river_wall`: without
power 7 it stands, with it Suzy drops it 50 at the level's start and the way is open. Headless, on a scratch export; an
export from before prints an info line instead.

## The debug menu (2026-11-11; **port**)
F4 (keyboard only; Triangle on a pad is the close-up camera since 2026-11-11): the game's progress at a glance, and switches for it (tc_debug.h; the user's wish: "see flags at a
glance, what they affect, being able to trigger them on/off"). The game holds while it is open, as under a scripted picture.
- **Two pages.** Powers: a row per mask the scripts use (7, 8, 0x70, 0x80, 0x100, 0x200), ON / off / part, then "all on",
  "story start" (no powers, every save value 0), whose uses to show, and "reload the level". Save values: the 31 slots.
- **Moves.** Forward / back, the look keys or the left stick choose; left / right change (a power off / on, a value - / +,
  by 10 with Fast); Jump or Action switch (a value: 0 <-> 1). A held direction repeats.
- **A switch is live:** it writes tc_ai_globals at once (flying goes the moment 0x80 is off). Most scripts test once, at
  the level's start, so "reload the level" starts this level again at Ed's start with the progress as set: a level change
  into the same level (tc_viewer_pending_level), the portals' path, so the frontends and the recordings need nothing new.
- **What uses a flag:** the pane beside the rows lists the characters whose scripts hold a call with that mask or slot, in
  this level (with the fix's persos: Ed, the cheat-code reader) or in every level. It is the progress survey as data:
  `python3 -m tools.ttre.progress files/out` (a step of tools/convert_files.sh) reads every perso's scripts in the 21
  retail levels and writes files/out/ttlv/progress.ttpg (337 rows) and progress.tsv, the same as text. A row says a script
  holds the call, not that it runs. Calls with a computed slot are listed under every save value ("any slot").
- **All in the core:** the state, the moves and the picture (800 x 480, an 8x8 public-domain font: font8x8). A frontend
  loads progress.ttpg from the levels' folder (tc_viewer_set_survey), toggles (tc_viewer_toggle_debug), and draws
  tc_viewer_debug_panel as it draws the speech panel. While open, only the held actions and F4 reach the viewer.
- **Recordings:** opening and closing is a door (TC_RD_DEBUG); the moves are the frames' held actions. The rows do not
  depend on the survey, so a replay is the same without it. The powers are still not in the recordings' hash.
- **Checked** by `make debugcheck` (in the gate): the moves, power 7 off, the reload with the river wall standing, a recorded
  session replayed exact. **Not seen in the app.** The SDL build's part type-checks against SDL3 here, no more (its shaders
  do not compile on this machine).
- **Not done:** what 7, 8, 0x70, 0x100, 0x200 are (the menu says "not read"); the lives; the camera-key settings and the
  close-up key's remap (the user's step 2); labels for save values 7-30.

## Custom gameplay (2026-11-12)

Gameplay Tonic Trouble never had is kept apart from the game's, so it can't be mistaken for it (the user's rule). In the
core: `core/src/custom/` and `core/include/tonic/custom/`, everything named `tcx_*`; the rest of the core calls in only
through `custom/tcx.h` (a handful of hooks in tc_viewer.c and one in tc_level.c: grep `tcx_`), and the level's custom data
hangs off one pointer (`tc_level.custom`). `make CUSTOM=0` builds the core without any of it (the hooks become no-ops,
the custom layer's chunks are skipped) into `core/build-nocustom`; `make -C core CUSTOM=0 checkall` passing shows the
game doesn't lean on it (2026-11-12: both builds pass, camcheck the same). Without it a level the custom layer gave a
car still loads, the car standing as a plain perso. On the tools side the custom layer is `tools/ttre/custom.py` and its
helpers (`vehicle.py`, `dff2gltf.py`, `rw.py`), and the SSX Tricky import (`ssx.py`, below). Not yet sorted (another session): what the port added on top of the game
elsewhere (the walkable hint scenes, the arches between editions, the camera's own passes) against what is the game's.

## Vehicles (custom gameplay, 2026-11-12)

Tonic Trouble has no vehicles: this is a toy for the imported levels, the first piece of gameplay of our own. Ed's car in
LibertyCity2001 is a GTA III car mod (files/sources/Vehicles/manana: the Mazda MX-5 that replaces the Manana).

- **Model:** `python3 -m tools.ttre.dff2gltf MODEL.dff OUT.glb [--paint R,G,B]` turns a GTA III-era .dff + .txd (PC
  RenderWare, tools/ttre/rw.py) into a self-contained .glb: the undamaged parts, the four wheels, the paint baked into the
  textures (per material as the game splits them: the BinMesh PLG; the triangles' own material field is 0 in GTA's
  files). It also opens in Blender or goes through import_gltf.py as a level of its own.
- **Into a level:** tools/ttre/custom.py VEHICLES (level, model under files/sources, paint, place, heading) ->
  tools/ttre/vehicle.py: a PRSO perso (body on channel 0, a channel per wheel), a PCOL box (Ed bumps into it, stands on it)
  and a VEHI record. The .glb and the PNGs go to files/out/Vehicles/<car> (the install scripts copy the PNGs). The custom
  layer skips a level whose edits are already in it (CUST holds their digest), so a re-run doesn't rewrite Liberty City.
- **Core:** core/include/tonic/custom/tcx_vehicle.h, core/src/custom/tcx_vehicle.c (the car, the ride), tcx_play.c (its
  part of the play session: the viewer's hooks), tcx_level.c (VEHI) (second pass 2026-11-12, "tighter, more engaging and realistic"). Fixed
  1/60 s ticks: a rear-wheel-drive engine whose push falls off with speed against aero drag (0-100 km/h in 5 s, about
  150 km/h top), brakes into reverse; two axles whose tyres push sideways with their slip angle up to a grip limit (about
  15 u/s^2; 12 felt too slippery to the user), the yaw from the two about the centre of mass: it runs wide at the limit (the front's limit a little under
  the rear's, the rear tyres stiffer), the drive uses up some of the rear's grip (power oversteer out of slow corners), the
  handbrake (Jump / Space) leaves the rear 40 % of its grip, eased in over 0.1 s and back over 0.35 s (the user found the first
  version's drift "dodgy": it snapped both ways), and the steering lock opens up again while it slides, to catch it; below
  3 u/s and in reverse the plain bicycle model, blended into the tyres' up to 7 u/s. Fast (Shift) hops it (7.2 u/s up, about
  1.3 high, one per press); it keeps turning in the air. The body is drawn on
  springs: nose up under power, down braking, leaning out of turns, bouncing on landing (Ed in the seat rides with it).
  On the ground it follows the static collision under its wheels (a ray each, steps up to 0.7, tilts with the ground,
  falls off ledges); three spheres along the body against walls (a knock takes some of its spin); back to its start after
  4 s in the air. Measured headless on a flat test square (scratch probe); not tuned by feel yet. The ride: Action within 1.6 of the body hops Ed into the driver's seat (0.55 s, his "jump"
  clip stretched over the arc, kept under a ceiling), shrinking to the car's rider scale (0.42: the user asked for a
  shrunken Ed inside), where he stands in his idle, tilting with the car (the seat: GTA's ped_frontseat dummy mirrored to
  the driver's side); W / S throttle and brake, A / D steer, Space (Jump) the handbrake,
  Shift (Fast) a hop (swapped on the user's word, 2026-11-12; the left stick too); Action hops him out beside the driver's door (else the other side, the back, the front), where
  his control comes back. Meanwhile his AI and mechanics hold, and so does the cast (it steps on his clock: no level with
  a cast has a car yet). The camera: the follow with the ski preset (pulled back with speed) and a chase that turns in
  behind the car (tc_cam_target.chase, critically damped, 0.35 s); the cars don't block the camera and aren't in its sight
  test. Title bar: "driving N km/h".
- **Not done:** collisions with persos or other cars, damage, sounds, lights, a sitting pose for Ed, a prompt (the
  imported levels have no font), the cast running while he drives.

## SSX Tricky courses (custom layer, 2026-11-12)

The user's wish: ride SSX Tricky's courses (PS2, 2001) as the retail skier. `tools/ttre/ssx.py` reads their own disc
image (an .iso under files/sources) and writes each course as an imported level, SSX_<Course>.ttlv + .ttai, its textures
in files/out/Imported/SSX_<Course>/ (the pickers file it under "Imported"; the checks pass it by). tools/convert_files.sh
step 6b runs it after the levels (it needs Totalski: the skier is borrowed from there). About two minutes for all 11.
Format notes come from the SSX modding community (GlitcherOG's SSX-Library, SsxMapper's map extractor, both GPL or
unlicensed: read, not copied); the details are in the module's docstrings.

- **Read:** the ISO 9660 volume, the C0FB archives (RefPack); the .pbd's patches (bicubic, stored as polynomial
  coefficients, front = dS/dt x dS/ds), models (PS2 packets: strips, uv, normals, positions; per object matrices) and
  instances (row-vector matrices: checked, 1985 of 1997 Aloha instances land in their own stored box); the .ssf's
  per-instance properties (visible, solid, surface, collision mode) and triangle-proxy collision models; textures
  (8-bit + a CSM1 palette, colours stored 0..127), lightmaps, the sky (<course>_sky.pbd / .ssh), the start gates.
- **Lighting:** the PS2 draws a patch as (texture - L.rgb) x L.a / 128 (L its lightmap texel; SSX-Library's
  TrickyLightmapConverter). The first pass multiplied by L.rgb (~0.1): everything black (user, 11-12). Now the textures
  are doubled to full range and each vertex gets (1 - L / texture mean) x L.a / 256: exact on flat textures (port
  choice; SSX-Library least-squares fits per texel instead). Models: ambient + their instance's three directional
  lights (PS2 0x80 = 1) against the doubled textures (port choice: so they match the snow; not checked).
- **Collision:** patches (type 17 not solid); instances by the .ssf: mode 1 its triangle proxy, 2 the model's box,
  0 / 3 (moving props, left static) none; invisible solid ones = the course's walls. Floors (ssx.FLOORS): snow, ice,
  wood, metal grip 0; off-track, powder, rock, sand 0.04-0.08; props' tops 0.1, their steep faces 0 (friction acts on
  every contact: at 0.1 the skier stalled against walls). The start gates' barriers (instances round the gate) are
  drawn but not solid: SSX opens them at the race start, which there isn't.
- **Start:** down the middle gate's path to the first place with ground just under it whose own normal slopes down
  the path (>= ~5 deg). The .ttai carries AUTO (tc_ai.h `auto_start`): the driver presses Jump until the skier leaves
  his starting behaviour B2, so he sets off by himself (Totalski / skymap had that by level name already).
- **Sky:** the dome (one model, ~28 m, camera-centred in SSX) scaled to 1.3 x the course's half-diagonal round its
  middle, faces turned inwards, unlit (vertex alpha TC_SKY_ALPHA), not solid (port choice: it doesn't follow the camera).
- **Port choices:** 1 SSX unit = 1 cm (Eddie's bone chain is ~182), scaled 1.25x so a rider would be ~2.28 to Ed's
  2.17 (user: size felt fine); 8 x 8 quads a patch. Music: Totalski's track for now (tools/music.tsv).
- **Headless (aicheck AI_SCRIPT=ski, all 11):** he sets off by himself, 5-41 u/s at 2 s, 0 frames into the snow on
  every course. The script's timed hard turns run him into the course walls on four (Aloha, Elysium, Mesablanca,
  Snowdream), where he ends slow or stopped: needs a playtest with real steering.
- **Not done:** SSX music; out-of-bounds (Reset, type 0) surfaces don't respawn (falling out of the world does, 20
  below the lowest geometry); animated / moving props stand still; LODs, flipbooks, UV scrolling, particles; rails
  (splines, no grinding); no tricks / boost. Nothing seen in the app since the lighting fix.
