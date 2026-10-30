/* .ttlv loader (spec in tools/ttre/ttlv.py, container in tools/ttre/chunks.py). The platform layer reads the file into
 * memory; tc_level_load parses it into plain structs owned by the tc_level (free with tc_level_free).
 * Texture paths are relative to the .ttlv file's folder; resolving them is the platform's job. */
#ifndef TONIC_TC_LEVEL_H
#define TONIC_TC_LEVEL_H
#include <stddef.h>
#include <stdint.h>
#include "tc_math.h"
#include "tc_collision.h"
#include "tc_sector.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_FLAG_ALPHA_TEST (1u << 0)
#define TC_FLAG_TRANSPARENT (1u << 1)
#define TC_FLAG_BILLBOARD (1u << 2)  /* sprites: vertex = centre, normal.xy = corner offset (camera plane) */
#define TC_FLAG_WATER (1u << 3)      /* set by tc_viewer on draws: a water material (the mesh holds water surface
                                        triangles: a range of a SECT water group): the renderers add the sheen to its
                                        upward faces (NOTES "Water surfaces") */
/* Texture addressing per axis, as the original's driver sets it from the texture's name (tools/ttre/ttlv.py
 * `tiling`): wrap along u / v when set, clamp to the edge otherwise. */
#define TC_FLAG_WRAP_U (1u << 4)
#define TC_FLAG_WRAP_V (1u << 5)
/* The original's alpha test for cut-outs (GliDX6vr.dll: ALPHAFUNC GREATEREQUAL, ALPHAREF 0xE1). */
#define TC_ALPHA_REF (225.0f / 255.0f)
#define TC_HIDDEN 0xFFFFu /* "shown object" value of a hidden channel */

typedef enum tc_result {
    TC_OK = 0,
    TC_ERR_BAD_MAGIC,
    TC_ERR_STALE,          /* written by another converter (old format, missing or resized chunk): re-run the conversion */
    TC_ERR_TRUNCATED,
    TC_ERR_OUT_OF_MEMORY
} tc_result;

const char *tc_result_string(tc_result r);

/* 64 bytes, tightly packed: exactly the GPU vertex layout the viewer uses.
 * color[i]: baked lighting set i, rgba8 little-endian (r in the low byte); alpha 255 = baked, TC_SKY_ALPHA = baked, a
 * sky's (neutral under the port's old lighting), 0 = none (persos: no radiosity).
 * uv2: lightmap atlas coordinates (texel space / atlas size, top-left origin); -1 = no lightmap.
 * centre: the bounding-sphere centre of the object the vertex comes from, in the mesh's space (.ttlv LITE; the vertex
 * itself without it): a spherical light reaches an object only when this point is within its far range. */
typedef struct tc_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    uint32_t color[3];
    float uv2[2];
    float centre[3];
} tc_vertex;
/* Platform backends upload tc_vertex arrays to the GPU as-is (64-byte stride). */
_Static_assert(sizeof(tc_vertex) == 64, "tc_vertex must stay the packed 64-byte GPU vertex layout");
#define TC_SKY_ALPHA 128u

typedef struct tc_frame { int32_t texture; float seconds; } tc_frame;

