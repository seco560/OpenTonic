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
#include "tc_sound.h"

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
 * color[i]: baked lighting set i, rgba8 little-endian (r in the low byte), each channel the radiosity R as (R + 256) / 2
 * (TC_RADIOSITY; signed, as the exe adds it); alpha 255 = baked, TC_SKY_ALPHA = baked, a sky's (neutral under the
 * port's old lighting), 0 = none (persos: no radiosity).
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
/* A stored colour channel (tc_vertex.color) -> the radiosity, the exe's i16 (-256 .. 254 here; / 256 = its light). */
#define TC_RADIOSITY(byte) ((float)(byte) * 2.0f - 256.0f)

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

/* .ttlv CAST (tools/ttre/cast.py): the level persos whose own AI the core runs (tc_cast.h), as data. A family's states
 * and state animations are shared by its persos; each perso has its scripts (a .ttai SCPT body) and the std game's
 * activity fields of the snapshot. */
typedef struct tc_cast_sound { uint16_t frame, event; uint8_t first, period; } tc_cast_sound;
typedef struct tc_cast_state {
    int16_t next;                /* -1 none; itself = loop */
    uint16_t frames;
    uint8_t speed, flags, repeats; /* frames per second (0: 30); flags 1: only the listed transitions */
    uint16_t anim;               /* index into the family's anims, 0xFFFF none */
    uint16_t transition_count, prohibited_count, sound_count;
    uint16_t *transitions;       /* transition_count pairs { target, via } */
    uint16_t *prohibited;
    tc_cast_sound *sounds;
} tc_cast_state;
typedef struct tc_cast_anim {
    uint16_t channel_count, frame_count;
    float rate;                  /* the A3D's own frames per second (the AI's state clock picks the frame) */
    tc_mat34 *transforms;        /* [frame * channel_count + channel], channel -> perso */
    uint16_t *shown;             /* object shown, TC_HIDDEN none */
} tc_cast_anim;
/* A mechanics ID card (MEC hidcard): type 0-12 picks the engine's handler pair; words = the card after its type
 * word, raw (mostly f32; see docs/NOTES.md "Ed's mechanics ID cards" and "MEC physics"). */
typedef struct tc_ai_card { uint32_t type; uint32_t word_count; uint32_t words[19]; } tc_ai_card;
/* A state's mechanics collision body (the collide set's ZDM zones active in it): spheres in the perso's axes. */
#define TC_AI_MAX_SPHERES 12
/* One part of the mechanics body: a sphere, or a point (point = 1, radius 0: collide element type 5, e.g. Ed's hand
 * point 1.3 ahead and 2.0 up that catches ledges). flags: its collide material identifier (GameMaterial +0xC -> u16
 * +2; a point's from its vertex's material; ZDMF chunk): 0x10 = no collision, 8 = floors only (a separate sweep),
 * 1 = may step up (zone type 1). The material conditions (GroundMaterialIs ...) test the flags of the part that made
 * the contact. See tc_mec_sweep. */
typedef struct tc_ai_sphere {
    tc_vec3 center;
    float radius;
    uint16_t flags;
    uint8_t point;
    float rebound, grip; /* its own mechanics material ([1], [2]; none: 1, 1), times the obstacle's in the responses */
} tc_ai_sphere;
typedef struct tc_ai_sphere_set { uint16_t count; tc_ai_sphere spheres[TC_AI_MAX_SPHERES]; } tc_ai_sphere_set;
/* A perso's ZDE zones (the hit zones: COND 18-29, CollectNearby; tools/ttre/ai.py zde_block): spheres (kind 7: a centre,
 * b.x radius) and boxes (8: a min, b max) in its axes, each with its type (the collide material flags the conditions
 * mask), and per state which zones are active (bit k: zone k, the scripts' zone k + 1). */
typedef struct tc_zde_prim { uint8_t kind; uint16_t type; tc_vec3 a, b; } tc_zde_prim;
typedef struct tc_zde_zone { uint32_t first; uint16_t count; } tc_zde_zone;       /* prims[first .. first + count) */
typedef struct tc_zde_set {
    uint16_t zone_count, state_count;
    tc_zde_zone *zones;
    uint32_t prim_count;
    tc_zde_prim *prims;
    uint32_t *active;            /* per state */
} tc_zde_set;
void tc_zde_set_free(tc_zde_set *z);

