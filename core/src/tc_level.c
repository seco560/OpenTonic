#include "tc_internal.h"
#include "tonic/tc_level.h"
#include "tonic/tc_alloc.h"
#include "tc_reader.h"
#include <ctype.h>
#include <string.h>

static void rd_mat34(reader *r, tc_mat34 *m) {
    int i;
    for (i = 0; i < 12; i++) m->m[i] = rd_f32(r);
}

static void rd_surface(reader *r, tc_surface *s) {
    uint32_t i, n;
    s->texture = rd_i32(r);
    s->flags = rd_u32(r);
    s->scroll[0] = rd_f32(r);
    s->scroll[1] = rd_f32(r);
    for (i = 0; i < 3; i++) s->ambient[i] = 0.5f, s->diffuse[i] = 0; /* full light until LITE says otherwise */
    n = rd_u16(r);
    (void)rd_u16(r);
    s->frames = (tc_frame *)rd_array(r, n, sizeof(tc_frame), 8);
    if (!s->frames) return;
    s->frame_count = n;
    for (i = 0; i < n; i++) { s->frames[i].texture = rd_i32(r); s->frames[i].seconds = rd_f32(r); }
}

static void rd_geometry(reader *r, tc_mesh *m) {
    uint32_t nv = rd_u32(r), ni = rd_u32(r), i, k;
    m->vertices = (tc_vertex *)rd_array(r, nv, sizeof(tc_vertex), 52); /* 52 bytes each in the file (no centre) */
    if (r->error) return;
    m->vertex_count = m->vertices ? nv : 0;
    for (i = 0; i < m->vertex_count; i++) {
        tc_vertex *v = &m->vertices[i];
        for (k = 0; k < 3; k++) v->position[k] = rd_f32(r);
        for (k = 0; k < 3; k++) v->normal[k] = rd_f32(r);
        v->uv[0] = rd_f32(r);
        v->uv[1] = rd_f32(r);
        for (k = 0; k < 3; k++) v->color[k] = rd_u32(r);
        v->uv2[0] = rd_f32(r);
        v->uv2[1] = rd_f32(r);
        for (k = 0; k < 3; k++) v->centre[k] = v->position[k]; /* until LITE gives its object's */
    }
    m->indices = (uint32_t *)rd_array(r, ni, sizeof(uint32_t), 4);
    if (r->error) return;
    m->index_count = m->indices ? ni : 0;
    for (i = 0; i < m->index_count; i++) m->indices[i] = rd_u32(r);
}

static void rd_mesh(reader *r, tc_mesh *m) {
    rd_surface(r, &m->surface);
    rd_geometry(r, m);
}

/* n_samples x n_channels matrices, then the same count of u16 objects. */
static void rd_samples(reader *r, uint32_t count, tc_mat34 **transforms, uint16_t **shown) {
    uint32_t i;
    *transforms = (tc_mat34 *)rd_array(r, count, sizeof(tc_mat34), 48);
    for (i = 0; *transforms && i < count; i++) rd_mat34(r, &(*transforms)[i]);
    *shown = (uint16_t *)rd_array(r, count, sizeof(uint16_t), 2);
    for (i = 0; *shown && i < count; i++) (*shown)[i] = rd_u16(r);
}

static void free_mesh(tc_mesh *m) {
    tc_free(m->surface.frames);
    tc_free(m->vertices);
    tc_free(m->indices);
}