typedef struct tc_surface {
    int32_t texture;      /* -1 = untextured */
    uint32_t flags;       /* TC_FLAG_* */
    float scroll[2];      /* UV per second */
    uint32_t frame_count; /* animated texture loop; 0 = static */
    tc_frame *frames;
    /* The material's lighting coefficients (.ttlv LITE; without it ambient 0.5, diffuse 0: full light): the original's
     * light = min(1, (radiosity / 256 + lights) x diffuse + ambient + ambient lights x diffuse) x 512 / 255). */
    float ambient[3], diffuse[3];
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

/* One of Ed's baked animations. Channels index only this animation's tracks. */
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

/* A level perso that sets the baked light set: its scripts, behaviours and dsgvars (the body of a .ttai SCPT chunk,
 * for tc_ai_model_load_scripts). */
typedef struct tc_light_perso { uint32_t size; uint8_t *scripts; } tc_light_perso;

/* A light of the original (.ttlv LITE; GLI light record, docs/DECOMP.md "Rendering and lighting"). The sectors list
 * which of them light a frame drawn from them (tc_sector.lights). */
typedef enum tc_light_type {
    TC_LIGHT_PARALLEL = 1, TC_LIGHT_SPHERICAL = 2, TC_LIGHT_HOTSPOT = 3, TC_LIGHT_AMBIENT = 4
} tc_light_type;
typedef struct tc_light {
    uint8_t type;                /* tc_light_type */
    uint8_t on;
    uint8_t paint;               /* +0xa6: a spherical light without N.L (none in the 21 levels) */
    uint8_t alpha_mode;          /* +0xa7: 0 colour and alpha, 1 alpha only, 2 colour only */
    float near_range, far_range; /* spherical: full inside near, fading out to far */
    tc_vec3 position;
    tc_vec3 direction;           /* parallel: the way it shines (faces turned against it are lit) */
    float color[4];
} tc_light;

typedef struct tc_level {
    char converter[16];          /* the converter's stamp (STMP), for log lines */
    uint32_t light_set_count;    /* baked sets per vertex, 1-3 */
    float fog_near, fog_far;     /* linear fog by view depth (as the original's driver); fog_far <= fog_near: none */
    float fog_rgb[3];
    uint32_t light_perso_count;  /* .ttlv LAIS: sud0's day / night cycle, Land's lightning (SetLightSet callers) */
    tc_light_perso *light_persos;
    char *lightmap_atlas;        /* per-triangle lightmaps packed into one image, path relative to the .ttlv (NULL: none) */
    uint32_t texture_count;
    char **textures;             /* NUL-terminated UTF-8 paths relative to the .ttlv */
    uint32_t mesh_count;
    tc_mesh *meshes;             /* static world, engine space, Z up */
    uint32_t perso_count;
    tc_perso *persos;
    uint32_t collision_triangle_count;
    tc_vec3 *collision;          /* 3 vertices per triangle, world space */
    tc_floor *collision_floors;  /* one per triangle */
    uint32_t collision_crc;      /* CRC-32 of the COLL chunk: a .ttai's per-triangle tables match when its LINK holds it */
    int has_ed;
    tc_ed ed;
    tc_sectors sectors;          /* .ttlv SECT (count 0: none, e.g. a level made by another tool) */
    int has_lighting;            /* .ttlv LITE was there: the lights, the sectors' lights and fogs, the materials, the */
    uint32_t light_count;        /* centres (without it only the port's old lighting works) */
    tc_light *lights;
} tc_level;

/* Parses `size` bytes at `data`. On failure *level is left empty (safe to tc_level_free). */
tc_result tc_level_load(tc_level *level, const uint8_t *data, size_t size);
void tc_level_free(tc_level *level);

/* Axis-aligned bounds of the static meshes' vertices (zero when there are none). */
void tc_level_bounds(const tc_level *level, tc_vec3 *lo, tc_vec3 *hi);

/* Whether a level (file name without extension, any case: "bar") is one of the game's cutscene-only levels: bar,
 * death, mammyon, outro, pharmmap, pyr_fou, SpyCarota, spynorth (per the user). They stay explorable; level
 * pickers list them after the playable ones. */
int tc_level_is_cutscene(const char *name);
/* The level a cutscene level hands over to (its character's ChangeLevel: bar -> cock01, mammyon -> lecanyon, pharmmap
 * -> marmite, pyr_fou -> pyramide, SpyCarota -> carota, spynorth -> north), or NULL. Until cutscenes play, portals
 * into a cutscene level go straight on to this one (tc_viewer). */
const char *tc_level_after_cutscene(const char *name);

/* Index of Ed's animation called `name`, or -1. */
int tc_ed_find_anim(const tc_ed *ed, const char *name);

#ifdef __cplusplus
}
#endif
#endif
