# The camera

How Tonic Trouble's camera works (read from the exe and the level data, 2026-11-01), how the port's camera worked
before, the original's weak points, and a list of improvements. **The user chose the suggested first set (A1, B1, B2,
C1, D1, E1), with recentring as the only way out of free view; it is implemented: see "6. Implemented" at the end.**

Sources: the camera's scripts (dumped as `level-ai` dumps Ed's), the typed decompiles of its primitives
(`files/out/_decomp_artifacts/typed/ai/`), the camera module `0x4A0B70`–`0x4A5F00`, the type 3 mechanics
(`DNM_IntegrateType3_004b2900`, `DNM_RequestType3_004b3810`, `FUN_004b3d33`, `FUN_004b533c`), and the camera's
record and dsgvars read from bar / sud0. Names added to `tools/exe/ai_names.tsv` (PROC 156–197).
Confidence is marked: **read** = followed in the code; **inferred** = from usage or values, not traced.

## 1. How the original works

### The pieces
- **StdCam**, the fix perso 1 (family `Camera`, instance `StdCamer`): 35 behaviours, 25 reflexes, 126 dsgvars, 2 states
  with **card type 3** (camera mechanics). Its scripts decide *what kind* of shot; the engine's camera module decides
  *where the camera goes* each frame; the type 3 mechanics *move it there*.
- **The camera record** ("cine block", Perso `+0x10`): pointers to three parameter records — `+0` the default, `+4` the
  current, `+0xC` a second one used by the special modes (0x65 / 0x68 / 0x69) — plus the target perso (`+0x10`),
  per-frame scratch, flags (`+0xC4` / `+0xC6` / `+0xC8`) and counters (`+0xB8`–`+0xC2`). The engine accessors
  that read and write them are the `GAM_cineinfo_004266xx`–`00427470` one-liners. **read**
