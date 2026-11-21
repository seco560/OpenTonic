/* The exe's particle module and MSPrtSrc, read 2026-11-20 (tc_particles.h; docs/ENGINE.md "Particles"). */
#include "tonic/tc_particles.h"
#include <float.h>
#include <math.h>
#include <string.h>
#include "tonic/tc_alloc.h"
#include "tonic/tc_sector.h"

/* DAT_004e2780: which of (x, y, z, -x, -y, -z) of the field's random vector each of 48 particles in a row adds to the
 * field's acceleration (x, y, z); a new random vector every 48 (FUN_0047f750, 0047feb0, 00480680, 004809a0). */
static const uint8_t JITTER[48][3] = {
    {0, 1, 2}, {1, 2, 0}, {2, 0, 1}, {0, 2, 1}, {1, 0, 2}, {2, 1, 0}, {0, 1, 5}, {1, 2, 3}, {2, 0, 4}, {0, 2, 4},
    {1, 0, 5}, {2, 1, 3}, {0, 5, 2}, {1, 4, 0}, {2, 3, 1}, {0, 5, 1}, {1, 3, 2}, {2, 4, 0}, {0, 5, 5}, {1, 4, 3},
    {2, 3, 4}, {0, 5, 4}, {1, 3, 5}, {2, 4, 3}, {3, 1, 2}, {4, 2, 0}, {5, 0, 1}, {3, 2, 1}, {4, 0, 2}, {5, 1, 0},
    {3, 1, 5}, {4, 2, 3}, {5, 0, 4}, {3, 2, 4}, {4, 0, 5}, {5, 1, 3}, {3, 5, 2}, {4, 4, 0}, {5, 3, 1}, {3, 5, 1},
    {4, 3, 2}, {5, 4, 0}, {3, 5, 5}, {4, 4, 3}, {5, 3, 4}, {3, 5, 4}, {4, 3, 5}, {5, 4, 3}};

/* The exe's random numbers (FUN_004500c0 float in [lo, hi], FUN_00450070 int, FUN_00450120 per axis): its own
 * table-driven generator, not read; an LCG here (only the look depends on it). */
static float rnd(tc_particles *ps) {
    ps->rng = ps->rng * 1664525u + 1013904223u;
    return (float)(ps->rng >> 8) * (1.0f / 16777216.0f);
}
static float rndf(tc_particles *ps, float lo, float hi) { return lo + (hi - lo) * rnd(ps); }
static int rndi(tc_particles *ps, int lo, int hi) {
    int k;
    if (hi <= lo) return lo;
    k = lo + (int)(rnd(ps) * (float)(hi - lo + 1));
    return k > hi ? hi : k;
}
static tc_vec3 rnd3(tc_particles *ps, float j) { return tc_v3(rndf(ps, -j, j), rndf(ps, -j, j), rndf(ps, -j, j)); }

static tc_vec3 translation(const tc_mat34 *m) { return tc_v3(m->m[3], m->m[7], m->m[11]); }

bool tc_particles_init(tc_particles *ps, const tc_level *level, uint32_t seed) {
    memset(ps, 0, sizeof(*ps));
    ps->level = level;
    ps->rng = seed ? seed : 0x5eed1234u;
    if (!level || !level->particle_source_count) return true;
    ps->pools = (tc_particle_pool *)tc_calloc(level->particle_source_count, sizeof(tc_particle_pool));
    if (!ps->pools) return false;
    ps->pool_count = level->particle_source_count;
    for (uint32_t i = 0; i < ps->pool_count; i++) { /* the field as the data holds it, until placed */
        const tc_particle_source *s = &level->particle_sources[i];
        ps->pools[i].box_min = s->box_min, ps->pools[i].box_max = s->box_max, ps->pools[i].centre = s->centre;
        ps->pools[i].deflector_a = s->deflector_a, ps->pools[i].deflector_b = s->deflector_b;
    }
    return true;
}

void tc_particles_free(tc_particles *ps) {
    for (uint32_t i = 0; ps->pools && i < ps->pool_count; i++) {
        tc_particle_pool *p = &ps->pools[i];
        tc_free(p->position);
        tc_free(p->previous);
        tc_free(p->velocity);
        tc_free(p->death);
    }
    tc_free(ps->pools);
    memset(ps, 0, sizeof(*ps));
}

