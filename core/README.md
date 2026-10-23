# TonicCore: the portable core

Plain C11 library with the platform-independent part of the port: the `.ttlv` loader (v1-v8),
world collision (uniform grid, raycast, closest point), Ed's play-mode controller, the
animation sampling helpers, the AI interpreter (`tc_ai.h`: runs Ed's own scripts from a
`.ttai`, see docs/NOTES.md "AI interpreter"), the exe's mechanics (`tc_mec.h`: ground/air with the
collide-and-move, flight, the skier's board; docs/NOTES.md "MEC physics" onward) and `tc_ed_ai.h`, which drives
Ed with them. Every platform (macOS now, PS Vita and 3DS later) links the same code.

## Layering rules
- **Core = no platform code.** Only the C standard library and `<math.h>`: no OS, file, GPU, audio
  or input headers. No VLAs, fixed-width types, little-endian reads byte by byte (no casts onto
  the buffer), every allocation through `tc_alloc`/`tc_free`.
- Must stay clean with `-std=c11 -Wall -Wextra -Wpedantic`.
- No FP contraction (`src/tc_internal.h`), except the cross product, which uses `fmaf` on purpose
  to match the original Swift code exactly. `tests/playcheck.c` must keep matching the reference output.
- Platform layers (Swift/AppKit/Metal in `Sources/TonicViewer`, a future Vita backend) own the
  window, rendering, audio, file I/O and input, and talk to the core only through `include/tonic/*.h`.

## What a platform backend provides
1. **File reading:** read the `.ttlv` into memory and call `tc_level_load(&level, bytes, size)`.
   Texture paths in `level.textures` are relative to the `.ttlv` folder; load the PNGs yourself.
2. **Allocator (optional):** `tc_set_allocator()` before loading, to use a console heap.
3. **Renderer:** the simplest route is `tc_viewer.h` (cameras, play mode, settings, title readout): upload its
   mesh table once (`tc_viewer_mesh`), then each frame `tc_viewer_update`, `tc_viewer_frame` (pack into your
   uniforms) and draw `tc_viewer_draws` (matrix, texture, flags; transparent draws last). Both viewers do this.
   Or draw from the loaded structs yourself. `tc_vertex` is a packed 44-byte vertex (pos, normal,
   uv, 3 rgba8 baked-light sets) that can go to a vertex buffer as is. Persos: `tc_sample_at_time`
   + `tc_channel_matrix`; Ed: `tc_phase_advance` + `tc_sample_at_phase` + `tc_player_model_matrix`.
   Matrices are column-major `tc_mat4` (GL/Metal layout).
4. **Input mapping:** with `tc_viewer`, map keys to `tc_view_action`s (`tc_viewer_set_action`) and pass mouse
   deltas; it drives Ed. Without it: each frame, turn pad/keys into a `tc_player_input` (camera-relative world XY
   move of length <= 1, run, jump held) and call `tc_player_step(&player, dt, &input)` on a
   `tc_player` created with `tc_player_init` over a `tc_collision` built from `level.collision`.
   With Ed's AI instead: load the level's `.ttai` with `tc_ai_model_load`, `tc_ed_ai_init` over that
   player, and each frame `tc_ed_ai_step(&ed, dt, buttons, stick_x, stick_y, camera_eye)` with
   `1 << TC_AI_IN_*` bits (the game's own inputs: directions, SpeedUp, Jump, Action, Shoot, ...);
   draw Ed's animation `"s<tc_ed_ai_anim>"` at frame `tc_ed_ai_frame` (`tc_ed_ai_loops`: wrap).

## Building
- SwiftPM (macOS): target `TonicCore`, test `swift run -c release CorePlayCheck files/out/ttlv` from the repo root.
- Any C compiler: `make` here builds `build/libtoniccore.a` and `build/playcheck` (`make check` runs
  it). Without make: `cc -std=c11 -O2 -Iinclude src/*.c tests/playcheck.c -lm -o playcheck`.
  `make aicheck` runs Ed's AI headless on bar (`python3 -m tools.ttre level-ttai bar files/out/ttlv/bar.ttai`
  first); `AI_TRACE=<frame>` prints the statements executed from that frame on; `AI_SCRIPT=wander` is a 60 s
  collision stress run (other options in the header of tests/aicheck.c). `make mechcheck` checks the ground/air
  mechanics (steps, walls, falling) on hand-made geometry, no game data needed; `make crashcheck` rides the Totalski
  skier into a tree (the scripted obstacle crash). `make viewcheck` runs `tc_viewer` headless on bar
  (scripted keys: AI, free-fly and drop, simple controller, settings; every frame's draw list validated).
  The test (not the library) uses POSIX `<dirent.h>` to list the level folder.

## Vita backend sketch (VitaSDK + vitaGL)
- Build: `make CC=arm-vita-eabi-gcc AR=arm-vita-eabi-ar lib`, then a VitaSDK CMake app linking
  `libtoniccore.a`, vitaGL, SceCtrl, and the usual `vita_create_self`/`vita_create_vpk`.
- Data: converted levels and textures copied to `ux0:data/TonicTrouble/` (e.g. `ttlv/`, textures
  beside them as the converter writes them). Read with `sceIoOpen`/`sceIoRead` (or stdio) into
  memory, then `tc_level_load`.
- Render: vitaGL (OpenGL ES 2-like). One VBO/IBO per `tc_mesh`, the Metal shader ported to GLSL
  (baked light sets, gamma, alpha test, UV scroll). The 960x544 screen sets the aspect; textures
  probably need a downscale or compressed format to fit the memory budget.
- Input: SceCtrl left stick -> `tc_player_input.move` (rotated by camera yaw), right stick ->
  orbit camera, Cross = jump, R = run. Music: decode the extracted tracks with SceAudio.