- **Per frame:** every camera behaviour ends with **PROC 156 UpdateCamera**. That builds a context (`FUN_004a0f90`:
  the camera's dynamics, record, position), runs the mode logic (`FUN_004a3130`), and queues mechanics request 0x11.
  The type 3 request handler then moves the camera towards the wanted position at a capped speed, and the type 3
  integrator sweeps a **collision sphere** from the old position to the new one (`MEC_SweepSphere_0043d480`) and
  builds the look-at orientation with angular smoothing (`FUN_004b533c`). **read** (the smoothing maths: not traced
  line by line)

### The parameter record (defaults from the data; the same in every level checked)
| Offset | Default | Meaning | Set by |
|---|---|---|---|
| `+0x0C` vec | 0 | pivot offset, in the target's frame: the point the distance is kept from (`FUN_004a1e10`) **read** | PROC 170 |
| `+0x18` vec | 0 | look-at offset, in the target's frame; 0 means (0, -0.05, 0) (`FUN_004a1c60`) **read** | PROC 171 |
| `+0x24` vec | 0 | another offset; the follow preset puts (0, 0, 1.35) here **inferred**: aim height | PROC 172 |
| `+0x2C` | 1.35 | aim height above the target's origin **inferred** | — |
| `+0x30` / `+0x34` | 8 / 8 | distance min / max: the camera stays this far from the pivot (`FUN_004a2380`) **read** | PROC 163 |
| `+0x38` / `+0x3C` | 5 / 8.5 | a second distance range; under its minimum the camera looks for a new spot (`FUN_004a5600`) **read** | PROC 164 |
| `+0x40` / `+0x44` | 0 / 0.15 rad | horizontal angle round the target, and its tolerance (`FUN_004a2180` -> `FUN_004a1f30`) **read** | PROC 173 slots 0 / 5 |
| `+0x48` / `+0x4C` | 0.6 / 0 rad | elevation, and its tolerance **read** | PROC 173 slots 1 / 6 |
| `+0x50` `+0x54` `+0x58` | 0 | three more angles, copied to the mechanics **inferred**: the view's own turn / tilt / roll | PROC 173 slots 2–4 |
| `+0x5C` `+0x60` `+0x64` | 0.35 | three smoothing factors **inferred** | PROC 195 |
| `+0x68` / `+0x6C` | 200 / 12 | linear speed max / min: the camera may move at 2 × the target's speed within these (`FUN_004a2a10`) **read** | PROC 165 |
| `+0x70` / `+0x74` | 25 / 7 | **inferred**: angular speed max / min | PROC 166 |
| `+0x78` | 3.14 | **inferred**: an angle limit (π) | PROC 167 |
| `+0x7C` / `+0x80` | 10 / 5 | not identified | PROC 168 |
| `+0x84`, `+0x88` | 0.6, 0.04 | not identified | PROC 196, 169 |
| `+0x8C` | 1.5 | **inferred**: the field of view (rad); PROC 178 / 179 animate it (flag 0x20 = zooming) | PROC 178 / 179 |
| `+0x92`, `+0x94` | flags | two flag words (behaviour switches: slope tilt 0x800 on `+0x94`, axis lock 0x80 / 0x100, ...) **read** (bits only partly named) | PROC 184 |
| `+0x98` | 0 | **the mode** | PROC 177 |
| `+0xA0` | 0xFF | the target's channel to follow (0xFF = its origin) **read** | PROC 197 |

### Modes (`+0x98`, PROC 177) **read** unless marked
- **0, follow** (the normal camera, the only one with the full pipeline):
  1. *Target and pivot:* the target perso (default Ed; any perso via PROC 180) or one of its channels; pivot and
     look-at points = its world matrix applied to the two offsets.
  2. *Direction:* it **keeps the camera's current direction from Ed** (a lazy follow), measures that direction's
     horizontal angle relative to *behind Ed* and its elevation, and clamps each into [angle, angle + tolerance].
     With the defaults that is "behind Ed within 0.15 rad (9°), at exactly 0.6 rad (34°) up". The follow preset (camera B2)
     uses 0.45 rad.
  3. *Distance:* clamped into [min, max] (B2: 7 ± 0.3, stretching up to 1.5× (10.5) as the camera swings round to Ed's front).
  4. *Height band:* the camera's z clamped to `cine +0xA4 .. +0xA8` (`FUN_004a1690`).
  5. *Slopes:* with flag 0x800 the elevation follows the slope Ed runs on, rate-limited (`FUN_004a2e20`).
  6. *Speed:* the move is capped at 2 × Ed's speed, within [12, 200] u/s, and raised while Ed stays hidden.
  7. *Visibility watchdog* (the camera's behaviour most players notice): every few frames it casts a ray from the camera to Ed
     (`FUN_004a4260` / `FUN_004a42c0`, collide mask 0x2F). After enough failures **it searches 13 candidate spots and
     jumps there** (`FUN_004a5400`, `FUN_004a5c60`): a hard cut. It does the same with 7 candidates when the camera
     gets closer than the second range's minimum (`FUN_004a5600`), and when Ed runs at the camera (`FUN_004a5860`).
  8. *Ideal position:* the same computation with zero tolerance, kept as "where the camera would be dead behind"
     (`FUN_004a2c80`); used when the camera resets.
- **1, freeze:** the camera stays where it is, recording its position and angles (fixed shot from the current spot).
- **2, locked position, turning to the target:** used by first person (attached to a bone, below) and by fixed shots.
- **3:** a fixed offset from the target (set from where the camera is when flag 0x40 is on). **inferred**
- **5:** a path (the cine `+0x14` / `+0x18` counters). **inferred**
- **6, 0x65, 0x68, 0x69:** special or cinematic modes, not read.

### The camera's scripts: presets around one follow loop
- **B0** is the main loop: it reads Ed's state, applies the per-situation settings, calls UpdateCamera.
- Most other behaviours are **presets**: they set distance, angles, offsets, speeds and flags from the camera's own
  dsgvars, then return to B0. B2, the standard follow, uses elevation `dsg113` 0.45, distance `dsg114` 7 (± 0.3), a
  second range −1.3 .. +3, aim offset (0, 0, 1.35), and distance × 1.5 at most when the camera is in front of Ed.
  Close-up preset (`dsg54` = 1): elevation 0.15, distance 4. Tight preset (`dsg39` = 2): 2.3–3 at 0.3.
- **Reflex 2 dispatches Ed's situation code** (camera `dsg40`, written by Ed's scripts) to a preset:

| Code | Ed's behaviours that send it | Camera behaviour | Situation |
|---|---|---|---|
| 0 | most | B0 (follow) | normal |
| 10 | B2, B4, B8, B17 | B14 | swimming (B4 = treading water) **inferred** |
| 11 | B9, B18, B61 | B12 | dying (B61 = the deadly-liquid death) **inferred** |
| 12 | B1, B45, B46, B50, B74 | B16 | flight (B45 / B46 = the flight loop) **read** |
| 13 | B28, B29 | **B1: first person** | stick mode (B29 reads the camera button) **read** |
| 14 | B86, B94, B105 | B2 (+ PROC 189 / 191) | not identified |
| 15 | B44, B50, B54, B62, B90 | B18 | landing, dropping from a hang (B62 / B90) **read** |
| 16 | B85, B89, B91 | B26 | jumping (B85 / B89) **read** |
| 17 | B54, B58, B78 | B31 | not identified |

- Other dsgvars Ed writes: `dsg38` (0 / 1, 64 times), `dsg1`, `dsg18`, `dsg39` (shot variant), `dsg53`, `dsg69`, `dsg71`.
  Ed reads `dsg20` (8 times; the idle gate in NOTES "AI interpreter"). Not yet mapped one by one.

### Field of view and zoom **read** (2026-11-01, after the user asked)
- **The original's field of view is horizontal: 1.5 rad (85.9 degrees).** Every engine camera is made with
  `GLIGLOU_004420e0(cam, 1.5, 1.0)` (GAM_Initeng_0040de70, GAM_Basic_00410380 / 00410480 / 004105a0), type 0x67,
  near 0.1, far 1000 (`GLIGLOU_00442070`). Each frame `GAM_00424a60` copies the camera record's `+0x8C` (1.5 by default)
  into the engine camera's `+100`. The projection (`GLIGLOU_004421a0`, case 0x67): focal = viewport width x k x 0.5 /
  tan(angle / 2), and the vertical angle is then *derived* from the viewport's height: 2 atan(height x k' / 2 focal). So
  the horizontal angle is fixed and a wider viewport would see less vertically ("Vert-"). k, k' are the device
  viewport's two factors (`GLIGLOU_DevVpt_0043dad0` +0x10 / +0x14), taken as 1 (square pixels): **inferred**.
- At 4:3 (the game's era; Ubi.ini has no resolution setting to check): **vertical 69.9 degrees**.
- Scripts can change it: PROC 178 / 179 animate `+0x8C` (flag 0x20 while zooming). StdCam's own scripts never call them;
  the level scripts were not checked.
- **The original has no player zoom:** the distance comes from the presets (7 normally, 4 in flight, ...).
- **The port today:** vertical FOV 80 degrees by default ("Hor+": wider windows see more at the sides), `,` / `.` in
  5 degree steps (30-100), carried through portals. Horizontal at 4:3: 96.4 degrees, at 16:9: 112.3 (the original:
  85.9 at 4:3). Zoom = the wheel on the follow distance (default 7, the original's), x1.1 per notch, 1.5-25.
- **Done 2026-11-01 (the user: "definitely"):** vertical 69.88 degrees (`TC_CAMERA_FOV`), so 4:3 frames exactly as the
  original did, Hor+ beyond it (16:9: 102.3 horizontal, 21:9: 117.7); below 4:3 (tall windows) the horizontal held at
  85.94 instead (`tc_camera_fov_y`), so it never gets narrower than the original. The title and `,` / `.` (5 degree
  steps, 50-120) use the original's measure, the horizontal at 4:3: "FOV 86°". camcheck checks the mapping.

### The player's camera keys in the original **read** (2026-11-01)
Decoded from the camera's button entries (the Buttons formulas, DIK codes): the camera reads **Numpad 4, 5, 6** (5 also
pad button 15); Ed's "camera" button is **Numpad 0** (DIK 0x52; the stick / first-person mode toggle).
- **Numpad 4 / 6 held: orbit round Ed**, `SetCameraAngle(Me, 0, add, -/+0.04)` per camera tick on the follow's
  horizontal angle (slot 0, the centre of its tolerance window). Only while Ed lets it: his scripts set the camera's
  `dsg38` to 1 in his still behaviours, 0 in the moving ones. Idle follow B24 -> (dsg38 = 1 and 4 / 6 held) B32, the
  orbit loop; B27 the same from the idle side.
- **Ed moves: back behind him.** B32 / B24 go to B0 when the stick is pushed, and B0 sets the angle back to 0 every tick
  (the lazy follow then swings the camera behind him). So the original's orbit is "look round while standing".
- **Numpad 5: the close-up toggle** (dsg54: elevation 0.15, distance 4, stretch up to 2.5x), also only with dsg38 = 1.
- **Locked presets** (swimming, dying, flight, code 17 set dsg8 = 1): a numpad press only shows an icon and plays a
  sound (camera reflex 23 scripts 6 / 7).
- No vertical control and no zoom for the player.

### First person (stick mode, E) **read**
Camera B1 (code 13): target = Ed (`KW_5`), Ed's channels 0, 1, 2, 3, 5 hidden (his head and the parts that would
block the view), all angles 0, look-at offset (0, 0, −3) and pivot (0, 0, −0.2) in Ed's frame, **mode 2 on Ed's
channel 0x27** (a bone, presumably the head), speeds 1. So first person is the normal camera machinery, attached
to a bone and looking along it, with Ed's head hidden. How the aim works (Ed's B29 and the camera button) is the next
thing to read for shooting.

