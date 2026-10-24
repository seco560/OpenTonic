#include "tc_internal.h"
#include "tonic/tc_collision.h"
#include "tonic/tc_alloc.h"
#include <string.h>

/* floor((v - origin) / cell) clamped to 0..n-1, without float->int overflow on far-away input. */
static int32_t cell_index(float v, float origin, float cell, int32_t n) {
    float f = floorf((v - origin) / cell);
    if (!(f >= 0)) return 0; /* also NaN */
    if (f >= (float)(n - 1)) return n - 1;
    return (int32_t)f;
}

typedef void (*cell_fn)(int32_t cell, void *user);

static void for_cells(const tc_collision *col, float lox, float loy, float hix, float hiy, cell_fn fn, void *user) {
    int32_t x0 = cell_index(lox, col->origin_x, col->cell, col->nx), x1 = cell_index(hix, col->origin_x, col->cell, col->nx);
    int32_t y0 = cell_index(loy, col->origin_y, col->cell, col->ny), y1 = cell_index(hiy, col->origin_y, col->cell, col->ny);
    int32_t x, y;
    if (x0 > x1 || y0 > y1) return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) fn(y * col->nx + x, user);
}

typedef struct fill_ctx { int32_t *counts; int32_t *fill; int32_t *cell_tris; int32_t tri; } fill_ctx;
static void count_cell(int32_t c, void *user) { ((fill_ctx *)user)->counts[c + 1] += 1; }
static void fill_cell(int32_t c, void *user) {
    fill_ctx *f = (fill_ctx *)user;
    f->cell_tris[f->fill[c]] = f->tri;
    f->fill[c] += 1;
}

void tc_collision_free(tc_collision *col) {
    if (!col) return;
    tc_free(col->tris);
    tc_free(col->cell_start);
    tc_free(col->cell_tris);
    tc_free(col->stamp);
    memset(col, 0, sizeof *col);
}

int tc_collision_init(tc_collision *col, const tc_vec3 *vertices, uint32_t vertex_count, const tc_floor *floors) {
    uint32_t i, n = 0, cells;
    tc_vec3 lo, hi;
    float ex, ey;
    int32_t *fill = NULL;
    fill_ctx ctx;

    memset(col, 0, sizeof *col);
    if (vertex_count >= 3) {
        col->tris = (tc_tri *)tc_calloc(vertex_count / 3, sizeof(tc_tri));
        if (!col->tris) return -1;
    }
    for (i = 0; i + 2 < vertex_count; i += 3) {
        tc_vec3 a = vertices[i], b = vertices[i + 1], c = vertices[i + 2];
        tc_vec3 nrm = tc_v3_cross(tc_v3_sub(b, a), tc_v3_sub(c, a));
        float len = tc_v3_length(nrm);
        tc_tri *t;
        if (!(len > 1e-8f) || !isfinite(len)) continue;
        t = &col->tris[n++];
        t->a = a; t->b = b; t->c = c;
        t->source = i / 3;
        t->normal = tc_v3_div(nrm, len);
        t->lo = tc_v3_min(a, tc_v3_min(b, c));
        t->hi = tc_v3_max(a, tc_v3_max(b, c));
        if (floors) t->floor = floors[i / 3];
        else { t->floor.grip = 1; t->floor.rebound = 1; }
    }
    col->tri_count = n;
    lo = tc_v3(INFINITY, INFINITY, INFINITY);
    hi = tc_v3(-INFINITY, -INFINITY, -INFINITY);
    for (i = 0; i < n; i++) { lo = tc_v3_min(lo, col->tris[i].lo); hi = tc_v3_max(hi, col->tris[i].hi); }
    if (n == 0) { lo = tc_v3(0, 0, 0); hi = lo; }
    col->bounds_min = lo;
    col->bounds_max = hi;
    ex = hi.x - lo.x;
    ey = hi.y - lo.y;
    col->cell = fmaxf(2, fmaxf(ex, ey) / 256);
    col->origin_x = lo.x;
    col->origin_y = lo.y;
    col->nx = (int32_t)(ex / col->cell) + 1;
    col->ny = (int32_t)(ey / col->cell) + 1;
    if (col->nx < 1) col->nx = 1;
    if (col->ny < 1) col->ny = 1;
    cells = (uint32_t)(col->nx * col->ny);

    col->stamp = (uint32_t *)tc_calloc(n ? n : 1, sizeof(uint32_t));
    col->cell_start = (int32_t *)tc_calloc(cells + 1, sizeof(int32_t));
    fill = (int32_t *)tc_calloc(cells + 1, sizeof(int32_t));
    if (!col->stamp || !col->cell_start || !fill) goto oom;
    ctx.counts = col->cell_start;
    for (i = 0; i < n; i++) {
        const tc_tri *t = &col->tris[i];
        for_cells(col, t->lo.x, t->lo.y, t->hi.x, t->hi.y, count_cell, &ctx);
    }
    for (i = 0; i < cells; i++) col->cell_start[i + 1] += col->cell_start[i];
    memcpy(fill, col->cell_start, (cells + 1) * sizeof(int32_t));
    if (col->cell_start[cells] > 0) {
        col->cell_tris = (int32_t *)tc_calloc((size_t)col->cell_start[cells], sizeof(int32_t));
        if (!col->cell_tris) goto oom;
    }
    ctx.fill = fill;
    ctx.cell_tris = col->cell_tris;
    for (i = 0; i < n; i++) {
        const tc_tri *t = &col->tris[i];
        ctx.tri = (int32_t)i;
        for_cells(col, t->lo.x, t->lo.y, t->hi.x, t->hi.y, fill_cell, &ctx);
    }
    tc_free(fill);
    return 0;
oom:
    tc_free(fill);
    tc_collision_free(col);
    return -1;
}

