/* The viewer's drawing: the mesh table, transparent pieces, sector ranges, the draw list, the frame's values. */
#include "tc_viewer_internal.h"

/* Appends a range to v->ranges (tc_alloc has no realloc). false when out of memory. */
static bool push_range(tc_viewer *v, uint16_t group, uint32_t first, uint32_t count) {
    if (v->range_count == v->range_cap) {
        uint32_t cap = v->range_cap ? v->range_cap * 2 : 256;
        tc_sector_range *r = (tc_sector_range *)tc_calloc(cap, sizeof(*r));
        if (!r) return false;
        if (v->ranges) memcpy(r, v->ranges, v->range_count * sizeof(*r));
        tc_free(v->ranges);
        v->ranges = r;
        v->range_cap = cap;
    }
    v->ranges[v->range_count].group = group;
    v->ranges[v->range_count].first = first;
    v->ranges[v->range_count].count = count;
    v->range_count++;
    return true;
}

/* The level's ranges of static mesh i (from tc_sectors), or none. */
static bool level_ranges(const tc_viewer *v, uint32_t i, const tc_sector_range **r, uint32_t *n) {
    const tc_sectors *s = v->sectors;
    if (!s || s->mesh_count != v->level->mesh_count || i >= s->mesh_count || !s->ranges) return false;
    *r = &s->ranges[s->mesh_first_range[i]];
    *n = s->mesh_first_range[i + 1] - s->mesh_first_range[i];
    return true;
}

/* Piece `group` of static mesh `origin`: its triangles (tri_group[t] == group) with their vertices compacted, in their
 * order, so their sector groups (sector_group[t], NULL: no sectors) stay runs: the piece's ranges go to
 * piece_ranges[2 k] (first in v->ranges) and [2 k + 1] (count). map: one UINT32_MAX per vertex of m on entry, restored
 * on return. */
static bool add_piece(tc_viewer *v, const tc_mesh *m, uint32_t origin, const uint32_t *tri_group, uint32_t group,
                      uint32_t *map, const uint16_t *sector_group, uint32_t **piece_ranges) {
    uint32_t t, k, nt = m->index_count / 3, ni = 0, nv = 0, first_range = v->range_count;
    tc_mesh *p;
    bool ok = true;
    for (t = 0; t < nt; t++)
        if (tri_group[t] == group) ni += 3;
    if (v->piece_count == v->piece_cap) {
        uint32_t cap = v->piece_cap ? v->piece_cap * 2 : 64;
        tc_mesh *pieces = (tc_mesh *)tc_calloc(cap, sizeof(*pieces));
        uint32_t *origins = (uint32_t *)tc_calloc(cap, sizeof(*origins));
        uint32_t *ranges = (uint32_t *)tc_calloc((size_t)cap * 2, sizeof(*ranges));
        if (!pieces || !origins || !ranges) {
            tc_free(pieces);
            tc_free(origins);
            tc_free(ranges);
            return false;
        }
        if (v->pieces) memcpy(pieces, v->pieces, v->piece_count * sizeof(*pieces));
        if (v->piece_origin) memcpy(origins, v->piece_origin, v->piece_count * sizeof(*origins));
        if (*piece_ranges) memcpy(ranges, *piece_ranges, (size_t)v->piece_count * 2 * sizeof(*ranges));
        tc_free(v->pieces);
        tc_free(v->piece_origin);
        tc_free(*piece_ranges);
        v->pieces = pieces;
        v->piece_origin = origins;
        *piece_ranges = ranges;
        v->piece_cap = cap;
    }
    p = &v->pieces[v->piece_count];
    memset(p, 0, sizeof(*p));
    p->surface = m->surface;  /* frames stay the level's */
    p->indices = (uint32_t *)tc_calloc(ni, sizeof(*p->indices));
    p->vertices = (tc_vertex *)tc_calloc(ni, sizeof(*p->vertices));  /* at most one new vertex per index */
    if (!p->indices || !p->vertices) {
        tc_free(p->indices);
        tc_free(p->vertices);
        memset(p, 0, sizeof(*p));
        return false;
    }
    for (t = 0; t < nt && ok; t++) {
        if (tri_group[t] != group) continue;
        if (sector_group) {
            tc_sector_range *last = v->range_count > first_range ? &v->ranges[v->range_count - 1] : NULL;
            if (last && last->group == sector_group[t]) last->count += 3;
            else ok = push_range(v, sector_group[t], p->index_count, 3);
        }
        for (k = 0; k < 3; k++) {
            uint32_t vi = m->indices[3 * t + k];
            if (map[vi] == UINT32_MAX) {
                map[vi] = nv;
                p->vertices[nv++] = m->vertices[vi];
            }
            p->indices[p->index_count++] = map[vi];
        }
    }
    for (t = 0; t < nt; t++)
        if (tri_group[t] == group)
            for (k = 0; k < 3; k++) map[m->indices[3 * t + k]] = UINT32_MAX;
    if (!ok) {
        tc_free(p->indices);
        tc_free(p->vertices);
        memset(p, 0, sizeof(*p));
        return false;
    }
    (*piece_ranges)[2 * v->piece_count] = first_range;
    (*piece_ranges)[2 * v->piece_count + 1] = v->range_count - first_range;
    p->vertex_count = nv;
    v->piece_origin[v->piece_count++] = origin;
    return true;
}

/* The .ttlv holds one mesh per material for the whole level, so a transparent material (water, glass, the OMBRE
 * shadows) would sort as a single draw. Each transparent static is cut by a grid of TC_VIEW_PIECE_SIZE cells over
 * its triangles' centroids into pieces with their own vertices; one that fits a single cell stays whole. Triangles
 * with an index out of range (none in converted levels) go to the first piece. Each piece keeps its triangles' sector
 * groups as ranges (piece_ranges: add_piece). */