### Level scripts
Each level has its own camera directors: `GenD_Cam` (fixed shots, e.g. sud0 `JFR_cutentreemaisonsud`,
`NBD_SiEdMonteChgCam`), `genplaine*` generators, bosses and set pieces (mummies, Pyrex, Sud_bloc, boats, balancing
platforms). They mainly call **PROC 180 SetCameraTarget** (sud0 ×28, north ×20: "look at me"), **PROC 165** (speed),
**PROC 182 LockCameraAxis** (camera height kept in a band) and write the camera's dsgvars. sud0: 18 persos drive the
camera directly, north 16, bar 1 (the barman). The portals' camera places (TC_AI_POP_CAMERA) are another of these.

## 2. The port's camera today
`core/src/tc_viewer.c` `update_play` / `look`:
- **A manual orbit round Ed's chest** (his drawn position + 1.1 up), 6 units (wheel 1.5–25, kept across levels).
- **Yaw and pitch only from the player:** mouse 0.003 rad per pixel, keys and the right stick at `turn_speed`. Pitch is
  limited to ±1.5. There is no invert and no sensitivity setting, and the camera never comes round behind Ed by itself
  (except in the demo, which has its own smoothed follow: NOTES "demo").
- **Collision:** one ray from the chest backwards; on a hit the camera is put 0.3 in front of it. Nothing smooths that,
  so it pops in and out; thin gaps or a corner the ray misses let it see through walls.
