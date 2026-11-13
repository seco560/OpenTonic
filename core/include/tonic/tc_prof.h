/* Profiling hook: where a frame's time goes, stage by stage. The core has no clock of its own (no platform code), so the
 * host lends it one, as it lends the allocator (tc_alloc.h). Without one every zone costs a branch and nothing is kept.
 * Zones nest (a zone's time includes the zones inside it); counters count work (collision queries, triangles tested).
 * Process-wide like the allocator: one viewer at a time is measured. Used by tests/perfcheck.c; a frontend may show it. */
#ifndef TONIC_TC_PROF_H
#define TONIC_TC_PROF_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tc_prof_zone {
    TC_PZ_UPDATE,         /* tc_viewer_update, whole */
    TC_PZ_TICKS,          /* Ed's AI and the cast, every 30 Hz tick of the frame (step_ai) */
    TC_PZ_CAST_FIRST,     /*   the run-first persos (lifts, platforms) */
    TC_PZ_ED,             /*   Ed's scripts and physics */
    TC_PZ_CAST_REST,      /*   every other perso */
    TC_PZ_CUSTOM,         /* custom gameplay (a car) */
    TC_PZ_SIMPLE,         /* the simple controller */
    TC_PZ_CAMERA,         /* the play camera */
    TC_PZ_SIGHT,          /*   its sight test against the drawn persos (camera_sight) */
    TC_PZ_LIGHTING,       /* sectors, lights, fog */
    TC_PZ_PORTALS,        /* portals, transitions, the light persos */
    TC_PZ_DRAWS,          /* tc_viewer_draws, whole */
    TC_PZ_DRAW_STATICS,   /*   the level's statics and pieces */
    TC_PZ_DRAW_PERSOS,    /*   the persos (and Ed) */
    TC_PZ_DRAW_SORT,      /*   the blended draws back to front */
    TC_PZ_DRAW_CULL,      /* the culling of tc_viewer_set_aspect (levels without sectors) */
    TC_PZ_COUNT
} tc_prof_zone;

typedef enum tc_prof_counter {
    TC_PC_COLLISION_QUERIES,  /* tc_collision_query calls */
    TC_PC_COLLISION_CELLS,    /*   grid cells visited */
    TC_PC_COLLISION_TRIS,     /*   triangles handed to the caller (after the box test) */
    TC_PC_COLLISION_LOOSE,    /*   moved sectors' triangles tested one by one */
    TC_PC_SIGHT_TRIS,         /* triangles the camera's sight test transformed and crossed */
    TC_PC_COUNT
} tc_prof_counter;

typedef double (*tc_prof_clock)(void *user); /* seconds, monotonic */

typedef struct tc_prof_stats {
    double seconds[TC_PZ_COUNT];
    uint32_t calls[TC_PZ_COUNT];
    uint64_t counts[TC_PC_COUNT];
} tc_prof_stats;

/* Install a clock (NULL: profiling off, the default). Clears what was kept. */
void tc_prof_set_clock(tc_prof_clock clock, void *user);
bool tc_prof_enabled(void);
/* What was kept since the last reset; reset: clear it after reading. */
void tc_prof_read(tc_prof_stats *out, bool reset);
const char *tc_prof_zone_name(tc_prof_zone z);
const char *tc_prof_counter_name(tc_prof_counter c);

#ifdef __cplusplus
}
#endif
#endif
