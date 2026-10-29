/* SDL_GPU plumbing shared by the level view and the 2D screens: device and window, the level and UI pipelines
 * (shaders/, compiled to SPIR-V and embedded at build time), samplers, uploads, frames, and textured
 * quads / debug-font text for the level picker and overlays. */
#ifndef TV_GPU_H
#define TV_GPU_H
#include <stdbool.h>
#include <SDL3/SDL.h>

typedef struct gpu {
    SDL_Window *window;
    SDL_GPUDevice *device;
    SDL_GPUTextureFormat swapchain_format, depth_format;
    /* Level pipelines (as Renderer.swift): textured and flat (opaque, depth write); TC_FLAG_TRANSPARENT surfaces
     * are drawn after everything opaque without writing depth: textured alpha-blended, or flat. */
    SDL_GPUGraphicsPipeline *textured, *flat, *textured_blended, *flat_no_depth_write;
    /* Multisample antialiasing of the level (1 = off): the level pipelines and 3D targets use this many samples,
     * resolved into the swapchain at the end of the 3D pass. Opaque textured surfaces get alpha to coverage then,
     * so alpha-tested cutouts (foliage, fences, sprites) are antialiased too. */
    int msaa;
    SDL_GPUShader *level_vs, *textured_fs, *flat_fs;  /* kept to rebuild the level pipelines for another msaa */
    /* Ed's round shadow (shaders/shadow.*): blended, depth-tested without writing, biased towards the camera; rebuilt
     * with the level pipelines (the same sample count). */
    SDL_GPUGraphicsPipeline *shadow;
    SDL_GPUShader *shadow_vs, *shadow_fs;
    SDL_GPUGraphicsPipeline *ui;      /* 2D quads, alpha-blended, no depth */
    SDL_GPUGraphicsPipeline *overlay; /* screen transitions (docs/PORTALS.md): free-cornered quads, as ui */
    SDL_GPUGraphicsPipeline *bar;     /* the loading screen's rainbow bar (shaders/bar.frag), as ui */
    SDL_GPUSampler *repeat_sampler;   /* trilinear, repeat, 16x anisotropy (the loading bar's pool) */
    /* Level textures: as repeat_sampler, but repeating or clamped per axis as the surface's TC_FLAG_WRAP_U / _V say
     * (the original's driver addressing, from the texture's name). Index: wrap u | wrap v << 1. */
    SDL_GPUSampler *level_samplers[4];
    SDL_GPUSampler *clamp_sampler;    /* lightmap atlas: linear, clamp to edge */
    SDL_GPUSampler *ui_sampler;       /* thumbnails and text: trilinear, clamp */
    SDL_GPUSampler *nearest_sampler;  /* text drawn at an integer multiple of its size */
    SDL_GPUTexture *white;            /* 1x1, for untextured quads and levels without a lightmap */
    SDL_GPUTexture *depth, *msaa_color;  /* 3D targets: msaa samples, the swapchain's size x scale */
    /* Supersampling: the level drawn at scale percent of the swapchain's size (100, 150, 200) into `scaled`, then
     * filtered down into the swapchain (a linear blit: at 200 % a 2 x 2 box filter). On top of the MSAA. */
    int scale;
    SDL_GPUTexture *scaled;
    Uint32 target_w, target_h;
    int target_samples;
} gpu;

/* msaa: the wanted sample count (1, 2, 4, 8); the largest supported one up to it is used. */
bool gpu_init(gpu *g, SDL_Window *window, int msaa);
void gpu_quit(gpu *g);
/* Switches the level's antialiasing (rebuilds the level pipelines; waits for the GPU). false when unsupported. */
bool gpu_set_msaa(gpu *g, int samples);
/* The next supported setting after the current one, cycling 1 -> 2 -> 4 -> 8 -> 1. */
int gpu_next_msaa(const gpu *g);

/* A batch of uploads in one command buffer: textures (optionally mipmapped) and buffers. Each call stages its data
 * at once, so the source memory can be freed right after it returns. gpu_upload_end submits. */
typedef struct gpu_upload {
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *pass;
    SDL_GPUTexture **mipmapped;  /* generated after the copy pass */
    int mipmapped_count, mipmapped_cap;
} gpu_upload;

