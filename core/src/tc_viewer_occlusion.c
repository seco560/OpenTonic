/* Occlusion culling (port: the levels without sectors, after tc_viewer_set_aspect's frustum culling): what is in the
 * camera's view but wholly behind something solid is left out of the frame's draws.
 *
 * How: each frame the nearest big triangles of the opaque statics (the occluders: walls, ground) are drawn on the CPU
 * into a small depth picture of the view (TC_OCC_W wide, no colours). Then every draw's bounding box (and every cell's
 * of a wide static) is laid over that picture: when all of it lies behind what the picture holds, nothing of it could
 * reach the screen, and it is left out. A box, not the triangles: the test costs the same for 12 triangles or 90000.
 *
 * Erring towards drawing: the box is tested at its nearest corner; a box that reaches the near plane is drawn; the
 * picture is shrunk by a pixel all round (each pixel takes the farthest of its 3 x 3: an occluder's edge crossing a
 * pixel never hides what shows beside it, and a sloped occluder counts at its farthest in the pixel); and only what no
 * texture can see through is an occluder (no cut-outs, no blended materials, no water, no billboards, no persos: those
 * move). What remains possible: a gap between two occluders thinner than a picture pixel (a few screen pixels) can
 * close. tests/viewcheck.c measures it against every pixel of a full-size software render. */
#include "tc_viewer_internal.h"

#define TC_OCC_W 256            /* the picture's width; its height by the window's shape */
#define TC_OCC_H_MIN 32
#define TC_OCC_H_MAX 512
#define TC_OCC_MIN_AREA 0.25f   /* triangles under this many square units are never occluders (not kept) */
#define TC_OCC_MIN_PIXELS 48.0f /* an occluder triangle is drawn when it would cover about this much of the picture */
#define TC_OCC_BUDGET 6000u     /* triangles drawn into the picture per frame at most, nearest sets first */
#define TC_OCC_BIAS 1.002f      /* hidden: behind the picture by more than this share of its depth */
#define TC_OCC_BUCKETS 32
#define TC_OCC_TILE 8
#define TC_OCC_TILES_X (TC_OCC_W / TC_OCC_TILE)
#define TC_OCC_TILES_Y (TC_OCC_H_MAX / TC_OCC_TILE)

typedef struct occ_tri { float p[9], area; } occ_tri;

/* The occluders of one cell of a wide static, or of a whole static: tris [first, first + count), largest first. */
typedef struct occ_set { float lo[3], hi[3]; uint32_t first, count; } occ_set;

struct tc_viewer_occ {
    occ_tri *tris;
    uint32_t tri_count;
    occ_set *sets;
    uint32_t set_count;
    uint32_t *order;            /* this frame's sets in view, nearest first */
    float *dist;                /* per set, this frame: its distance from the eye (< 0: out of view) */
    uint8_t *bucket;            /* per set, this frame: its distance's bucket */
    float *depth, *raw;         /* TC_OCC_W x TC_OCC_H_MAX: 1 / view depth (0: nothing), shrunk and as drawn */
    float *tile;                /* TC_OCC_TILES_X x TC_OCC_TILES_Y: the farthest of each TC_OCC_TILE square of depth */
    int w, h;
    bool ready;                 /* this frame's picture is made */
    tc_vec3 eye, f, r, u;
    float tx, ty, near_z;
};

/* ---- load ---- */

static float tri_area(const tc_mesh *m, const uint32_t *ix) {
    const float *a, *b, *c;
    tc_vec3 n;
    if (ix[0] >= m->vertex_count || ix[1] >= m->vertex_count || ix[2] >= m->vertex_count) return 0;
    a = m->vertices[ix[0]].position, b = m->vertices[ix[1]].position, c = m->vertices[ix[2]].position;
    n = tc_v3_cross(tc_v3(b[0] - a[0], b[1] - a[1], b[2] - a[2]), tc_v3(c[0] - a[0], c[1] - a[1], c[2] - a[2]));
    return 0.5f * tc_v3_length(n);
}

