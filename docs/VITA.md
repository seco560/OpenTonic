# PS Vita port (plan)

Status (2026-10-25): step 1 done (core sticks, SDL and Mac gamepads; the user tests on Linux and macOS). The rest
is a plan; VitaSDK is not installed on the Linux box.

## Summary
Feasible, and the core already suits it: plain C11, no platform headers, no file I/O, allocations through
`tc_alloc`, and every frontend concern (cameras, play mode, draw list, portals, sounds) behind `tc_viewer`. The work is
(1) analog input in the core plus a gamepad mapping (testable on Linux first, with any controller), (2) cutting the
per-frame draw count, which is the main performance risk, (3) making the SDL frontend's app code reusable over a second
renderer, (4) the vitaGL renderer and the Vita build, (5) the intro through the Vita's hardware video decoder,
(6) packaging. Steps 1-3 also improve the desktop builds. I can compile and package the Vita build here once VitaSDK is
installed, but not run it: every on-device result comes from the user's testing, as with the Linux build.

## The hardware against the data (measured 2026-10-25, headless, `files/out` of that day)
Vita: 4x Cortex-A9 at 333 MHz (444 MHz available to homebrew), PowerVR SGX543MP4+, 512 MB RAM + 128 MB video RAM,
960x544 screen, two analog sticks, front and rear touch.

| level | .ttlv | triangles | meshes | textures | mesh data | draws/frame (max) | core, J4125 |
|---|---|---|---|---|---|---|---|
| LeCanyon | 12.1 MB | 38,136 | 2,847 | 182 | 2.4 MB | 1,456 | 0.13 ms |
| sud0 | 10.6 MB | 27,931 | 1,824 | 353 | 2.4 MB | 1,258 | 0.12 ms |
| pyramide | 10.0 MB | 26,644 | 1,678 | 289 | 2.3 MB | 1,144 | 0.13 ms |
| Totalski | 5.9 MB | 7,053 | 866 | 179 | 1.0 MB | 588 | 0.06 ms |
| bar | 3.6 MB | 1,879 | 285 | 81 | 0.5 MB | 65 | 0.05 ms |

- **Geometry and memory: fine.** At most ~38k triangles; textures are small (6,910 PNGs: 3,336 at 32 px, 1,336 at
  128 px, 10 at 512 px), lightmaps 1024x1024. A level is a few tens of MB resident; music streams from the card.
- **Core CPU: fine.** 0.13 ms per update on the Celeron, so even ~10x slower is ~1.5 ms of a 33 ms frame.
- **Draw calls: the risk.** There is no culling: every static mesh is drawn every frame, up to ~1,450 draws. vitaGL
  costs CPU per draw call (state, uniforms), and at that count it could take a large share of a 30 fps frame. Step 2
  addresses this before any Vita code exists.
- **Load time.** Reading a 12 MB .ttlv and ~350 PNGs from the memory card, plus the alpha bleed per cutout texture
  (1.6 ms each on the Celeron), will take seconds. The loading screen covers portal changes; moving the bleed into the
  converter (already suggested in VIEWER_CORE.md) removes the largest share for every build.

## Steps

### 1. Analog input in the core, gamepad in the desktop SDL build (testable on Linux)
The game already reads a stick: Ed's scripts use pad entries (stick angle / magnitude, axis ranges, pad buttons;
NOTES "Inputs"), and `tc_ed_ai_step` takes `stick_x, stick_y`, but `tc_viewer` passes 0, 0 (the keyboard path). With a
real stick the original's own behaviour applies: the magnitude chooses walk or run (no SpeedUp needed), and the
scripts make the stick camera-relative themselves.
- Core: `tc_viewer_set_stick(v, x, y)` (left stick, -1..1, y forward) into the AI, the simple controller and free-fly;
  `tc_viewer_set_look(v, x, y)` (right stick) as a **turn rate** (deflection x `turn_speed`), not mouse deltas, so the
  camera turns smoothly while the stick is held. Dead zones in the frontend. Keyboard play stays as it is.