- Portal arrivals aim it from the level's camera place (`aim_camera`). None of the camera perso's scripts run.

## 3. The original's weak points
From the user (2026-11-01: "one of the game's weak points"; its fixed shots hid imperfections) and from the code above:
1. **No player control in the normal follow.** The camera decides; the player can only recentre or use first person.
   (Ed's scripts read a camera button in four places; what it does is not traced yet.)
2. **Hard cuts when Ed is hidden.** The watchdog teleports the camera to a searched spot rather than moving it, so
   walking behind a pillar or under a ledge cuts the shot. The 13-spot search is also slow to react (every few frames,
   after several failures).
3. **A rigid angle.** Within 9° of directly behind Ed at a fixed 26–34° elevation, the camera swings behind him
   whenever he turns, which is disorienting when he turns often (and on slides).
4. **Collision by a sphere sweep only** stops the camera at walls but does not choose a better view; combined with
   the cuts, corners produce both sticking and jumping.
5. **Scripted shots override everything.** Level directors take the target (PROC 180) or lock the camera's height,
   with no way for the player to look around.

## 4. Improvements to choose from
Each is marked **F** (keeps the original's behaviour or data), **E** (an enhancement over it), with its cost on the
Vita. Constraints for all of them: the camera logic lives in the core only (a camera module beside tc_viewer), the
frontends keep taking just the matrix, and the camera's sector already picks the frame's lights and fog.

**A. Structure**
- **A1. A core camera module (`tc_cam`)** that owns the follow, collision and input, with the original's parameters
  (the record above) as its tunables. *Recommended first step*: everything below builds on it. Cost: negligible.
- **A2. Run the camera perso's own scripts and the type 3 mechanics** (tc_ai already holds a second perso's
  dsgvars), so presets, Ed's situation codes and the level directors drive it as in the game. **F**. The most faithful;
  a large port (the camera module is ~3000 decompiled lines). Can come later on top of A1, feeding it parameters.
- **A3. Read the presets from the data instead** (the dsgvars above, exported in the .ttai) and switch them by Ed's
  situation code without running the camera's scripts. **F** (approximate). Much smaller than A2.

**B. Player control (the user's main request: free-cam built in, no mode switch)**
- **B1. Free look always available:** the player's mouse or stick input takes over at once; after a quiet period
  (e.g. 1.5–3 s of no look input, configurable) or on an event (Ed starts a new action, a director shot starts) the
  automatic follow eases back in. **E**. *Recommended.*
- **B2. Recentre button** (behind Ed, eased, not snapped). **E**, cheap.
- **B3. Director shots as hints:** a level's fixed shot or PROC 180 target *suggests* a direction; the player can
  still look around, and letting go returns to the shot. **E**. Needs A2 or A3 to know the shots.

**C. Follow behaviour**
- **C1. The original's lazy follow** (keep the current direction, clamp into angle + tolerance, distance range,
  speed cap 2 × Ed's), with a **wider horizontal tolerance** than 9° so it doesn't swing behind him on every turn. **F/E**.
- **C2. Slope tilt** (flag 0x800 behaviour, rate-limited). **F**.
- **C3. Look-ahead:** aim a little in front of Ed in his direction of travel, more at speed (slides, flight). **E**.

**D. Collision and visibility**
- **D1. A swept sphere** (the original's) with **smooth pull-in and slow release**: in fast when the view is blocked,
  out gently, never past a wall. Replaces the port's popping ray. **F/E**. *Recommended.*
- **D2. Replace the cut-and-search with a gradual move:** when Ed is hidden, swing round (along the angle range)
  towards an open direction instead of teleporting; cut only if no open direction exists. **E**.
- **D3. See-through for small occluders** (fade a thin pillar between the camera and Ed rather than moving).
  **E**; costs a draw-list pass. Probably later.

**E. Settings and input**
- **E1.** Sensitivity and invert (separate X / Y, mouse and pad), field of view (the original's 1.5 rad as the default
  if C1 confirms it), the same acceleration curve on mouse and pad. **E**, cheap.
- **E2.** First person in stick mode as the original's (attached to Ed's channel 0x27, his head hidden), which the
  shooting work needs next. **F**.

**Suggested set for a first pass:** A1 + B1 + B2 + C1 + D1 + E1, with A3 next so Ed's situations (swimming,
flight, jumps, landings) get the original's presets, then E2 for shooting. A2 and D2 / D3 later.

## 5. Not read yet
- The smoothing maths in `FUN_004b3d33` / `FUN_004b533c` (exact lag per axis; the 0.35 factors), and the sphere's
  radius (the camera's collset).
- `FUN_004a5400` (how the 13 / 7 candidate spots are chosen) and `FUN_004a4260` (visibility test details).
- The record fields marked "not identified", the `+0x92` / `+0x94` flag bits by name, modes 3 / 5 / 6 / 0x65+.
- Ed's camera button (B29 and three other places) and the meaning of codes 14 and 17.
- Whether `aicheck`'s skipped primitives (3_164 × 214, 4_290 × 87, ...) are camera calls.

## 6. Implemented (2026-11-01: A1, B1, B2, C1, D1, E1)
- **A1: `core/include/tonic/tc_cam.h`, `core/src/tc_cam.c`.** `tc_camera` (the view: moved there from tc_viewer) and
  `tc_cam`, the play camera. tc_viewer keeps one (`tc_viewer.cam`) and runs it in `update_play`; the frontends still
  take only the view's matrix. Cost per frame: one sphere sweep through the collision grid.
- **C1, the follow.** Pivot = Ed's drawn origin + 1.35 (`TC_CAM_AIM_HEIGHT`); elevation 0.45 rad; distance 7 (the wheel
  zooms it: `tc_viewer.orbit_distance`, carried across levels as before; the old default was 6), stretched linearly up
  to 1.5x as the camera swings round to Ed's front. The camera keeps its direction while it is within 0.7 rad of behind
  him (`TC_CAM_TOLERANCE`; the original's 0.15), and is dragged along at that edge beyond it, easing with a 0.35 s lag.
  The numbers marked "original" in `tc_cam.h` are the original's data (camera B2, the record, the camera's ZDM); the
  tolerance, the lags and the minimum distance are the port's choices, not read from the exe.
- **B1 / B2, free view and recentre.** Any look input (mouse, arrow keys, right stick) takes the camera over; it keeps
  following Ed's position, never his direction, until a recentre (X, the middle mouse button, the pad's L2): it eases
  back behind him (a 0.15 s lag: about 0.75 s from the tolerance's edge) and follows again. Recentring is the only way out of free view (the user's
  choice); entering play (Tab), a new level and the demo start in follow mode. The title shows ", free camera".
