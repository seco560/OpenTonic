#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_obstacle.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_sector.h"

tc_result tc_obstacles_init(tc_obstacles *o, const tc_level *l) {
    const tc_pcol *d = &l->pcol;
    uint32_t i;
    memset(o, 0, sizeof(*o));
    o->data = d;
    if (!d->count) return TC_OK;
    o->items = (tc_obstacle *)tc_calloc(d->count, sizeof(tc_obstacle));
    if (!o->items) return TC_ERR_OUT_OF_MEMORY;
    for (i = 0; i < d->count; i++)
        if (d->persos[i].id + 1u > o->id_count) o->id_count = d->persos[i].id + 1u;
    o->by_id = (int32_t *)tc_calloc(o->id_count ? o->id_count : 1, sizeof(int32_t));
    if (!o->by_id) { tc_obstacles_free(o); return TC_ERR_OUT_OF_MEMORY; }
    for (i = 0; i < o->id_count; i++) o->by_id[i] = -1;
    for (i = 0; i < d->count; i++) {
        const tc_pcol_perso *p = &d->persos[i];
        tc_obstacle *ob = &o->items[i];
        uint32_t cap = (uint32_t)p->object_count * 2u + p->pose_count;
        ob->data = p;
        ob->id = p->id;
        ob->sector = p->sector;
        ob->priority = p->priority;
        ob->flags = p->flags;
        ob->live = (p->flags & TC_PCOL_ACTIVE) && !(p->flags & TC_PCOL_DEAD);
        ob->part_cap = (uint16_t)(cap < 4 ? 4 : cap > 0xFFFF ? 0xFFFF : cap);
        ob->parts = (tc_obstacle_part *)tc_calloc(ob->part_cap, sizeof(tc_obstacle_part));
        if (!ob->parts) { o->count = i; tc_obstacles_free(o); return TC_ERR_OUT_OF_MEMORY; }
        o->by_id[p->id] = (int32_t)i;
        tc_obstacle_pose_static(ob, &p->world);
        o->count = i + 1;
    }
    return TC_OK;
}

void tc_obstacles_free(tc_obstacles *o) {
    uint32_t i;
    if (!o) return;
    for (i = 0; o->items && i < o->count; i++) tc_free(o->items[i].parts);
    tc_free(o->items);
    tc_free(o->by_id);
    memset(o, 0, sizeof(*o));
}

tc_obstacle *tc_obstacles_find(const tc_obstacles *o, int32_t id) {
    if (!o || id < 0 || (uint32_t)id >= o->id_count || o->by_id[id] < 0) return NULL;
    return &o->items[o->by_id[id]];
}

static const tc_pcol_object *object_of(const tc_obstacle *ob, uint16_t object) {
    uint16_t k;
    for (k = 0; k < ob->data->object_count; k++)
        if (ob->data->objects[k].object == object && ob->data->objects[k].count) return &ob->data->objects[k];
    return NULL;
}

static void begin(tc_obstacle *ob, const tc_mat34 *world) {
    ob->part_count = 0;
    ob->world = *world;
    ob->lo = tc_v3(INFINITY, INFINITY, INFINITY);
    ob->hi = tc_v3(-INFINITY, -INFINITY, -INFINITY);
}

/* One shown object at m (object axes -> world): its inverse for the tests, its world bound. */
static void add_part(tc_obstacle *ob, const tc_pcol_object *obj, const tc_mat34 *m) {
    tc_obstacle_part *q;
    float s2, *iv;
    int i, j;
    if (!obj || ob->part_count >= ob->part_cap) return;
    q = &ob->parts[ob->part_count++];
    q->object = obj;
    q->m = *m;
    s2 = m->m[0] * m->m[0] + m->m[4] * m->m[4] + m->m[8] * m->m[8];
    if (!(s2 > 1e-20f)) s2 = 1;
    q->scale = sqrtf(s2);
    iv = q->inv.m;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) iv[4 * i + j] = m->m[4 * j + i] / s2; /* (R s)^-1 = R^T / s for a uniform s */
    for (i = 0; i < 3; i++) iv[4 * i + 3] = -(iv[4 * i] * m->m[3] + iv[4 * i + 1] * m->m[7] + iv[4 * i + 2] * m->m[11]);
    q->centre = tc_m34_point(m, obj->centre);
    q->radius = obj->radius * q->scale;
    ob->lo = tc_v3_min(ob->lo, tc_v3_adds(q->centre, -q->radius));
    ob->hi = tc_v3_max(ob->hi, tc_v3_adds(q->centre, q->radius));
}

void tc_obstacle_pose(tc_obstacle *ob, const tc_mat34 *world, const tc_mat34 *transforms, const uint16_t *shown,
                      uint32_t channel_count) {
    uint32_t ch;
    begin(ob, world);
    for (ch = 0; ch < channel_count; ch++) {
        tc_mat34 m;
        const tc_pcol_object *obj;
        if (shown[ch] == TC_HIDDEN || !(obj = object_of(ob, shown[ch]))) continue;
        m = tc_m34_mul(world, &transforms[ch]);
        add_part(ob, obj, &m);
    }
}

void tc_obstacle_pose_static(tc_obstacle *ob, const tc_mat34 *world) {
    uint16_t k;
    begin(ob, world);
    for (k = 0; k < ob->data->pose_count; k++) {
        tc_mat34 m = tc_m34_mul(world, &ob->data->pose[k].m);
        add_part(ob, object_of(ob, ob->data->pose[k].object), &m);
    }
}