bool gpu_upload_begin(gpu *g, gpu_upload *u);
/* Any pixel format (converted to RGBA8). NULL on failure. Mipmapped images with transparent texels get their colour
 * bled outwards under the transparency first, so filtering and mipmaps don't fringe cutout edges with whatever
 * colour the artist left there. */
SDL_GPUTexture *gpu_upload_surface(gpu *g, gpu_upload *u, SDL_Surface *s, bool mipmaps);
/* NULL on failure (SDL_GetError has why). w/h may be NULL. */
SDL_GPUTexture *gpu_upload_png(gpu *g, gpu_upload *u, const char *path, bool mipmaps, int *w, int *h);
SDL_GPUBuffer *gpu_upload_buffer(gpu *g, gpu_upload *u, SDL_GPUBufferUsageFlags usage, const void *data, Uint32 size);
bool gpu_upload_end(gpu *g, gpu_upload *u);

/* Text in SDL's built-in 8x8 debug font, white on transparent ('\n' starts a line; line height 10 x scale). */
SDL_GPUTexture *gpu_text(gpu *g, const char *text, int scale, int *w, int *h);

/* One frame: acquire the swapchain, then one or more passes, then end (submit). gpu_frame_begin returns false when
 * there is nothing to draw into (minimised window); the frame is already finished then. */
typedef struct gpu_frame {
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *target;
    Uint32 width, height;
} gpu_frame;

bool gpu_frame_begin(gpu *g, gpu_frame *f);
/* Clears colour and depth. End it with gpu_pass_3d_end (it filters a supersampled frame down into the swapchain). */
SDL_GPURenderPass *gpu_pass_3d(gpu *g, gpu_frame *f, SDL_FColor clear);
void gpu_pass_3d_end(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass);
/* The level's supersampling, percent of the window's pixels per axis: 100 (off), 150, 200. */
void gpu_set_scale(gpu *g, int percent);
int gpu_next_scale(const gpu *g);
/* No depth; clear = false keeps what earlier passes drew (overlays). */
SDL_GPURenderPass *gpu_pass_2d(gpu *g, gpu_frame *f, bool clear, SDL_FColor clear_color);
void gpu_frame_end(gpu *g, gpu_frame *f);

/* A tinted, textured rectangle in target pixels (top-left origin) in a 2D pass. tex NULL: solid colour.
 * uv NULL: the whole texture. */
void gpu_quad(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_GPUTexture *tex, SDL_FRect rect, const SDL_FRect *uv,
              SDL_FColor color);
/* The same with a given sampler (e.g. nearest_sampler). */
void gpu_quad_sampled(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_GPUTexture *tex, SDL_GPUSampler *sampler,
                      SDL_FRect rect, const SDL_FRect *uv, SDL_FColor color);

/* A screen transition over the frame in a 2D pass (Shaders.metal's overlay_fragment): mode 0 black where tex's alpha
 * <= threshold (the mask wipe), 1 black with tex's alpha (the spinning logo), 2 solid black (tex unused). corners and
 * uv in viewport units (0..1, y down), triangles 0 1 2 and 1 3 2; NULL = the whole viewport and texture. */
void gpu_overlay(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_GPUTexture *tex, const float corners[4][2],
                 const float uv[4][2], int mode, float threshold);

/* Ed's shadow disc (tc_viewer_shadow) in the level's 3D pass, after the opaque draws. */
typedef struct gpu_shadow {
    float view_projection[16];
    float center[3], radius;
    float normal[3], inner;
    float alpha;
    float fog_near, fog_far;
    bool fog_on;
    float eye[3];
} gpu_shadow;
void gpu_draw_shadow(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, const gpu_shadow *s);

/* The loading screen's bar (main.swift's RainbowBar) over rect, in target pixels, in a 2D pass: tex the rainbow pool
 * (sud0's o_zarbi_auntxy.png, sampled repeating), NULL = plain white; scale = pixels per point; time in seconds. */
void gpu_loading_bar(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_GPUTexture *tex, SDL_FRect rect, float progress,
                     float time, float scale);

#endif
