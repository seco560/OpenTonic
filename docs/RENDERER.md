# Renderer interface (sketch)

Status: proposal, nothing built. Expands docs/VITA.md step 3. Goal: the SDL frontend's app code (screens, picker,
replays, intro, loading screen, `view.c`'s per-frame logic) is written once, and a backend is one file of draw
primitives: `gfx_sdlgpu.c` (today's code), then `gfx_gl.c` (GL 3.3 / GLES 3: Linux without Vulkan, Windows, Android),
and later vitaGL / citro3d ones if wanted.

## What is where today
- `gpu.h` leaks SDL_GPU types everywhere (`SDL_GPUTexture*`, `SDL_GPURenderPass*`, `SDL_GPUBuffer*`) into `main.c`,
  `picker.c`, `replays.c`, `intro.c` and `view.c`. Almost all of those uses are only *handles*, so the change is
  mechanical.
- `view.c` calls `SDL_BindGPU*` / `SDL_Push*UniformData` / `SDL_DrawGPUIndexedPrimitives` itself. This is the one place
  the real draw loop is backend code, and it has to move behind the interface.
- `intro.c` makes its own SDL_GPU texture and copy pass for video frames.
- `gpu.c` (773 lines) mixes shared logic (PNG decode, alpha bleed, transparent-texel test, MSAA and scale cycling, debug
  text) with backend code (pipelines, samplers, passes, swapchain, supersample blit).

## Design rules
1. **The interface is semantic, not a thin wrapper of SDL_GPU.** The backend gets "draw this mesh range with this
   texture and these wrap flags", not "bind this pipeline, push these bytes at slot 1". Backends differ most in
   binding and uniforms (UBOs vs plain uniforms vs the 3DS's constant registers), so that is what must stay inside them.
2. **Passes are implicit.** The frame knows whether a 3D or 2D pass is open. The `SDL_GPURenderPass *pass` parameter
   disappears from every call site.
3. **Uniform structs stay in shared code** (`gpu_uniforms`, `gpu_draw_params`, `gpu_lights`, `gpu_flags`; the `view.c`
   packing and their `_Static_assert`s). The backend receives them as plain structs. GL 3.3 / GLES 3 take them as std140
   UBOs unchanged. A GLES2-class backend (vitaGL) unpacks them into loose uniforms.
4. **Textures and buffers are opaque handles**, created through the existing upload batch API.
5. **Shared CPU work moves out of the backend**: PNG decode, `bleed_alpha`, `has_transparent_texel`, RGBA8 conversion,
   mip chain generation (CPU box filter, so GLES2 / Vita need no GPU mip generation), debug-font text, MSAA / scale
   cycling policy.

## Header sketch (`gfx.h`)
```c
typedef struct gfx gfx;                    /* backend state; one per window */
typedef struct gfx_texture gfx_texture;    /* RGBA8, 2D */
typedef struct gfx_buffer gfx_buffer;

/* ---- device ---- */
bool gfx_init(gfx **g, SDL_Window *window, int msaa);   /* window NULL where the platform owns the display (Vita) */
void gfx_quit(gfx *g);
/* Capabilities, so shared code decides policy once: */
typedef struct gfx_caps { int max_msaa; bool supersample; bool ubo; int max_texture; bool aniso; } gfx_caps;
const gfx_caps *gfx_get_caps(const gfx *g);
bool gfx_set_msaa(gfx *g, int samples);       /* false = unsupported; policy (cycling order) is shared code */
void gfx_set_scale(gfx *g, int percent);

/* ---- uploads: shared code decodes and prepares RGBA8 (+ mips); the backend only stores them ---- */
typedef struct gfx_image { int w, h; const uint8_t *rgba; } gfx_image;     /* level 0 */
gfx_texture *gfx_texture_create(gfx *g, int w, int h, const gfx_image *mips, int mip_count);
gfx_buffer  *gfx_buffer_create(gfx *g, gfx_buffer_kind kind, const void *data, size_t size);  /* VERTEX | INDEX */
void gfx_texture_free(gfx *g, gfx_texture *t);
void gfx_buffer_free(gfx *g, gfx_buffer *b);
/* Streaming (the intro): update in place, call before the frame's passes. */
gfx_texture *gfx_texture_create_stream(gfx *g, int w, int h);
void gfx_texture_update(gfx *g, gfx_texture *t, const uint8_t *rgba, int pitch);
```
No `gpu_upload` batch in the interface: backends that want batching (SDL_GPU's copy pass) can keep an internal open
batch flushed at `gfx_frame_begin`, or the shared loader can call `gfx_flush_uploads(g)` after a level. `gpu_upload_*`
wrappers (`png`, `surface`, `text`) stay as shared helpers on top of `gfx_texture_create`.

```c
/* ---- frame ---- */
typedef struct gfx_frame { int width, height; } gfx_frame;           /* backend keeps the rest privately */
bool gfx_frame_begin(gfx *g, gfx_frame *f);                          /* false: minimised */
void gfx_pass_3d(gfx *g, gfx_frame *f, gfx_color clear);            /* colour + depth; MSAA / supersample target */
void gfx_pass_3d_end(gfx *g, gfx_frame *f);                          /* resolve / downfilter into the swapchain */
void gfx_pass_2d(gfx *g, gfx_frame *f, bool clear, gfx_color c);
void gfx_frame_end(gfx *g, gfx_frame *f);

/* ---- 2D (screens, picker, replays, intro, loading) ---- */
void gfx_quad(gfx *g, gfx_texture *t, gfx_rect r, const gfx_rect *uv, gfx_color tint, gfx_sampler s);  /* LINEAR_CLAMP | NEAREST */
void gfx_overlay(gfx *g, gfx_texture *t, const float corners[4][2], const float uv[4][2], int mode, float threshold);
void gfx_loading_bar(gfx *g, gfx_texture *pool, gfx_rect r, float progress, float time, float scale);

/* ---- level (what view.c needs; everything else in view.c is shared) ---- */
typedef enum { GFX_TEXTURED, GFX_FLAT, GFX_TEXTURED_BLENDED, GFX_FLAT_NO_DEPTH_WRITE } gfx_material;
typedef struct gfx_level_frame {            /* once per 3D pass */
    const gpu_uniforms *u; const gpu_lights *lights;
    gfx_buffer *vertices, *indices;         /* bound for the whole pass; 32-bit indices */
    gfx_texture *lightmap;                  /* the 1x1 white one when the level has none */
} gfx_level_frame;
void gfx_level_begin(gfx *g, const gfx_level_frame *lf);   /* also used again after the shadow (state may be clobbered) */
void gfx_level_draw(gfx *g, gfx_material m, gfx_texture *tex, unsigned wrap /* u | v << 1 */, uint32_t flags,
                    const gpu_draw_params *p, uint32_t first_index, uint32_t index_count, int32_t vertex_offset);
void gfx_draw_shadow(gfx *g, const gpu_shadow *s);
```
Shadow and overlay-in-3D already carry all their state in a struct, so they port as they are.

## Shared / backend split of `gpu.c`
| stays shared (`gfx_util.c`) | goes into the backend |
|---|---|
| PNG load, any-format to RGBA8, `bleed_alpha`, `has_transparent_texel`, mip chain | texture and buffer objects, samplers |
| debug-font text (SDL software renderer) | pipelines / programs, blend and depth state |
| MSAA and scale cycling, `gfx_next_*` | swapchain, render targets, MSAA resolve, supersample downfilter |
| uniform struct packing (`view.c`) | binding, uniform upload, draw, shader set |
| everything in `main.c`, `picker.c`, `replays.c`, `intro.c`, `view.c` | |

Expected result: ~250 lines of `gpu.c` stay shared, ~500 become `gfx_sdlgpu.c`; `gfx_gl.c` is a second ~600-line file
plus shader work. `main.c`'s ~44 GPU mentions and the screens' ~70 become renames and drop the `pass` argument.

## Shaders
One GLSL source (`sdl/shaders/`), as now. Build pipeline per backend:
- SDL_GPU: `glslangValidator` to SPIR-V, embedded (today).
- GL 3.3 / GLES 3 / D3D11 later: the same SPIR-V through `spirv-cross` (build-time, to `--version 330` / `--es 300`
  / HLSL), embedded as strings. This keeps one source of truth. Fallback if `spirv-cross` fights `GL_GOOGLE_include_directive`
  or the UBO layout: a thin `#version` / `#define` preamble over hand-ported GLSL.
- Vita (Cg) and 3DS (PICA assembly): hand-written, and the 48-light loop in `level.vert` (`game_light`) will need
  a cut-down variant. Out of scope until those backends are started.

## Platform shell
`main.c` mixes the app and window / event handling. Split it as VITA.md says: `app.c` (screens, input to viewer, loading,
portals) and `shell_sdl.c` (window, events, frame loop). For GL the shell needs `SDL_WINDOW_OPENGL`, a context and swap;
`gfx_init` handles those internally, so the shell only passes the window flags `gfx_window_flags()` returns.

## Migration order (desktop verified after each step, as VITA.md asks)
1. Add `gfx.h`; typedef `gfx_texture` / `gfx_buffer` over the SDL_GPU types; rename call sites; drop `pass` arguments
   (pass kept inside `gpu_frame`). No behaviour change; Linux and Mac-style checks as today.
2. Move `view.c`'s draw loop into `gfx_level_begin/draw`; `view.c` stops including SDL_GPU calls.
3. Move `intro.c`'s texture into `gfx_texture_create_stream/update`.
4. Split shared helpers out of `gpu.c`; rename the rest `gfx_sdlgpu.c`; Makefile selects the backend (`BACKEND=sdlgpu|gl`).
5. `gfx_gl.c`: first on Linux with `llvmpipe` or the Nvidia driver (the GT 555M gives GL 4.5 on the proprietary driver),
   then Windows, then Android.

## Open questions
- **Per-draw cost.** `gfx_level_draw` does one UBO update per draw (up to ~1,450). Fine on desktop GL; for Vita, step 2
  of VITA.md (cull / batch in the core) matters more than the interface.
- **MSAA / supersampling on GL.** Needs renderbuffer or texture-multisample FBOs; GLES 3.0 has no multisample
  textures, only renderbuffers (resolve by blit), which is what the code needs. Alpha-to-coverage exists in both.
- **Mip generation.** The sketch makes CPU mips the shared default (simple, identical everywhere). GPU `glGenerateMipmap`
  could be an optional fast path via `gfx_caps`.
- **D3D11.** If ever wanted, spirv-cross HLSL plus a third backend file; nothing in this interface prevents it.

## Windows with the current Vulkan renderer (notes, 2026-11-09; untested)
Likely the quickest way to a Windows build, before any GL backend exists.
- **Source is nearly portable.** `sdl/src` has no POSIX includes (no dirent / unistd / `/proc`); files, env vars and
  case-insensitive compares all go through SDL. `SDL_GPU_SHADERFORMAT_SPIRV` selects Vulkan: the loader
  (`vulkan-1.dll`) ships with the GPU driver and SDL loads it at runtime, so no Vulkan SDK or `libvulkan-dev` is needed.
- **Build:** MSYS2 UCRT64 with `mingw-w64-ucrt-x86_64-{gcc,make,cmake,pkgconf,glslang,git}`; `sdl/Makefile` should
  run nearly as is (it clones and statically builds SDL 3.4.8, and the core builds through its own Makefile).
  Expected tweaks: `-mwindows` (no console window; omit for debug), `.exe` suffix, Windows-path quoting for
  `TONIC_REPO_DIR`, check `core/Makefile` for Unix-tool assumptions, and backslash handling in the data lookup
  (working dir, next to the binary, build checkout).
- **Data:** `tools/convert_files.sh` (shell + Python, plus `vgmstream-cli` and `ffmpeg`; vgmstream has a prebuilt
  Windows binary) runs under MSYS2, or convert on Linux / macOS and copy `files/out`.
- **Packaging:** a folder with `opentonic.exe` and `data/` (SDL is static); an install script can follow.
- **Caveat:** machines without Vulkan are the old ones (see docs/VITA.md, Windows 7 / D3D9 / GL 2.0), which is what the
  GL backend is for. A D3D12 fallback needs DXIL shaders from SDL_shadercross and both formats passed to
  `SDL_CreateGPUDevice`, with no renderer changes, but D3D12 needs Windows 10 and nearly the same hardware as Vulkan,
  so add it only if a driver problem is reported.
- **Risks:** static SDL3 under MinGW, and older Intel iGPU Vulkan drivers (the 8x MSAA, anisotropy and 150 / 200 %
  supersampling are the likely trouble spots). A `mingw-w64` cross-compile from Linux would show how much of the
  Makefile changes, but only a Windows machine can run the result.
