#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_mec.h"

/* Card types 0 and 4 (docs/NOTES.md "MEC collisions"). Card fields used (Ed's ground card / air cards):
 * +4 mass 120, +8 step height 1 / 0, +0x1C..+0x28 the contact classes by normal . up (cos 50 / 70 / 120 / 170 deg:
 * ground, steep slope, wall, overhang, else ceiling), +0x2C sin of how far a wall may lean and still be stepped
 * onto (7 deg), +0x30 cos of how flat the step's top must be (20 deg), +0x34 re-snap distance 0.4 / 0.3 at 30 fps,
 * +0x38 gravity factor 1 / 3.5 / 3. */

#define PUSH_OUT 0.01f       /* DAT_004d3010: every contact moves the target this far out along the normal */
#define HEAD_ON -0.70710677f /* DAT_004d3004: velocity . normal / |velocity| below this = head-on */
#define RISE 0.35f           /* DAT_004d301c */
#define PASSES 6             /* MEC_004be1bd */

static const tc_vec3 UP = {0, 0, 1};

void tc_mec_walk_reset(tc_mec_walk *w) {
    memset(w, 0, sizeof(*w));
    w->ground_normal = UP;
    w->wall_normal = tc_v3(1, 0, 0);
    w->ground_grip = 1;
}

uint32_t tc_mec_walk_contacts(const tc_mec_walk *w) { return w->report | w->report_prev; }

void tc_mec_walk_air(tc_mec_walk *w) {
    w->report = 0;
    w->report_prev = 0;
}

