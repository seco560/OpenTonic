#include "tc_internal.h"
#include "tonic/tc_sector.h"
#include "tc_reader.h"
#include <string.h>

/* FUN_00467d40: p inside the triangle, projected on the plane its normal is closest to (|n.z| >= 1/sqrt 3: xy, else
 * |n.x| >= 1/sqrt 3: yz, else xz); each edge's cross product with p must have the sign of that normal component (>= 0
 * counts as positive, so points on an edge are inside). */
static bool in_triangle(tc_vec3 p, tc_vec3 v0, tc_vec3 v1, tc_vec3 v2, tc_vec3 n) {
    const float limit = 0.57735026f; /* DAT_004d28f4 */
    tc_vec3 a = tc_v3_sub(v0, p), b = tc_v3_sub(v1, p), c = tc_v3_sub(v2, p);
    bool s;
    if (fabsf(n.z) >= limit) {
        s = n.z >= 0;
        return (b.y * a.x - b.x * a.y >= 0) == s && (c.y * b.x - c.x * b.y >= 0) == s && (c.x * a.y - c.y * a.x >= 0) == s;
    }
    if (fabsf(n.x) >= limit) {
        s = n.x >= 0;
        return (b.z * a.y - b.y * a.z >= 0) == s && (c.z * b.y - c.y * b.z >= 0) == s && (c.y * a.z - c.z * a.y >= 0) == s;
    }
    s = n.y >= 0;
    return (b.x * a.z - b.z * a.x >= 0) == s && (c.x * b.z - c.z * b.x >= 0) == s && (c.z * a.x - c.x * a.z >= 0) == s;
}

uint16_t tc_sectors_find(const tc_sectors *s, tc_vec3 p) {
    uint16_t best = TC_SECTOR_NONE, i;
    float best_d = 3.402822e+38f;
    uint32_t k;
    for (i = 0; s && i < s->count; i++) {
        const tc_sector *sec = &s->sectors[i];
        if (sec->is_virtual) continue;
        for (k = 0; k < sec->floor_count; k++) {
            const tc_vec3 *f = &sec->floors[4 * k];
            tc_vec3 n = f[3];
            float t;
            if (!(n.z > 0)) continue; /* a back face to the ray (FUN_00460350 keeps front faces only) */
            /* FUN_004687a0 along (0, 0, -1): t = -(n . p - n . v0) / (n . dir) */
            t = (tc_v3_dot(n, p) - tc_v3_dot(n, f[0])) / n.z;
            if (!(t >= 0)) continue;
            if (!in_triangle(tc_v3(p.x, p.y, p.z - t), f[0], f[1], f[2], n)) continue;
            if (t < best_d) {
                best_d = t;
                best = i;
            }
        }
    }
    return best;
}

/* FUN_00468070: the segment start + delta crosses the face from its front (the normal's side) within (0, 1);
 * t and the crossing point out. The bounding-box test first, as the exe's. */
static bool crosses(tc_vec3 start, tc_vec3 delta, const tc_portal *f, float *t, tc_vec3 *hit) {
    const float *sp = &start.x, *dp = &delta.x, *a = &f->v[0].x, *b = &f->v[1].x, *c = &f->v[2].x;
    float dn, dist;
    int k;
    for (k = 0; k < 3; k++) {
        float lo = dp[k] <= 0 ? dp[k] + sp[k] : sp[k], hi = dp[k] <= 0 ? sp[k] : dp[k] + sp[k];
        float tri_hi = fmaxf(a[k], fmaxf(b[k], c[k])), tri_lo = fminf(a[k], fminf(b[k], c[k]));
        if (!(lo <= tri_hi) || !(tri_lo <= hi)) return false;
    }
    dn = tc_v3_dot(f->normal, delta);
    if (!(dn < 0)) return false;
    dist = tc_v3_dot(f->normal, start) - tc_v3_dot(f->normal, f->v[0]);
    if (!(dist > 0) || !(dist < -dn)) return false;
    *t = -(dist / dn);
    *hit = tc_v3_add(tc_v3_scale(delta, *t), start);
    return in_triangle(*hit, f->v[0], f->v[1], f->v[2], f->normal);
}