typedef struct tc_cast_family {
    uint16_t state_count, anim_count;
    tc_cast_state *states;
    tc_cast_anim *anims;
    uint16_t *cards;             /* .ttlv CMEC: per state, its ID card (tc_cast_data.cards), 0xFFFF none; NULL: none */
    /* CMEC's walker part (tools/ttre/cast.py walk_block, as a .ttai BODY + ZDMF): root motion rows (units per animation
     * frame in its axes), per state its rows and its ZDM body (0xFFFF none); NULL: none. */
    uint32_t root_count;
    tc_vec3 *root;
    uint32_t *root_first;
    uint16_t *root_rows, *body;
    uint16_t body_count;
    tc_ai_sphere_set *bodies;
    tc_zde_set zde;              /* CMEC's ZDE part (none: no zones) */
} tc_cast_family;
/* A ZDD zone of a cast perso (COND 13): spheres (kind 7: a centre, b.x radius) and boxes (8: a min, b max) in its axes. */
#define TC_CAST_ZONE_PRIMS 8
typedef struct tc_cast_zone_prim { uint32_t kind; tc_vec3 a, b; } tc_cast_zone_prim;
typedef struct tc_cast_zone { uint16_t index, count; tc_cast_zone_prim prims[TC_CAST_ZONE_PRIMS]; } tc_cast_zone;
typedef struct tc_cast_entry {
    uint16_t id;                 /* as perso nodes: level perso k + 1 */
    uint16_t prso;               /* its entry in tc_level.persos (drawn with its parts, keyed by object), 0xFFFF none */
    uint16_t family, sector, initial_state;
    uint8_t hit_points, hit_points_max, active, dead, mechanics, animation, reinit; /* std game +0x14 ... +0x2B */
    uint8_t flags;               /* TC_CAST_MAY_FORCE_MAIN, _SAVE_POINT, _ALWAYS_AWAKE, _RUN_FIRST, _SCENE */
    uint16_t flags2;             /* TC_CAST2_MOVES_MAIN, _DIRECTOR (0 in exports before 2026-11-13) */
    float radius2;               /* std game +0x2C: -1 always, else the squared activation radius */
    tc_mat34 world;
    uint16_t zone_count;
    float zone_inverse[12];      /* world -> its axes, row-major 3x4 */
    tc_cast_zone *zones;         /* index = k of COND_13(Me, k, ..) */
    uint32_t size;
    uint8_t *scripts;            /* for tc_ai_model_load_scripts */
    uint32_t tables_size;        /* TC_CAST_TABLES: the strings and constant vectors they index (tc_ai_model_load_tables) */
    uint8_t *tables;
    /* .ttlv CMEC: its own sweep body for its mechanics (its collide set's single sphere at the origin, +0x36: radius
     * +0x30 / 2; 0 none) and the set's priority (+0x34) and +0x35 (it meets persos) */
    float own_radius;
    uint8_t own_priority, own_meets;
    uint16_t family_type;        /* CMEC: its family's index (std game +0; what T22 nodes hold), 0xFFFF unknown */
} tc_cast_entry;
#define TC_CAST_MAY_FORCE_MAIN 1u /* its PROC 237 / 238 may set the main actor's behaviours (tools/ttre/cast.py) */
#define TC_CAST_SAVE_POINT 2u     /* a save point: when it takes itself out, the main actor's checkpoint is where he is */
#define TC_CAST_ALWAYS_AWAKE 4u   /* on the engine's always-awake list (gpt -> 0x8c172c): runs whatever sector the main
                                     character is in (World, HEG_Inventaire, the bee counters) */
#define TC_CAST_RUN_FIRST 8u      /* on the engine's run-first list (gpt -> 0x8c1738): stepped before the others (the
                                     platforms, lifts, crates: what others stand on or push) */
#define TC_CAST_TALK 128u         /* port: a hint scene's speaker (cast.py SCENE_SPEAKERS): asleep until the main actor
                                     talks to it, then its scripts run, briskly (docs/ENGINE.md "Walkable scenes") */
#define TC_CAST_SCENE 16u         /* port: a scene's actor (the Special Edition's outro, cast.py SE_CAST_LEVELS): its
                                     removals and deactivations take wherever the main actor is (no removal guard) */
#define TC_CAST_SPAWN 64u         /* a spawnable template (LevelData.spawnables): out until a script instances it (META 11) */
#define TC_CAST2_MOVES_MAIN 1u    /* flags2: a portal or level changer (an arch, a passage, a zone teleport, Doc's way
                                     home ...: tools/ttre/cast.py portal_ids, level_changer_ids): it moves the main actor
                                     elsewhere, by its own scripts (docs/PORTALS.md "The arches run themselves") */
#define TC_CAST2_DIRECTOR 2u       /* flags2: a camera director (GenD_Cam): it picks the camera's shots (docs/CAMERA.md
                                     "The directors' shots") */
#define TC_CAST_TABLES 32u        /* the entry carries its scripts' strings and constant vectors (tables_size, tables) */
/* A perso's inventory object (perso +0x34 -> { u16 amount, i16 slot }; .ttlv INVT): picked up, `amount` goes onto
 * save value `slot` (PROC 142, 0x419310). */
