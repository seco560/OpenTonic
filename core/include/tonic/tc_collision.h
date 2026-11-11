/* Static world collision: world-space triangles (Z up) bucketed in a uniform 2D grid over XY.
 * Each triangle is listed in every cell its XY bounding box touches; queries filter by full 3D
 * bounds. Not the original engine's collision (ZDx/MEC); just robust enough to walk on. */
#ifndef TONIC_TC_COLLISION_H
#define TONIC_TC_COLLISION_H
#include <stdbool.h>
#include <stdint.h>
#include "tc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mechanics material of a collision triangle (.ttlv FLOR). grip 1 = normal ground; below 1 the
 * engine keeps momentum and scales the steering by grip^4. rebound: share of the normal speed kept
 * on impact (1 elastic). No material = {1, 1}. */
typedef struct tc_floor { float grip, rebound; } tc_floor;

typedef struct tc_tri {
    tc_vec3 a, b, c;
    tc_vec3 normal; /* unit, from counter-clockwise winding */
    tc_vec3 lo, hi;
    tc_floor floor;
    uint32_t source; /* the triangle's index in the input (degenerate ones are dropped): per-triangle tables (.ttai FSND, CLID) */
    uint16_t sector; /* tc_collision_set_sectors; 0xFFFF: none, always solid */
} tc_tri;

typedef struct tc_collision {
    tc_tri *tris;
    uint32_t tri_count;
    tc_vec3 bounds_min, bounds_max;
    float origin_x, origin_y, cell;
    int32_t nx, ny;
    int32_t *cell_start; /* CSR: triangles of cell i are cell_tris[cell_start[i] .. cell_start[i + 1]) */
    int32_t *cell_tris;
    uint32_t *stamp;     /* per-triangle visit mark: multi-cell triangles are visited once per query */
    uint32_t query;
    /* Non-NULL: queries see only the triangles of the sectors whose byte is set (and those without a sector), as the
     * exe sweeps a perso against its own sector and that sector's collide list (tc_sectors_solid). The caller points
     * it at a row while it moves a perso. */
    const uint8_t *solid;
    uint32_t solid_count;
    /* The triangles of the sectors that have moved (tc_collision_move_sector): out of the grid, tested by every query.
     * loose: their indices in tris; loose_rest: 3 points each, where the export has them; is_loose: per triangle. */
    uint32_t *loose;
    tc_vec3 *loose_rest;
    uint8_t *is_loose;
    uint32_t loose_count;
    /* Perso obstacles (tc_obstacle.h; NULL: none). The mover sets, for the length of its step, who it is: self (not
     * its own obstacle), self_priority (its collide set +0x34: it meets obstacles of a priority up to it) and
     * self_meets (+0x35: without it, no perso at all, MEC_SweepPass_0043c830); ignore = an obstacle it does not meet
     * (its carrier while it is carried, DNM_FollowCarrier_004a83d2). Queries and raycasts see only the static world. */
    struct tc_obstacles *obstacles;
    int32_t self, ignore;
    uint8_t self_priority;
    bool self_meets;
} tc_collision;

typedef struct tc_hit {
    float distance;
    tc_vec3 normal;
    const tc_tri *tri;
} tc_hit;

/* `vertices`: 3 per triangle (vertex_count / 3 triangles); `floors`: one per triangle, or NULL
 * (all {1, 1}). Degenerate triangles are dropped. Returns 0 on success, -1 when out of memory
 * (then *col is empty). */
int tc_collision_init(tc_collision *col, const tc_vec3 *vertices, uint32_t vertex_count, const tc_floor *floors);
void tc_collision_free(tc_collision *col);

/* Each triangle's sector from a per-input-triangle table (tc_sectors.collision_sector; NULL clears them), among
 * sector_count sectors (the length of a solid row). Clears solid. */
void tc_collision_set_sectors(tc_collision *col, const uint16_t *sector, uint32_t count, uint32_t sector_count);
/* A sector's triangles (tc_collision_set_sectors) at m x where the export has them: the scripts' moving sectors (a gate
 * that drops away, a door that turns; tc_cast's sector moves). The first call for a sector takes its triangles out of the
 * grid for good. false: out of memory. */
bool tc_collision_move_sector(tc_collision *col, uint16_t sector, const tc_mat34 *m);

/* Calls fn once for each triangle whose bounds overlap the box lo..hi (and solid allows). Not reentrant. */
typedef void (*tc_tri_fn)(const tc_tri *tri, void *user);
void tc_collision_query(tc_collision *col, tc_vec3 lo, tc_vec3 hi, tc_tri_fn fn, void *user);

/* Nearest hit of the segment o -> o + dir * max_distance (both faces count). 1 = hit. */
int tc_collision_raycast(tc_collision *col, tc_vec3 o, tc_vec3 dir, float max_distance, tc_hit *hit);

/* Closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5). */
tc_vec3 tc_closest_point_on_triangle(tc_vec3 a, tc_vec3 b, tc_vec3 c, tc_vec3 p);

#ifdef __cplusplus
}
#endif
#endif