uint16_t tc_sectors_cross(const tc_sectors *s, uint16_t sector, tc_vec3 from, tc_vec3 to) {
    int guard;
    for (guard = 0; guard < 64; guard++) {
        const tc_sector *sec;
        const tc_portal *best = NULL;
        tc_vec3 best_hit = from, delta, hit;
        float best_t = 2, t;
        uint32_t k;
        if (!s || sector >= s->count) return sector;
        sec = &s->sectors[sector];
        if (sec->portal_count == 0) return sector;
        delta = tc_v3_sub(to, from);
        for (k = 0; k < sec->portal_count; k++)
            if (crosses(from, delta, &sec->portals[k], &t, &hit) && t < best_t) {
                best_t = t;
                best = &sec->portals[k];
                best_hit = hit;
            }
        /* only the nearest face counts: leading nowhere, or back into this sector, ends the walk */
        if (!best || best->target >= s->count || best->target == sector) return sector;
        sector = best->target;
        from = best_hit;
    }
    return sector;
}

uint16_t tc_sectors_env_index(const tc_sectors *s, uint16_t sector, tc_vec3 p, float *depth) {
    const tc_sector *sec;
    uint16_t k;
    if (depth) *depth = 0;
    if (!s || sector >= s->count) return TC_SECTOR_NONE;
    sec = &s->sectors[sector];
    for (k = 0; k < sec->surface_count; k++) {
        float d;
        if (sec->surfaces[k] >= s->surface_count) break;
        d = s->surface_z[sec->surfaces[k]] - p.z; /* FUN_0045d880 */
        if (!(d > 0)) break;
        if (depth) *depth = d;
    }
    return k < sec->env_count ? k : TC_SECTOR_NONE;
}

float tc_sectors_env(const tc_sectors *s, uint16_t sector, tc_vec3 p, tc_sector_env *env) {
    const tc_sector_env air = {9.81f, 1.3f, 1.0f, 0, 0, 0, 0, {0, 0, 0}}; /* the fix's 0x36b51c, every sector's env 0 */
    float depth;
    uint16_t k = tc_sectors_env_index(s, sector, p, &depth);
    *env = air;
    if (k != TC_SECTOR_NONE && s->sectors[sector].envs[k].water != 0xFFFFFFFFu) *env = s->sectors[sector].envs[k];
    return depth;
}

bool tc_sectors_group_shown(const tc_sectors *s, uint16_t g, const uint8_t *shown) {
    uint16_t k;
    uint8_t need;
    if (!s || g >= s->group_count) return true;
    need = s->groups[g].flags & TC_GROUP_WATER ? TC_SHOWN_SECTOR : TC_SHOWN_GEOMETRY;
    for (k = 0; k < s->groups[g].count; k++)
        if (shown[s->groups[g].sectors[k]] & need) return true;
    return false;
}

const char *tc_sectors_name(const tc_sectors *s, uint16_t sector) {
    return s && sector < s->count && s->sectors[sector].name ? s->sectors[sector].name : "";
}

void tc_sector_place(tc_sector_track *t, const tc_sectors *s, tc_vec3 p) {
    uint16_t found = tc_sectors_find(s, p);
    if (found != TC_SECTOR_NONE) t->sector = found;
    else if (t->sector == TC_SECTOR_NONE && s && s->count) t->sector = (uint16_t)(s->count - 1);
    t->position = p;
}

void tc_sector_move(tc_sector_track *t, const tc_sectors *s, tc_vec3 p) {
    if (t->sector == TC_SECTOR_NONE) {
        tc_sector_place(t, s, p);
        return;
    }
    t->sector = tc_sectors_cross(s, t->sector, t->position, p);
    t->position = p;
}