static uint32_t capacity(const tc_particle_source *s) {
    float c = (float)s->max * TC_PARTICLE_DETAIL;
    return c > 0 ? (uint32_t)c : 0;
}

static bool allocate(tc_particle_pool *p, uint32_t n) {
    if (p->position) return true;
    if (!n) return false;
    p->position = (tc_vec3 *)tc_calloc(n, sizeof(tc_vec3));
    p->previous = (tc_vec3 *)tc_calloc(n, sizeof(tc_vec3));
    p->velocity = (tc_vec3 *)tc_calloc(n, sizeof(tc_vec3));
    p->death = (float *)tc_calloc(n, sizeof(float));
    if (p->position && p->previous && p->velocity && p->death) return true;
    tc_free(p->position), tc_free(p->previous), tc_free(p->velocity), tc_free(p->death);
    p->position = p->previous = p->velocity = NULL;
    p->death = NULL;
    return false;
}

/* GAM_0042e900: whether p is somewhere the source may be (its environment mask); *depth below the last surface. */
static bool env_ok(const tc_particles *ps, const tc_particle_source *s, uint16_t sector, tc_vec3 p, float *depth) {
    bool water;
    *depth = 0;
    if (s->env_mask == 0xFFFFFFFFu) return true;
    water = ps->water ? ps->water(ps->water_user, sector, p, depth) : false;
    return water ? (s->env_mask & 1u) != 0 : (s->env_mask & 2u) != 0;
}

/* GAM_0042e950 then GAM_0042e200: with `follow`, the generator's first three corners must be where the source may be;
 * then the field goes where the perso is (a box: its offsets from the perso's place, cut at a water surface between its
 * corners; a sphere: its centre in the perso's axes) and the deflector, once, by the perso's place. false: no new
 * particles this tick (src +9). */
static bool place(const tc_particles *ps, const tc_particle_source *s, tc_particle_pool *pool, const tc_particle_emitter *e) {
    const tc_mat34 *m = &e->matrix;
    tc_vec3 t = translation(m);
    float d0, d1;
    bool a, b;
    int k;
    if (!s->follow) return true;
    for (k = 0; k < 3 && s->env_mask != 0xFFFFFFFFu; k++)
        if (!env_ok(ps, s, e->sector, tc_m34_point(m, s->corner[k]), &d0)) return false;
    if (s->deflector && !pool->deflector_placed) {
        pool->deflector_a = tc_v3_add(pool->deflector_a, t);
        pool->deflector_b = tc_v3_add(pool->deflector_b, t);
        pool->deflector_placed = true;
    }
    if (!s->sphere) {
        tc_vec3 lo = tc_v3_add(t, s->offset_min), hi = tc_v3_add(t, s->offset_max);
        float top;
        a = env_ok(ps, s, e->sector, lo, &d0);
        b = env_ok(ps, s, e->sector, hi, &d1);
        if (a == b) {
            top = hi.z;
            if (!a) return false;
        } else {
            top = d0 + lo.z;
            if (!a) {
                top = hi.z;
                lo.z = d0 + lo.z;
            }
        }
        if (!(lo.z < top)) return false;
        pool->box_min = lo;
        pool->box_max = tc_v3(hi.x, hi.y, top);
        return true;
    }
    /* the sphere: tested at its bottom and its top (the exe's second point is stale stack data; no retail sphere
     * source has a mask that can fail) */
    {
        tc_vec3 c = tc_m34_point(m, s->offset_min);
        tc_vec3 bottom = tc_v3(c.x, c.y, c.z - s->radius), top = tc_v3(c.x, c.y, c.z + s->radius);
        a = env_ok(ps, s, e->sector, bottom, &d0);
        b = env_ok(ps, s, e->sector, top, &d1);
        if (a != b) c.z = a ? d0 + bottom.z : d0 + c.z;
        else if (!a) return false;
        pool->centre = c;
    }
    return true;
}

/* FUN_0047f360: whether the generator makes particles this tick. */
static bool emits(tc_particles *ps, const tc_particle_source *s, tc_particle_pool *pool) {
    switch (s->emit) {
    case 1: return true;
    case 2: return rnd(ps) < s->chance;
    case 3:
        if (pool->counter < s->every) {
            pool->counter++;
            return false;
        }
        pool->counter = s->modulo > 0 ? (int16_t)((pool->counter + 1) % s->modulo) : 0;
        return true;
    default: return false;
    }
}

