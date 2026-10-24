/* The game's screen transitions (docs/PORTALS.md "Screen transitions"): a cover before a teleport or a level change,
 * a reveal after. The parameters are fix data, exported into every .ttai (tc_ai_transitions). Two effects, picked at
 * random (r in [0, range]: r > threshold -> B), each with texture variants picked at random:
 *   A (FUN_004cb600): a full-viewport black quad with one of the Alpha masks, drawn where the mask's alpha <= value
 *     (Direct3D alpha test LESSEQUAL); the value runs from start to end at speed per 16 ms (cover 0 -> 1 in 1.6 s);
 *   B (FUN_004cb710): a logo (Rotate1/2: Ubi Soft, Tonic Trouble) on a square centred on the viewport, spinning, its
 *     corner distance growing (cover) or shrinking (reveal) with an accelerating speed.
 * Times run in the exe's units: value += rate x ms / 16. */
#ifndef TONIC_TC_TRANSITION_H
#define TONIC_TC_TRANSITION_H
#include <stdbool.h>
#include <stdint.h>
#include "tc_ai.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_transition {
    const tc_ai_effect *effect;  /* NULL: none running */
    bool logo;                   /* effect B */
    bool done;
    uint16_t variant;            /* index into effect->variants */
    float value;                 /* A: the alpha threshold (0..1); B: the corner distance (viewport units) */
    float speed, angle;          /* B */
} tc_transition;

/* Starts a cover (reveal = false) or a reveal with random effect and variant (rng: any state, advanced). false when
 * the model has no transitions (then treat it as done at once). */
bool tc_transition_start(tc_transition *t, const tc_ai_transitions *c, bool reveal, uint32_t *rng);
/* Advances by dt seconds; returns done. */
bool tc_transition_step(tc_transition *t, float dt);
/* Effect B's quad: corners in viewport units (0..1, y down) and their texture coordinates, in the exe's order (two
 * triangles 0 1 2, 1 3 2). */
void tc_transition_logo_quad(const tc_transition *t, float corners[4][2], float uv[4][2]);

#ifdef __cplusplus
}
#endif
#endif
