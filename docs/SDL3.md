# SDL3 frontend (Linux, and anything else SDL runs on)

Plan for a second platform layer next to the Swift one. The Swift/Metal app stays the primary macOS build;
the SDL3 frontend is a separate C program over the same TonicCore, so nothing in `core/` or `Sources/` changes
apart from the small shared-header step below.

## Current state (checked on Linux, gcc)
- `make -C core all` builds cleanly with `-Wall -Wextra -Wpedantic`; `mechcheck` passes. The other checks
  need converted game data but are plain C.
- `tools/ttre` and `tools/audio_inventory.py` are stdlib-only Python. External tools: `ffmpeg`, `vgmstream-cli`
  (both packaged on Linux).
- macOS-only: `Sources/TonicViewer` (~1270 lines: AppKit, Metal/MetalKit, AVFoundation, simd),
  `tools/make_app.sh` (`sips`, `iconutil`, `.app` bundle), and the `swift` prerequisite in `tools/build_all.sh`.

## Layout
```
sdl/
  Makefile          # builds core/ as a library, compiles shaders, links against SDL3
  src/main.c        # window, event loop, mouse capture, key bindings     (main.swift, Input.swift)
  src/camera.c      # free-fly / follow camera, projection                (Camera.swift)
  src/renderer.c    # SDL_GPU device, pipelines, meshes, textures, draw   (Renderer.swift)
  src/assets.c      # level/texture/lightmap loading via TonicCore + stb_image (Core.swift)
  src/music.c       # SDL audio stream + stb_vorbis                       (Music.swift)
  shaders/level.hlsl  # port of Sources/TonicViewer/Shaders.metal
  third_party/      # stb_image.h, stb_vorbis.c (single-file, vendored)
```
Dependencies: SDL3 (system package or vendored), SDL_shadercross (build-time only, to compile HLSL to SPIR-V).

## Milestones
Each milestone should leave something runnable.

### 1. Window and core
- SDL3 window, `SDL_GPUDevice` (Vulkan backend), clear to a colour, quit on close/Esc.
- Link `core/build/libtoniccore.a`; load a `.ttlv` given on the command line (`tonicviewer-sdl files/out/ttlv/bar.ttlv`)
  and print mesh/texture counts, the same way `core/tests/playcheck.c` does.
- Assets path: command-line argument first, then `./files/out`, so there's no bundle lookup like the macOS app's.

### 2. Shader port
Port `Shaders.metal` to `shaders/level.hlsl`, entry points `level_vertex`, `level_fragment_textured`,
`level_fragment_flat`. Compile at build time:
`shadercross level.hlsl -s HLSL -d SPIRV -t vertex -e level_vertex -o level_vertex.spv` (and so on per entry point).

| Metal | HLSL / SDL_GPU |
|---|---|
| `[[attribute(n)]]` vertex struct | `TEXCOORDn` semantics; layout in C via `SDL_GPUVertexAttribute`, offsets 0/12/24/32/36/40/44, stride as in `Renderer.swift` |
| `.uchar4Normalized` (colour sets) | `SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM` |
| `Uniforms [[buffer(1)]]` (vertex + fragment) | `cbuffer` at `register(b0, space1)` (vertex) / `register(b0, space3)` (fragment); `SDL_PushGPUVertexUniformData` / `SDL_PushGPUFragmentUniformData` slot 0 |
| `DrawParams [[buffer(2)]]` (vertex) | vertex uniform slot 1 (`b1, space1`) |
| `flags [[buffer(2)]]` (fragment) | fragment uniform slot 1 (`b1, space3`) |
| `texture(0)` + `sampler(0)`, `texture(1)` | `register(t0/s0, space2)`, `register(t1/s1, space2)`; bound as texture-sampler pairs |
| `constexpr sampler lightmapSampler` | a real `SDL_GPUSampler` (linear, clamp to edge) created in C, paired with the lightmap |
| `fract`, `mix`, `discard_fragment()` | `frac`, `lerp`, `discard` |
| `m * v` | `mul(m, v)` with matrices declared `column_major` (matches the existing column-major matrix data) |

Notes:
- Depth range is 0..1 in both Metal and SDL_GPU, so the projection matrix carries over unchanged.
- Keep both shaders in sync by hand (they're ~100 lines each). Put a header comment in each pointing to the other.
- Uniform layout: `Uniforms` already follows 16-byte packing. Pad `DrawParams` to a multiple of 16.

### 3. Shared uniform structs
Add `core/include/tonic/tc_gpu.h` with plain C `Uniforms`/`DrawParams` structs and `_Static_assert`s on size
and key offsets. The SDL frontend uses it directly; the Swift side may adopt it later through the TonicCore
module (optional, not required for this work).

### 4. Static level rendering
- Upload vertex/index buffers and textures (PNG from `files/out/Textures` via stb_image), lightmap atlas
  (`<level>_lightmap.png`).
- Three `SDL_GPUGraphicsPipeline`s, as in `Renderer.swift`: textured, flat, and textured alpha-blended with depth
  write off. Depth target `SDL_GPU_TEXTUREFORMAT_D32_FLOAT`.
- Same draw order: opaque first, transparent surfaces collected and drawn last.
- Free-fly camera with captured relative mouse (`SDL_SetWindowRelativeMouseMode`), WASD.

Parity check: screenshot `bar` (lightmaps + fog) and one radiosity-lit level in both builds from the same
camera position; they should match apart from filtering differences.

### 5. Gameplay and controls
- Play-as-Ed / free-fly toggle, Ed's AI toggle, persos, animation, billboards, UV scrolling: all driven by
  TonicCore, so this is mostly porting the per-frame logic in `Renderer.swift`/`main.swift`.
- Key bindings from `main.swift` (macOS keycodes) mapped to `SDL_Scancode`s: Esc, Tab, I, P, M, G, `[` `]`,
  `-` `=`, `,` `.`, digits. Keep the same keys so both builds have the same controls.
- Gamepad via `SDL_Gamepad` if Input.swift supports one.

### 6. Music
Ogg tracks from `files/out` (see `tools/music.tsv`) decoded with stb_vorbis into an `SDL_AudioStream`; mute
toggle on M.

### 7. Optional
- Level picker: a simple in-game list (or Dear ImGui) instead of `LevelPicker.swift`. Until then, a CLI argument.
- Intro movie: decode with libavcodec/libavformat, or skip it. Lowest priority.

## Tooling changes
- `tools/build_all.sh` stays macOS-only. Linux gets its own script (e.g. `tools/build_linux.sh`) that runs the
  same conversion steps (textures, levels, music, intro) into `files/out`, checks for `python3`, `ffmpeg`,
  `vgmstream-cli`, `shadercross` and `pkg-config --exists sdl3`, then builds `sdl/`.
- Linux filesystems are case-sensitive and the game install mixes case (`pharmmap`, `SpyCarota`, `Bnk_0.bnm`,
  `BNK_200.BNM`). If a level or bank fails to convert with "not found", check letter case first.
- `tools/make_app.sh` stays macOS-only. A Linux equivalent (if ever wanted) is just `make -C sdl` plus
  copying `files/out`. **Same rule as the app: never redistribute anything containing converted assets.**
- `docs/BUILD.md`: add a Linux section with the `apt`/`dnf` package names (`libsdl3-dev`, `ffmpeg`, `vgmstream`).

## Estimate
Milestones 1–4: ~800–1000 lines of C plus the shader. That's the minimum useful result, a Linux level viewer.
Milestones 5–6 bring it to feature parity with the Swift app except the intro and level picker.