/* FUN_004661b0: real roots of a t^2 + b t + c (as tc_mec_walk.c's). */
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

int tc_mec_point_sphere(tc_vec3 p0, tc_vec3 d, tc_vec3 c, float R, float *t, tc_vec3 *centre, tc_vec3 *normal) {
    float r1 = 0, r2 = 0, tt, k;
    tc_vec3 f;
    int n;
    /* the swept box test, axis by axis, as the exe orders it */
    if (!((d.x <= 0 ? d.x + p0.x : p0.x) <= R + c.x && c.x - R <= (d.x <= 0 ? p0.x : d.x + p0.x))) return 0;
    if (!((d.y <= 0 ? d.y + p0.y : p0.y) <= c.y + R && c.y - R <= (d.y <= 0 ? p0.y : d.y + p0.y))) return 0;
    if (!((d.z <= 0 ? d.z + p0.z : p0.z) <= c.z + R && c.z - R <= (d.z <= 0 ? p0.z : d.z + p0.z))) return 0;
    f = tc_v3(p0.x - c.x, p0.y - c.y, p0.z - c.z);
    n = quadratic(d.z * d.z + d.x * d.x + d.y * d.y, (d.z * f.z + d.y * f.y + d.x * f.x) * 2,
                  (f.z * f.z + f.y * f.y + f.x * f.x) - R * R, &r1, &r2);
    if (n == 1) tt = r1;
    else if (n == 2) tt = r2 <= r1 ? r2 : r1;
    else return 0;
    if (!(0 <= tt) || !(tt <= 1)) return 0;
    *t = tt;
    *centre = tc_v3(d.x * tt + p0.x, d.y * tt + p0.y, d.z * tt + p0.z);
    k = 1 / R;
    *normal = tc_v3(k * (centre->x - c.x), k * (centre->y - c.y), k * (centre->z - c.z));
    return 1;
}

/* FUN_004663e0: the region code of p against lo..hi. */
static unsigned region(tc_vec3 p, tc_vec3 lo, tc_vec3 hi) {
    return (p.x < lo.x) | (hi.x < p.x) << 1 | (p.y < lo.y) << 2 | (hi.y < p.y) << 3 | (p.z < lo.z) << 4 |
           (hi.z < p.z) << 5;
}

int tc_mec_sphere_box(tc_vec3 p0, tc_vec3 p1, tc_vec3 d, tc_vec3 lo, tc_vec3 hi, float r, float *t, tc_vec3 *centre,
                      tc_vec3 *normal) {
    unsigned a, b;
    float tt;
    tc_vec3 q;
    lo = tc_v3(lo.x - r, lo.y - r, lo.z - r);
    hi = tc_v3(hi.x + r, hi.y + r, hi.z + r);
    a = region(p0, lo, hi);
    if (!a) return 0;
    b = region(p1, lo, hi);
    if (a & b) return 0;
    /* each face the start is outside of: the crossing within the face's rectangle (FUN_004664c0 / 00466520 / 00466460,
     * bounds inclusive) */
    if (a & 1) {
        tt = (lo.x - p0.x) / d.x;
        q = tc_v3(lo.x, d.y * tt + p0.y, d.z * tt + p0.z);
        if (!(lo.y > q.y) && !(hi.y < q.y) && !(lo.z > q.z) && !(hi.z < q.z)) { *t = tt; *centre = q; *normal = tc_v3(-1, 0, 0); return 1; }
    }
    if (a & 2) {
        tt = (hi.x - p0.x) / d.x;
        q = tc_v3(hi.x, d.y * tt + p0.y, d.z * tt + p0.z);
        if (!(lo.y > q.y) && !(hi.y < q.y) && !(lo.z > q.z) && !(hi.z < q.z)) { *t = tt; *centre = q; *normal = tc_v3(1, 0, 0); return 1; }
    }
    if (a & 4) {
        tt = (lo.y - p0.y) / d.y;
        q = tc_v3(d.x * tt + p0.x, lo.y, d.z * tt + p0.z);
        if (!(q.x < lo.x) && !(q.x > hi.x) && !(lo.z > q.z) && !(hi.z < q.z)) { *t = tt; *centre = q; *normal = tc_v3(0, -1, 0); return 1; }
    }
    if (a & 8) {
        tt = (hi.y - p0.y) / d.y;
        q = tc_v3(d.x * tt + p0.x, hi.y, d.z * tt + p0.z);
        if (!(q.x < lo.x) && !(q.x > hi.x) && !(lo.z > q.z) && !(hi.z < q.z)) { *t = tt; *centre = q; *normal = tc_v3(0, 1, 0); return 1; }
    }
    if (a & 0x10) {
        tt = (lo.z - p0.z) / d.z;
        q = tc_v3(d.x * tt + p0.x, p0.y + d.y * tt, lo.z);
        if (!(q.x < lo.x) && !(q.x > hi.x) && !(lo.y > q.y) && !(hi.y < q.y)) { *t = tt; *centre = q; *normal = tc_v3(0, 0, -1); return 1; }
    }
    if (a & 0x20) {
        tt = (hi.z - p0.z) / d.z;
        q = tc_v3(d.x * tt + p0.x, p0.y + d.y * tt, hi.z);
        if (!(q.x < lo.x) && !(q.x > hi.x) && !(lo.y > q.y) && !(hi.y < q.y)) { *t = tt; *centre = q; *normal = tc_v3(0, 0, 1); return 1; }
    }
    return 0;
}