typedef struct tc_ai_inv_entry { uint16_t id; int16_t slot; uint16_t amount; } tc_ai_inv_entry;

typedef struct tc_cast_data {
    uint16_t main_actor;         /* the player's perso id */
    uint16_t main_alias;         /* the fix Ed where another perso is the player (the skier): the same as him, 0 none */
    uint16_t world;              /* the world perso's id (the World keyword, KW 6), 0 none */
    uint16_t family_count, count;
    tc_cast_family *families;
    tc_cast_entry *entries;
    uint16_t inv_count;          /* .ttlv INVT: the persos' inventory objects (tc_ai.h), by perso id */
    tc_ai_inv_entry *inv;
    tc_cast_entry main_zones;    /* the main actor's ZDD zones (zones, zone_count only: in his moving axes) */
    uint16_t ghost_count;        /* the persos the cast's scripts name that do not run: their dsgvars, shared */
    struct tc_cast_ghost { uint16_t id; tc_vec3 position; uint32_t size; uint8_t *dsg; } *ghosts;
    uint16_t card_count;         /* .ttlv CMEC: the families' states' ID cards (the cast's mechanics, tc_cast_mec.c) */
    tc_ai_card *cards;
} tc_cast_data;

/* .ttlv SPCH (tools/ttre/speech.py; docs/ENGINE.md "Speech"): the level's dialogues and the engine's font FontLips. */
typedef struct tc_glyph {
    uint16_t code;
    float step, advance, shift;  /* glyph +4, +8; its table entry's +4 */
    float half_w, half_h;        /* its quad, font units */
    float u0, v0, u1, v1;
} tc_glyph;
typedef struct tc_dialogue_event { uint32_t frame, state; } tc_dialogue_event;
#define TC_DIALOGUE_VOICE_NONE 0xFFFFu
#define TC_DIALOGUE_CHANNELS 10
typedef struct tc_dialogue {
    uint16_t frames, rate;       /* the lip table's: frames / rate is how long the line is */
    uint16_t voice;              /* its sound event, TC_DIALOGUE_VOICE_NONE none */
    uint8_t channel_count, channels[TC_DIALOGUE_CHANNELS]; /* the channels the mouth shapes go on (0xFF any) */
    uint16_t event_count;
    uint32_t window[4];          /* top, bottom, left, right, percent of the frame */
    uint8_t *phonemes;           /* frames x 3 */
    char *text;                  /* the game's 8-bit codes, NUL-terminated; NULL none */
    tc_dialogue_event *events;   /* the speaker goes to `state` at `frame` (GAM_00437260) */
} tc_dialogue;
typedef struct tc_speech_data {
    uint32_t dialogue_count, glyph_count;
    tc_dialogue *dialogues;
    tc_glyph *glyphs;
    float spacing;
    uint16_t atlas_w, atlas_h;
    uint8_t panel[4];            /* the window's backdrop */
    bool has_arrival;            /* a hint scene (docs/ENGINE.md "Walkable scenes"): Ed starts where its own Ed stands */
    tc_vec3 arrival;
    uint8_t *atlas;              /* rgba, rows top first */
} tc_speech_data;

/* .ttlv PCOL (tools/ttre/ttlv.py perso_collision; docs/ENGINE.md "Moving collision, read"): each perso's own collision,
 * in its objects' axes (the object-list scale applied), posed where the perso is by the core (tc_obstacle.h). Kinds are
 * the collide element types: 1 a triangle (a, b, c; n its unit normal from the winding), 7 a sphere (a the centre, r),
 * 8 an axis-aligned box (a min, b max). */
#define TC_PCOL_TRIANGLE 1u
#define TC_PCOL_SPHERE 7u
#define TC_PCOL_BOX 8u
typedef struct tc_pcol_prim {
    uint8_t kind, sound;         /* sound: GameMaterial +8 (the footstep switch), 0xFF none */
    uint16_t collide_id;         /* the collide material's identifier (GameMaterial +0xC -> +2) */
    tc_floor floor;              /* its mechanics material (none: {1, 1}) */
    tc_vec3 a, b, c, n;
    float r;
} tc_pcol_prim;
typedef struct tc_pcol_object {
    uint16_t object;             /* object-list index: drawn by the channels that show it */
    uint32_t first, count;       /* its primitives in tc_pcol.prims */
    tc_vec3 centre;              /* a bounding sphere of them, object axes */
    float radius;
} tc_pcol_object;
typedef struct tc_pcol_pose { uint16_t object; tc_mat34 m; } tc_pcol_pose; /* object -> perso, the initial frame 0 */
#define TC_PCOL_MEETS_PERSOS 1u  /* collide set +0x35: its sweeps meet other persos; as an obstacle, others may meet it */
#define TC_PCOL_FLOOR_OK 2u      /* collide set +0x37: the hand point (flag-8 sweep) may land on it */
#define TC_PCOL_CARRIES 4u       /* std game +0x31: persos standing on it ride it (become its children) */
#define TC_PCOL_HAS_SET 8u       /* it has a collide set (without one: priority 0, always met) */
#define TC_PCOL_ACTIVE 16u       /* std game +0x18 at load */
#define TC_PCOL_DEAD 32u         /* std game +0x17 at load */
#define TC_PCOL_CAST 64u         /* in the cast: posed by its actor every tick (else by `pose`, once) */
typedef struct tc_pcol_perso {
    uint16_t id, prso, sector;
    uint8_t priority, flags;     /* collide set +0x34 (0 without one); TC_PCOL_* */
    tc_mat34 world;
    uint16_t object_count, pose_count;
    tc_pcol_object *objects;
    tc_pcol_pose *pose;
} tc_pcol_perso;
typedef struct tc_pcol {
    uint8_t main_priority, main_flags; /* the player's own collide set (his sweeps: +0x35 must be set to meet persos) */
    uint16_t count;
    tc_pcol_perso *persos;
    uint32_t prim_count;
    tc_pcol_prim *prims;
} tc_pcol;

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