/* The occluder triangles of m's indices [first, first + count): counted (out NULL), or written to out. */
static uint32_t gather(const tc_mesh *m, uint32_t first, uint32_t count, occ_tri *out) {
    uint32_t t, n = 0;
    int k;
    for (t = first; t + 2 < first + count; t += 3) {
        float area = tri_area(m, m->indices + t);
        if (!(area >= TC_OCC_MIN_AREA)) continue;
        if (out) {
            for (k = 0; k < 3; k++) memcpy(out[n].p + 3 * k, m->vertices[m->indices[t + k]].position, 3 * sizeof(float));
            out[n].area = area;
        }
        n++;
    }
    return n;
}

/* Largest first (a heap sort: in place, no recursion). */
static void sort_by_area(occ_tri *a, uint32_t n) {
    uint32_t start, end, root, child;
    occ_tri t;
    if (n < 2) return;
    for (start = n / 2, end = n;;) {
        if (start > 0) start--;
        else {
            if (--end == 0) return;
            t = a[end], a[end] = a[0], a[0] = t;
        }
        for (root = start; (child = 2 * root + 1) < end; root = child) { /* a min-heap: the smallest goes to the end */
            if (child + 1 < end && a[child + 1].area < a[child].area) child++;
            if (a[root].area <= a[child].area) break;
            t = a[root], a[root] = a[child], a[child] = t;
        }
    }
}

/* Whether static mesh i can hide things: opaque to the last texel, still, in the level's own space. */
static bool occluder_mesh(const tc_viewer *v, uint32_t i) {
    const tc_mesh *m = v->meshes[i];
    if (!drawable(m) || m->index_count % 3) return false;
    if (m->surface.flags & (TC_FLAG_TRANSPARENT | TC_FLAG_ALPHA_TEST | TC_FLAG_BILLBOARD | TCX_FLAG_ROUND_CAMERA)) return false;
    if ((v->split && v->split[i]) || (v->mesh_water && v->mesh_water[i])) return false;
    return true;
}

/* Set k's triangles, at tris[at]: returns how many. pass 0 counts only. */
static uint32_t fill_set(tc_viewer *v, struct tc_viewer_occ *o, uint32_t k, const tc_mesh *m, uint32_t first, uint32_t count,
                         uint32_t at, bool write) {
    uint32_t n = gather(m, first, count, write ? o->tris + at : NULL), q;
    int c;
    (void)v;
    if (write && n) {
        occ_set *s = &o->sets[k];
        sort_by_area(o->tris + at, n);
        s->first = at;
        s->count = n;
        for (c = 0; c < 3; c++) s->lo[c] = INFINITY, s->hi[c] = -INFINITY;
        for (q = 0; q < 3 * n; q++)
            for (c = 0; c < 3; c++) {
                float x = o->tris[at + q / 3].p[3 * (q % 3) + c];
                s->lo[c] = fminf(s->lo[c], x), s->hi[c] = fmaxf(s->hi[c], x);
            }
    }
    return n;
}

void tcv_occ_free(tc_viewer *v) {
    struct tc_viewer_occ *o = v->occ;
    if (o) {
        tc_free(o->tris);
        tc_free(o->sets);
        tc_free(o->order);
        tc_free(o->dist);
        tc_free(o->bucket);
        tc_free(o->tile);
        tc_free(o->depth);
        tc_free(o->raw);
        tc_free(o);
    }
    tc_free(v->mesh_box);
    v->occ = NULL;
    v->mesh_box = NULL;
}

