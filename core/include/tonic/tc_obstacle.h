/* Perso obstacles: each perso's own collision (.ttlv PCOL), posed where the perso is now, for the mechanics sweeps
 * (docs/ENGINE.md "Moving collision, read"). The exe sweeps a mover against the persos of its sector and of that sector's
 * collide list (MEC_SweepPass_0043c830 -> FUN_0043c640): each of their channel objects' ZDR collide object, at the
 * channel's world matrix, tested in that object's axes (FUN_00464240; a uniform scale). The port keeps one part per
 * shown object with its matrix, its inverse and its bounding sphere; tc_mec_sweep (tc_mec_walk.c) tests them.
 *
 * Read from the exe: who meets whom (active and not dead, the collide set's +0x35 on both sides, the priority +0x34: an
 * obstacle is met when its priority <= the mover's), the tests (triangles as the static world, spheres FUN_0046d600,
 * boxes FUN_0046d8a0), the hand point's floor_ok rule (+0x37). Port: the bounding-volume pretest (FUN_00464650) is the
 * port's own (a sphere per part), an obstacle's sector is the one it was placed in unless its owner tracks it. */
#ifndef TONIC_TC_OBSTACLE_H
#define TONIC_TC_OBSTACLE_H
#include <stdbool.h>
#include "tc_level.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_obstacle_part {
    const tc_pcol_object *object;
    tc_mat34 m;                  /* object axes -> world, now */
    tc_mat34 inv;                /* world -> object axes (the rigid part's inverse, divided by the scale) */
    float scale;                 /* uniform (the length of m's first column), as FUN_00464240 takes one */
    tc_vec3 centre;              /* world bounding sphere */
    float radius;
} tc_obstacle_part;

typedef struct tc_obstacle {
    const tc_pcol_perso *data;
    int32_t id;                  /* the perso's id (tc_cast's: level perso k + 1) */
    uint16_t sector;             /* the sector its perso is in (TC_SECTOR_NONE: every mover may meet it) */
    bool live;                   /* active and not dead: sweeps meet it (FUN_0043c640, GAM_00423d20) */
    uint8_t priority, flags;     /* tc_pcol_perso's */
    uint16_t part_count, part_cap;
    tc_obstacle_part *parts;
    tc_vec3 lo, hi;              /* world bounds of the parts */
    tc_mat34 world;              /* the perso's matrix, as last posed */
} tc_obstacle;

typedef struct tc_obstacles {
    const tc_pcol *data;
    uint32_t count;
    tc_obstacle *items;
    int32_t *by_id;              /* perso id -> index, -1 */
    uint32_t id_count;
} tc_obstacles;

/* From the level's PCOL (count 0 without one). Each obstacle starts at its static pose (tc_obstacle_pose_static at its
 * placement), live as the snapshot says. */
tc_result tc_obstacles_init(tc_obstacles *o, const tc_level *level);
void tc_obstacles_free(tc_obstacles *o);
tc_obstacle *tc_obstacles_find(const tc_obstacles *o, int32_t id);
/* Posed from an animation sample: world = perso -> world, transforms[ch] = channel -> perso, shown[ch] = the object the
 * channel shows (TC_HIDDEN none). Objects without collision are skipped. */
void tc_obstacle_pose(tc_obstacle *ob, const tc_mat34 *world, const tc_mat34 *transforms, const uint16_t *shown,
                      uint32_t channel_count);
/* Posed by the export's frame-0 pose (tc_pcol_perso.pose), at world. */
void tc_obstacle_pose_static(tc_obstacle *ob, const tc_mat34 *world);

/* The exe's shape tests, in the obstacle's axes (oracle-checked, tools/exe/oracle.py):
 * FUN_00469500: centre p0 + d t (t in 0..1) touching the fixed point c at distance R (the smaller root, >= 0: a start
 * already within R does not hit); *centre = p0 + d t, *normal = (centre - c) / R. */
int tc_mec_point_sphere(tc_vec3 p0, tc_vec3 d, tc_vec3 c, float R, float *t, tc_vec3 *centre, tc_vec3 *normal);
/* FUN_004679d0 (r > 0; r = 0: FUN_00466600, a point): centre p0 -> p1 (d = p1 - p0) against the axis-aligned box lo..hi
 * grown by r (square corners). The start must be outside it and the start's and end's region codes share no bit;
 * the first face (x-, x+, y-, y+, z-, z+) the start is outside of that the segment crosses within its rectangle:
 * *t, *centre (on the grown face), *normal (the face's axis). */
int tc_mec_sphere_box(tc_vec3 p0, tc_vec3 p1, tc_vec3 d, tc_vec3 lo, tc_vec3 hi, float r, float *t, tc_vec3 *centre,
                      tc_vec3 *normal);

#ifdef __cplusplus
}
#endif
#endif
