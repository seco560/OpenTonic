## Outstanding Bugs

Originally created by me (i.e. technically author of this repo, not Claude) to report bugs I found during playtesting; will be updated over time to reflect their status and may be updated with other bugs me or Claude find.

* Portal Ed Clone: after disappearing through a portal, a "clone" Ed is left behind. This may be a regression as it didn't happen before the previous update; possibly something to do with how "simple Ed" is still tied up in the physics engine. Fixing may uncover other pain points.
  * **Fixed 2026-10-29 (Claude), awaiting a playtest.** A regression from the 30 Hz tick blending (`step_ai`, tc_viewer.c), not simple Ed: the step-in (state 228) ends with all of Ed's parts hidden and holds that last frame (phase = the clip's length), and the blend wrapped the phase round the clip even for clips that don't loop, back to frame 0: Ed whole again where the step-in began. Only loops wrap now. viewcheck checks that none of Ed is drawn under the sud0 arch's cover. Any other state that ends holding its last frame had the same one-frame-to-start snap.

* Dodgy Edge Collision: jumping at some platforms causes Ed to repel off them and fall through, even though he should have grabbed on. May also have to do with his angle of approach. A few other related things feel off: the angle at which he grabs not being normalized to the edge itself can make him end up in weird angles which treat movement strangely.