bool tcv_split_transparent(tc_viewer *v, uint32_t **piece_ranges) {
    const tc_level *l = v->level;
    uint32_t i, t, g;
    bool ok = true;
    if (!l->mesh_count) return true;
    v->split = (uint8_t *)tc_calloc(l->mesh_count, 1);
    if (!v->split) return false;
    for (i = 0; ok && i < l->mesh_count; i++) {
        const tc_mesh *m = &l->meshes[i];
        uint32_t nt = m->index_count / 3, groups = 0, *tri_group, *map, nr = 0, r;
        uint16_t *sector_group = NULL;
        const tc_sector_range *ranges = NULL;
        int32_t *cells;
        if (!(m->surface.flags & TC_FLAG_TRANSPARENT) || !drawable(m) || nt < 2) continue;
        tri_group = (uint32_t *)tc_calloc(nt, sizeof(*tri_group));
        cells = (int32_t *)tc_calloc((size_t)nt * 3, sizeof(*cells));
        map = (uint32_t *)tc_calloc(m->vertex_count, sizeof(*map));
        if (level_ranges(v, i, &ranges, &nr)) {
            sector_group = (uint16_t *)tc_calloc(nt, sizeof(*sector_group));
            for (r = 0; sector_group && r < nr; r++)
                for (t = ranges[r].first / 3; t < (ranges[r].first + ranges[r].count) / 3 && t < nt; t++)
                    sector_group[t] = ranges[r].group;
        }
        if (!tri_group || !cells || !map || (nr && !sector_group)) {
            ok = false;
        } else {
            for (t = 0; t < nt; t++) {
                const uint32_t *ix = &m->indices[3 * t];
                int32_t c[3];
                int a;
                if (ix[0] >= m->vertex_count || ix[1] >= m->vertex_count || ix[2] >= m->vertex_count) {
                    tri_group[t] = 0;
                    if (!groups) groups = 1;
                    continue;
                }
                for (a = 0; a < 3; a++)
                    c[a] = (int32_t)floorf((m->vertices[ix[0]].position[a] + m->vertices[ix[1]].position[a] +
                                            m->vertices[ix[2]].position[a]) / (3 * TC_VIEW_PIECE_SIZE));
                for (g = 0; g < groups; g++)
                    if (cells[3 * g] == c[0] && cells[3 * g + 1] == c[1] && cells[3 * g + 2] == c[2]) break;
                if (g == groups) memcpy(&cells[3 * groups++], c, sizeof(c));
                tri_group[t] = g;
            }
            if (groups > 1) {
                for (t = 0; t < m->vertex_count; t++) map[t] = UINT32_MAX;
                for (g = 0; ok && g < groups; g++) ok = add_piece(v, m, i, tri_group, g, map, sector_group, piece_ranges);
                v->split[i] = 1;
            }
        }
        tc_free(tri_group);
        tc_free(cells);
        tc_free(map);
        tc_free(sector_group);
    }
    return ok;
}

/* The centre and half size of each transparent mesh's vertex bounds (mesh space): what blended draws sort by. */
bool tcv_mesh_centres(tc_viewer *v) {
    uint32_t i, k;
    if (!v->mesh_count) return true;
    v->mesh_centre = (tc_vec3 *)tc_calloc(v->mesh_count, sizeof(*v->mesh_centre));
    v->mesh_extent = (tc_vec3 *)tc_calloc(v->mesh_count, sizeof(*v->mesh_extent));
    if (!v->mesh_centre || !v->mesh_extent) return false;
    for (i = 0; i < v->mesh_count; i++) {
        const tc_mesh *m = v->meshes[i];
        tc_vec3 lo, hi;
        if (!(m->surface.flags & TC_FLAG_TRANSPARENT) || !drawable(m)) continue;
        lo = hi = tc_v3(m->vertices[0].position[0], m->vertices[0].position[1], m->vertices[0].position[2]);
        for (k = 1; k < m->vertex_count; k++) {
            tc_vec3 p = tc_v3(m->vertices[k].position[0], m->vertices[k].position[1], m->vertices[k].position[2]);
            lo = tc_v3_min(lo, p);
            hi = tc_v3_max(hi, p);
        }
        v->mesh_centre[i] = tc_v3_scale(tc_v3_add(lo, hi), 0.5f);
        v->mesh_extent[i] = tc_v3_scale(tc_v3_sub(hi, lo), 0.5f);
    }
    return true;
}

/* Every mesh's index ranges (tc_viewer.mesh_ranges): the statics' from the level's sectors (copied after the pieces'),
 * the pieces' from split_transparent; none for persos and Ed, or in a level without sectors. */
bool tcv_mesh_ranges(tc_viewer *v, const uint32_t *piece_ranges) {
    uint32_t i, k, n;
    const tc_sector_range *r;
    if (!v->sectors || !v->mesh_count) return true;
    v->mesh_ranges = (uint32_t *)tc_calloc((size_t)v->mesh_count * 2, sizeof(*v->mesh_ranges));
    if (!v->mesh_ranges) return false;
    for (i = 0; i < v->level->mesh_count; i++) {
        if (!level_ranges(v, i, &r, &n)) continue;
        v->mesh_ranges[2 * i] = v->range_count;
        v->mesh_ranges[2 * i + 1] = n;
        for (k = 0; k < n; k++)
            if (!push_range(v, r[k].group, r[k].first, r[k].count)) return false;
    }
    for (i = 0; piece_ranges && i < v->piece_count; i++) {
        v->mesh_ranges[2 * (v->piece_first + i)] = piece_ranges[2 * i];
        v->mesh_ranges[2 * (v->piece_first + i) + 1] = piece_ranges[2 * i + 1];
    }
    return true;
}

uint32_t tc_viewer_mesh_count(const tc_viewer *v) { return v->mesh_count; }

const tc_mesh *tc_viewer_mesh(const tc_viewer *v, uint32_t i) { return i < v->mesh_count ? v->meshes[i] : NULL; }

/* The texture shown at time t: animated textures loop through their frames. -1 when untextured or out of range. */
static int32_t texture_at(const tc_viewer *v, const tc_surface *s, float t) {
    int32_t tex = s->texture;
    float loop = 0, x;
    uint32_t i;
    for (i = 0; i < s->frame_count; i++) loop += s->frames[i].seconds;
    if (loop > 0) {
        x = fmodf(t, loop);
        tex = s->frames[s->frame_count - 1].texture;
        for (i = 0; i < s->frame_count; i++) {
            if (x < s->frames[i].seconds) { tex = s->frames[i].texture; break; }
            x -= s->frames[i].seconds;
        }
    }
    return tex >= 0 && (uint32_t)tex < v->level->texture_count ? tex : -1;
}