static tc_vec3 turn(float heading, tc_vec3 v) {
    float c = cosf(heading), s = sinf(heading);
    return tc_v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

/* ---- Sweep ---- */

typedef struct sweep_ctx { tc_vec3 c0, d; float r; tc_mec_hit best; bool found; } sweep_ctx;

static void take(sweep_ctx *x, float t, tc_vec3 n, tc_vec3 p, const tc_tri *tri) {
    if (x->found && t >= x->best.t) return;
    x->found = true;
    x->best.t = t;
    x->best.normal = n;
    x->best.point = p;
    x->best.tri = tri;
}

/* First t >= 0 at which c0 + d t is within r of segment ab (ray against a capsule), or -1. */
static float ray_capsule(tc_vec3 o, tc_vec3 d, tc_vec3 a, tc_vec3 b, float r) {
    tc_vec3 ba = tc_v3_sub(b, a), oa = tc_v3_sub(o, a);
    float baba = tc_v3_dot(ba, ba), bard = tc_v3_dot(ba, d), baoa = tc_v3_dot(ba, oa);
    float dd = tc_v3_dot(d, d), doa = tc_v3_dot(d, oa), oaoa = tc_v3_dot(oa, oa);
    float qa = baba * dd - bard * bard, best = -1;
    int k;
    if (qa > 1e-12f) {
        float qb = baba * doa - baoa * bard, qc = baba * oaoa - baoa * baoa - r * r * baba, h = qb * qb - qa * qc;
        if (h >= 0) {
            float t = (-qb - sqrtf(h)) / qa, y = baoa + t * bard;
            if (t >= 0 && y > 0 && y < baba) return t;
        }
    }
    for (k = 0; k < 2; k++) { /* the end spheres */
        tc_vec3 oc = tc_v3_sub(o, k ? b : a);
        float sb = tc_v3_dot(d, oc), sc = tc_v3_dot(oc, oc) - r * r, h = sb * sb - dd * sc;
        if (h >= 0 && dd > 1e-12f) {
            float t = (-sb - sqrtf(h)) / dd;
            if (t >= 0 && (best < 0 || t < best)) best = t;
        }
    }
    return best;
}

static tc_vec3 closest_on_segment(tc_vec3 a, tc_vec3 b, tc_vec3 p) {
    tc_vec3 ab = tc_v3_sub(b, a);
    float l = tc_v3_dot(ab, ab), t = l > 0 ? tc_v3_dot(tc_v3_sub(p, a), ab) / l : 0;
    return tc_v3_add(a, tc_v3_scale(ab, fminf(fmaxf(t, 0), 1)));
}

static bool inside(const tc_tri *t, tc_vec3 n, tc_vec3 p) {
    return tc_v3_dot(tc_v3_cross(tc_v3_sub(t->b, t->a), tc_v3_sub(p, t->a)), n) >= 0 &&
           tc_v3_dot(tc_v3_cross(tc_v3_sub(t->c, t->b), tc_v3_sub(p, t->b)), n) >= 0 &&
           tc_v3_dot(tc_v3_cross(tc_v3_sub(t->a, t->c), tc_v3_sub(p, t->c)), n) >= 0;
}

static void sweep_tri(const tc_tri *t, void *user) {
    sweep_ctx *x = (sweep_ctx *)user;
    tc_vec3 q = tc_closest_point_on_triangle(t->a, t->b, t->c, x->c0), diff = tc_v3_sub(x->c0, q), n = t->normal;
    float d2 = tc_v3_length_sq(diff), dist0, dn;
    const tc_vec3 *e[3][2];
    int k;
    if (d2 < x->r * x->r) { /* already touching: a contact only when moving into it */
        tc_vec3 m = d2 > 1e-12f ? tc_v3_div(diff, sqrtf(d2)) : (tc_v3_dot(n, diff) < 0 ? tc_v3_scale(n, -1) : n);
        if (tc_v3_dot(x->d, m) < 0) take(x, 0, m, q, t);
        return;
    }
    dist0 = tc_v3_dot(n, tc_v3_sub(x->c0, t->a));
    if (dist0 < 0) {
        n = tc_v3_scale(n, -1);
        dist0 = -dist0;
    }
    dn = tc_v3_dot(x->d, n);
    if (dn < 0 && dist0 >= x->r) { /* the face */
        float tt = (dist0 - x->r) / -dn;
        tc_vec3 p = tc_v3_sub(tc_v3_add(x->c0, tc_v3_scale(x->d, tt)), tc_v3_scale(n, x->r));
        if (tt <= 1 && inside(t, t->normal, p)) { /* the winding's normal: n may be flipped */
            take(x, tt, n, p, t);
            return;
        }
    }
    e[0][0] = &t->a; e[0][1] = &t->b;
    e[1][0] = &t->b; e[1][1] = &t->c;
    e[2][0] = &t->c; e[2][1] = &t->a;
    for (k = 0; k < 3; k++) { /* edges and corners */
        float tt = ray_capsule(x->c0, x->d, *e[k][0], *e[k][1], x->r);
        tc_vec3 c, p, m;
        if (tt < 0 || tt > 1 || (x->found && tt >= x->best.t)) continue;
        c = tc_v3_add(x->c0, tc_v3_scale(x->d, tt));
        p = closest_on_segment(*e[k][0], *e[k][1], c);
        m = tc_v3_sub(c, p);
        if (tc_v3_length_sq(m) < 1e-12f) continue;
        m = tc_v3_normalize(m);
        if (tc_v3_dot(x->d, m) < 0) take(x, tt, m, p, t);
    }
}

bool tc_mec_sweep(tc_collision *world, const tc_ai_sphere_set *body, float heading, tc_vec3 from, tc_vec3 to,
                  tc_mec_hit *hit) {
    sweep_ctx x;
    uint16_t i;
    x.found = false;
    x.d = tc_v3_sub(to, from);
    for (i = 0; world && body && i < body->count; i++) {
        tc_vec3 c1;
        x.c0 = tc_v3_add(from, turn(heading, body->spheres[i].center));
        x.r = body->spheres[i].radius;
        c1 = tc_v3_add(x.c0, x.d);
        tc_collision_query(world, tc_v3_adds(tc_v3_min(x.c0, c1), -x.r - 1e-3f),
                           tc_v3_adds(tc_v3_max(x.c0, c1), x.r + 1e-3f), sweep_tri, &x);
    }
    if (x.found && hit) *hit = x.best;
    return x.found;
}

/* ---- Responses (MEC_004bc477 and the handlers it calls) ---- */

typedef struct collide_ctx {
    tc_collision *world;
    const tc_ai_sphere_set *body;
    const tc_ai_card *card;
    float heading, dt;
    tc_vec3 start, target, velocity;
    uint32_t report_prev;
    tc_mec_walk *w;
} collide_ctx;

static float card_f(const tc_ai_card *c, uint32_t offset, float fallback) {
    return c ? tc_ai_card_f32(c, offset) : fallback;
}

/* FUN_004c99af: 1 when b <= 0 or a <= b, else (2ab - b^2) / a^2 (falls from 1 towards 2b / a). */
static float soften(float a, float b) { return b > 0 && b < a ? (a * b * 2 - b * b) / (a * a) : 1; }

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

/* Where the body was at the impact, and the time left after it. */
static tc_vec3 impact(const collide_ctx *x, float t) {
    return tc_v3_add(x->start, tc_v3_scale(tc_v3_sub(x->target, x->start), t));
}

/* v -= n x (2 - r)(v . n): r = 1 slides along, r = 0 bounces back fully (MEC_004bb66b). */
static tc_vec3 rebound(tc_vec3 v, tc_vec3 n, float r) { return tc_v3_sub(v, tc_v3_scale(n, (2 - r) * tc_v3_dot(v, n))); }

static tc_vec3 tangent(tc_vec3 v, tc_vec3 n) { return tc_v3_sub(v, tc_v3_scale(n, tc_v3_dot(v, n))); }

static void continue_from(collide_ctx *x, tc_vec3 at, float t) {
    x->target = tc_v3_add(at, tc_v3_scale(x->velocity, (1 - t) * x->dt));
}

/* MEC_004bb904: ground (slope = false) or steep slope (slope = true, with a card). */
static void respond_ground(collide_ctx *x, const tc_mec_hit *h, bool slope) {
    tc_vec3 at = impact(x, h->t), v = x->velocity;
    float s = tc_v3_length(v), c;
    if (!(s > 0)) {
        x->target = at;
        return;
    }
    c = fminf(fmaxf(tc_v3_dot(v, h->normal) / s, -1), 1);
    if (tc_v3_dot(v, UP) <= RISE || c >= HEAD_ON) {
        float k = clamp01(slope ? 1 - soften(s, 6) : 0); /* steep slopes: the speed along them tends to 12 */
        v = tc_v3_scale(tangent(v, h->normal), 1 - k);
    } else {
        v = rebound(v, h->normal, clamp01(h->tri ? h->tri->floor.rebound : 1));
    }
    x->velocity = v;
    continue_from(x, at, h->t);
}

/* MEC_004bc20a: overhang, ceiling. Head-on: bounce back (rebound 0), else slide. */
static void respond_ceiling(collide_ctx *x, const tc_mec_hit *h) {
    tc_vec3 at = impact(x, h->t), v = x->velocity;
    float s = tc_v3_length(v);
    if (!(s > 0)) {
        x->target = at;
        return;
    }
    v = tc_v3_dot(v, h->normal) / s >= HEAD_ON ? tangent(v, h->normal) : rebound(v, h->normal, 0);
    x->velocity = v;
    continue_from(x, at, h->t);
}

/* MEC_004bb06e: probe for a floor on top of the wall: put the body 0.15 beyond the contact (at the feet's height),
 * card +8 higher, and sweep it down. A floor within card +0x30 of flat lifts Ed onto it: this pass moves from
 * there (the start, raised) to the step's top; the velocity is that move per frame. */
static bool step_up(collide_ctx *x, const tc_mec_hit *h) {
    float height = card_f(x->card, 8, 0), flat = card_f(x->card, 0x30, 0.99619f);
    tc_vec3 d = tc_v3_sub(h->point, x->start), off, low, high, top, from;
    tc_mec_hit s;
    float l;
    d = tangent(d, UP);
    l = tc_v3_length(d);
    if (!(l > 1e-6f)) return false;
    off = tc_v3_add(d, tc_v3_scale(d, 0.15f / l));
    low = tc_v3_add(x->start, off);
    high = tc_v3_add(low, tc_v3_scale(UP, height));
    if (!tc_mec_sweep(x->world, x->body, x->heading, high, low, &s) || !(s.t > 0)) return false;
    if (fabsf(tc_v3_dot(s.normal, UP)) < flat) return false;
    top = impact(&(collide_ctx){.start = high, .target = low}, s.t);
    from = tc_v3_add(tc_v3_sub(top, off), tc_v3_scale(s.normal, PUSH_OUT));
    /* Not in the exe: the lift itself must be free, so a thin wall cannot be stepped through. */
    if (tc_mec_sweep(x->world, x->body, x->heading, x->start, from, NULL)) return false;
    x->start = from;
    x->target = top;
    x->velocity = x->dt > 0 ? tc_v3_div(off, x->dt) : tc_v3(0, 0, 0);
    x->w->ground_normal = s.normal;
    x->w->ground_grip = s.tri ? s.tri->floor.grip : 1;
    return true;
}

/* MEC_004bbc4c: wall. Stepped up (0x41) or slid along / bounced off (4). */
static uint32_t respond_wall(collide_ctx *x, const tc_mec_hit *h, float up_dot) {
    tc_vec3 at = impact(x, h->t), v = x->velocity;
    float s = tc_v3_length(v), lean = card_f(x->card, 0x2C, 0.08715574f);
    if (!(s > 0)) {
        x->target = at;
        return 4;
    }
    if ((x->report_prev & 3) && card_f(x->card, 8, 0) > 0 && up_dot >= -lean && up_dot <= lean && step_up(x, h))
        return TC_AI_CONTACT_GROUND | TC_MEC_STEPPED;
    if (tc_v3_dot(v, h->normal) / s >= HEAD_ON) {
        /* The exe softens with x^2 + x^2 (velocity x twice) against 36; z is not scaled. */
        float k = clamp01(x->card ? 1 - soften(v.x * v.x * 2, 36) : 0);
        v = tangent(v, h->normal);
        v.x *= 1 - k;
        v.y *= 1 - k;
    } else {
        v = rebound(v, h->normal, clamp01(h->tri ? h->tri->floor.rebound : 1));
    }
    x->velocity = v;
    continue_from(x, at, h->t);
    return 4;
}

/* MEC_004bc477: classify by normal . up with the card's cosines, respond, push the target out by 0.01. */
static uint32_t respond(collide_ctx *x, const tc_mec_hit *h) {
    float c = fminf(fmaxf(tc_v3_dot(h->normal, UP), -1), 1);
    uint32_t bits;
    if (c >= card_f(x->card, 0x1C, 0.70710677f)) {
        respond_ground(x, h, false);
        bits = TC_AI_CONTACT_GROUND;
    } else if (c >= (x->card || !(x->w->slope_cos > 0) ? card_f(x->card, 0x20, 0.5f) : x->w->slope_cos)) {
        respond_ground(x, h, x->card != NULL);
        bits = TC_AI_CONTACT_SLOPE;
    } else if (c >= card_f(x->card, 0x24, -0.5f)) {
        bits = respond_wall(x, h, c);
    } else {
        respond_ceiling(x, h);
        bits = c >= card_f(x->card, 0x28, -0.70710677f) ? TC_AI_CONTACT_OVERHANG : TC_AI_CONTACT_CEILING;
    }
    if (bits & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) {
        if (!(bits & TC_MEC_STEPPED)) {
            x->w->ground_normal = h->normal;
            x->w->ground_grip = h->tri ? h->tri->floor.grip : 1;
        }
    } else {
        x->w->wall_normal = h->normal;
    }
    x->target = tc_v3_add(x->target, tc_v3_scale(h->normal, PUSH_OUT));
    return bits;
}

/* MEC_004be1bd: sweep start -> target and respond, up to 6 times; a clear sweep ends it. Still blocked after the
 * sixth response: back to the start with no velocity (and last frame's contacts). */
static uint32_t collide(collide_ctx *x) {
    tc_vec3 start = x->start;
    uint32_t bits = 0;
    int pass;
    for (pass = 0; pass < PASSES; pass++) {
        tc_mec_hit h;
        x->w->passes = (uint32_t)pass;
        if (!tc_mec_sweep(x->world, x->body, x->heading, x->start, x->target, &h)) return bits;
        bits |= respond(x, &h);
    }
    x->w->passes = PASSES;
    x->w->blocked++;
    x->start = x->target = start;
    x->velocity = tc_v3(0, 0, 0);
    return bits | (x->report_prev & 0x1F);
}

/* MEC_004bf9f0: look for ground within distance below; with normal . up >= min_cos, stand on it (0.01 above) if the
 * way there is free. */
static bool resnap(collide_ctx *x, tc_vec3 *position, float distance, float min_cos) {
    tc_mec_hit h;
    tc_vec3 low = tc_v3_sub(*position, tc_v3_scale(UP, distance)), to;
    float c;
    if (!(distance > 0) || !tc_mec_sweep(x->world, x->body, x->heading, *position, low, &h)) return false;
    c = tc_v3_dot(h.normal, UP);
    if (!(c > 0) || c < min_cos) return false;
    to = tc_v3_add(impact(&(collide_ctx){.start = *position, .target = low}, h.t), tc_v3_scale(UP, PUSH_OUT / c));
    if (tc_mec_sweep(x->world, x->body, x->heading, *position, to, NULL)) return false;
    *position = to;
    x->w->ground_normal = h.normal;
    x->w->ground_grip = h.tri ? h.tri->floor.grip : 1;
    return true;
}

/* Push the body out of triangles it overlaps (not in the exe, which never overlaps by construction; here a state
 * change can swap in a bigger sphere set). */
typedef struct depen_ctx { tc_vec3 c; float r; tc_vec3 push; } depen_ctx;
static void depen_tri(const tc_tri *t, void *user) {
    depen_ctx *x = (depen_ctx *)user;
    tc_vec3 q = tc_closest_point_on_triangle(t->a, t->b, t->c, x->c), diff = tc_v3_sub(x->c, q);
    float d2 = tc_v3_length_sq(diff), d;
    if (!(d2 < x->r * x->r) || d2 < 1e-12f) return;
    d = sqrtf(d2);
    x->push = tc_v3_add(x->push, tc_v3_scale(diff, (x->r - d) / d));
}
static void depenetrate(collide_ctx *x, tc_vec3 *position) {
    int pass;
    uint16_t i;
    for (pass = 0; pass < 4 && x->body; pass++) {
        tc_vec3 total = tc_v3(0, 0, 0);
        for (i = 0; i < x->body->count; i++) {
            depen_ctx d;
            d.c = tc_v3_add(*position, turn(x->heading, x->body->spheres[i].center));
            d.r = x->body->spheres[i].radius;
            d.push = tc_v3(0, 0, 0);
            tc_collision_query(x->world, tc_v3_adds(d.c, -d.r), tc_v3_adds(d.c, d.r), depen_tri, &d);
            if (tc_v3_length_sq(d.push) > tc_v3_length_sq(total)) total = d.push;
        }
        if (tc_v3_length_sq(total) < 1e-8f) break;
        *position = tc_v3_add(*position, tc_v3_add(total, tc_v3_scale(tc_v3_normalize(total), 1e-3f)));
    }
}

void tc_mec_walk_step(tc_mec_walk *w, const tc_ai_card *card, tc_mec_env env, tc_collision *world,
                      const tc_ai_sphere_set *body, float heading, tc_vec3 wanted, bool wanted_in_air,
                      const tc_vec3 *impulse, float dt, tc_vec3 *position, tc_vec3 *velocity) {
    float mass = card_f(card, 4, 1), weight, energy;
    bool grounded = (tc_mec_walk_contacts(w) & 3) != 0;
    tc_vec3 force, v_old = impulse ? *impulse : *velocity, v;
    uint32_t before = w->report_prev, bits;
    if (!(mass > 0)) mass = 1;
    if (!(dt > 0)) return;

    /* FUN_004c41c5: forces and grip */
    weight = env.gravity * card_f(card, 0x38, 1);
    force = weight < 0 ? tc_v3(0, 0, 0) : tc_v3(0, 0, -mass * weight);
    if (!grounded) {
        if (!wanted_in_air) wanted = tc_v3(0, 0, 0);
    } else {
        float grip = clamp01(w->ground_grip);
        wanted = tangent(wanted, w->ground_normal); /* FUN_004c1541: along the ground (mode 12 does this) */
        if (grip >= 1) {
            force = tc_v3(0, 0, 0);
            if (!impulse) v_old = tc_v3(0, 0, 0);
        } else {
            /* Ice: the ground takes the weight's part into it (the slope part stays), the wanted motion is scaled by
             * grip^4 and friction -m grip^4 / dt x v brakes last frame's velocity, so each frame
             * v = (1 - grip^4) v_old + slope dt + grip^4 wanted: Ed eases towards the wanted speed (grip 1: at once).
             * grip^4 is the exe's share per 1/30 s frame, converted to dt. */
            float g4 = tc_mec_per_frame(grip * grip * grip * grip, dt);
            force = tc_v3_sub(force, tc_v3_scale(w->ground_normal, tc_v3_dot(force, w->ground_normal)));
            force = tc_v3_add(force, tc_v3_scale(v_old, -mass * g4 / dt));
            wanted = tc_v3_scale(wanted, g4);
        }
    }
    v = tc_v3_add(tc_v3_add(v_old, tc_v3_scale(force, dt / mass)), wanted);

    /* FUN_004c39f0: below 0.025 of kinetic energy, stop; never over 100 u/s */
    energy = tc_v3_length_sq(v) * mass * 0.5f;
    if (energy < 0.025f) v = tc_v3(0, 0, 0);
    else if (tc_v3_length_sq(v) > 10000) v = tc_v3_scale(v, 100 / tc_v3_length(v));
    *velocity = v;
    if (v.x == 0 && v.y == 0 && v.z == 0) return; /* standing: no move, the contacts stay (dynamics +0x38C) */

    bits = tc_mec_collide_move(w, card, world, body, heading, dt, position, velocity);

    /* Ground lost this frame and the last, not the one before: re-snap within card +0x34 (scaled by frames of 1/30 s) */
    if (!(bits & 3) && !(w->report_prev & 3) && (before & 3)) {
        float distance = card_f(card, 0x34, 0) * (dt > 1.0f / 30 ? dt * 30 : 1);
        tc_mec_resnap(w, world, body, heading, distance, card_f(card, 0x20, 0.5f), position);
    }
}

static void setup(collide_ctx *x, tc_mec_walk *w, const tc_ai_card *card, tc_collision *world,
                  const tc_ai_sphere_set *body, float heading, float dt) {
    memset(x, 0, sizeof(*x));
    x->world = world;
    x->body = body;
    x->card = card;
    x->heading = heading;
    x->dt = dt;
    x->w = w;
    x->report_prev = w->report;
}

uint32_t tc_mec_collide_move(tc_mec_walk *w, const tc_ai_card *card, tc_collision *world, const tc_ai_sphere_set *body,
                             float heading, float dt, tc_vec3 *position, tc_vec3 *velocity) {
    collide_ctx x;
    uint32_t bits;
    setup(&x, w, card, world, body, heading, dt);
    depenetrate(&x, position);
    x.start = *position;
    x.target = tc_v3_add(*position, tc_v3_scale(*velocity, dt));
    x.velocity = *velocity;
    bits = collide(&x);
    w->report_prev = w->report;
    w->report = bits;
    *position = x.target;
    *velocity = x.velocity;
    return bits;
}

bool tc_mec_resnap(tc_mec_walk *w, tc_collision *world, const tc_ai_sphere_set *body, float heading, float distance,
                   float min_cos, tc_vec3 *position) {
    collide_ctx x;
    setup(&x, w, NULL, world, body, heading, 0);
    if (!resnap(&x, position, distance, min_cos)) return false;
    w->report |= TC_AI_CONTACT_GROUND;
    return true;
}