/* Every mesh's vertex box and the occluder sets. Out of memory: no occlusion culling (the level still draws), true. */
bool tcv_occ_init(tc_viewer *v) {
    const tc_level *l = v->level;
    struct tc_viewer_occ *o;
    uint32_t i, k, pass, sets = 0, tris = 0;
    int c;
    if (v->sectors || !l->mesh_count || !v->mesh_count) return true;
    v->mesh_box = (float *)tc_calloc((size_t)v->mesh_count * 6, sizeof(float));
    o = (struct tc_viewer_occ *)tc_calloc(1, sizeof(*o));
    if (!v->mesh_box || !o) goto fail;
    v->occ = o;
    for (i = 0; i < v->mesh_count; i++) {
        const tc_mesh *m = v->meshes[i];
        float *b = v->mesh_box + 6 * i;
        if (!m || !m->vertex_count || !m->vertices) continue;
        for (c = 0; c < 3; c++) b[c] = INFINITY, b[3 + c] = -INFINITY;
        for (k = 0; k < m->vertex_count; k++)
            for (c = 0; c < 3; c++)
                b[c] = fminf(b[c], m->vertices[k].position[c]), b[3 + c] = fmaxf(b[3 + c], m->vertices[k].position[c]);
    }
    for (pass = 0; pass < 2; pass++) {
        sets = tris = 0;
        for (i = 0; i < l->mesh_count; i++) {
            const tc_mesh *m = v->meshes[i];
            uint32_t cells = v->mesh_cells ? v->mesh_cells[2 * i + 1] : 0, n;
            if (!occluder_mesh(v, i)) continue;
            if (!cells) {
                n = fill_set(v, o, sets, m, 0, m->index_count, tris, pass == 1);
                if (n) sets++, tris += n;
                continue;
            }
            for (k = 0; k < cells; k++) {
                const struct tc_viewer_cell *cell = &v->cells[v->mesh_cells[2 * i] + k];
                n = fill_set(v, o, sets, m, cell->first, cell->count, tris, pass == 1);
                if (n) sets++, tris += n;
            }
        }
        if (pass == 0) {
            if (!sets) return true; /* nothing can hide anything: v->occ stays, with no sets */
            o->tris = (occ_tri *)tc_calloc(tris, sizeof(*o->tris));
            o->sets = (occ_set *)tc_calloc(sets, sizeof(*o->sets));
            o->order = (uint32_t *)tc_calloc(sets, sizeof(*o->order));
            o->dist = (float *)tc_calloc(sets, sizeof(*o->dist));
            o->bucket = (uint8_t *)tc_calloc(sets, 1);
            o->tile = (float *)tc_calloc((size_t)TC_OCC_TILES_X * TC_OCC_TILES_Y, sizeof(float));
            o->depth = (float *)tc_calloc((size_t)TC_OCC_W * TC_OCC_H_MAX, sizeof(float));
            o->raw = (float *)tc_calloc((size_t)TC_OCC_W * TC_OCC_H_MAX, sizeof(float));
            if (!o->tris || !o->sets || !o->order || !o->dist || !o->bucket || !o->tile || !o->depth || !o->raw) goto fail;
        }
    }
    o->tri_count = tris;
    o->set_count = sets;
    return true;
fail:
    tcv_occ_free(v);
    return true;
}

/* ---- the frame's picture ---- */

/* Plain comparisons: the C library's min / max / rounding are calls on a baseline x86 build, and these loops run per pixel. */
static inline float lessf(float a, float b) { return a < b ? a : b; }
static inline float moref(float a, float b) { return a > b ? a : b; }
static inline double lessd(double a, double b) { return a < b ? a : b; }
static inline double mored(double a, double b) { return a > b ? a : b; }
static inline int ceil_pos(double x) { int i = (int)x; return (double)i < x ? i + 1 : i; } /* x >= 0 */

typedef struct occ_vert { double x, y, z; } occ_vert; /* view space: right, up, depth */

/* One triangle of the picture (screen x, y in pixels, iw = 1 / depth): every pixel whose centre it covers takes its
 * depth when nearer; row by row, the run of pixels between its edges. The edges in doubles: a wall cut at the near
 * plane reaches millions of pixels off the picture. */
