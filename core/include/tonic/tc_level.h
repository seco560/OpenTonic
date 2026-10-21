/* .ttlv loader (versions 1-5; spec in tools/ttre/ttlv.py). The platform layer reads the file into
 * memory; tc_level_load parses it into plain structs owned by the tc_level (free with tc_level_free).
 * Texture paths are relative to the .ttlv file's folder; resolving them is the platform's job. */
#ifndef TONIC_TC_LEVEL_H
#define TONIC_TC_LEVEL_H
#include <stddef.h>
#include <stdint.h>
#include "tc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_FLAG_ALPHA_TEST (1u << 0)
#define TC_FLAG_TRANSPARENT (1u << 1)
#define TC_HIDDEN 0xFFFFu /* "shown object" value of a hidden channel */

typedef enum tc_result {
    TC_OK = 0,
    TC_ERR_BAD_MAGIC,
    TC_ERR_UNSUPPORTED_VERSION,
    TC_ERR_TRUNCATED,
    TC_ERR_OUT_OF_MEMORY
} tc_result;

const char *tc_result_string(tc_result r);

/* 44 bytes, tightly packed: exactly the GPU vertex layout the viewer uses.
 * color[i]: baked lighting set i, rgba8 little-endian (r in the low byte); alpha 0 = none.
 * Files before v4 fill only color[0] (v3) or nothing (v1/v2). */
typedef struct tc_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    uint32_t color[3];
} tc_vertex;
/* Platform backends upload tc_vertex arrays to the GPU as-is (44-byte stride). */
_Static_assert(sizeof(tc_vertex) == 44, "tc_vertex must stay the packed 44-byte GPU vertex layout");

typedef struct tc_frame { int32_t texture; float seconds; } tc_frame;

typedef struct tc_surface {
    int32_t texture;      /* -1 = untextured */
    uint32_t flags;       /* TC_FLAG_* */
    float scroll[2];      /* UV per second */
    uint32_t frame_count; /* animated texture loop; 0 = static */
    tc_frame *frames;
} tc_surface;

typedef struct tc_mesh {
    tc_surface surface;
    uint32_t vertex_count, index_count;
    tc_vertex *vertices;
    uint32_t *indices;
} tc_mesh;

/* A rigid part of a perso: drawn with its channel's transform while the channel shows `object`. */
typedef struct tc_part { uint16_t channel, object; tc_mesh mesh; } tc_part;

typedef struct tc_perso {
    tc_mat34 world;              /* perso -> world */
    uint16_t channel_count, sample_count;
    float sample_rate;           /* samples per second */
    int16_t *parents;            /* channel_count; informational */
    uint32_t part_count;
    tc_part *parts;
    tc_mat34 *transforms;        /* [sample * channel_count + channel], channel -> perso */
    uint16_t *shown;             /* same indexing; TC_HIDDEN = channel hidden */
} tc_perso;

/* One of Ed's baked animations (v5). Channels index only this animation's tracks. */
typedef struct tc_ed_anim {
    char *name;                  /* NUL-terminated: idle, walk, run, jump, jump_air, fall, land */
    uint16_t channel_count, sample_count;
    float sample_rate;
    uint8_t loop;
    float ground_speed;          /* units/s the feet cover at sample_rate; 0 = not locomotion */
    tc_mat34 *transforms;        /* [sample * channel_count + channel], channel -> Ed */
    uint16_t *shown;
} tc_ed_anim;

typedef struct tc_ed_part { uint16_t object; tc_mesh mesh; } tc_ed_part;

typedef struct tc_ed {
    tc_mat34 start;              /* placement; he faces local -Y */
    int32_t static_perso;        /* his own entry in persos (hide while playing), -1 = none */
    uint32_t part_count;
    tc_ed_part *parts;           /* keyed by object */
    uint32_t anim_count;
    tc_ed_anim *anims;
} tc_ed;

typedef struct tc_level {
    uint32_t version;
    uint32_t light_set_count;    /* 1 before v4 */
    uint32_t texture_count;
    char **textures;             /* NUL-terminated UTF-8 paths relative to the .ttlv */
    uint32_t mesh_count;
    tc_mesh *meshes;             /* static world, engine space, Z up */
    uint32_t perso_count;
    tc_perso *persos;
    uint32_t collision_triangle_count;
    tc_vec3 *collision;          /* v5: 3 vertices per triangle, world space */
    int has_ed;
    tc_ed ed;
} tc_level;

/* Parses `size` bytes at `data`. On failure *level is left empty (safe to tc_level_free). */
tc_result tc_level_load(tc_level *level, const uint8_t *data, size_t size);
void tc_level_free(tc_level *level);

/* Axis-aligned bounds of the static meshes' vertices (zero when there are none). */
void tc_level_bounds(const tc_level *level, tc_vec3 *lo, tc_vec3 *hi);

/* Index of Ed's animation called `name`, or -1. */
int tc_ed_find_anim(const tc_ed *ed, const char *name);

#ifdef __cplusplus
}
#endif
#endif