/* Grows a draw array to hold at least need draws (tc_alloc has no realloc). false when out of memory. */
static bool reserve(tc_view_draw **p, uint32_t *cap, uint32_t need) {
    uint32_t c;
    tc_view_draw *n;
    if (need <= *cap) return true;
    c = *cap ? *cap : 256;
    while (c < need) c *= 2;
    n = (tc_view_draw *)tc_calloc(c, sizeof(*n));
    if (!n) return false;
    if (*p) memcpy(n, *p, (size_t)*cap * sizeof(*n));
    tc_free(*p);
    *p = n;
    *cap = c;
    return true;
}

typedef struct draw_list { uint32_t count, blended_count; } draw_list;

/* A water static, or a piece of one. */
static bool is_water(const tc_viewer *v, uint32_t mesh) {
    if (!v->mesh_water) return false;
    if (v->piece_count && mesh >= v->piece_first && mesh < v->piece_first + v->piece_count)
        mesh = v->piece_origin[mesh - v->piece_first];
    return mesh < v->level->mesh_count && v->mesh_water[mesh];
}

/* The blended draws back to front, as the original's driver sorts its deferred triangles: each by its farthest point
 * (GliDX6vr.dll keys a triangle by its vertices' smallest 1/w, bucketed far to near: GLI_DRV_xSendListToViewport), here
 * the view depth of the farthest corner of the draw's bounds; ties in list order (a stable insertion sort: a few dozen
 * to a few hundred draws). Until 2026-10-31 by the centre's distance, which put a small pane over a big surface's
 * nearer half behind it (cock01's ice over its water). Left unsorted if the depth buffer can't grow. */
static void sort_blended(tc_viewer *v, uint32_t n) {
    uint32_t i, j;
    tc_vec3 eye = v->camera.position, f = tc_camera_forward(&v->camera);
    if (n > v->blended_depth_cap) {
        float *d = (float *)tc_calloc(n, sizeof(*d));
        if (!d) return;
        tc_free(v->blended_depth);
        v->blended_depth = d;
        v->blended_depth_cap = n;
    }
    for (i = 0; i < n; i++) {
        const tc_view_draw *d = &v->blended[i];
        tc_vec3 c = v->mesh_centre ? v->mesh_centre[d->mesh] : tc_v3(0, 0, 0);
        tc_vec3 e = v->mesh_extent ? v->mesh_extent[d->mesh] : tc_v3(0, 0, 0);
        const float *m = d->model.m;  /* column-major */
        tc_vec3 w = tc_v3(m[0] * c.x + m[4] * c.y + m[8] * c.z + m[12], m[1] * c.x + m[5] * c.y + m[9] * c.z + m[13],
                          m[2] * c.x + m[6] * c.y + m[10] * c.z + m[14]);
        /* the box's reach along the view axis: each model axis's half size projected on it */
        float reach = fabsf(m[0] * f.x + m[1] * f.y + m[2] * f.z) * e.x + fabsf(m[4] * f.x + m[5] * f.y + m[6] * f.z) * e.y +
                      fabsf(m[8] * f.x + m[9] * f.y + m[10] * f.z) * e.z;
        v->blended_depth[i] = tc_v3_dot(tc_v3_sub(w, eye), f) + reach;
    }
    for (i = 1; i < n; i++) {
        tc_view_draw d = v->blended[i];
        float key = v->blended_depth[i];
        for (j = i; j > 0 && v->blended_depth[j - 1] < key; j--) {
            v->blended[j] = v->blended[j - 1];
            v->blended_depth[j] = v->blended_depth[j - 1];
        }
        v->blended[j] = d;
        v->blended_depth[j] = key;
    }
}

static void add_draw(tc_viewer *v, draw_list *dl, uint32_t mesh, const tc_mat4 *model, uint32_t first, uint32_t count,
                     float water) {
    const tc_mesh *m = v->meshes[mesh];
    const struct tc_viewer_char_lit *lit;
    tc_view_draw *d;
    uint32_t k;
    if (!drawable(m) || count == 0) return;
    if (m->surface.flags & TC_FLAG_TRANSPARENT) {
        if (!reserve(&v->blended, &v->blended_cap, dl->blended_count + 1)) return;
        d = &v->blended[dl->blended_count++];
    } else {
        if (!reserve(&v->draws, &v->draw_cap, dl->count + 1)) return;
        d = &v->draws[dl->count++];
    }
    d->mesh = mesh;
    d->texture = texture_at(v, &m->surface, v->animation_time);
    d->flags = m->surface.flags | (is_water(v, mesh) ? TC_FLAG_WATER : 0);
    d->first_index = first;
    d->index_count = count;
    d->model = *model;
    memcpy(d->ambient, m->surface.ambient, sizeof(d->ambient));
    memcpy(d->diffuse, m->surface.diffuse, sizeof(d->diffuse));
    d->underwater = !isnan(water);
    d->water_z = d->underwater ? water : 0;
    d->light_first = d->light_extra = 0;
    /* its sector's characters' lights; a water surface: the frame's sector's, drawn in that sector's pass */
    lit = v->sector_lit && !v->draw_lit && is_water(v, mesh) && v->light_sector < v->sectors->count
              ? &v->sector_lit[v->light_sector] : v->draw_lit;
    if (lit) {
        d->light_first = lit->first;
        d->light_extra = lit->count;
        for (k = 0; k < 3; k++) d->ambient[k] += lit->ambient[k] * d->diffuse[k];
    }
}

/* A mesh whole, or with `shown` (NULL: all) the runs of its ranges whose group is shown, each run of consecutive shown
 * ranges under the same water plane (sector_water: a sector's geometry; none on the water surfaces' groups) one draw. */