- Checks: scripted stick runs in `aicheck` / `viewcheck` (half deflection walks, full runs, direction follows the
  camera), since this path has never run.
- SDL frontend: `SDL_Gamepad` (hot-plug, a button table next to `input.c`'s scancodes), D-pad / Cross / Circle in the
  level select. The same code runs on the Vita, where SDL3 exposes the built-in controls as a gamepad.
- Proposed mapping (one table, easy to change; the original's pad numbering: Jump 10, Action 11, Shoot 12, Lateral 13,
  Camera 14, SpeedUp 16):

  | input | play | free-fly | menus |
  |---|---|---|---|
  | left stick | move Ed (walk / run by deflection) | fly | move selection |
  | right stick | camera (turn rate) | look | scroll |
  | Cross | Jump | | open / confirm |
  | Square | Action (stick attack) | | |
  | Circle | Shoot | | back |
  | Triangle | | | intro movie (level select) |
  | R | Lateral (side-step) | up | |
  | L | (Camera, the original's NUM0, once the camera is decoded) | down | |
  | R2 (desktop pads) | | fly faster | |
  | D-pad up / down | camera distance | fly speed | move selection |
  | R3 (desktop pads) | free-fly | play as Ed | |
  | Start | pause menu: level select, controls, settings (AI on/off, free-fly, fog, FOV, gamma, AA) | | open |
  | Select | controls overlay | | |

  As built for the desktop (step 1): Start opens the level select directly (the pause menu comes with step 4), R3
  toggles free-fly, Triangle plays the intro from the level select.

  The keyboard-only viewer settings (`[ ]`, `- =`, `, .`, G, I, Tab, 1-9) move into that pause menu on the Vita;
  the desktop builds keep their keys.

### 2. Fewer draws (core; all frontends benefit)
- **Static batching** at `tc_viewer_init`: merge static meshes that share texture, flags and UV scroll into one mesh
  (animated textures and water meshes stay separate). The mesh table keeps its contract; it just gets shorter.
- **Frustum culling** in `tc_viewer_draws`: per-mesh bounds against the frame's view-projection.
- Measure with `viewcheck` (draws per frame, level by level). Target: a few hundred at most in the largest levels.
- Alpha bleed into `tools/ttre` (converter), removing it from both frontends' texture loading.

### 3. Make `sdl/src` reusable over a second renderer
Most of the SDL frontend is plain SDL3, which the Vita port has (audio, gamepad, filesystem, I/O): screens and keys
(`main.c`), the level select, music, sound effects, the loading screen, portal level changes, file reading. What
isn't: `gpu.c` (SDL_GPU, which has no Vita backend), the drawing half of `view.c`, and window handling (vitaGL owns
the display on the Vita, SDL runs without its video subsystem there).
- Split `main.c` into the app (screens, input -> viewer, loading screen, portals, title/status text) and a small
  platform shell (window, events, frame loop).
- Make `gpu.h` a backend interface without SDL_GPU types (opaque textures, uploads, frame, 2D quads, overlay, debug
  text, the level draw); `gpu_sdlgpu.c` keeps today's implementation. The debug-font text keeps working everywhere:
  it uses SDL's software renderer, no video needed.
- The desktop build must behave exactly as now (the user re-tests on Linux before step 4 builds on it).

### 4. `vita/`: the vitaGL renderer and the build
Named after the platform (vitaGL is the library it uses); rename to `vitaGL/` if preferred.
```
vita/
  CMakeLists.txt        # VitaSDK toolchain; compiles core/src, the shared sdl/src files and vita/src
  src/gpu_vitagl.c      # the gpu.h backend over vitaGL
  src/platform_vita.c   # vglInit, frame loop, ARM clock 444 MHz, data path, controls
  src/intro_vita.c      # step 5
  shaders/              # level / ui / overlay shaders for vitaGL
  livearea/             # sce_sys layout (images generated at build time, see step 6)
```
- **Toolchain:** VitaSDK (vdpm), vitaGL, SDL3 built from the pinned `release-3.4.8` with VitaSDK's CMake toolchain
  (SDL_VIDEO off; audio, gamepad, filesystem on).
- **Shaders:** vitaGL takes GLSL through its GLSL -> CG translator and compiles at run time with the Vita's own shader
  compiler, `libshacccg.suprx`. It is not redistributable: the user extracts it once on the Vita (the usual
  requirement for vitaGL ports; tools such as ShaRKBR33D do it). vitaGL can cache the compiled shaders after the first
  launch. Port `level.vert/.frag`, `ui`, `overlay` from GLSL 450: no `std140` blocks or `uint` bit tests (flags become
  separate uniforms), plain attributes and varyings. To check on hardware: `fwidth` for the cutout sharpening and
  alpha to coverage under MSAA (vitaGL can set up 4x MSAA at init); fallback is the plain alpha test.
- **Pipelines -> GL state:** the four level pipelines become state sets (blend on/off, depth write on/off); same draw
  order (opaque, then transparent without depth writes, then the overlay and 2D).
- **Data:** `ux0:data/OpenTonic/` with the `files/out` layout (ttlv, Textures, Vignette/Level, audio, video), copied by
  the user (VitaShell over FTP or USB). Converted assets never go into the VPK.
- **Memory:** vitaGL's pools sized at init (`vglInitExtended`); one level resident at a time, as on desktop.

### 5. Intro movie
`pl_mpeg` decodes 800x480 in ~6 ms per frame on the Celeron; the Cortex-A9 would miss the 25 fps. The Vita has a
hardware H.264 decoder (`SceAvPlayer`): `tools/make_intro.sh vita` makes an H.264 960x544 MP4 (same deblock / upscale
chain), and `intro_vita.c` plays it, textures from the decoder's frames, audio through SDL. Skip as on desktop; after
it, Totalski.

### 6. Packaging
`tools/build_vita.sh`: build, `vita-mksfoex` / `vita-pack-vpk` into `files/out/OpenTonic.vpk` (title ID like
`OPNT00001`), optionally upload to VitaShell's FTP server. The LiveArea images (icon0 128x128, bg, startup) come from
the user's own converted icon and vignettes at build time, so like the .app the VPK stays on the user's machine.
Optional: music to Ogg Vorbis (`stb_vorbis` in both SDL builds) to shrink the 344 MB of WAV on the memory card.

### 7. On-device tuning (with the user)
A debug readout (fps, draws, ms in update / draw, free memory) in the pause menu; pool sizes, shader cache, frame
pacing (30 fps target with vsync, 60 where the level allows). Vita3K (the emulator) may help for quick checks, but real
hardware is the reference for performance and shaders.

## Effort
| step | size | testable on |
|---|---|---|
| 1. analog input + desktop gamepad | small (about a session) | Linux, any controller |
| 2. batching + culling (+ bleed in the converter) | medium | Linux (viewcheck numbers, visual parity) |
| 3. reusable `sdl/src` | medium; regression risk on desktop | Linux |
| 4. `vita/` renderer + build | the largest; first boot likely takes a few rounds | Vita |
| 5. intro via SceAvPlayer | small to medium | Vita |
| 6. packaging | small | Vita |
| 7. tuning | open-ended | Vita |

In all, about the size of the original SDL3 port plus a core refactor: a few sessions to a first boot, then
iterations paced by on-device testing.

## Needed from the user
- VitaSDK on the Linux box (`/usr/local/vitasdk`, `VITASDK` in the environment; vdpm installs vitaGL, vitaShaRK and
  friends), so builds and packaging can be checked here.
- On the Vita: HENkaku / Enso, VitaShell, `libshacccg.suprx` extracted.
- A controller on Linux for step 1 (any SDL-supported pad; a DualShock 4 / DualSense lays out like the Vita).
