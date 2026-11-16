/* Custom gameplay, private to core/src/custom: a course's end (.ttai CRSE; tools/ttre/ssx_end.py, written by the custom
 * layer, tools/ttre/custom.py run_finishes). Nothing here is the game's. */
#ifndef TONIC_TCX_COURSE_H
#define TONIC_TCX_COURSE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tonic/custom/tcx.h"

#define TCX_COURSE_NAME 64

typedef struct tcx_course {
    /* the way back: the level, Ed's place and the camera's place there (the course's arch in sud0) */
    char level[TCX_COURSE_NAME], place[TCX_COURSE_NAME], camera_place[TCX_COURSE_NAME];
    tc_vec3 end;                 /* where the racing line ends (in the field) */
    /* the finish field: cells of `cell` units, cell (i, j) centred at (x0 + i cell, y0 + j cell), row-major bits (bit k
     * % 8 of byte k / 8, k = j nx + i); Ed is in it standing in a set cell with z_lo <= z <= z_hi */
    float x0, y0, cell, z_lo, z_hi;
    uint16_t nx, ny;
    uint8_t *mask;
    /* the boost: within boost_radius of its path (boost_count points), on the snow, a push of boost_accel (u/s^2) along
     * the path's nearest leg up to boost_speed (u/s) along it */
    uint32_t boost_count;
    float boost_radius, boost_speed, boost_accel;
    tc_vec3 *boost;
    /* the invisible walls: 3 points per triangle, their fronts towards the field */
    uint32_t wall_count;
    tc_vec3 *walls;
} tcx_course;

/* The .ttai's CRSE chunk: NULL when there is none (or it doesn't read: a layout from another converter). */
tcx_course *tcx_course_read(const void *ai_data, size_t ai_size);
void tcx_course_free(tcx_course *c);
/* p (Ed's feet) is in the finish field. */
bool tcx_course_in_finish(const tcx_course *c, tc_vec3 p);
/* The boost's push this tick (added to ski->force): position his feet, velocity his. */
void tcx_course_boost(const tcx_course *c, tc_mec_ski *ski, tc_vec3 position, tc_vec3 velocity, float mass);
/* The play session's course (NULL none). */
const tcx_course *tcx_play_course(const tcx_play *x);

#endif
