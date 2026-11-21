/* Particles (docs/ENGINE.md "Particles"): the exe's particle module (0x47EF00..0x480FB0) and the per-perso particle
 * source (GAM\Actions\MSPrtSrc.c: GAM_0042df50 / 0042e200 / 0042e570 / 0042e5e0), read 2026-11-20. A source (.ttlv
 * PRTC, tc_particle_source) is set on a perso by its scripts (PROC 214, on / off: 212 / 213; tc_ai_perso.particle_*,
 * the camera's in tc_ai_globals). Each game tick, for every perso with a source (the emitters, in the host's order):
 * its field is placed by it (follow), then, while it is on and the field could be placed, the generator puts new
 * particles in; then every emitter's pool moves under its field (acceleration + jitter) and loses the particles that
 * left the field or whose life is over. Drawn after: camera-facing sprites, or lines from the last tick's place
 * (transparent) to this one's (opaque). The pools belong to the sources (the exe's particle system S is the source's),
 * so two persos with one source share one pool. Port: the drawing is interpolated between the last two ticks. */
#ifndef TONIC_TC_PARTICLES_H
#define TONIC_TC_PARTICLES_H
#include <stdbool.h>
#include <stdint.h>
#include "tc_level.h"
#include "tc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DAT_004e275c: the share of each pool used, the options' detail (GAM_00411ce0: 0.15, 0.25, 0.5, 1); the most here. */
#define TC_PARTICLE_DETAIL 1.0f
/* Type 4 (sprites) pre-warm (FUN_0047f260): this many generator passes at once when switched on, the clock +0.02 s
 * each (DAT_004d2c6c). */
#define TC_PARTICLE_PREWARM 70

typedef struct tc_particle_pool {
    uint32_t count;
    tc_vec3 *position, *previous, *velocity; /* previous: before this tick's move (a line's tail) */
    float *death;                            /* the clock's time it goes (s); types 3 and 4 */
    int16_t counter;                         /* G +0xC: emit mode 3 */
    bool warm;                               /* G +0x69 */
    bool deflector_placed;                   /* E +0x3D */
    tc_vec3 box_min, box_max, centre;        /* the field now (E +4, +0x10, +0x1C) */
    tc_vec3 deflector_a, deflector_b;
} tc_particle_pool;

/* A perso with a source, this tick: its place and axes (the camera's: front = -Y, right = -X, up = Z). */
typedef struct tc_particle_emitter {
    int32_t source;          /* PRTC index + 1 (tc_ai_perso.particle_source), 0 none */
    bool on;
    uint32_t serial;         /* tc_ai_perso.particle_serial: a change asks for the pre-warm again */
    tc_mat34 matrix;
    uint16_t sector;         /* where it is (the environment test), TC_SECTOR_NONE none */
} tc_particle_emitter;

/* The environment test (GAM_0042e900 -> MEC_EnvLookup): whether p is in water, *depth below the last surface. */
typedef bool (*tc_particle_water_fn)(void *user, uint16_t sector, tc_vec3 p, float *depth);

#define TC_PARTICLE_MAX_EMITTERS 16
typedef struct tc_particles {
    const tc_level *level;
    tc_particle_pool *pools;  /* one per source */
    uint32_t pool_count;
    uint32_t rng;
    float time;               /* the clock, s (DAT_008bd090 / 1000) */
    tc_particle_emitter emitters[TC_PARTICLE_MAX_EMITTERS]; /* this tick's */
    uint32_t emitter_count;
    uint32_t seen_serial[TC_PARTICLE_MAX_EMITTERS];
    int32_t seen_source[TC_PARTICLE_MAX_EMITTERS];
    tc_particle_water_fn water;
    void *water_user;
} tc_particles;

bool tc_particles_init(tc_particles *ps, const tc_level *level, uint32_t seed);
void tc_particles_free(tc_particles *ps);
/* One game tick (dt s) for these emitters (at most TC_PARTICLE_MAX_EMITTERS; kept for drawing). */
void tc_particles_tick(tc_particles *ps, const tc_particle_emitter *emitters, uint32_t count, float dt);
/* Particles alive in all the emitters' pools. */
uint32_t tc_particles_alive(const tc_particles *ps);

/* A vertex of the drawn particles: world position, uv, rgba (straight alpha). */
typedef struct tc_particle_vertex { float position[3], uv[2], color[4]; } tc_particle_vertex;
/* A run of triangles with one texture and blend. */
typedef struct tc_particle_batch {
    int32_t texture;         /* level texture, -1 none (white) */
    uint32_t additive;       /* 1: added (src alpha, one), 0: alpha-blended */
    uint32_t first, count;   /* vertices (count / 3 triangles) */
} tc_particle_batch;
#define TC_PARTICLE_MAX_BATCHES TC_PARTICLE_MAX_EMITTERS
/* The emitters' pools as triangles, between the last two ticks by t (0..1), facing the camera (eye, right, up);
 * pixel: the width of one pixel at distance 1 (a line's width). hidden[k]: emitter k is not drawn (its ceiling test).
 * Writes at most max_vertices; returns the batch count. */
uint32_t tc_particles_build(const tc_particles *ps, float t, tc_vec3 eye, tc_vec3 right, tc_vec3 up, float pixel,
                            const bool *hidden, tc_particle_vertex *out, uint32_t max_vertices, uint32_t *vertex_count,
                            tc_particle_batch *batches);

#ifdef __cplusplus
}
#endif
#endif
