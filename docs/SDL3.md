# SDL3 frontend (Linux, and anything else SDL runs on)

A second platform layer next to the Swift one, in `sdl/`. The Swift/Metal app stays the primary macOS build; the SDL3
frontend is a separate C11 program over the same TonicCore. Nothing in `core/` or `Sources/` changed for it.

## Building
Linux packages (Ubuntu 24.04 / Mint 22 names; SDL3 itself isn't packaged there, so the Makefile builds it):
```sh
sudo apt install build-essential cmake git pkg-config glslang-tools \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxkbcommon-dev libwayland-dev libdecor-0-dev \
  libvulkan-dev libpipewire-0.3-dev libpulse-dev libasound2-dev libudev-dev libdbus-1-dev
make -C sdl            # -> sdl/build/opentonic
```
- SDL3: a system SDL3 >= 3.4 when `pkg-config` finds one; otherwise `make` clones the pinned `release-3.4.8` into
  `sdl/build/SDL` and builds it statically into `sdl/build/sdl3` (git-ignored, done once). SDL picks its video/audio
  backends from the headers present when it is configured: after installing more `-dev` packages, `make -C sdl
  distclean` and rebuild. Its configure summary is at the end of the cmake output.
- Shaders: GLSL in `sdl/shaders/`, compiled by `glslangValidator` to SPIR-V and embedded in the binary as C arrays
  (`sdl/build/shaders/*.h`), so the binary needs no files next to it.
- The core is built through its own Makefile (`make -C core lib`).

## Converting the game files
`tools/build_linux.sh` is the Linux counterpart of `tools/build_all.sh` (docs/BUILD.md steps 3-6, then `make -C sdl`;
`--no-build` converts only, `--check` adds the core checks, `--force` redoes everything). It needs `python3`; music
needs `vgmstream-cli`, which apt doesn't package: put the official prebuilt `vgmstream-cli` from
https://github.com/vgmstream/vgmstream/releases (`vgmstream-linux.zip`) on the PATH or in `tools/bin/` (git-ignored;
the script looks there). The app icon needs `ffmpeg` (`sudo apt install ffmpeg`); the intro movie needs both
(`tools/make_intro.sh mpg`: `files/out/video/intro.mpg`, MPEG-1 video + MP2 audio at 800x480, ~130 MB, a minute or
two). Linux filesystems are case-sensitive and the game install mixes case (`pharmmap`, `SpyCarota`, `Bnk_0.bnm`,
`BNK_200.BNM`, and bank 200 names its stream `Usdolby.apm` while the file is `USDOLBY.APM`): the scripts find these
case-insensitively; if a level fails with "not found", check letter case first.

## Running
`sdl/build/opentonic [levels folder | level.ttlv]`. Without an argument it looks for `files/out/ttlv` in the
working directory, then relative to the binary in the checkout, then in the checkout it was built from. Converted data
as in docs/BUILD.md: `ttlv/`, plus optional `Vignette/Level` (picker thumbnails, portal loading screens), `Textures/Effects`
(screen transitions), `audio/` (music, `sfx/` sound effects) and `icon/` next to it. Levels from another converter
version do not load ("re-run the conversion"): `tools/build_linux.sh` re-exports exactly those (docs/BUILD.md "Formats").

Controls match the Swift app; macOS shortcuts become Ctrl+L (level select), F1 (controls overlay, replaces the
alert), F11 or Alt+Enter (full screen), Ctrl+Q (quit). The level select screen also takes the arrow keys and Enter,
and V plays the intro movie (Esc, Space or Enter skip). F2 cycles the level's antialiasing (off / 2x / 4x / 8x
MSAA, as far as the GPU goes; shown in the title bar); `TONIC_MSAA=n` sets where it starts (default 4).
`TONIC_GPU_DEBUG=1` turns on the GPU backend's validation layers (when installed).

## Installing
`tools/install_linux.sh [dest]` builds the frontend and installs it for the current user, as `tools/make_app.sh` makes
the .app: the binary and its data in `~/.local/share/opentonic` (`opentonic` + `data/`, the files/out layout: ttlv
without the `*_ai.txt` listings, Textures, Vignette/Level, audio with music and sfx, video/intro.mpg, icon, Imported),
a menu entry `~/.local/share/applications/org.opentonic.sdl.desktop` (named after the app ID the binary gives
`SDL_SetAppMetadata`, which SDL uses as the Wayland app ID / X11 class, so the desktop pairs window and entry) and its
icon in `~/.local/share/icons/hicolor` (ffmpeg scales it). Re-running it rebuilds and updates the install in place
(rsync: only changed files, removed ones deleted); `--uninstall [dest]` removes it all. The binary looks for
`data/ttlv` next to itself before anything else. About 680 MB, mostly the WAV music (the SDL build plays WAV only).

