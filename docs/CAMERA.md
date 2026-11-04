# The camera

How Tonic Trouble's camera works (read from the exe and the level data, 2026-11-01), how the port's camera worked
before, the original's weak points, and a list of improvements. **The user chose the suggested first set (A1, B1, B2,
C1, D1, E1), with recentring as the only way out of free view; it is implemented: see "6. Implemented" and the passes
after it. The camera today: "Sixth pass" near the end.**

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
| `+0x70` / `+0x74` | 25 / 7 | **read** (fifth pass): the mover's speeding-up divisor (a step <= the last + speed dt / +0x70) and its ease-in divisor (a step <= d / (+0x74 - d)); camera B0 sets 10 / 10 | PROC 166 |
| `+0x78` | 3.14 | copied to the proposal's `bank_step` (+0x340), times dt in the request; use not traced | PROC 167 |
| `+0x7C` / `+0x80` | 10 / 5 | **read**: +0x7C the look-at lag (the look-at point closes 1 / +0x7C of the way per frame, FUN_004b514b); +0x80 copied to the proposal (+0x378), use not traced. B0: 10 / 10 | PROC 168 |
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
- (Read in the fifth pass: the type 3 mover and look-at lag, the candidate search, the visibility rays.) Still open:
  the 0.35 factors (+0x5C..+0x64), the orientation angles' use (`FUN_004b533c` with +0x50..+0x58), the candidates'
  score `FUN_004a4660`.
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

