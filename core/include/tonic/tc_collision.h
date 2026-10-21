/* Static world collision: world-space triangles (Z up) bucketed in a uniform 2D grid over XY.
 * Each triangle is listed in every cell its XY bounding box touches; queries filter by full 3D
 * bounds. Not the original engine's collision (ZDx/MEC); just robust enough to walk on. */
#ifndef TONIC_TC_COLLISION_H
#define TONIC_TC_COLLISION_H
#include <stdint.h>
#include "tc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_tri {
    tc_vec3 a, b, c;
    tc_vec3 normal; /* unit, from counter-clockwise winding */
    tc_vec3 lo, hi;
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
} tc_collision;

typedef struct tc_hit {
    float distance;
    tc_vec3 normal;
    const tc_tri *tri;
} tc_hit;

/* `vertices`: 3 per triangle (vertex_count / 3 triangles). Degenerate triangles are dropped.
 * Returns 0 on success, -1 when out of memory (then *col is empty). */
int tc_collision_init(tc_collision *col, const tc_vec3 *vertices, uint32_t vertex_count);
void tc_collision_free(tc_collision *col);

/* Calls fn once for each triangle whose bounds overlap the box lo..hi. Not reentrant. */
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