void tc_sector_follow(tc_sector_track *t, const tc_sectors *s, tc_vec3 p, uint32_t placed) {
    if (!s || !s->count) return;
    if (t->placed != placed || t->sector == TC_SECTOR_NONE) {
        tc_sector_place(t, s, p);
        t->placed = placed;
    } else {
        tc_sector_move(t, s, p);
    }
}

/* ---- the SECT chunk (tools/ttre/ttlv.py) ---- */

static tc_vec3 rd_vec(reader *r) {
    tc_vec3 v;
    v.x = rd_f32(r);
    v.y = rd_f32(r);
    v.z = rd_f32(r);
    return v;
}

void tc_sectors_free(tc_sectors *s) {
    uint32_t i;
    for (i = 0; s->sectors && i < s->count; i++) {
        tc_sector *sec = &s->sectors[i];
        tc_free(sec->name);
        tc_free(sec->visible);
        tc_free(sec->envs);
        tc_free(sec->surfaces);
        tc_free(sec->portals);
        tc_free(sec->floors);
        tc_free(sec->lights);
    }
    tc_free(s->sectors);
    tc_free(s->surface_z);
    for (i = 0; s->groups && i < s->group_count; i++) tc_free(s->groups[i].sectors);
    tc_free(s->groups);
    tc_free(s->mesh_first_range);
    tc_free(s->ranges);
    tc_free(s->perso_sector);
    memset(s, 0, sizeof(*s));
}

/* Index fields out of range read as TC_SECTOR_NONE; ranges that do not tile a mesh's indices exactly, or counts that
 * differ from the level's meshes or persos, leave the ranges or the perso sectors out (everything drawn). */