static void add_mesh(tc_viewer *v, draw_list *dl, uint32_t mesh, const tc_mat4 *model, const uint8_t *shown,
                     bool water) {
    uint32_t n = v->mesh_ranges ? v->mesh_ranges[2 * mesh + 1] : 0, k, first = 0, count = 0;
    const tc_sector_range *r;
    float z = NAN, rz;
    /* the sectors the scripts have moved (tc_cast_sector_matrix): each of their ranges a draw of its own, at its matrix */
    const tc_cast *cast = v->play && v->play->has_cast && v->play->cast.moving_sector_count && v->sectors ? &v->play->cast : NULL;
    if ((!shown && !water && !cast) || n == 0) {
        add_draw(v, dl, mesh, model, 0, v->meshes[mesh]->index_count, NAN);
        return;
    }
    r = &v->ranges[v->mesh_ranges[2 * mesh]];
    for (k = 0; k < n; k++) {
        if (shown && !tc_sectors_group_shown(v->sectors, r[k].group, shown)) continue;
        rz = water && r[k].group < v->sectors->count ? v->sector_water[r[k].group] : NAN;
        if (cast && r[k].group < v->sectors->count) {
            const tc_mat34 *moved = tc_cast_sector_matrix(cast, r[k].group);
            if (moved) {
                tc_mat4 m4 = tc_mat4_from34(moved), at = tc_mat4_mul(model, &m4);
                add_draw(v, dl, mesh, &at, r[k].first, r[k].count, rz);
                continue;
            }
        }
        if (count && first + count == r[k].first && (isnan(z) ? isnan(rz) : z == rz)) {
            count += r[k].count;
            continue;
        }
        add_draw(v, dl, mesh, model, first, count, z);
        first = r[k].first;
        count = r[k].count;
        z = rz;
    }
    add_draw(v, dl, mesh, model, first, count, z);
}

/* The water plane FUN_00451e80 / 004521b0 set for sector s: below its first water environment k, the surface above it
 * (k - 1 of its list), or TC_VIEW_UNDERWATER_ALL for k = 0. NAN: no water environment. */
static float water_plane(const tc_viewer *v, uint16_t s) {
    const tc_sector *sec = &v->sectors->sectors[s];
    uint16_t k;
    for (k = 0; k < sec->env_count; k++) {
        if (sec->envs[k].water != 1) continue;
        if (k == 0) return TC_VIEW_UNDERWATER_ALL;
        /* env k lies below k surfaces; a sector listing fewer (a converter that missed them) has no plane */
        if (k - 1 >= sec->surface_count || !sec->surfaces) return NAN;
        return sec->surfaces[k - 1] < v->sectors->surface_count ? v->sectors->surface_z[sec->surfaces[k - 1]] : NAN;
    }
    return NAN;
}

/* Which sectors are drawn under the water effect this frame, and below which plane (v->sector_water; false: none). The
 * frame's sector is the camera's (as for the lights): its geometry always gets its own plane, and the camera is under
 * water when its first water env is env 0 or the camera is below that plane. Then every other sector gets the same
 * plane (004521b0 leaves it set); else each gets its own while the camera is above it (never for env 0: 9000 up). */
static bool water_planes(tc_viewer *v) {
    const tc_sectors *s = v->sectors;
    uint16_t main = v->light_sector, i;
    float main_z, z, cam = v->camera.position.z;
    bool under, any = false;
    if (!s || v->lighting != TC_VL_GAME || main >= s->count) return false;
    main_z = water_plane(v, main);
    under = !isnan(main_z) && (main_z == TC_VIEW_UNDERWATER_ALL || cam < main_z);
    for (i = 0; i < s->count; i++) {
        z = i == main ? main_z : under ? main_z : water_plane(v, i);
        if (i != main && !under && !(z < cam)) z = NAN;
        v->sector_water[i] = z;
        any |= !isnan(z);
    }
    return any;
}

/* What this frame draws of each sector (v->sector_shown), or NULL for everything: Ed's sector whole and its list (each
 * listed sector's characters and surfaces, its geometry when its link says so: FUN_00451d80 / 004521b0; the exe draws
 * from its camera's sector, the port from Ed's), or his sector alone. Everything while his sector is not known. */
const uint8_t *tcv_shown_sectors(tc_viewer *v) {
    const tc_sectors *s = v->sectors;
    uint16_t from, k;
    if (!s || v->sector_mode == TC_VS_ALL) return NULL;
    from = tc_viewer_ed_sector(v);
    if (from >= s->count) return NULL;
    memset(v->sector_shown, 0, s->count);
    v->sector_shown[from] = TC_SHOWN_SECTOR | TC_SHOWN_GEOMETRY;
    for (k = 0; v->sector_mode == TC_VS_SECTOR && k < s->sectors[from].visible_count; k++) {
        const tc_sector_link *link = &s->sectors[from].visible[k];
        if (link->sector < s->count)
            v->sector_shown[link->sector] |= TC_SHOWN_SECTOR | (link->draw > 0 ? TC_SHOWN_GEOMETRY : 0);
    }
    return v->sector_shown;
}

