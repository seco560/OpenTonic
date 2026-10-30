/* The level's sectors (.ttlv SECT, tools/ttre/ttlv.py; docs/NOTES.md "Sectors"), as the exe uses them:
 *   - which sector a perso is in: on level load and after a teleport, the sector whose floor lies nearest straight below
 *     the point (FUN_00451240); then every frame, from its last position to its new one through the portal faces of its
 *     sector into the next (FUN_00451670, from GAM_0042c810). tc_sector_track keeps one up to date;
 *   - the water: a sector's surfaces are unbounded planes within it, looked up in the perso's own sector
 *     (FUN_00451b90: tc_sectors_env);
 *   - what is drawn: the frame is drawn from the camera's sector, with the sectors of its list (FUN_00451d80, list mode
 *     0). Static triangles belong to membership groups (a sector's geometry, or a water surface drawn when one of the
 *     sectors listing it is), as index ranges of each static mesh; persos to their sector.
 * The data only says what is where: each mode chooses what to draw (the Tonic Viewer draws every sector). */
#ifndef TONIC_TC_SECTOR_H
#define TONIC_TC_SECTOR_H
#include <stdbool.h>
#include <stdint.h>
#include "tc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_SECTOR_NONE 0xFFFFu   /* no sector (unknown, or a portal leading nowhere) */
#define TC_GROUP_WATER 1u        /* tc_sector_group.flags: a water surface */

/* A mechanical environment (MecEnv): env k of a sector lies below k of its surfaces. */
typedef struct tc_sector_env {
    float gravity, density, drag; /* MecEnv +0, +0x14, +0x10 */
    uint32_t water;               /* MecEnv +0x28: 1 in water (the scripts' water zones); 0xFFFFFFFF: no environment */
    /* The fog while the camera is in this env (its visual environment; .ttlv LITE, without it none): linear by view
     * depth from none at near to full at far. */
    uint32_t fog;                 /* 0: off */
    float fog_near, fog_far, fog_rgb[3];
} tc_sector_env;

/* Crossed from the side its normal points to (the sector it belongs to) into target. */
typedef struct tc_portal {
    tc_vec3 v[3], normal;
    uint16_t target;              /* TC_SECTOR_NONE: a dead end (the crossing stops) */
} tc_portal;

typedef struct tc_sector_link { uint16_t sector, draw; } tc_sector_link; /* draw > 0: its static geometry too */

typedef struct tc_sector {
    char *name;                   /* NUL-terminated, e.g. "sctsud19" */
    uint8_t is_virtual;           /* sky, foliage, gates: never found by tc_sectors_find (sector data +0x88) */
    uint8_t no_child_lookup;      /* its IPOs are not looked up on (+0x89) */
    uint16_t visible_count;
    tc_sector_link *visible;      /* drawn with it when the frame is drawn from it (+0x4C), itself not included */
    uint16_t env_count;
    tc_sector_env *envs;
    uint16_t surface_count;
    uint16_t *surfaces;           /* into tc_sectors.surface_z, in the order the lookup walks them */
    uint32_t portal_count;
    tc_portal *portals;
    uint32_t floor_count;
    tc_vec3 *floors;              /* 4 per face: v0, v1, v2, the stored face normal; faces up only */
    uint16_t light_count;
    uint16_t *lights;             /* into tc_level.lights (.ttlv LITE): lit with these when the frame is drawn from it */
} tc_sector;

typedef struct tc_sector_group {
    uint16_t count, flags;        /* TC_GROUP_WATER */
    uint16_t *sectors;
} tc_sector_group;

/* A run of a static mesh's indices with one group (TC_SECTOR_NONE: drawn always). */
typedef struct tc_sector_range { uint16_t group; uint32_t first, count; } tc_sector_range;