/* FUN_0047f430 / 0047fb10 / 00480300: one new particle. */
static void spawn(tc_particles *ps, const tc_particle_source *s, tc_particle_pool *pool, const tc_mat34 *m) {
    uint32_t k = pool->count++;
    tc_vec3 v = tc_v3_add(tc_v3_scale(s->direction, s->speed), rnd3(ps, s->jitter)), p;
    if (s->transform == 0 || s->transform == 2) v = tc_m34_vector(m, v);
    if (s->position_mode == 1) {
        float a = rnd(ps), b = rnd(ps);
        p = tc_v3_add(tc_v3_add(tc_v3_scale(s->corner[2], b), tc_v3_scale(s->corner[1], a)),
                      tc_v3_scale(s->corner[0], 1 - a - b));
    } else if (s->position_mode == 2) {
        float a = rnd(ps), b = rnd(ps), c = rnd(ps);
        p = tc_v3_add(tc_v3_add(tc_v3_scale(s->corner[3], c), tc_v3_scale(s->corner[2], b)),
                      tc_v3_add(tc_v3_scale(s->corner[1], a), tc_v3_scale(s->corner[0], 1 - a - b - c)));
    } else {
        p = s->corner[0];
    }
    if (s->type == TC_PRT_SPRITES && s->spread) p.z -= rndf(ps, 0, 30);
    if (s->transform == 2) p = tc_m34_point(m, p);
    else if (s->transform == 1) p = tc_v3_add(p, translation(m));
    else p = tc_m34_vector(m, p);
    pool->position[k] = pool->previous[k] = p;
    pool->velocity[k] = v;
    pool->death[k] = s->type == TC_PRT_POINTS || s->life_mode == 0 ? FLT_MAX
                     : s->life_mode == 1 ? s->life + ps->time : rndf(ps, s->life, s->life_max) + ps->time;
}

/* FUN_0047fa00 / 004801d0 / 00480ea0: the generator's pass at clock `time`. */
static void generate(tc_particles *ps, const tc_particle_source *s, tc_particle_pool *pool, const tc_mat34 *m) {
    uint32_t cap = capacity(s);
    int n;
    if (!emits(ps, s, pool)) return;
    n = s->count_mode == 1 ? rndi(ps, s->count_lo, s->count_hi) : s->count;
    if (n <= 0 || !allocate(pool, cap)) return;
    if (pool->count + (uint32_t)n > cap) n = (int)cap - (int)pool->count;
    while (n-- > 0) spawn(ps, s, pool, m);
}

static void kill(tc_particle_pool *pool, uint32_t k) {
    uint32_t last = --pool->count;
    pool->position[k] = pool->position[last];
    pool->previous[k] = pool->previous[last];
    pool->velocity[k] = pool->velocity[last];
    pool->death[k] = pool->death[last];
}

/* FUN_00466580, or the sphere's test: whether the field still holds p. */
static bool in_field(const tc_particle_source *s, const tc_particle_pool *pool, tc_vec3 p) {
    if (s->sphere) return tc_v3_length_sq(tc_v3_sub(p, pool->centre)) <= s->radius * s->radius;
    return p.x >= pool->box_min.x && p.x <= pool->box_max.x && p.y >= pool->box_min.y && p.y <= pool->box_max.y &&
           p.z >= pool->box_min.z && p.z <= pool->box_max.z;
}

/* FUN_00480e50: the deflector's box (open below: up to a.z). */
static bool in_deflector(const tc_particle_pool *pool, tc_vec3 p) {
    return p.z <= pool->deflector_a.z && p.x >= pool->deflector_a.x && p.x <= pool->deflector_b.x &&
           p.y >= pool->deflector_a.y && p.y <= pool->deflector_b.y;
}