- **D1, collision.** The camera perso's own sphere (r 0.37) swept from the pivot out towards the wanted spot with the
  exe's sphere sweep (`tc_mec_sweep`, FUN_0043bff0's port), against every surface of the level (no sector row).
  Blocked: in at once, the same frame; clear again: back out with a 0.6 s lag. Minimum 0.5 from the pivot.
- **E1, settings.** `tc_viewer_set_look` (sensitivity 0.25-4 for mouse, keys and stick alike; invert X, invert Y);
  keys `;` / `'` (x1.25 steps) and Y (invert up / down); the title shows ", look 125 %" and ", invert Y" when not the
  default. The Swift app keeps them in UserDefaults (`lookSensitivity`, `invertX`, `invertY`); the SDL build starts
  from `TONIC_LOOK=percent` and `TONIC_INVERT=x|y|xy` and keeps them across levels for the run. Field of view: the
  existing `,` / `.`.
- **Checks:** `make -C core camcheck` (hand-made geometry: placement, the lazy tolerance and drag, the stretch, free
  view, recentre, the wall: in at once, out eased, nothing between Ed's head and the camera; then 60 s demos on north,
  sud0, Land, bar and LeCanyon: 0 frames with a surface between his head and the camera). viewcheck: free camera,
  recentre, the look settings and their title text.
