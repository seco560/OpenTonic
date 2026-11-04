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
  activity marks decide who is awake (.ttlv CAST flags 4 and 8, tools/ttre/cast.py); within a tick, the run-first ones
  step before the rest (**read**). Within each pass in level perso order (**port**: the exe walks the list's order,
  then the tree's); the frame stamp is implicit (each actor steps once).
- **Ed** is stepped by tc_ed_ai before the cast, every tick (tc_viewer step_ai) (**port**: in the exe he is one
  perso of the tree walk). Folding him into the same loop is the direction (docs/DIRECTION.md, point 2).
- **The cast does not run mechanics or move** (**port**, the biggest gap): persos with the mechanics flag turn in
  place. Their collision is baked into the static world at frame 0 by the exporter (tools/ttre/level.py
  `collision`), so nothing moves under Ed, `FUNC 54` (the perso he stands on) is always none and the crate cannot be
  pushed.
- Left out of the cast (**port**): the camera and its directors, the cheat-code reader, the arches, the persos the
  exporter reduced to data (portals and walk-in passages: .ttai PORT; Totalski's obstacles: TRIG; the light managers:
  LAIS). Each is a stopgap to fold back into the cast, one subsystem at a time (docs/DIRECTION.md).

## Next: moving collision (the "run first" persos)
What it takes, in order (each read from the exe, with a headless check):
1. **Per-perso collision.** Export each perso's ZDR collide set as its own mesh in its own axes (not baked into the
   static world), with the sector it is in; the sweep tests them where the perso is now. Read first: how
   `FUN_0043c640` (the characters of the swept sectors) builds their matrices, and the flag-8 sweep's perso
   exception (collide set +0x37) (docs/DECOMP.md "Where the port is not the exe", item 4).
2. **Their mechanics.** The card types the platforms and the crate use (9, 10, the fn1 handlers of 1, 3, 6, 8, 12:
   docs/DECOMP.md item 9), root motion from their animations, and the requests their scripts make
   (SetMoveDirection under mode 12: the crate's push).
3. **Riding.** The moving-frame second pass (`DAT_008bcaec`): a perso standing on another moves with it; `FUNC 54`.
4. Then the cavedoc crate puzzle (docs/NOTES.md "The cast": JET_AReparer), sud0's slab (dalfou), north's rings,
   Land's bridges become playable.