## Layout
```
sdl/
  Makefile
  shaders/level_common.glsl  # Uniforms block shared by both stages
  shaders/level.vert/.frag   # port of Sources/OpenTonic/Shaders.metal (.frag built twice: textured, -DFLAT)
  shaders/ui.vert/.frag      # 2D quads: level picker, controls overlay, loading screen
  shaders/overlay.vert/.frag # screen transitions (Shaders.metal's overlay_vertex / overlay_fragment)
  src/main.c     # window, screens, keys, title bar, controls overlay, loading screen, portal level changes (main.swift)
  src/view.c     # mesh table + texture upload, uniforms, draws the core's list (Renderer.swift)
  src/gpu.c      # SDL_GPU device, pipelines, samplers, uploads, quads, debug-font text
  src/play.c     # .ttlv file reading                                    (Core.swift's Level)
  src/input.c    # key bindings: SDL scancodes -> tc_view_action           (Input.swift)
  src/music.c    # music.tsv lookup, looping WAV streamed from disk      (Music.swift)
  src/sfx.c      # sound effects: events.tsv, one SDL_AudioStream per voice (Sfx.swift)
  src/picker.c   # level select grid                                     (LevelPicker.swift)
  src/intro.c    # intro movie: pl_mpeg decode, audio-clocked frames     (IntroPlayer.swift)
  src/util.c     # path helpers
  third_party/pl_mpeg.h  # MPEG-1 / MP2 decoder, MIT, vendored (github.com/phoboslab/pl_mpeg c871f2b)
```

## Porting notes
- **Shaders.** GLSL rather than the HLSL + SDL_shadercross first planned: shadercross isn't packaged (it needs DXC),
  glslang is. The GLSL states SDL_GPU's SPIR-V layout directly: vertex uniforms in set 1, fragment textures in set 2
  (combined samplers), fragment uniforms in set 3, binding = slot. For other backends, the SPIR-V can go through
  shadercross / SPIRV-Cross to MSL or DXIL later. **Keep `Shaders.metal` and `sdl/shaders/level.*` in sync by hand.**

  | Metal | SDL_GPU (GLSL) |
  |---|---|
  | `[[attribute(n)]]`, `.uchar4Normalized` | `layout(location = n) in`, `SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM`; offsets from `offsetof(tc_vertex, ...)` |
  | `Uniforms [[buffer(1)]]` (both stages) | `std140` block, set 1 / set 3 binding 0: uniform slot 0 of each stage |
  | `DrawParams [[buffer(2)]]` / `flags [[buffer(2)]]` | vertex / fragment uniform slot 1 |
  | `texture(0)`, `texture(1)` + samplers | `sampler2D` at set 2 bindings 0/1, bound as texture-sampler pairs (flat: the lightmap alone at 0) |
  | `constexpr sampler lightmapSampler` | an `SDL_GPUSampler` (linear, clamp) made in C |
  | `saturate`, `discard_fragment()` | `clamp(x, 0, 1)`, `discard` |
- **Uniform layout.** The C structs in `src/view.c` mirror the GLSL `std140` blocks, with `_Static_assert`s on sizes
  and offsets. The planned shared `core/include/tonic/tc_gpu.h` was dropped: the Metal structs lay out differently
  (Swift's `SIMD3<Float>` is 16 bytes, so `ambient` sits at 80 there and at 76 in std140), so one C struct can't serve
  both without changing the Metal side.
- **Clip space.** SDL_GPU uses Metal's conventions (+Y up, depth 0..1; its Vulkan backend flips the viewport), so the
  camera math in `Camera.swift` ports unchanged.
- **Pipelines.** Four instead of three, because depth writes are pipeline state in SDL_GPU: textured, flat,
  textured alpha-blended (no depth write), flat without depth write (untextured transparent surfaces, which the Metal
  build draws with the flat pipeline under the no-write depth state). Same draw order: opaque, persos, Ed, then the
  transparent surfaces collected on the way.
- **Buffers.** All meshes of a level share one vertex and one index buffer (draws use first index + vertex offset),
  instead of one buffer pair per mesh.
- **Images.** SDL 3.4's built-in `SDL_LoadPNG`, so no stb_image. Textures are RGBA8 UNORM (not sRGB, as in the Metal
  build) with generated mipmaps; the lightmap atlas has none.