- **Known effects:** Ed's scripts read the camera's position (FUNC 2: its angle round him, for the controls), so
  moving it to the original's 7 units changes the seeded runs a little: viewcheck's north demo (seed 12345) now has 4
  jumps and 4 kinds of act where its check wants 5. Over 24 seeds that check passes 15 times before the change and 13
  after (a scratch probe): it is that sensitive to any change, not a camera fault. Left failing for the user to decide
  (a sturdier check would look at several seeds). With the follow, holding a direction sideways makes Ed run in wide
  circles (the camera drags round, the controls are camera-relative): the original does the same, tighter.
- **Not done (from the list):** A2, the rest of A3 (below), B3 (director shots), C2 (slope tilt), C3 (look-ahead), D2 (swing round instead of the original's cuts; the port never cut), D3,
  E2 (first person). Portal arrivals still aim the camera from the level's camera place; the follow then drags it in
  if it is beyond the tolerance. docs/controls.png is not re-rendered yet (`tools/render_controls.sh`).

### Second pass (2026-11-01, after the user's first playtest: "works well in some places and quite bad in others")
- **Free view ends by itself** after 3 s without look input (`TC_CAM_FREE_IDLE`, the user's call), easing back behind Ed
  (0.4 s lag, gentler than a recentre's 0.15 s); each look input restarts the clock; recentring still ends it at once.
- **The mouse looks from the start of a level** (both frontends; not in the demo; the Swift app only while it is the
  active app, as capturing detaches the pointer system-wide). Esc releases it, a click takes it again. The first mouse
  movement after capturing is dropped. The user saw a click "cause a re-centre": nothing in the code recentres on a
  click (not reproduced headlessly); the likely cause is that jump in the first movement (or SDL moving the pointer to
  the window's middle as it enters relative mode), which the drop covers either way.
- **A3, the presets read so far,** chosen by what Ed's scripts write into the camera's dsg40 (`tc_viewer_camera_situation`;
  the port's AI already held the camera's dsgvars: `tc_ai_other_dsgvar_float`). Headless (the demo on north and Land):
  16 while he rises from a jump, 15 while he falls, 12 flying, 0 on the ground.
  - **Jump (16) and fall (15): the camera's height is held** (camera B26 sets its z band to its own z with PROC 188 /
    190; read: PROC 188 / 190 set, 189 / 191 clear the band, cine +0xA8 / +0xA4), so it looks at Ed without bobbing
    with him. It lets go when his origin is 3 below his take-off pivot less 0.2 (B17: `dsg74 - 3`), when he flies (B15
    clears it) or when he is back to normal (inferred: reflex 12, the camera's reset, clears it; not traced who calls
    it), then eases to the follow's height.
  - **Flight (12):** distance 4 (B16: 3.5 .. 4.5), level with the pivot (all angles 0). B16 also sets two offsets
    ((0, 0, 5) look, (0, 0, -2.5) at +0x24) that are not understood: not used, so the flight view is only part of the
    original's.
  - **Dying (11):** distance 2.6 (B12: 2.2 .. 3).
  - Distances ease between presets (0.4 s); the wheel's zoom scales them.
  - Not read yet: swimming (B14 -> B13 holds the camera's offset from Ed while the stick is pushed, B27 after), the fall's
    speed-ups (B17 raises the linear speed with the fall speed), code 14 and 17 (17: B31 puts the camera in front of Ed,
    angle pi, 0.15 up, 5 .. 6 away; who sends it is not identified), 13 (first person, E2).
- **Fixed on the way:** collision used to ease *every* lengthening of the distance (Ed rising in a hold, a preset change);
  now only what the level takes off it is eased (`tc_cam.pull`), the wanted distance is followed exactly.
- **Checks:** camcheck adds the idle return, the flight distance and elevation, the jump hold (height constant to 1e-4
  while Ed rises 2, still looking at him), its release (no jump that frame, then back to the follow). viewcheck passes
  again, the north demo included, but that is the seed's luck as before: 15 of 24 seeds pass (the same as before any
  camera change).
- **"Quite bad in others":** not known where. Candidates seen headlessly: tight interiors (bar: the camera nearer than
  2 units in ~10 % of a 60 s demo), where the next item would help (D2: swinging round to an open side instead of
  coming in close). Ask the user for the places.

### Third pass (2026-11-01): one camera, the original's orbit (the user: "option 2"; free view removed)
The user found free view janky (an implicit mode entered by any mouse twitch, left on a timer). Replaced by the
original's model, its numpad orbit mapped onto the mouse and stick (see "The player's camera keys in the original"):
- Mouse, right stick, arrow keys and **Numpad 4 / 6** turn the camera round Ed at once; up / down raise or lower it
  round him (`tc_cam.rise`, -0.8 .. +0.85 rad on the elevation; the original has no vertical control).
- **Ed standing: it stays** where the player put it. **Ed moving** (over 1 unit/s) **and no turning for 0.5 s: it eases
  back behind him** (0.45 s lag, about a second to settle) to within the original's tolerance, 0.15 rad (the record's
  +0x44; the port had used 0.7), and the height change eases back too. The original snaps back as soon as he moves (B0
  sets the orbit angle to 0 every tick); easing, and waiting while the player turns, is the port's.
- **Recentre** (X, middle mouse, L2): behind him and the default height, quickly (0.15 s lag).
- **Numpad 5: the original's close-up** (elevation 0.15, distance 4, stretch up to 2.5x; title ", close-up"). The
  original allows the toggle only while Ed stands; here always.
- Gone: `tc_cam.free`, the 3 s return, ", free camera", `tc_viewer_free_camera`. New: `tc_cam_look`,
  `tc_viewer_toggle_closeup`. Numpad 0 (the original's stick / first-person button) is still unbound.
- Checks: camcheck rewritten for it (placement, the orbit held while Ed stands, the 0.5 s grace, the ease back to the
  tolerance with the height, lazy within it, recentre exact, the close-up, the presets, collision); viewcheck (the stick
  turns it and it stays, recentre, close-up, look settings). All checks pass; the north demo seed passes (seed luck as
  before).

### Fourth pass (2026-11-01, after the playtest: "extremely jittery when Ed starts moving ... especially walking towards it")
Causes found (headless, `camcheck`'s new "no shaking" checks: Ed's AI walking as a player, the yaw per frame):
- **The ease back behind his facing fed on itself:** his controls are camera-relative, so walking at the camera meant
  "behind" was ~pi away, the short way round flipped side to side, and every turn of the camera changed his way. His
  facing also steps at the AI's 30 Hz. **Replaced by a rope** (the port's): the camera turns by his speed across its
  view over the follow's distance (smoothed 0.2 s, the first 0.5 units/s ignored), so it doesn't turn at all as he walks
  towards it (it backs off), comes round behind as he runs sideways or away, and has nothing to flip at pi.
- **A collision bug:** an older, larger wall pull-in on a distance that had since shortened (Ed walking at the camera)
  made the distance negative: the camera went through Ed for a frame and was re-placed behind him the next (a 2.3 rad
  jump in bar). The pull is now capped at what there is.
- **The front stretch** followed his 30 Hz facing in steps: now eased (0.5 s).
- Measured after (largest yaw step per frame / direction reversals): bar 0.005 / 0, north 0.018 / 1, sud0 and Land
  smaller (before: north 0.39 / 20, bar 2.3 / 39).
- **First person (0 keys, pad L3)**, the user's "free camera through pressing 0": at Ed's head (1.9 up), looking where
  the mouse / stick turn it; Ed is hidden and stands (his movement inputs held back; Action and Shoot pass). The
  original's camera button (Numpad 0, DIK 0x52) enters Ed's stick mode (B28 / B29, camera code 13 -> camera B1, which
  hangs the camera on his channel 0x27); the port can't use that path: its gate, Ed's dsg25, is never set by his
  scripts (an ability given by something else: not traced). Aim and shooting from it: later.
- **Keys:** 5 and 0 on the number row and the numpad (close-up, first person), across both frontends; the lighting
  variants moved to Alt / Option + 1-9 and Alt / Option + 0.