static void raster(struct tc_viewer_occ *o, const double *sx, const double *sy, const double *iw) {
    double area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
    double minx = lessd(sx[0], lessd(sx[1], sx[2])), maxx = mored(sx[0], mored(sx[1], sx[2]));
    double miny = lessd(sy[0], lessd(sy[1], sy[2])), maxy = mored(sy[0], mored(sy[1], sy[2]));
    double a[3], b[3], c[3], ia[3], gx, gy, g0, inv;
    int x0, x1, y0, y1, x, y, k;
    if (!(area != 0) || !(maxx >= 0) || !(maxy >= 0) || !(minx <= o->w) || !(miny <= o->h)) return;
    /* the pixels whose centres (x + 0.5) lie in [min, max] */
    x0 = minx <= 0.5 ? 0 : ceil_pos(minx - 0.5);
    y0 = miny <= 0.5 ? 0 : ceil_pos(miny - 0.5);
    x1 = maxx >= o->w - 0.5 ? o->w - 1 : maxx < 0.5 ? -1 : (int)(maxx - 0.5);
    y1 = maxy >= o->h - 0.5 ? o->h - 1 : maxy < 0.5 ? -1 : (int)(maxy - 0.5);
    if (x0 > x1 || y0 > y1) return;
    inv = 1 / area;
    for (k = 0; k < 3; k++) { /* edge k: opposite corner k; its value is corner k's weight, >= 0 inside */
        int p = (k + 1) % 3, q = (k + 2) % 3;
        a[k] = (sy[p] - sy[q]) * inv;
        b[k] = (sx[q] - sx[p]) * inv;
        c[k] = (sx[p] * sy[q] - sx[q] * sy[p]) * inv;
        ia[k] = a[k] != 0 ? 1 / a[k] : 0;
    }
    gx = a[0] * iw[0] + a[1] * iw[1] + a[2] * iw[2];
    gy = b[0] * iw[0] + b[1] * iw[1] + b[2] * iw[2];
    g0 = c[0] * iw[0] + c[1] * iw[1] + c[2] * iw[2];
    for (y = y0; y <= y1; y++) {
        double py = y + 0.5, lo = x0, hi = x1; /* the run, in pixel indices */
        float *row = o->raw + (size_t)y * TC_OCC_W, z, step = (float)gx;
        int xs, xe;
        for (k = 0; k < 3; k++) { /* a px + (b py + c) >= 0 at px = x + 0.5 */
            double rest = b[k] * py + c[k];
            if (a[k] > 0) lo = mored(lo, -rest * ia[k] - 0.5);
            else if (a[k] < 0) hi = lessd(hi, -rest * ia[k] - 0.5);
            else if (rest < 0) hi = -1;
        }
        if (!(lo <= hi)) continue;
        xs = ceil_pos(lo), xe = (int)hi; /* 0 <= lo <= hi */
        z = (float)(gx * (xs + 0.5) + gy * py + g0);
        for (x = xs; x <= xe; x++, z += step)
            if (z > row[x]) row[x] = z;
    }
}

/* A world triangle into the picture: cut at the near plane, the rest as a fan. */
static void draw_tri(struct tc_viewer_occ *o, const float *p) {
    occ_vert in[3], out[4];
    double sx[4], sy[4], iw[4], nz = o->near_z;
    int i, n = 0;
    for (i = 0; i < 3; i++) {
        tc_vec3 rel = tc_v3_sub(tc_v3(p[3 * i], p[3 * i + 1], p[3 * i + 2]), o->eye);
        in[i].x = tc_v3_dot(rel, o->r);
        in[i].y = tc_v3_dot(rel, o->u);
        in[i].z = tc_v3_dot(rel, o->f);
    }
    if (in[0].z < nz && in[1].z < nz && in[2].z < nz) return;
    for (i = 0; i < 3; i++) {
        const occ_vert *a = &in[i], *b = &in[(i + 1) % 3];
        bool ia = a->z >= nz, ib = b->z >= nz;
        if (ia) out[n++] = *a;
        if (ia != ib) {
            double t = (nz - a->z) / (b->z - a->z);
            out[n].x = a->x + t * (b->x - a->x);
            out[n].y = a->y + t * (b->y - a->y);
            out[n].z = nz;
            n++;
        }
    }
    if (n < 3) return;
    for (i = 0; i < n; i++) {
        iw[i] = 1 / out[i].z;
        sx[i] = (out[i].x * iw[i] / o->tx + 1) * 0.5 * o->w;
        sy[i] = (1 - out[i].y * iw[i] / o->ty) * 0.5 * o->h;
    }
    for (i = 1; i + 1 < n; i++) {
        double tx[3] = {sx[0], sx[i], sx[i + 1]}, ty[3] = {sy[0], sy[i], sy[i + 1]}, tw[3] = {iw[0], iw[i], iw[i + 1]};
        raster(o, tx, ty, tw);
    }
}