static const tc_view_draw *build_draws(tc_viewer *v, uint32_t *count, uint32_t *opaque_count) {
    const tc_level *l = v->level;
    const tc_mat4 identity = tc_mat4_identity();
    draw_list dl = {0, 0};
    uint32_t i, k, first = v->perso_first;
    float t = v->animation_time;
    const uint8_t *shown = tcv_shown_sectors(v);
    bool perso_sectors = shown && v->sectors->perso_count == l->perso_count;
    bool water = water_planes(v);
    float z;
    uint16_t sec;
    double prof = TC_PROF_BEGIN();
    v->ed_draw_count = 0;

    for (i = 0; i < l->mesh_count; i++) {
        tc_mat4 round; /* a custom sky, drawn round the camera (tcx.h) */
        if (v->split && v->split[i]) continue; /* split ones: as their pieces */
        add_mesh(v, &dl, i, tcx_mesh_model(v->meshes[i], v->camera.position, &round) ? &round : &identity, shown, water);
    }
    for (i = 0; i < v->piece_count; i++) add_mesh(v, &dl, v->piece_first + i, &identity, shown, water);
    TC_PROF_END(TC_PZ_DRAW_STATICS, prof);
    prof = TC_PROF_BEGIN();

    for (i = 0; i < l->perso_count; first += l->persos[i].part_count, i++) {
        const tc_perso *p = &l->persos[i];
        uint32_t nch = p->channel_count, base;
        tc_mat4 world;
        tc_sample s;
        tc_mat34 w34;
        const tc_cast_actor *actor;
        if (p->sample_count == 0 || (v->playing && (int32_t)i == l->ed.static_perso)) continue;
        /* drawn with its sector (the sector's character list); unknown: always. A tape says itself who is on show. */
        if (!(v->tape && v->play->has_cast && tc_cast_actor_of(&v->play->cast, i)) &&
            perso_sectors && v->sectors->perso_sector[i] != TC_SECTOR_NONE &&
            !(shown[v->sectors->perso_sector[i]] & TC_SHOWN_SECTOR))
            continue;
        /* Baked samples loop; blend each channel between neighbouring samples so motion is smooth at any display
         * rate and playback speed. Visibility follows the earlier sample. */
        world = tc_mat4_from34(&p->world);
        sec = v->sectors && v->sectors->perso_count == l->perso_count ? v->sectors->perso_sector[i] : TC_SECTOR_NONE;
        if (v->sector_lit) {
            uint16_t ls = tcv_perso_light_sector(v, i);
            v->draw_lit = ls < v->sectors->count ? &v->sector_lit[ls] : NULL;
        }
        z = water && sec < v->sectors->count ? v->sector_water[sec] : NAN; /* drawn in its sector's pass */
        actor = v->playing && v->play && v->play->has_cast && v->play->use_ai ? tc_cast_actor_of(&v->play->cast, i) : NULL;
        if (!actor && v->spawn_prso && v->spawn_prso[i]) continue;
        if (!actor && v->play && tcx_perso_pose(v->play->custom, i, &w34)) { /* moved by custom gameplay (a car) */
            world = tc_mat4_from34(&w34);
            for (k = 0; k < p->part_count; k++) {
                const tc_part *part = &p->parts[k];
                tc_mat34 c34;
                tc_mat4 m, model;
                if (part->channel >= nch || p->shown[part->channel] != part->object) continue;
                c34 = tcx_perso_channel(v->play->custom, i, part->channel);
                m = tc_mat4_from34(&c34);
                model = tc_mat4_mul(&world, &m);
                add_draw(v, &dl, first + k, &model, 0, part->mesh.index_count, z);
            }
            continue;
        }
        if (actor) { /* its own AI's state animation; parts keyed by object (channel numbering differs per animation) */
            const tc_cast_anim *an;
            tc_mat34 w34;
            uint32_t ch;
            if (v->tape ? !tcv_tape_actor_pose(v, actor, &w34, &an, &s)
                        : !tc_cast_pose(&v->play->cast, actor, v->play->tick_time / TC_ED_AI_TICK, &w34, &an, &s))
                continue;
            world = tc_mat4_from34(&w34);
            nch = an->channel_count;
            for (ch = 0; ch < nch; ch++) {
                uint16_t obj = an->shown[s.s0 * nch + ch];
                tc_mat4 m, model;
                if (obj == TC_HIDDEN || (ch < 64 && (actor->ai->hidden_channels >> ch) & 1u)) continue;
                m = tc_channel_matrix(an->transforms, an->shown, nch, s, ch);
                model = tc_mat4_mul(&world, &m);
                /* An object's parts (one per material) under the channel it first appears with: the same geometry
                 * repeats under every channel that shows it in the initial animation. */
                uint16_t c0 = 0;
                bool found = false;
                for (k = 0; k < p->part_count; k++) {
                    if (p->parts[k].object != obj) continue;
                    if (!found) c0 = p->parts[k].channel, found = true;
                    if (p->parts[k].channel == c0) add_draw(v, &dl, first + k, &model, 0, p->parts[k].mesh.index_count, z);
                }
            }
            continue;
        }
        s = tc_sample_at_time(t, p->sample_rate, p->sample_count);
        base = s.s0 * nch;
        for (k = 0; k < p->part_count; k++) {
            const tc_part *part = &p->parts[k];
            tc_mat4 m, model;
            if (part->channel >= nch || p->shown[base + part->channel] != part->object) continue;
            /* Lerping full matrices is fine for samples this close together; skipped across a part swap. */
            m = tc_channel_matrix(p->transforms, p->shown, nch, s, part->channel);
            model = tc_mat4_mul(&world, &m);
            add_draw(v, &dl, first + k, &model, 0, part->mesh.index_count, z);
        }
    }

    /* first person, or the camera pushed into him by a wall (tc_cam.hide_target): not drawn */
    if (v->playing && v->play && v->ed_clip >= 0 && !v->cam.first_person && !v->cam.hide_target) {
        const tc_ed_anim *a = &l->ed.anims[v->ed_clip];
        tc_sample s = tc_sample_at_phase(v->ed_phase, a->sample_count, v->ed_loop);
        uint32_t nch = a->channel_count, ch;
        /* The skier's board tilts with the slope; custom gameplay (a car) may pose him. */
        tc_mat4 model = !tc_main_is_ed(&v->play->main)              ? v->play->main.rider.model
                        : v->play->use_ai && v->play->main.has_driver ? v->play->pose_draw
                                                                 : tc_player_model_matrix(&v->play->main.player);
        sec = tc_viewer_ed_sector(v);
        z = water && sec != TC_SECTOR_NONE && sec < v->sectors->count ? v->sector_water[sec] : NAN;
        v->draw_lit = v->sector_lit && sec < v->sectors->count ? &v->sector_lit[sec] : NULL;
        for (ch = 0; ch < nch; ch++) {
            uint16_t obj = a->shown[s.s0 * nch + ch];
            tc_mat4 m, world;
            if (obj == TC_HIDDEN) continue;
            m = tc_channel_matrix(a->transforms, a->shown, nch, s, ch);
            world = tc_mat4_mul(&model, &m);
            for (k = 0; k < l->ed.part_count; k++)
                if (l->ed.parts[k].object == obj) {
                    add_draw(v, &dl, v->ed_first + k, &world, 0, l->ed.parts[k].mesh.index_count, z);
                    v->ed_draw_count++;
                }
        }
    }

    v->draw_lit = NULL;
    TC_PROF_END(TC_PZ_DRAW_PERSOS, prof);
    *opaque_count = dl.count;
    prof = TC_PROF_BEGIN();
    sort_blended(v, dl.blended_count);
    TC_PROF_END(TC_PZ_DRAW_SORT, prof);
    v->draw_count = 0; /* set below, once the list is whole */
    if (dl.blended_count && reserve(&v->draws, &v->draw_cap, dl.count + dl.blended_count)) {
        memcpy(v->draws + dl.count, v->blended, dl.blended_count * sizeof(*v->draws));
        dl.count += dl.blended_count;
    }
    *count = dl.count;
    v->draw_count = dl.count;
    return v->draws;
}

/* ---- culling (tc_viewer_set_aspect; port: the levels without sectors) ---- */

void tc_viewer_set_aspect(tc_viewer *v, float aspect) { v->cull_aspect = aspect > 0 && isfinite(aspect) ? aspect : 0; }

void tc_viewer_set_culling(tc_viewer *v, bool on) { v->cull_off = !on; }