/* .ttlv WAYS (tools/ttre/ai.py ways_chunk; docs/ENGINE.md "Ways and waypoints"): the level's waypoints, numbered from 1
 * as the scripts hold them (node type 29, waypoint dsgvars; 0 none), and the persos' ways (perso +0x1C, the MS_Way:
 * GoOverWay's routes), each a list of waypoint numbers. */
typedef struct tc_waypoint {
    tc_vec3 position;            /* world, or in the father's axes */
    float radius;                /* +0x10: where the GoTo meta-actions arrive */
    uint16_t father;             /* the perso it moves with (perso id), 0 none */
} tc_waypoint;
typedef struct tc_way { uint32_t first; uint16_t count; } tc_way;              /* points[first .. first + count) */
typedef struct tc_perso_ways { uint16_t perso, count; uint32_t first; } tc_perso_ways; /* ways[first .. first + count) */
/* A perso's own sound rolloff (.ttlv SND3; perso +0x40, tc_sound.h), by its id (as CAST's). */
typedef struct tc_level_rolloff {
    uint16_t perso;
    tc_sound_rolloff rolloff;
} tc_level_rolloff;

typedef struct tc_ways {
    uint16_t waypoint_count;
    tc_waypoint *waypoints;      /* number k is waypoints[k - 1] */
    uint16_t perso_count;
    tc_perso_ways *persos;
    uint32_t way_count;
    tc_way *ways;
    uint32_t point_count;
    uint16_t *points;            /* waypoint numbers */
} tc_ways;
/* The ways of a perso (by id), or NULL. */
const tc_perso_ways *tc_ways_of(const tc_ways *w, int32_t perso);

typedef struct tc_level {
    char converter[16];          /* the converter's stamp (STMP), for log lines */
    uint64_t data_hash;          /* the file's content without the stamp and paths (recordings: tc_rec.h) */
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
    tc_cast_data cast;           /* .ttlv CAST (count 0: none) */
    tc_pcol pcol;                /* .ttlv PCOL (count 0: none; older files baked the persos' collision into COLL) */
    tc_ways ways;
    uint32_t sound_rolloff_count; /* .ttlv SND3 (absent: none): the persos with a sound rolloff of their own */
    tc_level_rolloff *sound_rolloffs;
    tc_speech_data speech;       /* .ttlv SPCH (optional) */                /* .ttlv WAYS (none: no waypoints; the way primitives then do nothing) */
    uint32_t edition;            /* .ttlv EDIT: 0 retail, 1 the Special Edition (tools/ttre/se.py; docs/SE.md) */
    struct tcx_level *custom;    /* custom gameplay's data, not the game's (core/include/tonic/custom/tcx.h: the custom
                                    layer's cars, .ttlv VEHI); NULL: none */
    int has_lighting;            /* .ttlv LITE was there: the lights, the sectors' lights and fogs, the materials, the */
    uint32_t light_count;        /* centres (without it only the port's old lighting works) */
    tc_light *lights;
} tc_level;

#define TC_EDITION_RETAIL 0u
#define TC_EDITION_SPECIAL 1u /* the 1998 Special Edition disc (build 8.5.2) */

/* Parses `size` bytes at `data`. On failure *level is left empty (safe to tc_level_free). */
tc_result tc_level_load(tc_level *level, const uint8_t *data, size_t size);
void tc_level_free(tc_level *level);

/* The sound rolloff of the perso with that id (.ttlv SND3), or NULL: the default one (tc_sound.h). */
const tc_sound_rolloff *tc_level_sound_rolloff(const tc_level *level, int32_t perso);

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