/* A box in the level's space against the view: its squared distance from the eye, or < 0 when it is wholly out of view
 * (its bounding sphere against the four sides and the plane through the eye, as tc_viewer_draw.c's sphere_seen). */
static float set_distance2(const struct tc_viewer_occ *o, const occ_set *s, float kx, float ky) {
    tc_vec3 c = tc_v3((s->lo[0] + s->hi[0]) / 2, (s->lo[1] + s->hi[1]) / 2, (s->lo[2] + s->hi[2]) / 2);
    tc_vec3 h = tc_v3((s->hi[0] - s->lo[0]) / 2, (s->hi[1] - s->lo[1]) / 2, (s->hi[2] - s->lo[2]) / 2);
    tc_vec3 rel = tc_v3_sub(c, o->eye), d;
    float r = tc_v3_length(h), z = tc_v3_dot(rel, o->f);
    if (z < -r) return -1;
    if (fabsf(tc_v3_dot(rel, o->r)) - z * o->tx > r * kx) return -1;
    if (fabsf(tc_v3_dot(rel, o->u)) - z * o->ty > r * ky) return -1;
    d = tc_v3(moref(0, fabsf(rel.x) - h.x), moref(0, fabsf(rel.y) - h.y), moref(0, fabsf(rel.z) - h.z));
    return tc_v3_dot(d, d);
}

/* The box (lo xyz, hi xyz) at a model matrix (NULL: none) on the picture: the pixels it reaches (rect: x0, y0, x1, y1)
 * and the 1 / depth of its nearest corner. false: a corner at or behind the near plane, or wholly off the picture. */
static bool box_rect(const struct tc_viewer_occ *o, const tc_mat4 *model, const float *box, int *rect, float *nearest) {
    float minx = INFINITY, maxx = -INFINITY, miny = INFINITY, maxy = -INFINITY, near = 0;
    float kx = 0.5f * (float)o->w / o->tx, ky = 0.5f * (float)o->h / o->ty, cx = 0.5f * (float)o->w, cy = 0.5f * (float)o->h;
    int i;
    for (i = 0; i < 8; i++) {
        float px = box[i & 1 ? 3 : 0], py = box[i & 2 ? 4 : 1], pz = box[i & 4 ? 5 : 2], z, iw, sx, sy;
        tc_vec3 w = tc_v3(px, py, pz), rel;
        if (model) {
            const float *m = model->m;
            w = tc_v3(m[0] * px + m[4] * py + m[8] * pz + m[12], m[1] * px + m[5] * py + m[9] * pz + m[13],
                      m[2] * px + m[6] * py + m[10] * pz + m[14]);
        }
        rel = tc_v3_sub(w, o->eye);
        z = tc_v3_dot(rel, o->f);
        if (!(z > o->near_z)) return false;
        iw = 1 / z;
        sx = cx + tc_v3_dot(rel, o->r) * iw * kx;
        sy = cy - tc_v3_dot(rel, o->u) * iw * ky;
        minx = lessf(minx, sx), maxx = moref(maxx, sx);
        miny = lessf(miny, sy), maxy = moref(maxy, sy);
        near = moref(near, iw);
    }
    if (!(maxx >= 0) || !(maxy >= 0) || !(minx <= (float)o->w) || !(miny <= (float)o->h)) return false;
    rect[0] = minx <= 0 ? 0 : (int)minx;
    rect[1] = miny <= 0 ? 0 : (int)miny;
    rect[2] = maxx >= (float)(o->w - 1) ? o->w - 1 : (int)maxx;
    rect[3] = maxy >= (float)(o->h - 1) ? o->h - 1 : (int)maxy;
    *nearest = near;
    return true;
}

/* Whether a set's box already lies behind what the picture holds so far, by a look at every third pixel: its triangles
 * would add nothing to it. Only a saving (a set passed by hides nothing: the picture errs towards drawing). */