void tc_level_free(tc_level *level) {
    uint32_t i, k;
    if (!level) return;
    for (i = 0; i < level->texture_count; i++) tc_free(level->textures[i]);
    tc_free(level->textures);
    for (i = 0; i < level->mesh_count; i++) free_mesh(&level->meshes[i]);
    tc_free(level->meshes);
    for (i = 0; i < level->perso_count; i++) {
        tc_perso *p = &level->persos[i];
        tc_free(p->parents);
        for (k = 0; k < p->part_count; k++) free_mesh(&p->parts[k].mesh);
        tc_free(p->parts);
        tc_free(p->transforms);
        tc_free(p->shown);
    }
    tc_free(level->persos);
    tc_free(level->collision);
    tc_free(level->collision_floors);
    tc_free(level->lightmap_atlas);
    for (i = 0; i < level->light_perso_count; i++) tc_free(level->light_persos[i].scripts);
    tc_free(level->light_persos);
    for (i = 0; i < level->ed.part_count; i++) free_mesh(&level->ed.parts[i].mesh);
    tc_free(level->ed.parts);
    for (i = 0; i < level->ed.anim_count; i++) {
        tc_free(level->ed.anims[i].name);
        tc_free(level->ed.anims[i].transforms);
        tc_free(level->ed.anims[i].shown);
    }
    tc_free(level->ed.anims);
    tc_sectors_free(&level->sectors);
    tc_free(level->lights);
    memset(level, 0, sizeof *level);
}

/* Mesh i of the viewer's table: the statics, each perso's parts, Ed's parts (NULL past them). */
static tc_mesh *table_mesh(tc_level *l, uint32_t i) {
    uint32_t p;
    if (i < l->mesh_count) return &l->meshes[i];
    i -= l->mesh_count;
    for (p = 0; p < l->perso_count; p++) {
        if (i < l->persos[p].part_count) return &l->persos[p].parts[i].mesh;
        i -= l->persos[p].part_count;
    }
    return i < l->ed.part_count ? &l->ed.parts[i].mesh : NULL;
}

/* .ttlv LITE (after SECT and EDPL: it names their sectors and meshes). Light indices out of range are dropped; sectors,
 * envs and meshes past the level's are read and skipped. */
static void read_lite(tc_level *level, reader *r) {
    uint32_t i, k, j, n, runs;
    uint16_t ns, nl;
    n = rd_u16(r);
    (void)rd_u16(r);
    level->lights = (tc_light *)rd_array(r, n, sizeof(tc_light), 52);
    if (level->lights) level->light_count = n;
    for (i = 0; i < level->light_count; i++) {
        tc_light *l = &level->lights[i];
        l->type = rd_u8(r);
        l->on = rd_u8(r);
        l->paint = rd_u8(r);
        l->alpha_mode = rd_u8(r);
        l->near_range = rd_f32(r);
        l->far_range = rd_f32(r);
        l->position.x = rd_f32(r), l->position.y = rd_f32(r), l->position.z = rd_f32(r);
        l->direction.x = rd_f32(r), l->direction.y = rd_f32(r), l->direction.z = rd_f32(r);
        for (k = 0; k < 4; k++) l->color[k] = rd_f32(r);
    }
    ns = rd_u16(r);
    (void)rd_u16(r);
    for (i = 0; i < ns && !r->error; i++) {
        tc_sector *sec = i < level->sectors.count ? &level->sectors.sectors[i] : NULL;
        uint16_t ne;
        nl = rd_u16(r);
        ne = rd_u16(r);
        if (sec) sec->lights = (uint16_t *)rd_array(r, nl, sizeof(uint16_t), 2);
        for (k = 0; k < nl; k++) {
            uint16_t l = rd_u16(r);
            if (sec && sec->lights && l < level->light_count) sec->lights[sec->light_count++] = l;
        }
        for (k = 0; k < ne; k++) {
            tc_sector_env e;
            e.fog = rd_u32(r);
            e.fog_near = rd_f32(r);
            e.fog_far = rd_f32(r);
            for (j = 0; j < 3; j++) e.fog_rgb[j] = rd_f32(r);
            if (sec && k < sec->env_count) {
                tc_sector_env *d = &sec->envs[k];
                d->fog = e.fog;
                d->fog_near = e.fog_near;
                d->fog_far = e.fog_far;
                memcpy(d->fog_rgb, e.fog_rgb, sizeof d->fog_rgb);
            }
        }
    }
    n = rd_u32(r);
    for (i = 0; i < n && !r->error; i++) {
        tc_mesh *m = table_mesh(level, i);
        float a[6];
        for (k = 0; k < 6; k++) a[k] = rd_f32(r);
        if (m) for (k = 0; k < 3; k++) m->surface.ambient[k] = a[k], m->surface.diffuse[k] = a[3 + k];
        runs = rd_u32(r);
        for (k = 0; k < runs && !r->error; k++) {
            uint32_t first = rd_u32(r), count = rd_u32(r), v;
            float c[3];
            c[0] = rd_f32(r), c[1] = rd_f32(r), c[2] = rd_f32(r);
            for (v = first; m && v < m->vertex_count && v - first < count; v++) memcpy(m->vertices[v].centre, c, sizeof c);
        }
    }
}