/* The camera's view as four side planes through the eye (half widths tx, ty per unit of depth along f) and the plane
 * through the eye across f. k: the planes' normals' lengths, for a sphere's radius. */
typedef struct frustum { tc_vec3 eye, f, r, u; float tx, ty, kx, ky; } frustum;

static void make_frustum(const tc_viewer *v, frustum *fr) {
    float fy = tc_camera_fov_y(&v->camera, v->cull_aspect);
    fr->eye = v->camera.position;
    fr->f = tc_camera_forward(&v->camera);
    fr->r = tc_camera_right(&v->camera);
    fr->u = tc_v3_cross(fr->r, fr->f);
    fr->ty = tanf(fy / 2);
    fr->tx = fr->ty * v->cull_aspect;
    fr->kx = sqrtf(1 + fr->tx * fr->tx);
    fr->ky = sqrtf(1 + fr->ty * fr->ty);
}

/* A sphere (centre xyz, radius: mesh space) at a model matrix (the radius by its largest axis scale) is seen unless it is
 * wholly behind the eye or beyond a side. */
static bool sphere_seen(const frustum *fr, const tc_mat4 *model, const float *s) {
    const float *m = model->m;
    tc_vec3 c, rel;
    float scale, r, z, x, y;
    c = tc_v3(m[0] * s[0] + m[4] * s[1] + m[8] * s[2] + m[12], m[1] * s[0] + m[5] * s[1] + m[9] * s[2] + m[13],
              m[2] * s[0] + m[6] * s[1] + m[10] * s[2] + m[14]);
    scale = fmaxf(tc_v3_length(tc_v3(m[0], m[1], m[2])),
                  fmaxf(tc_v3_length(tc_v3(m[4], m[5], m[6])), tc_v3_length(tc_v3(m[8], m[9], m[10]))));
    r = s[3] * scale;
    rel = tc_v3_sub(c, fr->eye);
    z = tc_v3_dot(rel, fr->f);
    if (z < -r) return false;
    x = fabsf(tc_v3_dot(rel, fr->r));
    if (x - z * fr->tx > r * fr->kx) return false;
    y = fabsf(tc_v3_dot(rel, fr->u));
    return y - z * fr->ty <= r * fr->ky;
}

/* A draw by its mesh's bounding sphere (tc_viewer.mesh_sphere). Billboards (the shaders turn them) and Ed always are. */
static bool draw_seen(const tc_viewer *v, const frustum *fr, const tc_view_draw *d) {
    if (d->flags & TC_FLAG_BILLBOARD) return true;
    if (d->mesh >= v->ed_first && d->mesh < v->ed_first + v->level->ed.part_count) return true;
    return sphere_seen(fr, &d->model, v->mesh_sphere + 4 * d->mesh);
}

/* ---- cells (levels without sectors: tc_viewer.cells) ---- */

#define CELL_MIN 64.0f      /* the smallest cell's side */
#define CELL_SPLIT 16.0f    /* else the level's wider side in this many */

static bool push_cell(tc_viewer *v, uint32_t first, uint32_t count, const float sphere[4]) {
    if (v->cell_count == v->cell_cap) {
        uint32_t cap = v->cell_cap ? v->cell_cap * 2 : 256;
        struct tc_viewer_cell *c = (struct tc_viewer_cell *)tc_calloc(cap, sizeof(*c));
        if (!c) return false;
        if (v->cells) memcpy(c, v->cells, v->cell_count * sizeof(*c));
        tc_free(v->cells);
        v->cells = c;
        v->cell_cap = cap;
    }
    v->cells[v->cell_count].first = first;
    v->cells[v->cell_count].count = count;
    memcpy(v->cells[v->cell_count].sphere, sphere, sizeof(v->cells[0].sphere));
    v->cell_count++;
    return true;
}

/* Static mesh i cut into cells (a square of the ground each, by triangle centroid) when it is opaque, drawn at the
 * identity and wider than two cells: its copy in v->celled with its indices in cell order, the cells' ranges and
 * spheres in v->cells. false: out of memory. */