- **Music.** The tracks are WAV (docs/BUILD.md step 5), not Ogg, so no stb_vorbis either: the data chunk is streamed
  from disk into an `SDL_AudioStream` and wraps at the end.
- **Sound effects.** As Sfx.swift: `tc_viewer_sounds` after every update goes to `sfx_play`; events.tsv rows play,
  loop or stop a WAV (read once with `SDL_LoadWAV`). Each voice is its own `SDL_AudioStream` bound to one shared
  playback device: one-shots are queued whole and freed once played out, a loop's stream refills itself from its get
  callback until a stop event (or a level change) destroys it. M mutes them with the music (`music_set_muted`).
- **Portals and screen transitions** (docs/PORTALS.md). The level's effect textures (`tc_viewer_effect_texture`) are
  uploaded with the level. After the 3D pass, a 2D pass draws `tc_viewer_overlay`: the mask wipe (alpha test
  `a <= threshold`, black), the spinning logo (black x the texture's alpha on the core's rotated quad), or black, in
  their own `overlay` pipeline; for the loading screen, the vignette (`Vignette/Level/<level>.png`, matched
  case-insensitively), "Loading <level>" in the debug font and a progress bar on top, laid out as main.swift's
  `LoadingScreen`. Once `tc_viewer_pending_level` is set (after a frame of the full bar), the main loop copies the
  carry and opens that level with it; mouse-look capture stays on across the change. A missing level: a message
  box, then the level select.
- **Gamepad.** `SDL_Gamepad`: every pad is opened as it connects; the one last used (a button, or a stick pushed
  past ~40 %) drives the viewer. The sticks go to `tc_viewer_set_move_stick` / `_look_stick` each frame as they are
  (the dead zones are the core's, so the Swift app behaves the same); buttons by position (SOUTH = Cross / A):
  `input_pad_actions` in `input.c` has the level's actions (Cross jump, Square action, Circle shoot, R1 lateral and
  free-fly up, L1 free-fly down), `main.c` the rest (R2 fly faster, D-pad up / down as the mouse wheel, R3 play /
  free-fly, Start level select, Select the controls; level select: D-pad or left stick with repeat, right stick
  scrolls, Cross opens, Triangle the intro; intro: Cross, Circle, Start skip). The Mac app's `Gamepad.swift`
  (GameController) maps the same in a level; its level select and intro are mouse / keyboard only so far.
- **Level picker.** Thumbnails and captions as textured quads; captions and the controls overlay use SDL's 8x8 debug
  font, rendered into a surface by a software renderer and uploaded.
- **Intro movie.** SDL has no video decoding, so the movie is MPEG-1 + MP2 for the single-header pl_mpeg rather than
  the Swift build's HEVC .mp4. The soundtrack is the clock: decoded audio is kept ~0.25 s ahead in an
  `SDL_AudioStream`, and a frame goes up (CPU YCbCr -> RGBA, one texture upload) once the device has played to its
  time. Decoding costs ~6 ms per 800x480 frame on a Celeron J4125, so it stays on the main thread. The 2.25x
  upscale of the 400x240 original happens offline (Lanczos + unsharp, as for the .mp4), the rest is the GPU's.
- **Antialiasing (SDL build only, for now).** The Metal build renders at Retina resolution without MSAA, which hides
  jaggies; at 1366x768 they show. The level renders into a multisampled target (4x by default) resolved into the
  swapchain; the UI draws unsampled on top. The opaque textured pipeline uses alpha to coverage, and the shader
  sharpens alpha-tested texels to about a pixel around the 0.5 threshold, so cutouts (foliage, fences, sprites)
  get smooth edges too (`coverage` in the fragment `Flags` block; blended and flat draws keep the old path).
  Changing the sample count rebuilds the four level pipelines.
- **Texture prep.** Level textures get 16x anisotropic filtering (8x in the Metal build), and fully transparent
  texels take their neighbours' colour before mipmapping (`bleed_alpha` in `src/gpu.c`): about one in ten cutout
  textures has a contrasting colour under its transparency, which filtering and mipmaps would otherwise smear into
  a fringe. ~1.6 ms per cutout texture at load.

## Not done yet
- `tools/make_app.sh` stays macOS-only; its Linux counterpart is `tools/install_linux.sh` (see Installing). **Same
  rule as the app: never redistribute anything containing converted assets.**

Parity check once data is converted: screenshot `bar` (lightmaps + fog) and one radiosity-lit level in both builds
from the same camera position; they should match apart from filtering differences.