typedef struct tc_sectors {
    uint16_t count;               /* 0: the level has no sectors (everything drawn, no water) */
    tc_sector *sectors;
    uint16_t surface_count;
    float *surface_z;
    uint16_t group_count;
    tc_sector_group *groups;      /* group i < count is sector i's own geometry */
    uint32_t mesh_count;          /* = the level's static meshes, 0 when the ranges did not match them */
    uint32_t *mesh_first_range;   /* mesh_count + 1: mesh i's ranges are [mesh_first_range[i], mesh_first_range[i + 1]) */
    tc_sector_range *ranges;
    uint32_t perso_count;         /* = the level's persos, 0 when it did not match */
    uint16_t *perso_sector;       /* TC_SECTOR_NONE: unknown, drawn always */
} tc_sectors;

/* FUN_00451240: the non-virtual sector with a floor face nearest straight below p (the faces its normal turns up),
 * TC_SECTOR_NONE when none is. */
uint16_t tc_sectors_find(const tc_sectors *s, tc_vec3 p);
/* FUN_00451670: moving from `from` to `to` starting in `sector`, the sector reached: through the nearest portal face of
 * the current sector the segment crosses from its front, again from the crossing point in the next, until none is
 * crossed or the nearest leads nowhere (or back into the same sector). */
uint16_t tc_sectors_cross(const tc_sectors *s, uint16_t sector, tc_vec3 from, tc_vec3 to);
/* FUN_00451b90: in `sector`, the environment at p: env k when p is below the first k surfaces of the sector's list
 * (walked in order, stopping at the first p is not below); returns the depth below the last of them (> 0), 0 above the
 * first. No sector, or none listed: the air, 0. env->water 0xFFFFFFFF: the sector names no environment there. */
float tc_sectors_env(const tc_sectors *s, uint16_t sector, tc_vec3 p, tc_sector_env *env);
/* The index of that env in the sector's list (TC_SECTOR_NONE: no sector, or the list is shorter), *depth (may be NULL)
 * as above: FUN_00451c40, the env whose visual environment gives the frame its fog (the camera's position). */
uint16_t tc_sectors_env_index(const tc_sectors *s, uint16_t sector, tc_vec3 p, float *depth);
/* What a mode draws of each sector, one byte per sector: TC_SHOWN_SECTOR its characters and the water surfaces it
 * lists, TC_SHOWN_GEOMETRY its static geometry too (FUN_004521b0: a listed sector's link draw > 0; the sector drawn from
 * has both). */
#define TC_SHOWN_SECTOR 1u
#define TC_SHOWN_GEOMETRY 2u
/* Whether group g is drawn: a water group when one of its sectors has TC_SHOWN_SECTOR, a sector's geometry when it has
 * TC_SHOWN_GEOMETRY. TC_SECTOR_NONE (or out of range): always. */
bool tc_sectors_group_shown(const tc_sectors *s, uint16_t g, const uint8_t *shown);
const char *tc_sectors_name(const tc_sectors *s, uint16_t sector); /* "" for none */

/* A perso's current sector (SectorInfo: +4 the sector, +0xC the last position). */
typedef struct tc_sector_track {
    uint16_t sector;              /* TC_SECTOR_NONE until placed (or in a level without sectors) */
    tc_vec3 position;
    uint32_t placed;              /* the tc_player.placed it was placed at (tc_ed_ai) */
} tc_sector_track;
/* After a teleport (and on load): tc_sectors_find at p; where none is found the sector stays the one it was, or, never
 * placed, the last sector (the exe's fallback, GAM_00427b80: the father sector's last child, "Univers" in sud0). */
void tc_sector_place(tc_sector_track *t, const tc_sectors *s, tc_vec3 p);
/* Every frame after the move: tc_sectors_cross from the last position (placed first if never). */
void tc_sector_move(tc_sector_track *t, const tc_sectors *s, tc_vec3 p);
/* A perso at p: placed when `placed` (tc_player.placed) differs from the last call's or it never was, else moved. */
void tc_sector_follow(tc_sector_track *t, const tc_sectors *s, tc_vec3 p, uint32_t placed);

#ifdef __cplusplus
}
#endif
#endif