static bool cell_mesh(tc_viewer *v, uint32_t i, float cell) {
    const tc_mesh *m = &v->level->meshes[i];
    uint32_t tris = m->index_count / 3, nx, ny, cells, t, k, c, *start = NULL, *fill = NULL, *tri_cell = NULL, *idx = NULL;
    uint32_t first_cell = v->cell_count;
    float lo[2] = {INFINITY, INFINITY}, hi[2] = {-INFINITY, -INFINITY};
    bool ok = false;
    if (!drawable(m) || (m->surface.flags & (TC_FLAG_TRANSPARENT | TC_FLAG_BILLBOARD | TCX_FLAG_ROUND_CAMERA)) ||
        (v->split && v->split[i]) || m->index_count % 3)
        return true;
    for (k = 0; k < m->vertex_count; k++)
        for (c = 0; c < 2; c++) lo[c] = fminf(lo[c], m->vertices[k].position[c]), hi[c] = fmaxf(hi[c], m->vertices[k].position[c]);
    if (!(hi[0] - lo[0] > 2 * cell || hi[1] - lo[1] > 2 * cell)) return true;
    nx = (uint32_t)((hi[0] - lo[0]) / cell) + 1;
    ny = (uint32_t)((hi[1] - lo[1]) / cell) + 1;
    cells = nx * ny;
    start = (uint32_t *)tc_calloc(cells + 1, sizeof(*start));
    fill = (uint32_t *)tc_calloc(cells + 1, sizeof(*fill));
    tri_cell = (uint32_t *)tc_calloc(tris, sizeof(*tri_cell));
    idx = (uint32_t *)tc_calloc(m->index_count, sizeof(*idx));
    if (!start || !fill || !tri_cell || !idx) goto done;
    for (t = 0; t < tris; t++) { /* each triangle's cell by its centroid (an index out of range: cell 0) */
        const uint32_t *q = m->indices + 3 * t;
        uint32_t cx = 0, cy = 0;
        if (q[0] < m->vertex_count && q[1] < m->vertex_count && q[2] < m->vertex_count) {
            float x = (m->vertices[q[0]].position[0] + m->vertices[q[1]].position[0] + m->vertices[q[2]].position[0]) / 3;
            float y = (m->vertices[q[0]].position[1] + m->vertices[q[1]].position[1] + m->vertices[q[2]].position[1]) / 3;
            float fx = floorf((x - lo[0]) / cell), fy = floorf((y - lo[1]) / cell);
            cx = fx >= 0 ? (fx < (float)nx ? (uint32_t)fx : nx - 1) : 0;
            cy = fy >= 0 ? (fy < (float)ny ? (uint32_t)fy : ny - 1) : 0;
        }
        tri_cell[t] = cy * nx + cx;
        start[tri_cell[t] + 1]++;
    }
    for (c = 0; c < cells; c++) start[c + 1] += start[c];
    memcpy(fill, start, (cells + 1) * sizeof(*fill));
    for (t = 0; t < tris; t++) memcpy(idx + 3 * fill[tri_cell[t]]++, m->indices + 3 * t, 3 * sizeof(*idx));
    for (c = 0; c < cells; c++) { /* each cell's sphere: its vertices' bounds' centre, the farthest of them */
        float s[4] = {0, 0, 0, 0}, blo[3] = {INFINITY, INFINITY, INFINITY}, bhi[3] = {-INFINITY, -INFINITY, -INFINITY}, r2 = 0;
        uint32_t a = 3 * start[c], b = 3 * start[c + 1];
        int e;
        if (a == b) continue;
        for (k = a; k < b; k++) {
            if (idx[k] >= m->vertex_count) continue;
            for (e = 0; e < 3; e++)
                blo[e] = fminf(blo[e], m->vertices[idx[k]].position[e]), bhi[e] = fmaxf(bhi[e], m->vertices[idx[k]].position[e]);
        }
        for (e = 0; e < 3; e++) s[e] = blo[e] <= bhi[e] ? (blo[e] + bhi[e]) / 2 : 0;
        for (k = a; k < b; k++) {
            const float *p;
            float dx, dy, dz;
            if (idx[k] >= m->vertex_count) { r2 = INFINITY; continue; } /* never left out */
            p = m->vertices[idx[k]].position;
            dx = p[0] - s[0], dy = p[1] - s[1], dz = p[2] - s[2];
            r2 = fmaxf(r2, dx * dx + dy * dy + dz * dz);
        }
        s[3] = sqrtf(r2);
        if (!push_cell(v, a, b - a, s)) goto done;
    }
    v->celled[i] = *m;
    v->celled[i].indices = idx;
    v->meshes[i] = &v->celled[i];
    v->mesh_cells[2 * i] = first_cell;
    v->mesh_cells[2 * i + 1] = v->cell_count - first_cell;
    idx = NULL;
    ok = true;
done:
    if (!ok) v->cell_count = first_cell;
    tc_free(start);
    tc_free(fill);
    tc_free(tri_cell);
    tc_free(idx);
    return ok;
}

bool tcv_make_cells(tc_viewer *v) {
    const tc_level *l = v->level;
    tc_vec3 lo, hi;
    float cell;
    uint32_t i;
    if (v->sectors || !l->mesh_count) return true;
    v->mesh_cells = (uint32_t *)tc_calloc(2 * (size_t)l->mesh_count, sizeof(*v->mesh_cells));
    v->celled = (tc_mesh *)tc_calloc(l->mesh_count, sizeof(*v->celled));
    if (!v->mesh_cells || !v->celled) return false;
    tc_level_bounds(l, &lo, &hi);
    cell = fmaxf(CELL_MIN, fmaxf(hi.x - lo.x, hi.y - lo.y) / CELL_SPLIT);
    for (i = 0; i < l->mesh_count; i++)
        if (!cell_mesh(v, i, cell)) return false;
    return true;
}

/* One draw of the culled list: d, or with count > 0 its index range [first, first + count). */
static bool emit(tc_viewer *v, uint32_t *n, const tc_view_draw *d, uint32_t first, uint32_t count) {
    if (!reserve(&v->culled, &v->culled_cap, *n + 1)) return false;
    v->culled[*n] = *d;
    v->culled[*n].first_index = first;
    v->culled[*n].index_count = count;
    (*n)++;
    return true;
}

/* A whole celled static: the runs of its cells in view, each one draw (consecutive cells' ranges joined). */
static bool emit_cells(tc_viewer *v, const frustum *fr, uint32_t *n, const tc_view_draw *d) {
    const struct tc_viewer_cell *c = v->cells + v->mesh_cells[2 * d->mesh];
    uint32_t k, cells = v->mesh_cells[2 * d->mesh + 1], first = 0, count = 0;
    for (k = 0; k < cells; k++) {
        if (!sphere_seen(fr, &d->model, c[k].sphere)) {
            v->culled_cells++;
            continue;
        }
        if (count && first + count == c[k].first) {
            count += c[k].count;
            continue;
        }
        if (count && !emit(v, n, d, first, count)) return false;
        first = c[k].first;
        count = c[k].count;
    }
    return !count || emit(v, n, d, first, count);
}

/* The seen draws of the list, in its order, into v->culled (a celled static as the runs of its cells in view); the list
 * itself as it was when nothing can be culled (or out of memory). */
static const tc_view_draw *cull_draws(tc_viewer *v, const tc_view_draw *d, uint32_t *count, uint32_t *opaque_count) {
    frustum fr;
    uint32_t i, n = 0, opaque = 0;
    v->culled_count = v->culled_cells = 0;
    if (v->sectors || v->cull_aspect <= 0 || v->cull_off || *count == 0) return d;
    tcv_mesh_spheres(v);
    if (!v->mesh_sphere) return d;
    make_frustum(v, &fr);
    for (i = 0; i < *count; i++) {
        bool ok;
        uint32_t m = d[i].mesh;
        if (!draw_seen(v, &fr, &d[i])) {
            v->culled_count++;
            continue;
        }
        if (v->mesh_cells && m < v->level->mesh_count && v->mesh_cells[2 * m + 1] && d[i].first_index == 0 &&
            d[i].index_count == v->meshes[m]->index_count) {
            uint32_t before = n;
            ok = emit_cells(v, &fr, &n, &d[i]);
            if (ok && n == before) v->culled_count++; /* every cell out of view: left out whole */
        } else
            ok = emit(v, &n, &d[i], d[i].first_index, d[i].index_count);
        if (!ok) {
            v->culled_count = v->culled_cells = 0;
            return d;
        }
        if (i < *opaque_count) opaque = n;
    }
    *count = n;
    *opaque_count = opaque;
    return v->culled;
}

