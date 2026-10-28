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
    w->ground_tri = w->wall_tri = UINT32_MAX;
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

/* ---- Sweep (the collision library, docs/DECOMP.md: FUN_0043bff0 -> FUN_00464440 -> FUN_004657f0, a body made
 * of spheres only, -> FUN_00465520 -> FUN_00464c80 -> FUN_00465b60 per sphere and triangle) ---- */

#define FLOOR_ONLY_Z 0.93f /* DAT_004dc9bc: the flag 8 spheres' sweep counts only floors this flat */

/* FUN_004661b0: real roots of a t^2 + b t + c; 2 roots: r1 = (-b + sqrt) / 2a, r2 = (-b - sqrt) / 2a. */
static int quadratic(float a, float b, float c, float *r1, float *r2) {
    float disc, inv;
    if (a == 0) {
        if (b != 0) { *r1 = -(c / b); return 1; }
        return 0;
    }
    disc = b * b - c * a * 4;
    if (disc > 0) {
        inv = 1 / (a + a);
        *r1 = (sqrtf(disc) - b) * inv;
        *r2 = (-b - sqrtf(disc)) * inv;
        return 2;
    }
    if (disc == 0) { *r1 = -(b / (a + a)); return 1; }
    return 0;
}

/* The smaller root of quadratic(), when it lies in 0..1 (FUN_0046a220 / FUN_00469500's choice). The edge test rejects
 * t < 0 or t > 1, the corner test keeps 0 <= t <= 1: they differ only on a NaN root, which the edge test keeps. */
static bool first_root(float a, float b, float c, bool keep_nan, float *t) {
    float r1 = 0, r2 = 0;
    int n = quadratic(a, b, c, &r1, &r2);
    if (n == 0) return false;
    if (n == 2 && r2 <= r1) r1 = r2;
    if (keep_nan ? r1 < 0 || r1 > 1 : !(r1 >= 0 && r1 <= 1)) return false;
    *t = r1;
    return true;
}

/* The exe's box rejection on one axis: the interval swept from p by d against the one from q by e, grown by r. */
static bool overlap_axis(float p, float d, float q, float e, float r) {
    return p + fminf(d, 0) <= q + fmaxf(e, 0) + r && q + fminf(e, 0) - r <= p + fmaxf(d, 0);
}
static bool overlap(tc_vec3 p, tc_vec3 d, tc_vec3 q, tc_vec3 e, float r) {
    return overlap_axis(p.x, d.x, q.x, e.x, r) && overlap_axis(p.y, d.y, q.y, e.y, r) &&
           overlap_axis(p.z, d.z, q.z, e.z, r);
}

/* FUN_0046a220: sphere p0 + d t (radius r) against segment e0 + e s: t in 0..1 (the smaller root, which must be >= 0:
 * a sphere already overlapping the edge's line does not hit), s in 0..1; centre at t and the segment point. */
static bool sphere_edge(tc_vec3 p0, tc_vec3 d, tc_vec3 e0, tc_vec3 e, float r, float *t, tc_vec3 *centre,
                        tc_vec3 *on_edge) {
    float ee, inv, de, pe, k, s;
    tc_vec3 f, u, w;
    if (!overlap(p0, d, e0, e, r)) return false;
    ee = tc_v3_dot(e, e);
    if (ee == 0) return false;
    inv = 1 / ee;
    f = tc_v3_sub(p0, e0);
    de = tc_v3_dot(e, d);
    pe = tc_v3_dot(e, f);
    k = de * inv;
    if (k * e.x == d.x && d.y == k * e.y && d.z == k * e.z) return false; /* moving exactly along the edge */
    u = tc_v3_sub(tc_v3_scale(e, k), d);                 /* the motion across the edge's line */
    w = tc_v3_sub(tc_v3_scale(e, pe * inv), f);          /* the start's offset from it */
    if (!first_root(tc_v3_dot(u, u), tc_v3_dot(u, w) * 2, tc_v3_dot(w, w) - r * r, true, t)) return false;
    *centre = tc_v3_add(tc_v3_scale(d, *t), p0);
    s = (de * *t + pe) * inv;
    if (!(s >= 0) || s > 1) return false;
    *on_edge = tc_v3_add(tc_v3_scale(e, s), e0);
    return true;
}