static bool set_covered(const struct tc_viewer_occ *o, const occ_set *s) {
    int rect[4], x, y;
    float nearest;
    if (!box_rect(o, NULL, s->lo, rect, &nearest)) return false;
    for (y = rect[1];; y += 3) {
        const float *row;
        if (y > rect[3]) y = rect[3];
        row = o->raw + (size_t)y * TC_OCC_W;
        for (x = rect[0];; x += 3) {
            if (x > rect[2]) x = rect[2];
            if (!(row[x] > nearest)) return false;
            if (x == rect[2]) break;
        }
        if (y == rect[3]) break;
    }
    return true;
}

/* This frame's picture, from the camera at the culling's aspect. false: no occlusion culling this frame. */
bool tcv_occ_begin(tc_viewer *v) {
    struct tc_viewer_occ *o = v->occ;
    uint32_t i, k, n = 0, start[TC_OCC_BUCKETS + 1], budget = TC_OCC_BUDGET;
    float fy, per_d2, kx, ky;
    double prof;
    int x, y, tx, ty;
    v->occluded_count = v->occluded_cells = v->occluder_tris = 0;
    if (o) o->ready = false;
    if (!o || !o->set_count || v->occlusion_off || v->cull_off || v->cull_aspect <= 0) return false;
    prof = TC_PROF_BEGIN();
    fy = tc_camera_fov_y(&v->camera, v->cull_aspect);
    o->eye = v->camera.position;
    o->f = tc_camera_forward(&v->camera);
    o->r = tc_camera_right(&v->camera);
    o->u = tc_v3_cross(o->r, o->f);
    o->ty = tanf(fy / 2);
    o->tx = o->ty * v->cull_aspect;
    o->near_z = moref(v->camera.near_z, 1e-3f);
    o->w = TC_OCC_W;
    o->h = (int)((float)TC_OCC_W / v->cull_aspect + 0.5f);
    if (o->h < TC_OCC_H_MIN) o->h = TC_OCC_H_MIN;
    if (o->h > TC_OCC_H_MAX) o->h = TC_OCC_H_MAX;
    memset(o->raw, 0, (size_t)TC_OCC_W * (size_t)o->h * sizeof(float));
    /* a triangle of area A at distance d covers about A / d^2 / (a pixel's share of the view) pixels, face on */
    per_d2 = TC_OCC_MIN_PIXELS * (2 * o->tx / (float)o->w) * (2 * o->ty / (float)o->h);
    kx = sqrtf(1 + o->tx * o->tx);
    ky = sqrtf(1 + o->ty * o->ty);

    /* the sets in view, nearest first: sorted into buckets half an octave of distance wide (the exponent of the
     * squared distance) */
    memset(start, 0, sizeof(start));
    for (i = 0; i < o->set_count; i++) {
        float d2 = set_distance2(o, &o->sets[i], kx, ky);
        int e = 0;
        o->dist[i] = d2;
        if (d2 < 0) continue;
        if (d2 > 1) frexpf(d2, &e);
        o->bucket[i] = (uint8_t)(e < TC_OCC_BUCKETS ? e : TC_OCC_BUCKETS - 1);
        start[o->bucket[i] + 1]++;
    }
    for (k = 0; k < TC_OCC_BUCKETS; k++) start[k + 1] += start[k];
    n = start[TC_OCC_BUCKETS];
    for (i = 0; i < o->set_count; i++)
        if (o->dist[i] >= 0) o->order[start[o->bucket[i]]++] = i;
    for (i = 0; i < n && budget; i++) {
        const occ_set *s = &o->sets[o->order[i]];
        float least = per_d2 * o->dist[o->order[i]];
        if (!s->count || s->first >= o->tri_count || o->tris[s->first].area < least) continue; /* its largest: too small */
        if (set_covered(o, s)) continue;
        for (k = 0; k < s->count && budget; k++) {
            const occ_tri *t = &o->tris[s->first + k];
            tc_vec3 rel;
            if (t->area < least) break; /* largest first: the rest are smaller still */
            rel = tc_v3_sub(tc_v3((t->p[0] + t->p[3] + t->p[6]) / 3, (t->p[1] + t->p[4] + t->p[7]) / 3, (t->p[2] + t->p[5] + t->p[8]) / 3), o->eye);
            if (t->area < per_d2 * tc_v3_dot(rel, rel)) continue; /* too small where it is */
            draw_tri(o, t->p);
            budget--;
        }
    }
    v->occluder_tris = TC_OCC_BUDGET - budget;

    /* shrink: each pixel the farthest (smallest 1 / depth) of its 3 x 3; across, then down */
    for (y = 0; y < o->h; y++) {
        const float *in = o->raw + (size_t)y * TC_OCC_W;
        float *out = o->depth + (size_t)y * TC_OCC_W;
        out[0] = lessf(in[0], in[1]);
        for (x = 1; x + 1 < o->w; x++) out[x] = lessf(in[x], lessf(in[x - 1], in[x + 1]));
        out[x] = lessf(in[x], in[x - 1]);
    }
    for (y = 0; y < o->h; y++) {
        const float *mid = o->depth + (size_t)y * TC_OCC_W, *up = y > 0 ? mid - TC_OCC_W : mid, *down = y + 1 < o->h ? mid + TC_OCC_W : mid;
        float *out = o->raw + (size_t)y * TC_OCC_W;
        for (x = 0; x < o->w; x++) out[x] = lessf(mid[x], lessf(up[x], down[x]));
    }
    { float *t = o->depth; o->depth = o->raw; o->raw = t; }
    /* each tile's farthest: a box behind it is behind every pixel of it */
    for (ty = 0; ty * TC_OCC_TILE < o->h; ty++)
        for (tx = 0; tx < TC_OCC_TILES_X; tx++) {
            float m = INFINITY;
            for (y = ty * TC_OCC_TILE; y < (ty + 1) * TC_OCC_TILE && y < o->h; y++) {
                const float *row = o->depth + (size_t)y * TC_OCC_W + (size_t)tx * TC_OCC_TILE;
                for (x = 0; x < TC_OCC_TILE; x++) m = lessf(m, row[x]);
            }
            o->tile[ty * TC_OCC_TILES_X + tx] = m;
        }
    o->ready = true;
    TC_PROF_END(TC_PZ_DRAW_OCCLUDERS, prof);
    TC_PROF_COUNT(TC_PC_OCCLUDER_TRIS, v->occluder_tris);
    return true;
}

