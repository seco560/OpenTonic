#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_transition.h"

static uint32_t next(uint32_t *rng) {
    *rng = *rng * 1103515245u + 12345u;
    return (*rng >> 8) & 0xFFFFFF;
}

bool tc_transition_start(tc_transition *t, const tc_ai_transitions *c, bool reveal, uint32_t *rng) {
    const tc_ai_effect *e;
    uint16_t threshold = reveal ? c->reveal_threshold : c->cover_threshold, range = reveal ? c->reveal_range : c->cover_range;
    memset(t, 0, sizeof(*t));
    t->logo = next(rng) % ((uint32_t)range + 1) > threshold; /* FUN_004cb380 / FUN_004cb4b0 */
    e = &c->effects[(reveal ? 2 : 0) + (t->logo ? 1 : 0)];
    if (e->variant_count == 0) return false;
    t->effect = e;
    t->variant = (uint16_t)(next(rng) % e->variant_count); /* FUN_004cb5d0 / FUN_004cb6d0 -> FUN_004cb980 */
    t->value = e->fields[0];
    if (t->logo) {
        t->speed = e->fields[3];
        t->angle = e->fields[6];
    }
    return true;
}

bool tc_transition_step(tc_transition *t, float dt) {
    const float *f;
    float ticks = dt * 1000 / 16;
    if (!t->effect || t->done) return true;
    f = t->effect->fields;
    if (!t->logo) { /* FUN_004cb600: start [0], end [1], speed [3] */
        t->value += f[3] * ticks;
        if ((f[3] > 0 && t->value >= f[1]) || (f[3] < 0 && t->value <= f[1]) || f[3] == 0) {
            t->value = f[1];
            t->done = true;
        }
        return t->done;
    }
    /* FUN_004cb710: speed += acceleration [5], angle += spin [8], distance += speed; end [1] */
    t->speed += f[5] * ticks;
    t->angle += f[8] * ticks;
    t->value += t->speed * ticks;
    if ((t->speed > 0 && t->value >= f[1]) || (t->speed < 0 && t->value <= f[1])) {
        t->value = f[1];
        t->done = true;
    }
    return t->done;
}

void tc_transition_logo_quad(const tc_transition *t, float corners[4][2], float uv[4][2]) {
    static const float k[4] = {1, 0, 2, 3};          /* angle - k pi / 2: GLIGLOU_00441d30 / 50 / 70 / 90 */
    static const float st[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    int i;
    for (i = 0; i < 4; i++) {
        float a = t->angle - k[i] * TC_PI / 2;
        corners[i][0] = 0.5f - cosf(a) * t->value;
        corners[i][1] = sinf(a) * t->value + 0.5f;
        uv[i][0] = st[i][0];
        uv[i][1] = st[i][1];
    }
}