const tc_view_draw *tc_viewer_draws(tc_viewer *v, uint32_t *count, uint32_t *opaque_count) {
    double prof = TC_PROF_BEGIN();
    const tc_view_draw *d = build_draws(v, count, opaque_count);
    TC_PROF_END(TC_PZ_DRAWS, prof);
    /* the recording keeps the whole list: what the game put up, not what this window's shape let through */
    if (v->rec) tc_rec_draw(v->rec, v, d, *count);
    prof = TC_PROF_BEGIN();
    d = cull_draws(v, d, count, opaque_count);
    TC_PROF_END(TC_PZ_DRAW_CULL, prof);
    return d;
}

void tc_viewer_frame(const tc_viewer *v, float aspect, tc_view_frame *out) {
    const tc_camera *c = &v->camera;
    tc_vec3 right = tc_camera_right(c);
    const tc_level *l = v->level;
    bool game = v->lighting == TC_VL_GAME;
    bool fog_on = v->fog_enabled && (game ? v->sector_fog_strength > 0 : v->has_fog);
    const float *fog_color = game ? v->sector_fog_color : v->fog_color;
    const float none[3] = {0.08f, 0.09f, 0.14f};
    float vis;
    uint32_t i, k;
    memset(out, 0, sizeof(*out));
    out->view_projection = tc_camera_view_projection(c, aspect);
    out->light_direction = tc_v3_normalize(tc_v3(0.4f, -0.3f, 0.85f));
    out->ambient = 0.45f;
    out->time = v->animation_time;
    out->baked_scale = 255.0f / 128.0f;
    out->light_set = v->light_set;
    out->light_next = v->light_rate > 0 ? v->light_next : v->light_set;
    out->light_blend = v->light_rate > 0 ? v->light_blend : 0;
    out->gamma = v->gamma;
    out->fog_on = fog_on;
    out->fog_strength = game ? v->sector_fog_strength : 1;
    if (game ? v->sector_fog_strength > 0 : v->has_fog) {
        out->fog_near = game ? v->sector_fog_near : v->fog_near;
        out->fog_far = game ? v->sector_fog_far : v->fog_far;
        memcpy(out->fog_color, fog_color, sizeof(out->fog_color));
    }
    out->eye = c->position;
    out->cam_right = right;
    out->cam_up = tc_v3_cross(right, tc_camera_forward(c));
    /* The fog colour with fog (as much as it is faded in), else a dark blue. The engine's default fog (near 20000:
     * every sector without one of its own) is out of sight: its colour only from near 10000 down to 5000 (a fade). */
    vis = !fog_on ? 0 : !game ? 1 : fminf(1, fmaxf(0, (10000 - v->sector_fog_near) / 5000)) * out->fog_strength;
    for (i = 0; i < 3; i++) out->clear_color[i] = none[i] + (fog_color[i] - none[i]) * vis;

    out->lighting = v->lighting;
    for (i = 0; i < 3; i++) {
        out->sky_light[i] = game ? v->sky_light[out->light_set % 3][i] +
                                       (v->sky_light[out->light_next % 3][i] - v->sky_light[out->light_set % 3][i]) *
                                           out->light_blend
                                 : 1;
    }
    for (i = 0; game && v->light_weight && i < l->light_count; i++) {
        const tc_light *L = &l->lights[i];
        float w = v->light_weight[i];
        tc_view_light *o;
        if (w <= 0 || !L->on || L->alpha_mode == 1) continue; /* 1: alpha only, no colour */
        if (L->type == TC_LIGHT_AMBIENT) {
            for (k = 0; k < 3; k++) out->ambient_light[k] += L->color[k] * w;
            continue;
        }
        if ((L->type != TC_LIGHT_SPHERICAL && L->type != TC_LIGHT_PARALLEL) || out->light_count >= TC_VIEW_MAX_LIGHTS)
            continue; /* hotspots: not read yet (docs/DECOMP.md) */
        o = &out->lights[out->light_count++];
        tcv_view_light(o, L, w);
    }
    /* the draws' own (tc_view_draw.light_first) */
    for (i = 0; game && i < v->char_light_count && out->light_count + i < TC_VIEW_MAX_LIGHTS; i++)
        out->lights[out->light_count + i] = v->char_lights[i];
    out->special_water = l->edition == TC_EDITION_SPECIAL;
}

bool tc_viewer_shadow(const tc_viewer *v, tc_view_shadow *out) {
    tc_viewer_play *p = v->play;
    tc_vec3 o;
    tc_hit hit;
    float h, k, s;
    memset(out, 0, sizeof(*out));
    if (!p || !v->playing || v->ed_draw_count == 0) return false;
    if (p->use_ai && p->main.has_driver && (p->main.driver.dyn.card_type == 1 || p->main.driver.dyn.card_type == 7)) return false; /* swimming */
    /* Where he is drawn (between the AI's ticks), as the camera's target */
    o = !tc_main_is_ed(&p->main) ? p->main.player.position /* where the character in the slot puts him */
        : p->use_ai ? tc_v3(p->pose_draw.m[12], p->pose_draw.m[13], p->pose_draw.m[14]) : p->main.player.position;
    if (!tc_collision_raycast(&p->world, tc_v3_add(o, tc_v3(0, 0, 0.5f)), tc_v3(0, 0, -1), TC_VIEW_SHADOW_REACH + 0.5f, &hit))
        return false;
    out->normal = hit.normal.z < 0 ? tc_v3_scale(hit.normal, -1) : hit.normal; /* the raycast is two-sided */
    if (out->normal.z < 0.34f) return false; /* a wall-steep face (beyond ~70 degrees): no disc stuck to it */
    h = fmaxf(0, hit.distance - 0.5f);  /* his feet's height above the ground */
    s = fmaxf(0, 1 - h / 6);            /* 1 on the ground, 0 from 6 up: the size and the edge */
    k = 1 - h / TC_VIEW_SHADOW_REACH;   /* the darkness, gone at the reach */
    out->position = tc_v3(o.x, o.y, o.z + 0.5f - hit.distance);
    out->radius = 0.62f * (0.35f + 0.65f * s);
    out->alpha = 0.55f * k * (0.35f + 0.65f * s);
    out->inner = 0.1f + 0.35f * s;      /* a firm core on the ground, softer as he rises */
    return true;
}