/* Whether the box (lo xyz, hi xyz: mesh space) at a model matrix is wholly behind the frame's picture. false whenever
 * unsure: no picture, a corner at or behind the near plane, off the picture. */
bool tcv_occ_hidden(const tc_viewer *v, const tc_mat4 *model, const float *box) {
    const struct tc_viewer_occ *o = v->occ;
    float nearest, limit;
    int rect[4], x, y, tx, ty;
    if (!o || !o->ready || !(box[0] <= box[3]) || !box_rect(o, model, box, rect, &nearest)) return false;
    limit = nearest * TC_OCC_BIAS;
    for (ty = rect[1] / TC_OCC_TILE; ty <= rect[3] / TC_OCC_TILE; ty++)
        for (tx = rect[0] / TC_OCC_TILE; tx <= rect[2] / TC_OCC_TILE; tx++) {
            int x0, x1, y0, y1;
            if (o->tile[ty * TC_OCC_TILES_X + tx] > limit) continue; /* behind the whole tile */
            x0 = tx * TC_OCC_TILE > rect[0] ? tx * TC_OCC_TILE : rect[0];
            y0 = ty * TC_OCC_TILE > rect[1] ? ty * TC_OCC_TILE : rect[1];
            x1 = tx * TC_OCC_TILE + TC_OCC_TILE - 1 < rect[2] ? tx * TC_OCC_TILE + TC_OCC_TILE - 1 : rect[2];
            y1 = ty * TC_OCC_TILE + TC_OCC_TILE - 1 < rect[3] ? ty * TC_OCC_TILE + TC_OCC_TILE - 1 : rect[3];
            for (y = y0; y <= y1; y++) {
                const float *row = o->depth + (size_t)y * TC_OCC_W;
                for (x = x0; x <= x1; x++)
                    if (!(row[x] > limit)) return false;
            }
        }
    return true;
}

void tc_viewer_set_occlusion(tc_viewer *v, bool on) { v->occlusion_off = !on; }

bool tc_viewer_occlusion(const tc_viewer *v) { return !v->occlusion_off; }