/* Chunks (tools/ttre/ttlv.py): LEVL, LMAP and LAIS (optional), TEXS, MESH, PRSO, COLL, FLOR, SECT, EDPL and LITE
 * (optional: no sectors, no player, the old lighting only). */
tc_result tc_level_load(tc_level *level, const uint8_t *data, size_t size) {
    chunks cs;
    reader rr, *r = &rr;
    uint32_t i, k, n;
    tc_result e;
    tc_ed *ed;

    memset(level, 0, sizeof *level);
    level->ed.static_perso = -1;
    if ((e = chunks_open(&cs, data, size, "TTLV")) != TC_OK) return e;
    chunk_stamp(&cs, level->converter, sizeof level->converter);

    chunk_need(&cs, "LEVL", r);
    level->light_set_count = rd_u32(r);
    level->fog_near = rd_f32(r);
    level->fog_far = rd_f32(r);
    for (i = 0; i < 3; i++) level->fog_rgb[i] = rd_f32(r);
    if ((e = chunk_end(r)) != TC_OK) goto fail;

    if (chunk_need(&cs, "LMAP", r)) {
        level->lightmap_atlas = rd_string(r);
        if (level->lightmap_atlas && !level->lightmap_atlas[0]) { tc_free(level->lightmap_atlas); level->lightmap_atlas = NULL; }
        if ((e = chunk_end(r)) != TC_OK) goto fail;
    }

    if (chunk_need(&cs, "LAIS", r)) {
        n = rd_u16(r);
        (void)rd_u16(r);
        level->light_persos = (tc_light_perso *)rd_array(r, n, sizeof(tc_light_perso), 4);
        if (level->light_persos) level->light_perso_count = n;
        for (i = 0; i < level->light_perso_count && !r->error; i++) {
            tc_light_perso *lp = &level->light_persos[i];
            uint32_t size = rd_u32(r);
            if (!have(r, size)) break;
            lp->scripts = (uint8_t *)tc_alloc(size ? size : 1);
            if (!lp->scripts) { r->error = TC_ERR_OUT_OF_MEMORY; break; }
            memcpy(lp->scripts, r->data + r->offset, size);
            lp->size = size;
            r->offset += size + (4 - size % 4) % 4;
            if (r->offset > r->size) r->error = TC_ERR_TRUNCATED;
        }
        if ((e = chunk_end(r)) != TC_OK) goto fail;
    }

    chunk_need(&cs, "TEXS", r);
    n = rd_u32(r);
    level->textures = (char **)rd_array(r, n, sizeof(char *), 2);
    if (level->textures) level->texture_count = n;
    for (i = 0; i < level->texture_count && !r->error; i++) level->textures[i] = rd_string(r);
    if ((e = chunk_end(r)) != TC_OK) goto fail;

    chunk_need(&cs, "MESH", r);
    n = rd_u32(r);
    level->meshes = (tc_mesh *)rd_array(r, n, sizeof(tc_mesh), 16);
    if (level->meshes) level->mesh_count = n;
    for (i = 0; i < level->mesh_count && !r->error; i++) rd_mesh(r, &level->meshes[i]);
    if ((e = chunk_end(r)) != TC_OK) goto fail;

    chunk_need(&cs, "PRSO", r);
    n = rd_u32(r);
    level->persos = (tc_perso *)rd_array(r, n, sizeof(tc_perso), 60);
    if (level->persos) level->perso_count = n;
    for (i = 0; i < level->perso_count && !r->error; i++) {
        tc_perso *p = &level->persos[i];
        rd_mat34(r, &p->world);
        p->channel_count = rd_u16(r);
        p->sample_count = rd_u16(r);
        p->sample_rate = rd_f32(r);
        p->parents = (int16_t *)rd_array(r, p->channel_count, sizeof(int16_t), 2);
        for (k = 0; p->parents && k < p->channel_count; k++) p->parents[k] = rd_i16(r);
        n = rd_u32(r);
        p->parts = (tc_part *)rd_array(r, n, sizeof(tc_part), 28);
        if (p->parts) p->part_count = n;
        for (k = 0; k < p->part_count && !r->error; k++) {
            p->parts[k].channel = rd_u16(r);
            p->parts[k].object = rd_u16(r);
            rd_mesh(r, &p->parts[k].mesh);
        }
        rd_samples(r, (uint32_t)p->sample_count * p->channel_count, &p->transforms, &p->shown);
    }
    if ((e = chunk_end(r)) != TC_OK) goto fail;

    chunk_need(&cs, "COLL", r);
    level->collision_crc = tc_crc32(r->data, r->size);
    n = rd_u32(r);
    level->collision = (tc_vec3 *)rd_array(r, n, 3 * sizeof(tc_vec3), 36);
    if (level->collision) level->collision_triangle_count = n;
    for (i = 0; i < 3 * level->collision_triangle_count; i++) {
        level->collision[i].x = rd_f32(r);
        level->collision[i].y = rd_f32(r);
        level->collision[i].z = rd_f32(r);
    }
    if ((e = chunk_end(r)) != TC_OK) goto fail;

    chunk_need(&cs, "FLOR", r);
    {
        uint32_t floor_count = rd_u16(r);
        tc_floor *table;
        (void)rd_u16(r);
        table = (tc_floor *)rd_array(r, floor_count, sizeof(tc_floor), 8);
        for (i = 0; i < floor_count && table; i++) {
            table[i].grip = rd_f32(r);
            table[i].rebound = rd_f32(r);
        }
        level->collision_floors = (tc_floor *)rd_array(r, level->collision_triangle_count, sizeof(tc_floor), 2);
        for (i = 0; i < level->collision_triangle_count && level->collision_floors; i++) {
            uint16_t f = rd_u16(r);
            if (f < floor_count && table) level->collision_floors[i] = table[f];
            else { level->collision_floors[i].grip = 1; level->collision_floors[i].rebound = 1; }
        }
        tc_free(table);
    }
    if ((e = chunk_end(r)) != TC_OK) goto fail;

    if (chunk_need(&cs, "SECT", r)) {
        e = tc_sectors_read(&level->sectors, r, level->meshes, level->mesh_count, level->perso_count);
        if (e == TC_OK) e = chunk_end(r);
        if (e != TC_OK) goto fail;
    }

    if (chunk_need(&cs, "EDPL", r)) {
        ed = &level->ed;
        rd_mat34(r, &ed->start);
        ed->static_perso = rd_i32(r);
        n = rd_u32(r);
        ed->parts = (tc_ed_part *)rd_array(r, n, sizeof(tc_ed_part), 28);
        if (ed->parts) ed->part_count = n;
        for (i = 0; i < ed->part_count && !r->error; i++) {
            ed->parts[i].object = rd_u16(r);
            (void)rd_u16(r);
            rd_mesh(r, &ed->parts[i].mesh);
        }
        n = rd_u16(r);
        (void)rd_u16(r);
        ed->anims = (tc_ed_anim *)rd_array(r, n, sizeof(tc_ed_anim), 18);
        if (ed->anims) ed->anim_count = n;
        for (i = 0; i < ed->anim_count && !r->error; i++) {
            tc_ed_anim *a = &ed->anims[i];
            a->name = rd_string(r);
            a->channel_count = rd_u16(r);
            a->sample_count = rd_u16(r);
            a->sample_rate = rd_f32(r);
            a->loop = rd_u8(r);
            (void)rd_u8(r);
            (void)rd_u16(r);
            a->ground_speed = rd_f32(r);
            rd_samples(r, (uint32_t)a->sample_count * a->channel_count, &a->transforms, &a->shown);
        }
        if ((e = chunk_end(r)) != TC_OK) goto fail;
        level->has_ed = 1;
    }

    if (chunk_need(&cs, "LITE", r)) {
        read_lite(level, r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
        level->has_lighting = 1;
    }
    return TC_OK;

fail:
    tc_level_free(level);
    return e;
}

void tc_level_bounds(const tc_level *level, tc_vec3 *lo, tc_vec3 *hi) {
    uint32_t i, k;
    tc_vec3 a = tc_v3(INFINITY, INFINITY, INFINITY), b = tc_v3(-INFINITY, -INFINITY, -INFINITY);
    for (i = 0; i < level->mesh_count; i++)
        for (k = 0; k < level->meshes[i].vertex_count; k++) {
            const float *p = level->meshes[i].vertices[k].position;
            tc_vec3 v = tc_v3(p[0], p[1], p[2]);
            a = tc_v3_min(a, v);
            b = tc_v3_max(b, v);
        }
    if (!isfinite(a.x)) { a = tc_v3(0, 0, 0); b = a; }
    *lo = a;
    *hi = b;
}

int tc_ed_find_anim(const tc_ed *ed, const char *name) {
    uint32_t i;
    for (i = 0; i < ed->anim_count; i++)
        if (ed->anims[i].name && strcmp(ed->anims[i].name, name) == 0) return (int)i;
    return -1;
}

const char *tc_result_string(tc_result r) {
    switch (r) {
    case TC_OK: return "ok";
    case TC_ERR_BAD_MAGIC: return "not a .ttlv / .ttai file (bad magic)";
    case TC_ERR_STALE: return "made by another version of the converter: re-run the conversion (docs/BUILD.md)";
    case TC_ERR_TRUNCATED: return "truncated or damaged file";
    case TC_ERR_OUT_OF_MEMORY: return "out of memory";
    }
    return "unknown error";
}

static int same_level(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
    return *a == 0 && *b == 0;
}

const char *tc_level_after_cutscene(const char *name) {
    /* The character of each cutscene level whose script changes level (tools: every ChangeLevel / PROC_29 in the
     * level's persos; NBD_Appui*Start, which names every level, is the generic "press start" selector). */
    static const char *const next[][2] = {{"bar", "cock01"},        /* PAT_Barman_Astuce01 */
                                          {"mammyon", "lecanyon"},  /* PAT_Mammouth01 */
                                          {"pharmmap", "marmite"},  /* PAT_Pharmacien001 */
                                          {"pyr_fou", "pyramide"},  /* PAT_Fou */
                                          {"spycarota", "carota"},  /* PAT_EspionNews */
                                          {"spynorth", "north"}};   /* PAT_EspionAstuce02 */
    size_t i;
    for (i = 0; name && i < sizeof(next) / sizeof(next[0]); i++)
        if (same_level(name, next[i][0])) return next[i][1];
    return NULL;
}

int tc_level_is_cutscene(const char *name) {
    static const char *const cutscenes[] = {"bar", "death", "mammyon", "outro", "pharmmap", "pyr_fou", "spycarota",
                                            "spynorth"};
    size_t i;
    for (i = 0; name && i < sizeof(cutscenes) / sizeof(cutscenes[0]); i++) {
        const char *a = name, *b = cutscenes[i];
        while (*a && tolower((unsigned char)*a) == *b) a++, b++;
        if (*a == 0 && *b == 0) return 1;
    }
    return 0;
}