* Ghost Water Bodies: namely under Doc's house, in the entrance to Pressure Cooker, but may also be present in other areas. Possibly level-export related.
  * **Root cause (user, 2026-10-29): misreading which sector of the level Ed is in.** The game looks water up in Ed's current sector; the port has no sectors and guesses from sector bounding boxes, which overlap. Today's rules are workarounds; the real fix is "Level Sectors" below.
  * **Mostly fixed 2026-10-29 (Claude), awaiting a playtest; needs the re-export your next build does by itself.** Under Doc's house (and the way towards the Pressure Cooker arch), the caves under the river and the well, the frozen pond in sud0, and pyramide's lower chambers are dry now (docs/NOTES.md "Ghost water"). Left: marmite's sctV01a area, the pit under sud0's falls, BigBoss.
  * **Fixed the faithful way 2026-10-30 (Claude), awaiting a playtest (re-export: your next build does it).** The port has real sectors now ("Level Sectors" below): Ed's water is looked up in his own sector, as the exe does, and the box rules above are gone. A sector that lists no water surface is dry, whatever lies above it: Doc's tunnel (sctsud17), the caves (sctsud16, 16a, 20) checked headless. Where a sector lists a surface over a dry floor (marmite's sctV01a), that is the original's data.

* Glacier Cocktail Lake (regression, 2026-10-29): the lake in sud0's snow area that leads to Glacier Cocktail (surface 0.32, around (-153, 130)) is no longer swimmable. Cause: the ghost-water ceiling rule (`open_above` in core/src/tc_mec.c) counts a point as dry when a collision face turned down lies between it and the surface, and a solid slab sits just over that lake's water (0.06..1.39), so the rule took it for ice. Water contact in general feels janky since the ghost-water changes; the underlying problem is the port having no sectors (docs/NOTES.md "Ghost water"); see "Level Sectors" below. Parked.
  * **Fixed 2026-10-30 (Claude), awaiting a playtest:** the ceiling rule is gone with the sectors; headless, Ed dropped at (-153, 130) enters sctsud10bottom01 and treads water (his water reflex, state 7).

* Water Jump Buoyancy: when Ed jumps into the water, he sinks way too low. This sometimes triggers his swimming movement too early.
  * **Partly fixed 2026-10-29 (Claude), awaiting a playtest.** Sitting on the bottom and the early (surface) swimming came from the port's water lookup: near a pool's floor the swim forces' probe fell out of the water sector's box, so Ed had no buoyancy and his scripts read "at the surface" (docs/NOTES.md "Water at a pool's floor"). The plunge depth itself (about 2.7 below the surface after a running jump) follows the original's scripts and mechanics as read, so cavedoc's 2.9-deep pool still brings him close to the floor. If the original stops him clearly higher, that's the next thing to trace.
  * **2026-10-30:** the box slack is gone too: a sector's water is an unbounded plane within it, so it no longer ends above a pool's or river's floor. sud0's river dive (aicheck swim) reaches the bed (-11.3) and swims along it again, which the boxes had broken (it surfaced at -9.6); cavedoc's pool jump is unchanged (state 5 at the surface).

* Camera Stuck Top-Down after cavedoc's Ruby Lifts (the user, 2026-11-13, playtesting the camera directors): when Ed
  steps on the moving rubies of the first lava room the camera takes over as usual, but when it lets go it stays in a
  top-down state.
  * **Fixed 2026-11-13 (Claude), awaiting a playtest.** The lift (SDO_PFDeplaceAscenseur) picks the camera's shot 7 with
    its distance and elevation from Function_Camera_GetDistMax / GetTeta (FUNC 60 / 65), which the port did not model:
    they gave a number that is not one, and the follow camera kept it for good. Now read from the exe (the camera's
    record; with 0, the default: 8 units, 0.6 rad), and the camera never keeps such a value (docs/CAMERA.md "The
    directors' shots"). camcheck rides the lift.

* Camera Inside Persos (the user, 2026-11-13; written down by Claude from the user's report): whenever Ed collides with another perso, the camera clips through it and shows its inside. Try walking into Suzy or the tomato in sud0. An earlier fix (2026-11-11: the sight test puts the eye beyond a character Ed overlaps, `tc_cam_sight_fn`'s `inside`, core/src/tc_viewer_camera.c) did not cover it fully.
  * **Partly fixed 2026-11-13 (Claude), awaiting a playtest.** Measured (camcheck "walking into Suzy and the tomato": Ed
    walks into each from 8 sides, and backs into it, 3 s each): Suzy and the tomato have no collision of their own (no
    ZDR geometry: by the exe's rules, docs/ENGINE.md "Moving collision", Ed walks into them, 30 of 80 walks went right
    through), and the camera's sight ray stopped the eye where it met them but beside a slanted surface (a shoulder),
    within the near plane: 11 frames of 12772 showed a perso's surface at the eye. The sight test now also keeps the
    eye's radius clear of every drawn perso's surface round it (it comes in towards Ed until clear): 4 frames left,
    the one seen a scripted shot's eye. Left: while Ed stands inside one of them the camera puts itself beyond it,
    as before (51 frames with the character between his head and the eye). Making Ed collide with characters that have
    no collision of their own would be a port choice (by the exe's rules as read he walks into them): not done, ask the user.

## Bigger Areas of Improvement

These might not necessarily be called bugs, but rather more overarching issues with certain parts of the game. Also not including obvious ones, like implementing actual game mechanics, story progression, item collection etc. There's a lot to go still.

* Level Export Amendments: noticed some teleports not walking in sud0; walking into the cave that connects Doc's House, walking into his house... **(Teleports done 2026-10-29 (Claude), awaiting a playtest; needs a re-export: the sud0 walk-in passages, HEG_GenTransitionFade, into and around Doc's house, the tower and the ice; docs/PORTALS.md.)** There are also some moving / disappearing textures that are rendered and with collision, we can take these one by one; the first one is in sud0 right outside the Ski Slope portal to the left. Before rescuing Doc in the actual game the area is blocked off; it opens up afterwards. Would be good to keep it open for now as we're quite a ways off with implementing full game flow and mechanics.
  * **Done 2026-10-29 (Claude), awaiting a playtest; needs a re-export.** The wall across the river is static geometry, the sector `sctporte` (one object: 4 triangles, x -104..-101, y 39..51, z -6..9.3). `tools/ttre/level.py` leaves story-gate sectors out of the geometry and collision while `GATES_OPEN = True` (temporary; `STORY_GATES` lists them per level). Set it False and re-export to have the wall back. sud0's other gates (`sctporte1`, `sctporte2` by the dungeon door; `sctportedroite` / `sctportegauche`, the double door at (-161, -28)) are in the list too (user's request).

* Level Sectors (user, 2026-10-29; keep the game faithful): the original splits every level into sectors and always knows which one Ed (each perso) is in. The port has none: it stands in with sector bounding boxes, and that is what makes the ghost water, the Glacier Cocktail lake regression and the general water jank (water is looked up in the perso's current sector). The user's hunch: the game also only renders parts of a level, sector by sector, from each sector's lists of sectors to draw; that would be the game's own culling (docs/VITA.md step 2). To read: how the engine picks a perso's current sector (who sets SectorInfo +4) and which of the sector data's lists the renderer walks; then export sectors to the .ttlv / .ttai and track the current sector in the core. Unlocks faithful water, faithful visibility (fewer draws for the Celeron, the EEE PC and the Vita), and per-sector lights and fog later. **Constraint (user, 2026-10-29): keep a "Tonic Viewer" mode in the menu for exploring; it renders ALL sectors (with a disclaimer that swimming and water may be buggy there, unless that gets fixed anyway). So the sector split must not be baked into the .ttlv: export the sectors as data (membership and visibility lists) and let the core choose per mode; game mode draws by sector, Tonic Viewer draws everything.**
  * **Done 2026-10-30 (Claude), awaiting a playtest; needs the re-export (your next build does it).** Read from the exe (docs/NOTES.md "Sectors"): every frame a perso walks from its last position to its new one through the portal faces of its sector (FUN_00451670); after a teleport, the sector whose floor is straight below (FUN_00451240); the frame is drawn from the **camera's** sector with the sectors of its list (+0x4C, FUN_00451d80). Exported as data (.ttlv SECT: sectors, portals, lists, water, which triangles and persos belong where), the core chooses per mode. **K** in both builds cycles, all from Ed's sector: his sector and the sectors it sees, sky included (the default; the user's choice 2026-10-30: draw by Ed, not by the camera) / all sectors (the Tonic Viewer's view) / his sector alone (debug); the title bar names it. Water now goes through Ed's sector. Checked against the game's own data: every exported level perso's snapshot sector is found again by the lookup, Ed's tracked sector agrees with a fresh lookup on all but about one frame per crossing. Known edge case, faithful to the rule as read: a ledge whose floor ends at a portal (Land, into the pit at (163, 26)) can be walked off under the portal's lower edge, and Ed keeps the old sector until a teleport or respawn. The exe draws from its camera's sector (they differ on 0-9 % of frames, headless); the port from Ed's. Per-sector fog and lights: done 2026-10-30 (docs/NOTES.md "Lighting: the original's formula"; L compares with the old look).

* Integrating Cutscenes: this would be a fun bit of work that would involve some custom spawn-locations (we'll eventually need to add Tooling!! see next item)

* Tonic Tooling: we're building a game engine here. So a full editing suite for it would enable freedom in doing things such as: making the lake at the bottom of the Vegetable HQ teleport location swimmable (and adding some way to get back up, too!) In the original game you die by falling there, but it'd be cool to actually swim in it and climb back up. Many other such situations, but also giving way to creating whole new levels. I still want to see Tonic Trouble 2, and am also envisioning my own interactive action-adventure ideas in this engine. This admittedly big piece of work could make or break the project, so be very careful. But still...
  * **Design considerations written down 2026-11-01 (Claude), from our discussion:** docs/TOOLING.md (source -> cook -> runtime, glTF for assets + a JSON level file, scripts in a text form, a `.ttlv` -> glTF exporter first). Not started; the camera comes first (docs/NOTES.md "Next session: start here").

## Playtest interview (2026-11-11)

The user's answers on what the notes still called unseen ("awaiting playtest"). Anything marked so elsewhere in docs/ and
listed here as fine is seen: this section wins. Special Edition items were left out (parked).

**Seen and fine:** no Ed clone after a portal; ghost water gone; the Glacier Cocktail lake swims; drawing by sector with
its fog and lights; the story walls are open (to be handled as the game does once progress exists); cock01's ice.

**The queue** (the user's words, shortened; order is not priority):
1. **Water:** jumping in is mostly fine, wants polish. Ed still clips through the bottom of water bodies. The underwater
   camera sits too high and is hard to steer with.
2. **Walk-in passages (sud0):** work, but must not play the portals' transition animation: that is for portals only.
3. **Ledge hang:** rough: Ed hangs at odd angles, or clips instead of grabbing. Later.
4. **Decals:** Agent XYZ's poster still flickers in one small corner.
5. **Pickups:** some thermometers do not work (sud0, by the portal to the SE's sud0, where the tomato enemy is). The
   antidotes (the swirly ones) play the wrong sound.
6. **Levers and machines:** most levers work. **The lever and machine persos turn to face Ed, instead of Ed taking his
   place at them.** Missing: the platforms on sud0's way to the north plain, pushing the boulders there, cavedoc's
   popcorn machine (its puzzle).
7. **Dying:** in some pits Ed still hits the bottom; some pits do not work; after a respawn he is shown falling for about
   half a second; lava and deadly water do not kill him.
8. **Lifts and platforms:** most work. Sometimes one disappears mid-path. Ed flying clips through moving platforms.
9. **Pushed things:** cavedoc's crate moves but does not start the machine (a story gate?). marmite's boxes fly off one
   way when hit, without colliding.
10. **Enemies:** bare: no proper sounds or collision. They can be killed and they hurt Ed.
11. **Shooting:** rough. Aiming in first person does not tilt the blowpipe up or down. No shot leaves while one is in
    the air (what the exe does there is to be read).
12. **Stick attacks:** hit, with no sound from Ed or the enemy and no other feedback.
13. **Cast sounds:** many still unheard. A script sometimes loops a laugh (cavedoc: fighting the corn, near the popcorn
    puzzle; probably Doc's robot's).
    * **2026-11-14 (Claude), awaiting a playtest; needs the re-export your next build does:** every sound is now placed in
      3D as the original's engine does (docs/ENGINE.md "3D sound"): distance falloff with the exe's own rolloffs, stereo
      pan from the camera, sector muting, and a perso's own sounds stopping when it is deactivated. The 30-unit cut is
      gone, so far-off persos with long rolloffs are heard now; the laugh is either placed and quieter, or still loops (then
      it is the script, not the mixer). Later the same day: the mixer's own levels (sounds about 12 dB quieter against the
      music), the pitch spread, Dolby Surround (J), and persos with no known sector muted. The likely laugh: the popcorn
      persos' idle loop plays `20_rv05` three times a cycle, in the game's own data (docs/ENGINE.md "3D sound"); not heard.
    * **The user, the same day: still there (recording 2026-11-14_125856), and sud0 louder.** The real cause, found in that
      recording: `FRB_PopcornAnim` is a 3-second scene (FRB_PopCinema activates it, then deactivates it); the removal
      guard kept it as a frozen prop that went on laughing. Fixed: a guard prop makes no sound of its own (docs/ENGINE.md
      "3D sound"). The recording replayed headless: its 13 laughs as a prop are gone, the 5 during the scene stay.
    * **The user again: the laugh must not play at all; in the original the popcorn's shot starts the popping.** The real
      cause: the level banks' events were mapped by the wrong id (docs/ENGINE.md "3D sound"): the popcorn's click played
      Bnk_20's rv05 (the laugh) and its pops were dropped. Fixed in tools/ttre/sfx.py; needs the re-export.
14. **Hint scenes:** only bar is reliable. spynorth sometimes. pharmmap, pyr_fou and mammyon can be started with effort
    but skip to the level without the line. SpyCarota can leave Ed stuck: Agent XYZ is up on the ledge, out of reach
    (once it started from below).
15. **The follow camera** (answered later the same day): fine, faithful down to the original's quirks (stuck behind walls
    in caves, centring behind Ed oddly). **It clipped into characters and showed their insides** (Suzy, the tomatoes,
    Grogh's henchmen). **Changed 2026-11-11 (Claude), not seen in the app:** the characters Ed does not collide with can
    stand in him, and the sight ray then started inside them; the eye now goes beyond such a character, at once
    (tc_viewer.c `camera_sight`, tc_cam.c). A scratch probe on sud0's boxers: the eye inside a character on 40 of 600
    frames before, 10 after (one boxer), 1 -> 0 (the other). The bar's walk in camcheck changes too (Ed hidden on 518
    frames, was 963). Recordings made before will not replay the same.
16. **The ski camera:** still odd. The original demos' camera looked more faithful, but sat too far behind Ed (why is
    open: the field of view, the aspect, the physics).

## cavedoc playtest (2026-11-11, later; the user on Linux, no powers set in the debug menu)

**2026-11-13, the user:** cavedoc can be played to its end, but the cutscene that should follow beating the robot
(FRB_RoBneus) never starts. Open: not traced yet (its B11 gives power 7 through Ed's B76, below; what should start the
cutscene after that is not read).
  * **Traced and fixed 2026-11-13 (Claude), awaiting a playtest; needs the re-export your next build does.** The cutscene
    is cavedoc's Doc (BEM_Doc, FRB_DocCave, 105). The last door (BEM_PorteDeFinDoc, 25) starts it once the lever by the
    cage (SDO_OuvrePorte, 186) is pulled: it holds Ed, opens, and forces Doc out (B5, then B3). Doc walks out under the
    fixed shot, his cage fades, he comes to Ed, says his line, then the cover and sud0 before his house
    (ChangeMapAtPosition: heg_positionedcavedocdevantdoc). Doc was never in the port's cast: his script changes level
    (PROC 29), and the exporter left every such perso out. He runs now (docs/PORTALS.md "Level changers"); the door may
    hold Ed. Checked headless from the lever on (portalcheck: sud0 at 20.3 s); not checked: the robot fight leading to
    the lever.

The debug menu works. With it the user played cavedoc from no powers: Suzy's line, through the level, the robot fight.
Reported: (a) Ed does not get the power at the end of the boss fight; (b) the crate puzzle does not trigger; (c) the
popcorn machine never starts; (d) the corn cob's shots never rebound from the targets, they go through.

**Changed the same day (Claude), headless only, needs a reinstall (the levels re-export):**
- **Perso references in dsgvars were dead in every level.** A level designer's perso dsgvar (type 10) holds the
  perso's SuperObject in the snapshot; the exporter wrote that pointer raw and the core reads a perso id there, so it
  named no one (tc_ai.c dsg_get says as much). Now ai.perso_dsg_block writes the id the scripts' perso nodes use
  (the SuperObject's +4 is the perso). cavedoc alone has 42: the crate's snap marker BEM_RecaleCaisse and its
  reset BEM_RemetCaissePositionOrigine (their crate), the falling platforms' chain (BEM_PFDes1-8), the toaster
  cameras' toasters, the rock generators, the chameleon mine. **(b), and so (c):** the crate pushed within 2 of the
  marker is now snapped into place (crate B0 -> B2); JET_AReparer, on Action, gives Super Ed only while the crate is
  in its B2 (scratch probe: before, the marker stayed in B1 for good). Other levels: same kind of fix, unseen.
- **(a)** cavedoc's robot FRB_RoBneus gives power 7 in its B11, after it ends Ed's Super Ed (his dsg15 := 0), sends him
  to take it (forces his B76, which sets his dsg58 = 1) and waits for that. cast.py kept it off Ed (MAY_FORCE_MAIN),
  so it waited for good. Now allowed (MAY_FORCE_MAIN_MODELS). Probe, from powers 0, the robot beaten: Ed's B76,
  dsg58 = 1, **powers 0x7** 6.75 s later, Ed back in his play behaviour (B87).
- Also allowed: `grille` (the popcorn grille: Super Ed holding Action in its zone works it) and `mais` (the corn cob:
  its intro and end hold Ed and let him go, as the robot's). Not seen working: no probe for either.

- **Elsewhere, from the same dsgvar fix (headless, unseen):** sud0's HEG_PlateFormeTour01-03 (the platforms on the way
  to the north plain: queue item 6, "missing") now move in movecheck (99 / 7 / 82 % of frames; were still). The
  `grille` persos are on Land (SDO_GrilleDebut, SDO_GrilleGrotteD1 / D2) and north (ARG_GrilleTour) too: they now may
  take Ed, with Super Ed. **Unexplained:** pyramide's walker 335 (YAS_Actor2, the false teeth) walked 35 in movecheck
  and now stands; movecheck visits 4 more pyramide persos first (318-321) in the same run, so it may be the order. Watch it.
- **Gate:** `make -C core checkall`'s checks, run on a full re-export (scratch) and on a clean export of the commit
  before, all pass on both; viewcheck, camcheck, replaycheck and debugcheck print the same. castcheck now starts each
  lever / machine visit from a fresh level: a grille visited first left Ed in B69, and Land's popcorn machine (54) then
  never took him, so its Super Ed check silently dropped out.

**Open:**
- **(d) The corn's rebound: prototype fix, same day (Claude), headless.** The targets (glac_cible, JET_CibleSalleMais) carry
  the 0x8000 zone; Ed, the corn, the robot and the duck carry 0x1 (the kernel bursts on them). The cause: the target's
  own R0 notices the kernel first and goes to its hit state (B1, state 4), whose zones are off, and the port applied
  that at once, so the kernel's test the same tick found no zone and it flew on. The exe applies a script's state
  change after every AI of the frame has run, so all of them see the zones of the state the frame began in. Port:
  tc_ai_perso.zone_state, set by the cast for every actor and Ed at the start of each tick (tc_cast.c begin_step);
  tc_ai.c zde_zone reads it. Probe: Ed 1.8 behind target 63, a kernel from the corn: before, it passed 0.27 from the
  zone's centre and flew through; now it turns back at 1.66 and flies past the corn. **Not shown:** the corn losing a
  hit point (the probe could not stage the fight: the corn never moves sideways to line up with Ed, its B1
  SetMoveDirection under mode 12 leaves it in place, and teleported Ed lands at z -3, so most shots aim into the floor).
  The general rule holds for every ZDE test (shots, stick hits, pickups): the whole gate prints the same.
- **(d) again, 2026-11-14 (the user: "the kernels clip through the corn after ricocheting off the targets").** Staged
  headless (scratch probe: Ed on the ledge before target 64, stepping back along the line of fire when the kernel
  leaves; the corn held where it fired from): the kernel hits the target, turns back along its path and flies through
  the corn's origin, 0.00 away, without bursting, and the corn keeps its 6 hit points. Why: the corn's only ZDE zone
  (type 0x1) is a box 0.79-3.01 above its origin (its body is drawn 0.42-3.5 above it), and the kernel (a 0.31 sphere)
  starts at the corn's origin (`META_SpawnInState_0049f190` -> `GAM_Always_004259a0` at the corn's world matrix), so
  its way back runs under the box. The exe's ZDE test poses zones with the perso's world matrix too (`GAM_ZdxStuff_0041fe60`,
  not the module path), and the distances the kernel aims with are signed, as ported (`AI_00491ba0` / `AI_00491ab0`).
  So the exe must start or move the instance somewhere not read yet: the same gap as Ed's own shots, which the port
  starts at his mouth (tc_cast.c TC_CAST_SHOT_HEIGHT: "offset not found"). Next: find where the exe puts a spawned
  instance (`GAM_Objinit_004197c0` mode 6 and the 3dData init it calls; or under the emulator). **Found and fixed the
  same day:** the instance waits inactive and the corn's shooting animation launches it from its mouth channel
  (docs/ENGINE.md "Launching generated objects"); probe: the returning kernel bursts on the corn, 6 -> 4 hit points.
  Also seen:
  with Ed one step aside the corn slides to line up with him again before the kernel is back, so the kernel misses it
  by 2-4 units whatever the zones (as the exe would: the corn's B1 runs while the kernel flies).
- **The corn does not move** (found on the way, open): B1 slides it along x to line up with Ed (SetMoveDirection,
  SetMechMode12, its card) and it stays put; then it never shoots unless Ed walks into its line himself.
- **Was (d):** the rebound is the kernel's (BNE_GrainMais, STH_tir_mais, spawned by the corn): its R0 sends it back
  along its path (B1) when its ZDE zone 1 meets a ZDE zone of type mask 0x8000
  (Cond_CollidePersoZDENoWithTypeZDE); the corn's R0 takes 1 hit point when a kernel's family touches it. To read:
  which persos carry 0x8000 zones (the targets? Ed's stick?) and why the port's test never fires.
- **The allow list itself.** cavedoc has 17 kinds of persos that force Ed; 7 may (after today). Allowing them all
  (tried on a scratch export) holds Ed for good at the spies (espion, MAD_EspionCanard: the hint conversations), as
  feared when the list was made. Still kept off: the cage lift (ascens), Doc's end door (doc_porte), Doc (docfou), the
  mines (glac_mine), the popcorn cutscene (FRB_PopCine), MAD_GenEdContent, the inventory and thermometer explainers
  (genplainesud). Each needs its own look; castcheck finds pit markers and levers by the same flag, so it must learn
  another way before the list grows much.

## Totalski (2026-11-11, late; the user: start text, the finish animations; begin with no powers)

**Changed (Claude), headless only; needs a reinstall (Totalski and skymap re-export):**
- **No powers at the game's start.** A fresh run that starts at retail Totalski (picked, or after the intro movie)
  starts with powers 0 (tc_viewer.c, after `arrive`); every other fresh start keeps them all on, and a level change
  carries them. Checked: Totalski 0, sud0 / cavedoc 0xFFFF, SE Totalski 0xFFFF.
- **The finish runs as in the game.** The chain, read from the scripts: the finish gates (genski: SYM_Gen_Arrivee
  "next", NBD_Arrivee_Et_Retry "retry", box zones across the run) set the fix Ed's dsg148 = 3 and the skier's
  arrival waypoints (dsg35 / 36) and next / retry (dsg40); Totalski's World (WSki) R0, while its dsg25 = 1 (set by
  "press Jump to start"), runs the race clock and on dsg148 = 3 forces the skier's B19. Two port faults broke it:
  `genski` was kept off the player (cast.py MAY_FORCE_MAIN: now allowed), and the fix Ed was a pure alias of the
  skier, so his dsgvars (148 the stage, 149 the best time, 150) went to the skier, who has fewer, and were lost. Now
  the fix Ed is exported as a ghost with his own dsgvars and the alias sends dsgvar access there (tc_ai.h
  alias_dsg; hit points and position still the skier's). Probe, the skier slid through the retry gate: B19 at 11.6 s,
  **B13 state 321 (stepping off), B14 state 298 for 5.6 s (likely the animation the demo shows near its end)**, B18
  state 319, B7 the walk-in (228) to the arch, back at the top. Through the "next" gate it should go on to sud0: not
  probed. The gate's checks print the same.

**Open:**
- **The start text and the clock.** "Press 'Jump' to start" is game.dsb's TXTTOTALSKI; NBD_PRESSZTOSTART blinks two
  strings (Proc_DisplayString, T30 0x31 and 0x250, every 300 ms) and World draws the run time and best time
  (Proc_DisplayChrono, T30 0x5 / 0x6). T30's numbers do not index game.dsb's strings in file order: the table is to
  read. Then the core keeps HUD text slots (ClearText clears one) and both frontends draw them in the game's font.
- skymap has the same genski persos: unprobed.