typedef struct query_ctx { tc_collision *col; tc_vec3 lo, hi; tc_tri_fn fn; void *user; } query_ctx;

static void query_cell(int32_t c, void *user) {
    query_ctx *q = (query_ctx *)user;
    tc_collision *col = q->col;
    int32_t k;
    for (k = col->cell_start[c]; k < col->cell_start[c + 1]; k++) {
        int32_t i = col->cell_tris[k];
        const tc_tri *t;
        if (col->stamp[i] == col->query) continue;
        col->stamp[i] = col->query;
        t = &col->tris[i];
        if (t->lo.x <= q->hi.x && t->lo.y <= q->hi.y && t->lo.z <= q->hi.z &&
            t->hi.x >= q->lo.x && t->hi.y >= q->lo.y && t->hi.z >= q->lo.z)
            q->fn(t, q->user);
    }
}

void tc_collision_query(tc_collision *col, tc_vec3 lo, tc_vec3 hi, tc_tri_fn fn, void *user) {
    query_ctx q;
    if (col->tri_count == 0) return;
    col->query += 1;
    if (col->query == 0) { memset(col->stamp, 0, col->tri_count * sizeof(uint32_t)); col->query = 1; }
    q.col = col; q.lo = lo; q.hi = hi; q.fn = fn; q.user = user;
    for_cells(col, lo.x, lo.y, hi.x, hi.y, query_cell, &q);
}

typedef struct ray_ctx { tc_vec3 o, d; float max_distance, best; const tc_tri *tri; } ray_ctx;

static void ray_tri(const tc_tri *t, void *user) {
    ray_ctx *r = (ray_ctx *)user;
    tc_vec3 e1 = tc_v3_sub(t->b, t->a), e2 = tc_v3_sub(t->c, t->a);
    tc_vec3 p = tc_v3_cross(r->d, e2), s, q;
    float det = tc_v3_dot(e1, p), inv, u, v, dist;
    if (!(fabsf(det) > 1e-9f)) return;
    inv = 1 / det;
    s = tc_v3_sub(r->o, t->a);
    u = tc_v3_dot(s, p) * inv;
    if (!(u >= 0 && u <= 1)) return;
    q = tc_v3_cross(s, e1);
    v = tc_v3_dot(r->d, q) * inv;
    if (!(v >= 0 && u + v <= 1)) return;
    dist = tc_v3_dot(e2, q) * inv;
    if (dist >= 0 && dist <= r->max_distance && dist < r->best) { r->best = dist; r->tri = t; }
}

int tc_collision_raycast(tc_collision *col, tc_vec3 o, tc_vec3 dir, float max_distance, tc_hit *hit) {
    ray_ctx r;
    tc_vec3 e = tc_v3_add(o, tc_v3_scale(dir, max_distance));
    r.o = o; r.d = dir; r.max_distance = max_distance; r.best = INFINITY; r.tri = NULL;
    tc_collision_query(col, tc_v3_adds(tc_v3_min(o, e), -1e-3f), tc_v3_adds(tc_v3_max(o, e), 1e-3f), ray_tri, &r);
    if (!r.tri) return 0;
    if (hit) { hit->distance = r.best; hit->normal = r.tri->normal; hit->tri = r.tri; }
    return 1;
}

tc_vec3 tc_closest_point_on_triangle(tc_vec3 a, tc_vec3 b, tc_vec3 c, tc_vec3 p) {
    tc_vec3 ab = tc_v3_sub(b, a), ac = tc_v3_sub(c, a), ap = tc_v3_sub(p, a), bp, cp;
    float d1 = tc_v3_dot(ab, ap), d2 = tc_v3_dot(ac, ap), d3, d4, d5, d6, va, vb, vc, denom;
    if (d1 <= 0 && d2 <= 0) return a;
    bp = tc_v3_sub(p, b);
    d3 = tc_v3_dot(ab, bp);
    d4 = tc_v3_dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return tc_v3_add(a, tc_v3_scale(ab, d1 / (d1 - d3)));
    cp = tc_v3_sub(p, c);
    d5 = tc_v3_dot(ab, cp);
    d6 = tc_v3_dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return tc_v3_add(a, tc_v3_scale(ac, d2 / (d2 - d6)));
    va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
        return tc_v3_add(b, tc_v3_scale(tc_v3_sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    denom = 1 / (va + vb + vc);
    return tc_v3_add(tc_v3_add(a, tc_v3_scale(ab, vb * denom)), tc_v3_scale(ac, vc * denom));
}