### Fifth pass (2026-11-04): the original's motion model, clipping (the user: "a lot of jank is stemming from it")
Read this time (exe and data; the decompiles of the camera module and the type 3 mechanics, the code at 0x4a1080 that
the dump never decompiled, disassembled; the camera's sud0 scripts dumped with `ai.dump`):
- **`GAMEDATA/Camera.cst` is a development leftover:** the retail exe never reads it (no strings). Its constants are
  compiled in, set by `GAM_00425ee0` into 0x521320..0x52137a, and scripts can change them with **PROC 198**
  (SetCameraConstant(index, value)). Retail values (Camera.cst's dev values in brackets): sphere 0.45 (0.35, unused),
  can-go rays 0.35 / 0.35 x 3 (0.30, 2), can-see ray 0.1 / 0.1 x 1, base angles pi/4 and pi/8, target averaging
  window **0.4 s** (0.75), speed window 0.4, angle parsing step pi/8 (pi/4), find-better / force-position ticks 60 / 60,
  watch ticks 1 (camera B0 sets 8 with PROC_198(15, 8)), slope tilt 0.01 rad per frame.
- **The target is averaged:** `FUN_004a1aa0` follows `GAM_0042b730`, a time-weighted mean of the target's last 8
  positions (the dynamics' history, pushed by `GAM_DynamHistPosition_0042af40` each mechanics frame) within 0.4 s; at
  30 Hz that is all 8 (0.27 s). Flag 2 on +0x94 switches to `GAM_0042b89b` (the same plus a least-squares slope).
- **The speed** (`FUN_004a2a10`): 2 x the target's averaged speed (`GAM_0042b5b0`), within [+0x6C, +0x68] = [12, 200];
  x1.5 when the hidden counter passes 1/3 and again 2/3 of 60 ticks.
- **The mover** (`DNM_RequestType3_004b3810` -> `FUN_004b29fa`, `FUN_004b2e4b`): a step towards the wanted spot of at
  most speed x dt; near it (d < speed dt (P0 - d)) at most d / (P0 - d) per frame (an ease-in, P0 = +0x74); and at most
  the last frame's step + speed dt / +0x70 (gaining speed gradually). Flag 0x80 on +0x92 skips both; flag 2 moves on
  an arc round the pivot instead (not ported); `FUN_004b3034` keeps it 1.5 from the pivot horizontally (not ported).
- **The look-at point lags** (`FUN_004b514b`): 1 / +0x7C of the way per frame (B0: 10; riding a perso 2; first
  person 1).
- **The body** collides and slides along walls (`FUN_004b3d33`, up to 5 contacts); nothing pulls it in towards Ed.
- **The watchdog** (`FUN_004a56e0`, every 8 ticks in B0) and the too-close check (`FUN_004a5600`): when Ed can't be
  seen, or the camera is nearer than the second range's minimum, it builds candidates (`FUN_004a4f20`: the ideal offset
  turned by the angle table of `FUN_004a3760`, k pi / 4 all round; variants when the camera is above the pivot; one
  behind the target's velocity), sorts them (`FUN_004a4660`, not decoded), validates them (`FUN_004a4bb0`) and cuts
  (`FUN_004a5c60`).
- **What the camera sees is the drawn geometry:** its ray (`FUN_004a3a00` -> `FUN_00484210`) tests the visual objects
  (`GLIGLOU_004427d0`) of the camera's and the target's sectors, and the persos in them (`FUN_004a3920`).
- **CameraFlag (PROC 184)** masks: word 0 (+0x94) bits 1..7 = 1, 2, 4, 0x10, 0x200, 0x400, 0x800; word 1 (+0x92)
  1..12 = 1, 2, 4, 0x10, 0x40, 0x100, 0x80, 0x20, 0x200, 0x8000, 0x400, 0x800. B0 sets +0x94 0x1 and 0x400, clears
  0x2 / 0x4 / 0x10 / 0x200 / 0x800; on +0x92 resets 0x1 and 0x40 to the defaults and clears the rest (so: the mean
  average, no slope tilt, the linear mover with its limits, the look-at lag on).

Ported (core/src/tc_cam.c; `tc_cam.h` lists each number with its source):
- **A 30 Hz camera tick** (`TC_CAM_TICK`), drawn between its last two ticks: target history and average, pivot, speed,
  the wanted spot, the mover (ease-in and speeding-up with B0's 10 / 10), the look-at lag (10). Read from the exe.
- **The lazy follow** from the camera's own position with B2's distance band (7 -+ 0.3, stretched in front of Ed), the
  preset's elevation exactly (tolerance 0, as the exe), the horizontal window wide open (**port**: the exe's 0.15 rad
  swings it behind Ed; the user's orbit choice stands). The rope of the fourth pass is gone: a camera that lags behind
  Ed on its own position is what the rope imitated.
- **The player's orbit** (mouse, stick, keys, recentre, close-up, first person) as before, applied to the camera's last
  two ticks at once (**port**).
- **Collision, the port's way:** the rig (the original's camera) is not pulled in; the **eye** is, on the line from Ed
  to the rig, in at once and back out at the mover's pace, and checked again every frame from Ed as drawn. The view
  looks the way the rig looks, Ed's pivot kept within 0.2 rad of the centre (`TC_CAM_LOOK_OFF_MAX`, **port**): a
  pulled-in eye near Ed would otherwise swing with each of his 30 Hz steps. No more minimum distance: the old 0.5
  floor put the eye inside walls (cavedoc's swim along a wall: 305 frames) and inside Ed. **Ed is hidden** when the eye
  is nearer than 0.9 to his pivot (`TC_CAM_HIDE_DISTANCE`, **port**; the original's first person hides his channels).
- **The line of sight against the drawn persos** (`camera_sight` in tc_viewer.c, from last frame's draw list; not
  sprites, water or blended draws), as the exe's ray does; the level itself by its collision (its visual geometry is
  not tested: **port**). This catches the moving platforms whose collision the exporter bakes at frame 0 (Land's slab,
  cavedoc's cone, until moving collision comes: docs/ENGINE.md) and the characters with none.
- **The watchdog swings instead of cutting** (`search`, every 8 ticks): only when a wall has the eye nearer than 2
  (`TC_CAM_SWING_NEAR`, **port**: from the exe's 5.7 it turned Ed mid-run on every rock, his controls being
  camera-relative), the most open of the 8 turns of the wanted offset, 0.6 s lag, handing back to the follow within
  0.05 rad or once clear.
- Not ported: the angle window and its parsing step, slope tilt, the hidden-counter speed-ups, the arc mover, the
  keep-away, the body's slide, the candidates' score and validation, modes other than follow.

Checks (camcheck; all suites pass: viewcheck, aicheck, mechcheck, crashcheck, skicheck, portalcheck, sectorcheck,
castcheck):
- The synthetic checks were rewritten for the new model (what changed on purpose: the look-at lags, so "looking
  straight at Ed during a jump" became "Ed in view, within 0.4 rad"; recentre ends within 1e-3 rad; the distance
  settles in the band, not on 7 exactly). New: Ed bobbing 0.3 at 5 Hz moves the camera < 0.1; Ed moving 3 at once
  moves it < 0.5 per frame; the watchdog swings to an open side; a wall 0.3 behind Ed's head keeps the eye this side
  and hides him.
- **No shaking:** the follow's frames keep the old bound (0.03 rad per frame, <= 3 reversals). The watchdog's swing
  is deliberate and has its own bound (0.1 rad per frame, no reversal of over 0.002 rad on both sides): **changed
  in the test this pass**, said here so it isn't read as tuned to pass.
- **Clipping, against what is drawn** (new, `level_clip`: every drawn triangle against the eye and the line from Ed's
  head; a scripted walk with the follow camera, and the 60 s demo), before -> after:

| | near plane in level geometry (walk) | persos between Ed and the eye | Ed hidden (eye in him) |
|---|---|---|---|
| cavedoc | 305 -> 0 | demo 13 -> 1 | walk 318 frames |
| pyramide | 67 -> 0 | demo 92 -> 0 (a see-through beam: not a blocker) | walk 1121 (a small room he fell into) |
| Land | 0 | walk 40 -> 0 | 0 |
| bar | 0 | demo 6 -> 10 (an animated character, one frame late) | walk 471, demo 266 |
| sud0, north | 1 -> 0, 0 | 0 | 140, 132 |

  The old camera had the eye inside Ed in those frames instead (pyramide 505 frames touching his mesh).
- Not seen in the app (never run here): the feel of the lag (the look-at point trails Ed by ~2.7 at a run, held to
  0.2 rad), the swing, Ed vanishing in tight spots.

### After the fifth pass's playtest (2026-11-04): the classic camera is back as the default
The user: the faithful model "stays lazy way too long, while still being jerky, and is completely wrong on Totalski".
The fourth pass's camera is restored as the default (`tc_cam_classic` in core/src/tc_cam.c, unchanged except: its line
of sight also tests the drawn persos, and the 0.5 minimum distance is gone, so walls no longer put the eye inside them;
Ed is hidden when the eye is nearer than 0.5 to his pivot instead of being drawn round it). **9** (number row or
numpad, both apps) switched to the fifth pass's faithful model and back (removed in the sixth pass). Not
looked into yet: why the faithful model is wrong on Totalski (the skier's averaged target and speed cap at ski speeds
are the first suspects). In camcheck's harsh scripted walks (Ed pushed into walls, camera behind him) the classic eye
is within 0.5 of him in many frames (bar 1,114, pyramide 1,160, cavedoc 770): there the old floor had it inside Ed or
the wall; now he is hidden. If that vanishing reads badly in play, the floor can come back (TC_CAM_CLASSIC_MIN).

### Sixth pass (2026-11-04): one camera, merged (the user: "take elements from both")
The 9 toggle is gone; `tc_cam` is one model again (core/src/tc_cam.c, the numbers and their sources in `tc_cam.h`).
**Measured first, then built** (a scratch probe: camcheck's scripted walk with Ed's AI on bar, sud0, north, Land,
cavedoc, pyramide, LeCanyon, and random steering on Totalski / skymap, per frame: the eye's acceleration, the view's
angular acceleration, Ed's jitter on screen, the distance while running, frames Ed is hidden). What it showed:
- **The faithful model** trailed: 9-10 units behind a running Ed instead of 7, **14-16 on Totalski / skymap** (the
  averaged target ~0.13 s behind, the mover's ease-in, the look-at lag: the "lazy" and "completely wrong on Totalski").
  Its turning was 3-10x rougher at the 99th percentile (the look-at clamp and the mover's regimes switching), and
  its averaged target did *not* steady Ed on screen (his on-screen jitter was worse than the classic's): Ed as drawn is
  already blended between his AI's ticks (`pose_draw`), so a 30 Hz camera only added lag.
- **The classic model** was responsive and smooth in the open, but: **snaps of ~0.4 rad in one frame when Ed jumped
  near a wall** (on 6 of 7 levels: the jump's height hold took the *pulled-in eye's* height, level with his head, so the
  view swung from 0.45 rad down to level); Ed hidden far more in tight rooms (bar 1114 of 3440 walk frames); the
  world bobbing with his height (steps, bumps); and, found on the way, two jolts: the front stretch and the hold's
  release both started at full speed (the release: from standing still to Ed's fall speed plus the catch-up, ~13 u/s
  in one frame; the old fall test dropped him 1 unit at once while still held, which hid it).

The merged camera (**port** unless marked):
- **The classic's base:** per frame round Ed as drawn, the player's orbit, the rope, the presets eased (the original's
  data), recentre, close-up, first person, the drawn-perso sight, no minimum distance.
- **Rig vs eye (the fifth pass's split):** the rig is where the camera would be with no walls; walls only pull the eye
  in along the line to it. The jump's hold takes the rig's height: the snaps are gone.
- **The watchdog's swing (the original's watchdog, FUN_004a56e0, every 8 ticks; the original cuts):** when a wall
  has the eye nearer than 2 and the player isn't turning, it looks round Ed in pi / 4 steps for an open side and turns
  there. Now critically damped (gathers speed and slows down, ~1.4 s), starting from the camera's current turning
  speed, its speed dying out after it ends, and 1 s of rest before the next (it hunted in pyramide's small room).
- **The camera's height** goes through a second-order filter (two 0.08 s lags combined as 2 h1 - h2): **no lag on a
  steady climb or fall** (slopes, lifts, the slides), steps and bumps rounded off (a 0.5 step: 0.17 per frame at most,
  0.055 overshoot). The camera orbits and aims at Ed's head with that height, so the world doesn't bob; Ed moves a
  little on screen instead. A teleport of over 5 (a respawn) places it afresh behind him.
- **No jolts:** the front stretch eases in two stages (0.25 s each); the hold lets go as a critically damped spring
  from standing still; the rope fades in over 0.25 s whenever it takes over again (after the player's grace, a
  recentre or a swing: on Totalski it came back at -4.4 rad/s in one frame after a long swing).
- Ed is hidden when the eye is nearer than 0.9 to his head (the classic: 0.5, which let the near plane cut his mesh).
- Not taken from the faithful model: the 30 Hz tick, the averaged target, the speed-capped mover, the look-at lag.

Measured after (the probe; the classic -> merged; p99 = 99th percentile over the walk):

| | view turning p99 / max (rad/s²) | eye acceleration p99 (u/s²) | distance running (7 wanted) | Ed hidden (walk frames of 3440) |
|---|---|---|---|---|
| bar | 11 / 1544 -> 16 / 121 | 1366 -> 604 | 4.0 -> 6.7 | 1114 -> 249 |
| sud0 | 26 / 1706 -> 10 / 118 | 564 -> 545 | 7.2 -> 7.2 | 202 -> 88 |
| north | 10 / 1542 -> 14 / 109 | 421 -> 474 | 6.8 -> 7.1 | 206 -> 71 |
| Land | 8 / 126 -> 6 / 118 | 1345 -> 412 | 6.6 -> 7.2 | 197 -> 6 |
| cavedoc | 26 / 1709 -> 27 / 78 | 558 -> 552 | 6.2 -> 7.1 | 748 -> 289 |
| pyramide | 10 / 1546 -> 22 / 85 | 526 -> 553 | 6.2 -> 6.0 | 1160 -> 970 |
| LeCanyon | 9 / 1532 -> 7 / 116 | 439 -> 412 | 6.2 -> 6.2 | 234 -> 99 |
| Totalski | 6 / 19 -> 12 / 28 | 865 -> 737 | 7.4 -> 7.4 | 42 -> 60 |
| skymap | 5 / 13 -> 9 / 34 | 390 -> 269 | 8.2 -> 8.2 | 13 -> 16 |

(The faithful model for comparison: distance running 7.6-9.9, Totalski 14.3, skymap 15.9; turning p99 14-68.)
The higher turning p99 in bar / pyramide / the slides is the swings (deliberate turns, smooth now); without them the
classic's numbers hold. Cavedoc's remaining bumps (~30 rad/s², every 1/3 s) are Ed's swim strokes, followed exactly.

camcheck (all pass; what changed in the test, so it isn't read as tuned to pass):
- The synthetic part ran the faithful model (`c.faithful = true`); now the one camera. Ed's synthetic position moves
  every frame (the viewer blends his ticks), his facing still steps at 30 Hz.
- "Walking at the camera: no frame's move over 1.5 x Ed's speed" (the fifth pass's mover) became "the front stretch
  starts smoothly" (the change of the camera's own speed per frame < 0.02, from the two 0.25 s stages: 56 u/s² at
  most; one 0.5 s stage started at 7 u/s at once). The stretch has to back off faster than he walks to open the
  distance in front of him.
- "Ed bobbing 0.3 at 5 Hz moves the camera < 0.1" and "Ed moved 3 at once: no jump" (the averaged target's) became
  "a step up 0.5 rounded off (< 0.2 per frame, overshoot < 0.08)", "a steady 3 u/s climb followed with no lag", and
  "a teleport of 41 places it afresh". The merged camera follows Ed horizontally exactly, as the classic did.
- The fall now continues at 8 u/s until the hold lets go (it dropped him 1 at once, which his drawn origin never does).
- New: a jump with a wall 1.6 behind him: largest pitch step 0.013 rad (the classic snapped ~0.4).
- The jitter check judges each frame by the state it started in (the watchdog sets its flag at the end of a frame) and
  counts swing reversals within one swing (it counted across two swings, and the rope's last frame before a swing).
- Clipping (the walks / demos): Ed's mesh cut by the near plane: bar 14 -> 3, north 76 -> 0, pyramide 4 -> 5;
  persos between Ed and the eye: Land walk 3 -> 0, bar demo 4 -> 2. Worse: **the bar demo hides Ed in 286 frames
  (102 before**: the 0.9 hide distance; the demo uses its own direction, without the watchdog), and **one Land walk
  frame has the near plane 0.036 above a floor** (not looked into).
- All other suites pass (check, aicheck, mechcheck, crashcheck, skicheck, viewcheck, portalcheck, sectorcheck,
  castcheck), `swift build`, the SDL frontend compiles (syntax check). docs/controls.png re-rendered (9 is free).

Not seen in the app (never run here): the feel of all of it; in particular the swing (does turning round Ed on its own
in tight rooms feel helpful, or does it turn his camera-relative controls under the player), and Ed moving slightly on
screen on steps now that the world doesn't.

### Seventh pass (2026-11-04): the sliding stages' camera, from the exe (the user: "look at the sliding camera next")
**Read** (the camera's scripts dumped on Totalski, skymap and sud0: identical but for button addresses; the skier's and
the level persos' scripts; the decompiles):
- Totalski and skymap use the same StdCam. **What differs is the shot the level asks for: the camera's dsg39**, which
  its reflex 2 dispatches: 0 B2 (the walking follow), 1 B4, 2 B5, 4 B22, **5 B6, 6 B8**, 7 B10, 8 B20.
  - **5 (B6 -> B23, the ski follow)** is set by the skier's B1 / B11 / B17 / B22, Totalski's start trigger
    (YBA_GenD_DepartMap: once the skier is in his riding behaviour) and skymap's JET_InitSkymap.
  - **6 (B8 -> B7, a fixed place looking at the target: mode 3, PROC 183 / 192 / 194, the place in dsg92 / dsg93)**:
    the finish and retry triggers (SYM_Gen_Arrivee, NBD_Arrivee_Et_Retry), the skier's crash and respawn (B10, B17,
    B22), skymap's JET_InitSkymap (first) and its arrival cut (EBK_Cam_Arrivee, which also sets the speed and later 255).
  - Ed's own scripts write dsg39 once (0) and never read it.
- **Camera B6:** mode 0 (follow); look offset and +0x24 = 0, so it aims at the target's origin (B2's +0x24 is
  (0, 0, 1.35): +0x24 is added to the target point itself, FUN_004a1aa0); angle 0 within 0.05 rad of behind his facing,
  elevation 0.2 within 0.1 (measured from the world horizontal: FUN_004b6909 projects on the plane under world up);
  distance 2 .. 2.7, second range 1.9 .. 7; SetCameraLinearSpeed(110, 130) (FUN_004a2a10 raises 2 x his speed to +0x6C,
  130, then caps it at +0x68, 110: always 110 u/s); SetCameraPair70(1, 15) (record +0x70 = 1, +0x74 = 15: PROC 166);
  SetCameraPair7C(2, 2) (the look-at lag 2); dsg8 = 1 (the numpad locked). CameraFlag word 0: mask 1 off, **2 on
  (the target is GAM_0042b89b: a least-squares line through the last 8 positions, read at the newest; constants 0.5,
  1/3; no lag at a steady speed)**, **4 on (FUN_004a1f30: an angle outside its window steps back at most DAT_0052134c =
  pi / 8 a tick, instead of snapping to the edge)**, 0x10 / 0x200 / 0x800 off, 0x400 default; word 1: 0x800 on (not
  found tested anywhere: unknown), 0x1 / 0x40 default (the record's default flags: +0x92 0, +0x94 8), the rest off.
  CameraFlag's op -1 = the default record's bit (PROC 184 reads record +0).
- **Not resolved: which of +0x70 / +0x74 is the mover's ease-in and which its speeding-up limit.** FUN_004b2e4b reads
  the ease-in from the proposal's +0 and the speeding-up divisor from its +0x30 (or the card's +0xC with word-1 flag
  0x40); the camera's values reach the proposal through untyped copies not followed (the record is copied each frame
  to the working record at 0x521380, GAM_cineinfo_004271b0; nothing reads its +0x70 / +0x74 by name). The fifth pass
  took +0x70 = speeding-up, +0x74 = ease-in: for B6 that is ease-in 15, no speeding-up limit, a camera trailing
  2.7 + 15 s / (1 + s) behind (s his move per tick): measured ~11-12 at the port's 60-90 u/s, looking down up to 1.05
  rad on descents. The other way round (ease-in 1 = none, speeding-up 15) is a tight chase at 2 .. 2.7 gaining speed
  at 220 u/s^2. **The port takes the second**: it fits B6's other settings (the forced speed 110, just above the
  skier's top speed, does nothing with an ease-in of 15; the second range ends at 7). All other behaviours set equal
  pairs (10 / 10, 1 / 1, 2 / 2, 20 / 20). One swap of TC_CAM_SKI_EASE / TC_CAM_SKI_ACCEL tries the other.

**Ported, then dropped after the playtest** (the user: "pure garboleum: way too jittery, jumping everywhere, moving too
fast with Ed, zooming too close"; see "Eighth pass" for what replaced it; this was `ski_rig` in core/src/tc_cam.c):
- The shot: `tc_viewer_camera_shot` (the camera's dsg39), passed as `tc_cam_target.shot`. 5 -> the ski follow; 6 keeps
  the ski follow going (B8's fixed places are **not ported**); anything else -> the walking camera.
- **The start trigger's write stands in the viewer** (port): YBA_GenD_DepartMap is not in the cast (the converter
  reduces it to the port's ski start, PROC 6), so once the skier rides (his card type 2) on a level where he is the
  player (the cast's main alias), the shot is 5 unless the scripts set 5 or 6. And the cast may now write the camera's
  dsg39 (`TC_AI_CAMERA_SHOT_DSG`, the one exception to read_only_for_cast; Ed's scripts never read it).
- From the exe: the follow from the body's own direction, the angles into their windows (yaw within 0.05 of behind his
  facing, elevation 0.1 .. 0.3 + the player's rise; pi / 8 a tick at most outside), the distance into 2 .. 2.7 (times
  the wheel's zoom), aimed at his origin; the type 3 mover at 110 u/s with the two limits (per tick in the exe, as
  rates here); the fitted target (a least-squares line over 0.27 s of frames, read at now).
- **The port's, beyond the exe:**
  - his facing from his drawn pose (blended between his AI's ticks, as his position already was) through a lag-free
    0.05 s filter: the camera held to the tick's facing stepped with him every other frame (0.1-0.19 rad when steering);
    only on the slides (the walking camera keeps the tick's facing, unchanged);
  - the body collides and slides (up to 3 contacts: the exe's FUN_004b3d33 idea; the sphere sweep): it rides on the snow
    rising behind him and along the banks;
  - the line of sight is the exe's thin can-see ray (r 0.1) from 0.45 out of his origin (the body's sphere from his
    origin, 0.63 above the board, scraped the snow and the banks he rides against: he was hidden 33-51 % of a ride);
  - blocked from him, it rises (0.1 s ease) to the lowest elevation that sees him; hidden 0.5 s, it cuts to the spot
    its follow wants (the original's watchdog, every tick on skis, cuts);
  - the player's turn moves it at once and the window takes it back after 0.5 s (B6 locks the numpad); no watchdog
    swing; leaving it, the walking follow starts from where it is.

Measured (scratch probes; the walking camera's numbers on every walking level unchanged from the sixth pass):
- Totalski, a ride with gentle steering: 2.60 from his origin on average, hidden 1.6 % (spans under 0.5 s); skymap
  2.51, 3.8 %. Riding without input (he drifts into the banks): 4.9 % / 7.7 %.
- Turning, the probe's hard steering (left / right every 1.5 s): p50 2 rad/s^2, **p99 ~340 (Totalski) / ~280 (skymap)**
  against the walking camera's ~12 there: this camera turns with his facing within 0.05 rad at 2.7 units, as B6
  does. The largest single-frame jerks come with the eye pulled in against a bank during crashes, where the exe's pi / 8
  a tick catch-up turns it hard.
- camcheck (new, "the ski camera"): down a 27 degree slope at 30 u/s it settles 2.67 behind, on the snow, never
  pulled in; a 1 rad turn in 0.5 s (his facing in 30 Hz steps): largest yaw step 0.0485 rad a frame (the bound 0.05,
  set below stepping with him: 0.067), back in the window; leaving it moves the eye 0.4 in the first frame (bound 1).
- Not seen in the app: all of the feel. In particular: is 2.7 behind and ~1 above the snow what the original looked like
  (the reading question above), the turning with his steering, and the rise over banks.

### Eighth pass (2026-11-04): the slides on the approved camera, with a ski preset
The B6 port is gone (the user's playtest above; its own probe had shown turning 25x rougher than the sixth pass, which
I reported as "inherent in B6" instead of stopping). Kept from the seventh pass: the reading (above), the shot plumbing
(`tc_viewer_camera_shot`, `tc_cam_target.shot`), the start trigger's stand-in, the cast's dsg39 exception, and his
facing from the drawn pose on the slides. The slides now run the sixth pass's camera with a preset (**all the port's**,
not the original's B6; `TC_CAM_SKI_*` in tc_cam.h), chosen when the shot is 5 and the close-up (5) is off:
- 7 behind plus 0.04 per u/s of his speed, at most +3 (9 at 50 u/s), eased as the presets are; 0.35 rad up; no front
  stretch (it zoomed out whenever he turned sharply);
- the rope with his velocity smoothed over 0.45 s (0.2 walking) and 3 u/s of sideways speed ignored (0.5): his steering
  wobble doesn't swing the view, a real turn brings it round;
- the height filter at 0.1 s (0.08), still with no lag on a steady descent;
- no watchdog swing (tried: at his speed it turned the view three times as much, p99 13.5 against 4.0, and hid him no
  less, 72 against 66 frames).

Measured (the probe's hard steering; the sixth pass on the slides -> this): Totalski turning p99 11.7 -> 4.0, max 28 ->
18, hidden 60 -> 66 frames of 3440; skymap 9.5 -> 2.9, 34 -> 17, 16 -> 21. The walking levels' numbers are unchanged.
camcheck "the slides' preset": 9.00 behind at 50 u/s at -0.35; weaving -+ 0.2 rad at 1.5 Hz turns the view not at all
(the smoothed sideways speed, ~2.3 u/s, stays under the dead zone); a 1 rad turn is behind his new way within 0.06
after 4 s; the close-up still wins. Not seen in the app: the feel, in particular whether 7-9 back and 0.35 up suit the
slides, and how quickly it comes round in sharp turns.

### The close-up kept (2026-11-04, the user: "persist the camera height level set by 5")
The close-up (5) was already kept through jumps, swimming, flights (the flight and dying presets take over meanwhile,
then it is back) and in-level teleports and respawns. It was lost by `tc_cam_reset` (entering play with Tab, a new
level's start, the demo) and by a portal to another level (a new viewer). Now `tc_cam_reset` keeps it, and the portal
carry takes it along (`tc_viewer_carry.closeup`, beside the zoom and the field of view). Not carried: a level opened
from the level select (the zoom isn't either). Checks: camcheck (survives a reset; still on after a flight and a
teleport), viewcheck (bar keeps sud0's close-up through the portal).

### Next (user, 2026-11-04)
1. ~~One camera~~ (the sixth pass above), awaiting the user's playtest.
2. ~~The sliding stages~~: the B6 port (seventh pass) dropped; the slides' preset on the approved camera (eighth pass),
   awaiting the user's playtest. Open: B8's fixed places (finish, retry, respawn, skymap's transition and arrival).
3. Candidates seen on the way (not done): hold Ed's control frame while the stick stays pushed, so the rope and the
   swing don't curve his path (his controls are camera-relative: Ed's scripts read the camera's angle, FUNC 2); raise
   the eye over his head when a wall pulls it in close, instead of hiding him.
