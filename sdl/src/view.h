/* Draws one converted level. Port of Sources/TonicViewer/Renderer.swift.
 * Animation runs on one clock (animation_time, paused with P): perso parts take their channel's baked transform for
 * the current sample, materials scroll UVs and step animated textures. Aspect changes widen the view horizontally
 * (Hor+). */
#ifndef TV_VIEW_H
#define TV_VIEW_H
#include <stdbool.h>
#include "camera.h"
#include "gpu.h"
#include "input.h"
#include "play.h"

typedef struct view_mesh {
    const tc_mesh *src;          /* surface (texture, flags, scroll, frames), owned by the level */
    Uint32 first_index, index_count;
    Sint32 vertex_offset;        /* all meshes share one vertex and one index buffer */
    float loop_seconds;          /* animated textures: one loop through the frames; 0 = static */
} view_mesh;

typedef struct view_part { uint16_t channel, object; view_mesh mesh; } view_part;

typedef struct view_perso {
    tc_mat4 world;
    const tc_perso *src;         /* baked samples, owned by the level */
    view_part *parts;
    uint32_t part_count;
    uint32_t source_index;       /* index in the level's persos (Ed's static copy is hidden while playing) */
} view_perso;

typedef struct view_ed_part { uint16_t object; view_mesh mesh; } view_ed_part;

typedef struct view_stats { uint32_t meshes, triangles, textures, missing_textures, persos, parts; } view_stats;

typedef struct view_draw { const view_mesh *mesh; tc_mat4 model; } view_draw;

typedef struct view {
    gpu *g;
    tv_level level;              /* kept alive: persos and Ed's clips point into it */
    SDL_GPUBuffer *vertices, *indices;
    SDL_GPUTexture **textures;   /* level->textures order; NULL = missing */
    uint32_t texture_count;
    /* Per-triangle lightmap atlas (.ttlv v8), or the shared 1x1 white texture for levels without lightmaps. */
    SDL_GPUTexture *lightmap;
    view_mesh *meshes;
    uint32_t mesh_count;
    view_perso *persos;
    uint32_t perso_count;
    view_part *part_storage;     /* all persos' parts, one allocation */
    view_ed_part *ed_parts;
    uint32_t ed_part_count;
    view_stats stats;

    bool paused;
    /* Baked lighting set (1-9 keys; only levels with day/night variants have more than one) and display gamma. */
    uint32_t light_set, light_set_count;
    float gamma;
    /* The level's fog from its visual environment (.ttlv v7); G toggles it. */
    bool has_fog, fog_enabled;
    float fog_near, fog_far, fog_color[3];
    /* Viewer-only playback rate ([ and ] keys) for comparing against the original; 1 = engine speed. */
    float animation_speed, animation_time;

    camera cam;
    input in;

    /* Play mode (the default when the level has Ed; Tab toggles free-fly): Ed driven by the core's tc_player against
     * the level's collision triangles, or by his own AI. */
    bool playing;
    play_session *player;
    int ed_static_perso;
    char ed_clip[32];
    float ed_phase;              /* samples into the current clip */
    bool ed_loop;                /* AI clips: whether the playing state loops (blend last -> first frame) */
    float orbit_distance;
    /* Where the camera was when play mode was last left; if it has moved since, Ed is dropped there. */
    bool has_exit_camera;
    tc_vec3 exit_camera;

    view_draw *blended;          /* transparent surfaces of the current frame, drawn last */
    uint32_t blended_count, blended_cap;
} view;

/* Loads the .ttlv and uploads it. On failure err says why and the view is empty (view_free is still safe). */
bool view_load(view *v, gpu *g, const char *ttlv_path, char *err, size_t err_size);
void view_free(view *v);

/* Tab: enter/leave play mode. Entering puts the orbit camera behind Ed; leaving keeps the camera where it is.
 * Re-entering after flying elsewhere drops Ed onto the ground below the camera, facing the view direction. */
void view_toggle_play(view *v);
/* I: Ed's own AI <-> the simple controller (play mode, levels with a .ttai). */
void view_toggle_ai(view *v);
bool view_has_ai(const view *v);

/* Input, Ed and the camera for one frame. */
void view_update(view *v, float dt);
/* Clears and draws the level into the frame (its own 3D pass). */
void view_draw_frame(view *v, gpu_frame *f);

#endif
