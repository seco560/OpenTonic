/* Draws one converted level: the GPU half of Sources/OpenTonic/Renderer.swift. The core's tc_viewer owns the
 * cameras, play mode, settings and what to draw each frame; this uploads its mesh table and the level's textures once,
 * then packs its shading values into the shaders' uniforms and draws its list. */
#ifndef TV_VIEW_H
#define TV_VIEW_H
#include <stdbool.h>
#include "gpu.h"
#include "play.h"

/* One tc_viewer_mesh in the shared buffers; index_count 0 = empty, never drawn. */
typedef struct view_mesh {
    Uint32 first_index, index_count;
    Sint32 vertex_offset;        /* all meshes share one vertex and one index buffer */
} view_mesh;

typedef struct view {
    gpu *g;
    tv_level level;              /* kept alive: the viewer points into it */
    tc_viewer viewer;            /* cameras, play mode, settings, draw list (core) */
    bool has_viewer;
    SDL_GPUBuffer *vertices, *indices;
    SDL_GPUTexture **textures;   /* level->textures order; NULL = missing */
    uint32_t texture_count, missing_textures;
    /* Per-triangle lightmap atlas (.ttlv v8), or the shared 1x1 white texture for levels without lightmaps. */
    SDL_GPUTexture *lightmap;
    view_mesh *meshes;           /* tc_viewer_mesh order */
    SDL_GPUTexture **effects;    /* screen transitions, tc_viewer_effect_texture order; NULL = missing */
    uint32_t effect_count;
} view;

/* Loads the .ttlv (and the .ttai next to it) and uploads it. carry: arriving through a portal (a copy of the last
 * level's tc_viewer_pending_level), or NULL. On failure err says why and the view is empty (view_free is still
 * safe). */
bool view_load(view *v, gpu *g, const char *ttlv_path, const tc_viewer_carry *carry, char *err, size_t err_size);
void view_free(view *v);

/* Clears and draws the level into the frame (its own 3D pass). */
void view_draw_frame(view *v, gpu_frame *f);
/* The screen transition, black, or the loading screen's black backdrop (o from tc_viewer_overlay) in a 2D pass over
 * the frame. The loading screen itself (vignette, caption, bar) is the app's. */
void view_draw_overlay(view *v, gpu_frame *f, SDL_GPURenderPass *pass, const tc_view_overlay *o);

#endif