/* FUN_00469500 (called with the motion reversed): point v against sphere p0 + d t (radius r): t in 0..1. */
static bool sphere_vertex(tc_vec3 v, tc_vec3 d, tc_vec3 p0, float r, float *t) {
    tc_vec3 back = tc_v3_scale(d, -1), f = tc_v3_sub(v, p0);
    if (!overlap(v, back, p0, tc_v3(0, 0, 0), r)) return false;
    return first_root(tc_v3_dot(back, back), tc_v3_dot(back, f) * 2, tc_v3_dot(f, f) - r * r, false, t);
}

/* FUN_00465870: is the plane point (a, b, c = the corners minus it) inside, by the cross products' signs along the
 * normal's largest axis (z first, then y, else x; 1/sqrt 3 picks it). */
static bool same_side(float n, float cross) { return (n >= 0) == (cross >= 0); }
static bool inside(tc_vec3 a, tc_vec3 b, tc_vec3 c, tc_vec3 n) {
    if (fabsf(n.z) >= 0.57735026f)
        return same_side(n.z, b.y * a.x - a.y * b.x) && same_side(n.z, c.y * b.x - b.y * c.x) &&
               same_side(n.z, a.y * c.x - c.y * a.x);
    if (fabsf(n.y) >= 0.57735026f)
        return same_side(n.y, a.z * b.x - b.z * a.x) && same_side(n.y, b.z * c.x - b.x * c.z) &&
               same_side(n.y, c.z * a.x - a.z * c.x);
    return same_side(n.x, a.y * b.z - b.y * a.z) && same_side(n.x, b.y * c.z - b.z * c.y) &&
           same_side(n.x, c.y * a.z - c.z * a.y);
}

int tc_mec_sphere_tri(tc_vec3 p0, tc_vec3 d, float r, tc_vec3 n, tc_vec3 a, tc_vec3 b, tc_vec3 c, int *kind, float *t,
                      tc_vec3 *normal, tc_vec3 *point) {
    const tc_vec3 *from[3] = {&a, &a, &b}, *to[3] = {&b, &c, &c}, *corner[3] = {&a, &b, &c};
    float dn = tc_v3_dot(d, n), best = 0, tt;
    tc_vec3 centre, on_edge;
    int k, which = 0;
    if (dn < 0) { /* the face: its nearest point q = p0 - r n reaches the plane within this move */
        tc_vec3 q = tc_v3_add(tc_v3_scale(n, -r), p0);
        float s = -tc_v3_dot(tc_v3_sub(q, a), n);
        if (dn <= s && s <= 0) {
            tt = s / dn;
            *point = tc_v3_add(tc_v3_scale(d, tt), q);
            if (inside(tc_v3_sub(a, *point), tc_v3_sub(b, *point), tc_v3_sub(c, *point), n)) {
                *t = tt;
                *kind = TC_MEC_FACE;
                *normal = n;
                return 1;
            }
        }
    }
    for (k = 0; k < 3; k++) { /* the edges a-b, a-c, b-c: the earliest (a tie keeps the first) */
        if (!sphere_edge(p0, d, *from[k], tc_v3_sub(*to[k], *from[k]), r, &tt, &centre, &on_edge)) continue;
        if (which && !(tt < best)) continue;
        which = k + 1;
        best = tt;
        *normal = centre;
        *point = on_edge;
    }
    if (which) {
        *t = best;
        *kind = TC_MEC_EDGE;
        *normal = tc_v3_scale(tc_v3_sub(*normal, *point), 1 / r);
        return which;
    }
    for (k = 0; k < 3; k++) { /* the corners */
        if (!sphere_vertex(*corner[k], d, p0, r, &tt)) continue;
        if (which && !(tt < best)) continue;
        which = k + 1;
        best = tt;
    }
    if (!which) return 0;
    *t = best;
    *kind = TC_MEC_VERTEX;
    *point = *corner[which - 1];
    *normal = tc_v3_scale(tc_v3_sub(tc_v3_add(tc_v3_scale(d, best), p0), *point), 1 / r);
    return 1;
}

/* FUN_00467d40: the plane point inside the triangle, by cross products on the normal's largest axis (z first, then x,
 * else y): FUN_00465870's test with other operand orders, kept as the exe has them (they differ on zeros and NaN). */