/* FUN_0047f200 -> 0047f750 / 0047feb0 / 00480680 / 004809a0: the pool's particles move a tick under the field. */
static void move(tc_particles *ps, const tc_particle_source *s, tc_particle_pool *pool, float dt) {
    uint32_t k = 0, row = 48;
    float r[6] = {0};
    bool deflect = s->type == TC_PRT_SPRITES && s->deflector &&
                   (pool->deflector_a.x != pool->deflector_b.x || pool->deflector_a.y != pool->deflector_b.y ||
                    pool->deflector_a.z != pool->deflector_b.z);
    float cx = (pool->deflector_a.x + pool->deflector_b.x) * 0.5f, cy = (pool->deflector_a.y + pool->deflector_b.y) * 0.5f;
    while (k < pool->count) {
        tc_vec3 *p = &pool->position[k], *v = &pool->velocity[k], a;
        pool->previous[k] = *p;
        if (s->type != TC_PRT_POINTS && pool->death[k] <= ps->time) {
            kill(pool, k);
            continue;
        }
        if (++row > 47) {
            tc_vec3 j = rnd3(ps, s->field_jitter);
            row = 0;
            r[0] = j.x, r[1] = j.y, r[2] = j.z, r[3] = -j.x, r[4] = -j.y, r[5] = -j.z;
        }
        a = tc_v3_add(tc_v3(r[JITTER[row][0]], r[JITTER[row][1]], r[JITTER[row][2]]), s->acceleration);
        *v = tc_v3_add(*v, tc_v3_scale(a, dt));
        if (deflect && in_deflector(pool, *p)) { /* along the ground, away from the deflector's middle, as fast */
            float speed = tc_v3_length(*v), dx = p->x - cx, dy = p->y - cy, f;
            if (dx == 0 && dy == 0) dx += 0.1f, dy += 0.5f;
            f = 1.0f / sqrtf(dx * dx + dy * dy);
            p->x += speed * f * dx * dt;
            p->y += speed * f * dy * dt;
        } else {
            *p = tc_v3_add(*p, tc_v3_scale(*v, dt));
        }
        if (!in_field(s, pool, *p)) {
            kill(pool, k);
            continue;
        }
        k++;
    }
}

static bool listed(const tc_particle_emitter *e, uint32_t k) {
    for (uint32_t i = 0; i < k; i++)
        if (e[i].source == e[k].source) return true;
    return false;
}

void tc_particles_tick(tc_particles *ps, const tc_particle_emitter *emitters, uint32_t count, float dt) {
    uint32_t i;
    if (!ps->pools) return;
    if (count > TC_PARTICLE_MAX_EMITTERS) count = TC_PARTICLE_MAX_EMITTERS;
    /* GAM_00428070 -> GAM_0042df50, each perso's frame: the field placed, then the generator */
    for (i = 0; i < count; i++) {
        const tc_particle_emitter *e = &emitters[i];
        const tc_particle_source *s;
        tc_particle_pool *pool;
        bool ok;
        if (e->source <= 0 || (uint32_t)e->source > ps->pool_count) continue;
        s = &ps->level->particle_sources[e->source - 1];
        pool = &ps->pools[e->source - 1];
        if (e->serial != ps->seen_serial[i] || e->source != ps->seen_source[i]) {
            /* GAM_0042deb0 / 0042de80 ran: switched on (or set) again, the pre-warm is asked for again */
            if (e->on) pool->warm = false;
            ps->seen_serial[i] = e->serial;
            ps->seen_source[i] = e->source;
        }
        ok = place(ps, s, pool, e);
        if (!e->on || !ok) continue;
        if (s->type == TC_PRT_SPRITES && !pool->warm) {
            float t = ps->time;
            for (int k = 0; k < TC_PARTICLE_PREWARM; k++) {
                generate(ps, s, pool, &e->matrix);
                ps->time += 0.02f;
            }
            ps->time = t;
            pool->warm = true;
        } else {
            generate(ps, s, pool, &e->matrix);
        }
    }
    /* GAM_0042ea30 -> GAM_0042e7e0, after: every source's particles move */
    for (i = 0; i < count; i++) {
        const tc_particle_emitter *e = &emitters[i];
        if (e->source <= 0 || (uint32_t)e->source > ps->pool_count || listed(emitters, i)) continue;
        move(ps, &ps->level->particle_sources[e->source - 1], &ps->pools[e->source - 1], dt);
    }
    memcpy(ps->emitters, emitters, count * sizeof(*emitters));
    ps->emitter_count = count;
    ps->time += dt;
}

uint32_t tc_particles_alive(const tc_particles *ps) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < ps->emitter_count; i++) {
        const tc_particle_emitter *e = &ps->emitters[i];
        if (e->source > 0 && (uint32_t)e->source <= ps->pool_count && !listed(ps->emitters, i))
            n += ps->pools[e->source - 1].count;
    }
    return n;
}