tc_result tc_sectors_read(tc_sectors *s, reader *r, const tc_mesh *meshes, uint32_t mesh_count, uint32_t perso_count) {
    uint16_t n = rd_u16(r), ns = rd_u16(r), i;
    uint32_t k, m, total = 0, nr, np;
    bool ranges_ok = true;
    memset(s, 0, sizeof(*s));
    s->surface_z = (float *)rd_array(r, ns, sizeof(float), 4);
    if (s->surface_z) s->surface_count = ns;
    for (i = 0; i < s->surface_count; i++) s->surface_z[i] = rd_f32(r);
    s->sectors = (tc_sector *)rd_array(r, n, sizeof(tc_sector), 18);
    if (s->sectors) s->count = n;
    for (i = 0; i < s->count && !r->error; i++) {
        tc_sector *sec = &s->sectors[i];
        uint16_t c;
        sec->name = rd_string(r);
        sec->is_virtual = rd_u8(r);
        sec->no_child_lookup = rd_u8(r);
        c = rd_u16(r);
        sec->visible = (tc_sector_link *)rd_array(r, c, sizeof(tc_sector_link), 4);
        if (sec->visible) sec->visible_count = c;
        for (k = 0; k < sec->visible_count; k++) {
            sec->visible[k].sector = rd_u16(r);
            sec->visible[k].draw = rd_u16(r);
            if (sec->visible[k].sector >= n) sec->visible[k].sector = TC_SECTOR_NONE;
        }
        c = rd_u16(r);
        sec->envs = (tc_sector_env *)rd_array(r, c, sizeof(tc_sector_env), 16);
        if (sec->envs) sec->env_count = c;
        for (k = 0; k < sec->env_count; k++) {
            sec->envs[k].gravity = rd_f32(r);
            sec->envs[k].density = rd_f32(r);
            sec->envs[k].drag = rd_f32(r);
            sec->envs[k].water = rd_u32(r);
        }
        c = rd_u16(r);
        sec->surfaces = (uint16_t *)rd_array(r, c, sizeof(uint16_t), 2);
        if (sec->surfaces) sec->surface_count = c;
        for (k = 0; k < sec->surface_count; k++) sec->surfaces[k] = rd_u16(r);
        m = rd_u32(r);
        sec->portals = (tc_portal *)rd_array(r, m, sizeof(tc_portal), 52);
        if (sec->portals) sec->portal_count = m;
        for (k = 0; k < sec->portal_count; k++) {
            tc_portal *p = &sec->portals[k];
            p->target = rd_u16(r);
            (void)rd_u16(r);
            if (p->target >= n) p->target = TC_SECTOR_NONE;
            p->v[0] = rd_vec(r);
            p->v[1] = rd_vec(r);
            p->v[2] = rd_vec(r);
            p->normal = rd_vec(r);
        }
        m = rd_u32(r);
        sec->floors = (tc_vec3 *)rd_array(r, (size_t)m * 4, sizeof(tc_vec3), 12);
        if (sec->floors) sec->floor_count = m;
        for (k = 0; k < 4 * sec->floor_count; k++) sec->floors[k] = rd_vec(r);
    }
    n = rd_u16(r);
    s->groups = (tc_sector_group *)rd_array(r, n, sizeof(tc_sector_group), 4);
    if (s->groups) s->group_count = n;
    for (i = 0; i < s->group_count && !r->error; i++) {
        tc_sector_group *g = &s->groups[i];
        uint16_t c = rd_u16(r);
        g->flags = rd_u16(r);
        g->sectors = (uint16_t *)rd_array(r, c, sizeof(uint16_t), 2);
        if (g->sectors) g->count = c;
        for (k = 0; k < g->count; k++) {
            g->sectors[k] = rd_u16(r);
            if (g->sectors[k] >= s->count) {
                g->sectors[k] = 0;
                r->error = r->error ? r->error : TC_ERR_STALE;
            }
        }
    }
    /* The ranges, per mesh: counted in a first pass over the records (tc_alloc has no realloc), read in a second. */
    s->mesh_first_range = (uint32_t *)tc_calloc((size_t)mesh_count + 1, sizeof(uint32_t));
    if (!s->mesh_first_range) r->error = TC_ERR_OUT_OF_MEMORY;
    if (!r->error) {
        size_t start = r->offset;
        uint32_t t = 0;
        for (m = 0; m < mesh_count && !r->error; m++) {
            s->mesh_first_range[m] = total;
            nr = rd_u32(r);
            if (!have(r, (size_t)nr * 12)) break;
            r->offset += (size_t)nr * 12;
            total += nr;
        }
        s->mesh_first_range[mesh_count] = total;
        if (!r->error) {
            s->ranges = (tc_sector_range *)tc_calloc(total ? total : 1, sizeof(tc_sector_range));
            if (!s->ranges) r->error = TC_ERR_OUT_OF_MEMORY;
        }
        r->offset = start;
        for (m = 0; m < mesh_count && s->ranges && !r->error; m++) {
            uint32_t first = 0;
            nr = rd_u32(r);
            for (k = 0; k < nr; k++, t++) {
                tc_sector_range *rg = &s->ranges[t];
                rg->group = rd_u16(r);
                (void)rd_u16(r);
                rg->first = rd_u32(r);
                rg->count = rd_u32(r);
                if (rg->group != TC_SECTOR_NONE && rg->group >= s->group_count) ranges_ok = false;
                if (rg->first != first || rg->count % 3 != 0) ranges_ok = false;
                first += rg->count;
            }
            if (first != meshes[m].index_count) ranges_ok = false;
        }
    }
    np = rd_u32(r);
    if (np == perso_count) {
        s->perso_sector = (uint16_t *)rd_array(r, np, sizeof(uint16_t), 2);
        if (s->perso_sector) s->perso_count = np;
        for (k = 0; k < s->perso_count; k++) {
            s->perso_sector[k] = rd_u16(r);
            if (s->perso_sector[k] >= s->count) s->perso_sector[k] = TC_SECTOR_NONE;
        }
    } else if (have(r, (size_t)np * 2)) {
        r->offset += (size_t)np * 2;
    }
    if (r->error) return r->error;
    if (ranges_ok) {
        s->mesh_count = mesh_count;
    } else {
        tc_free(s->ranges);
        s->ranges = NULL;
    }
    return TC_OK;
}