static bool inside_point(tc_vec3 p, tc_vec3 a, tc_vec3 b, tc_vec3 c, tc_vec3 n) {
    tc_vec3 A = tc_v3_sub(a, p), B = tc_v3_sub(b, p), C = tc_v3_sub(c, p);
    if (fabsf(n.z) >= 0.57735026f)
        return same_side(n.z, B.y * A.x - B.x * A.y) && same_side(n.z, C.y * B.x - C.x * B.y) &&
               (n.z >= 0) == !(C.x * A.y - C.y * A.x < 0);
    if (fabsf(n.x) >= 0.57735026f)
        return same_side(n.x, B.z * A.y - B.y * A.z) && same_side(n.x, C.z * B.y - C.y * B.z) &&
               (n.x >= 0) == !(C.y * A.z - C.z * A.y < 0);
    return same_side(n.y, B.x * A.z - B.z * A.x) && same_side(n.y, C.x * B.z - C.z * B.x) &&
           same_side(n.y, C.z * A.x - C.x * A.z);
}

/* The exe's box rejection for a point: its swept interval on one axis against the triangle's. */
static bool point_axis(float p, float d, float a, float b, float c) {
    return fmaxf(fmaxf(a, b), c) >= p + fminf(d, 0) && p + fmaxf(d, 0) >= fminf(fminf(a, b), c);
}

int tc_mec_point_tri(tc_vec3 p0, tc_vec3 d, tc_vec3 n, tc_vec3 a, tc_vec3 b, tc_vec3 c, float *t, tc_vec3 *point) {
    float dn = n.z * d.z + n.y * d.y + n.x * d.x, s, tt;
    tc_vec3 hit;
    if (dn >= 0) return 0;
    if (!point_axis(p0.x, d.x, a.x, b.x, c.x) || !point_axis(p0.y, d.y, a.y, b.y, c.y) ||
        !point_axis(p0.z, d.z, a.z, b.z, c.z))
        return 0;
    s = n.z * p0.z + n.y * p0.y + n.x * p0.x + -(n.z * a.z + n.y * a.y + n.x * a.x); /* FUN_004683b0 */
    if (!(s > 0) || !(s < -dn)) return 0;
    tt = -(s / dn);
    hit = tc_v3_add(tc_v3_scale(d, tt), p0);
    if (!inside_point(hit, a, b, c, n)) return 0;
    *t = tt;
    *point = hit;
    return 1;
}

typedef struct sweep_ctx {
    tc_vec3 c0, d;
    float r;
    bool point;
    uint16_t flags;
    float rebound, grip;
    tc_mec_hit best;
    bool found;
} sweep_ctx;

/* FUN_00464c80: keep the contact when it is earlier than the best so far (the first of equal ones stays). */
static int zone_type(uint16_t flags);
static void sweep_tri(const tc_tri *tri, void *user) {
    sweep_ctx *x = (sweep_ctx *)user;
    tc_mec_hit h;
    if (x->point) { /* FUN_0046ff70: a face contact, normal = the face's */
        if (!tc_mec_point_tri(x->c0, x->d, tri->normal, tri->a, tri->b, tri->c, &h.t, &h.point)) return;
        h.kind = TC_MEC_FACE;
        h.normal = tri->normal;
    } else if (!tc_mec_sphere_tri(x->c0, x->d, x->r, tri->normal, tri->a, tri->b, tri->c, &h.kind, &h.t, &h.normal,
                                  &h.point)) {
        return;
    }
    if (x->found ? !(h.t < x->best.t) : !(h.t < 2)) return; /* DAT_0050e140 starts at 2 (FUN_00460210) */
    h.zone_type = zone_type(x->flags);
    h.own_flags = x->flags;
    h.own_rebound = x->rebound;
    h.own_grip = x->grip;
    h.own_point = x->point;
    h.tri = tri;
    x->best = h;
    x->found = true;
}

/* FUN_0043bf70: the zone type of a sphere's collide flags. */
static int zone_type(uint16_t flags) {
    if ((flags & 1) == 1) return 1;
    if ((flags & 2) == 2) return 4;
    if ((flags & 4) == 4) return 3;
    return (flags & 8) == 8 ? 2 : -1;
}

/* One sweep over the parts the mask selects (FUN_00420610: flag 8 set, or clear; flag 0x10 never). Within a zone the
 * exe tests its points (FUN_00470b40) before its spheres; the earliest contact wins either way. */
static bool sweep_spheres(tc_collision *world, const tc_ai_sphere_set *body, float heading, tc_vec3 from, tc_vec3 d,
                          bool floor_spheres, tc_mec_hit *hit) {
    sweep_ctx x;
    uint16_t i;
    x.found = false;
    x.d = d;
    for (i = 0; i < body->count; i++) {
        const tc_ai_sphere *s = &body->spheres[i];
        tc_vec3 c1;
        if (s->flags & 0x10 || ((s->flags & 8) != 0) != floor_spheres) continue;
        x.c0 = tc_v3_add(from, turn(heading, s->center));
        x.r = s->point ? 0 : s->radius;
        x.point = s->point != 0;
        x.flags = s->flags;
        x.rebound = s->rebound;
        x.grip = s->grip;
        c1 = tc_v3_add(x.c0, x.d);
        tc_collision_query(world, tc_v3_adds(tc_v3_min(x.c0, c1), -x.r - 1e-3f),
                           tc_v3_adds(tc_v3_max(x.c0, c1), x.r + 1e-3f), sweep_tri, &x);
    }
    if (x.found) *hit = x.best;
    return x.found;
}