static tc_particle_vertex vertex(tc_vec3 p, float u, float v, const float rgb[3], float a) {
    tc_particle_vertex x;
    x.position[0] = p.x, x.position[1] = p.y, x.position[2] = p.z;
    x.uv[0] = u, x.uv[1] = v;
    x.color[0] = rgb[0], x.color[1] = rgb[1], x.color[2] = rgb[2], x.color[3] = a;
    return x;
}

/* A quad as two triangles: corners 0 1 2, 2 1 3. */
static void quad(tc_particle_vertex *o, tc_particle_vertex a, tc_particle_vertex b, tc_particle_vertex c, tc_particle_vertex d) {
    o[0] = a, o[1] = b, o[2] = c, o[3] = c, o[4] = b, o[5] = d;
}

uint32_t tc_particles_build(const tc_particles *ps, float t, tc_vec3 eye, tc_vec3 right, tc_vec3 up, float pixel,
                            const bool *hidden, tc_particle_vertex *out, uint32_t max_vertices, uint32_t *vertex_count,
                            tc_particle_batch *batches) {
    uint32_t nb = 0, nv = 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    for (uint32_t i = 0; i < ps->emitter_count && nb < TC_PARTICLE_MAX_BATCHES; i++) {
        const tc_particle_emitter *e = &ps->emitters[i];
        const tc_particle_source *s;
        const tc_particle_pool *pool;
        tc_particle_batch *b;
        float rgb[3];
        if (e->source <= 0 || (uint32_t)e->source > ps->pool_count || listed(ps->emitters, i) || (hidden && hidden[i])) continue;
        s = &ps->level->particle_sources[e->source - 1];
        pool = &ps->pools[e->source - 1];
        if (!pool->count || s->type == TC_PRT_POINTS) continue;
        /* the vertex light under the original's formula with the material's ambient (sprites and lines carry no
         * normals worth lighting; every retail source's ambient makes it full white) */
        for (int c = 0; c < 3; c++) rgb[c] = fminf(1.0f, s->ambient[c] * 512.0f / 255.0f);
        b = &batches[nb];
        b->texture = s->type == TC_PRT_SPRITES ? s->texture : -1;
        b->additive = s->additive;
        b->first = nv;
        for (uint32_t k = 0; k < pool->count && nv + 6 <= max_vertices; k++) {
            tc_vec3 p0 = pool->previous[k], p1 = pool->position[k];
            tc_vec3 p = tc_v3_add(p0, tc_v3_scale(tc_v3_sub(p1, p0), t));
            if (s->type == TC_PRT_SPRITES) {
                tc_vec3 rx = tc_v3_scale(right, s->half_size[0]), uy = tc_v3_scale(up, s->half_size[1]);
                quad(&out[nv], vertex(tc_v3_sub(tc_v3_sub(p, rx), uy), 0, 1, rgb, 1),
                     vertex(tc_v3_sub(tc_v3_add(p, rx), uy), 1, 1, rgb, 1),
                     vertex(tc_v3_add(tc_v3_sub(p, rx), uy), 0, 0, rgb, 1),
                     vertex(tc_v3_add(tc_v3_add(p, rx), uy), 1, 0, rgb, 1));
            } else { /* a line, FUN_00448bf0: the tail (last tick's place) transparent, the head opaque */
                tc_vec3 tail = tc_v3_sub(p, tc_v3_sub(p1, p0)), along = tc_v3_sub(p, tail);
                tc_vec3 side = tc_v3_cross(along, tc_v3_sub(p, eye));
                float l = tc_v3_length(side), w = pixel * tc_v3_distance(p, eye) * 0.5f;
                if (!(l > 1e-9f)) continue;
                side = tc_v3_scale(side, w / l);
                quad(&out[nv], vertex(tc_v3_sub(tail, side), 0, 0, rgb, 0), vertex(tc_v3_add(tail, side), 0, 0, rgb, 0),
                     vertex(tc_v3_sub(p, side), 0, 0, rgb, 1), vertex(tc_v3_add(p, side), 0, 0, rgb, 1));
            }
            nv += 6;
        }
        b->count = nv - b->first;
        if (b->count) nb++;
    }
    *vertex_count = nv;
    return nb;
}