bool tc_mec_sweep(tc_collision *world, const tc_ai_sphere_set *body, float heading, tc_vec3 from, tc_vec3 to,
                  tc_mec_hit *hit) {
    tc_vec3 d = tc_v3_sub(to, from);
    tc_mec_hit floor_hit, hit_b;
    bool a, b;
    if (!world || !body || (d.x == 0 && d.y == 0 && d.z == 0)) return false;
    a = sweep_spheres(world, body, heading, from, d, true, &floor_hit);
    b = sweep_spheres(world, body, heading, from, d, false, &hit_b);
    /* FUN_0043bff0: the floor spheres' contact wins when it is a floor and not later than the others' */
    if (a && floor_hit.normal.z >= FLOOR_ONLY_Z && (!b || floor_hit.t <= hit_b.t)) {
        if (hit) *hit = floor_hit;
        return true;
    }
    if (b && hit) *hit = hit_b;
    return b;
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

/* A contact's rebound and grip: the obstacle's mechanics material times the touching part's own (record +0x34 x +0x50,
 * +0x38 x +0x54; no material: 1). Ed's body spheres have rebound 0.85 and grip 0, his feet none. */
static float hit_rebound(const tc_mec_hit *h) { return (h->tri ? h->tri->floor.rebound : 1) * h->own_rebound; }
static float hit_grip(const tc_mec_hit *h) { return (h->tri ? h->tri->floor.grip : 1) * h->own_grip; }

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
        v = rebound(v, h->normal, clamp01(hit_rebound(h)));
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
    if (!tc_mec_sweep(x->world, x->body, x->heading, high, low, &s)) return false;
    if (fabsf(tc_v3_dot(s.normal, UP)) < flat) return false;
    top = impact(&(collide_ctx){.start = high, .target = low}, s.t);
    from = tc_v3_add(tc_v3_sub(top, off), tc_v3_scale(s.normal, PUSH_OUT));
    x->start = from;
    x->target = top;
    x->velocity = x->dt > 0 ? tc_v3_div(off, x->dt) : tc_v3(0, 0, 0);
    x->w->ground_normal = s.normal;
    x->w->ground_grip = hit_grip(&s);
    x->w->ground_tri = s.tri ? s.tri->source : UINT32_MAX;
    x->w->ground_own = s.own_flags;
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
    /* Step up: ground last frame, the touching sphere of zone type 1 (collide flag 1: the feet and lower body), a step
     * height; and an edge, or a wall within the lean (the 0x40 collide flag that forces both is unused by Ed). */
    if ((x->report_prev & 3) && h->zone_type == 1 && card_f(x->card, 8, 0) > 0 &&
        (h->kind == TC_MEC_EDGE || (up_dot >= -lean && up_dot <= lean)) && step_up(x, h))
        return TC_AI_CONTACT_GROUND | TC_MEC_STEPPED;
    if (tc_v3_dot(v, h->normal) / s >= HEAD_ON) {
        /* The exe softens with x^2 + x^2 (velocity x twice) against 36; z is not scaled. */
        float k = clamp01(x->card ? 1 - soften(v.x * v.x * 2, 36) : 0);
        v = tangent(v, h->normal);
        v.x *= 1 - k;
        v.y *= 1 - k;
    } else {
        v = rebound(v, h->normal, clamp01(hit_rebound(h)));
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
    } else if (c >= card_f(x->card, 0x20, 0.5f)) {
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
            x->w->ground_grip = hit_grip(h);
            x->w->ground_tri = h->tri ? h->tri->source : UINT32_MAX;
            x->w->ground_own = h->own_flags;
        }
    } else {
        x->w->wall_normal = h->normal;
        x->w->wall_tri = h->tri ? h->tri->source : UINT32_MAX;
        x->w->wall_own = h->own_flags;
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
    x->w->ground_grip = hit_grip(&h);
    x->w->ground_tri = h.tri ? h.tri->source : UINT32_MAX;
    x->w->ground_own = h.own_flags;
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
