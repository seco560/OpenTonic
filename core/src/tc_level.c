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

static void rd_surface(reader *r, tc_surface *s, int v2) {
    uint32_t i, n;
    s->texture = rd_i32(r);
    s->flags = rd_u32(r);
    if (!v2) return;
    s->scroll[0] = rd_f32(r);
    s->scroll[1] = rd_f32(r);
    n = rd_u16(r);
    (void)rd_u16(r);
    s->frames = (tc_frame *)rd_array(r, n, sizeof(tc_frame), 8);
    if (!s->frames) return;
    s->frame_count = n;
    for (i = 0; i < n; i++) { s->frames[i].texture = rd_i32(r); s->frames[i].seconds = rd_f32(r); }
}

static void rd_geometry(reader *r, tc_mesh *m, uint32_t words) {
    uint32_t nv = rd_u32(r), ni = rd_u32(r), i, k;
    m->vertices = (tc_vertex *)rd_array(r, nv, sizeof(tc_vertex), (size_t)words * 4);
    if (r->error) return;
    m->vertex_count = m->vertices ? nv : 0;
    for (i = 0; i < m->vertex_count; i++) {
        tc_vertex *v = &m->vertices[i];
        for (k = 0; k < 3; k++) v->position[k] = rd_f32(r);
        for (k = 0; k < 3; k++) v->normal[k] = rd_f32(r);
        v->uv[0] = rd_f32(r);
        v->uv[1] = rd_f32(r);
        for (k = 0; k < 3 && k + 8 < words; k++) v->color[k] = rd_u32(r);
        if (words >= 13) {
            v->uv2[0] = rd_f32(r);
            v->uv2[1] = rd_f32(r);
        } else {
            v->uv2[0] = v->uv2[1] = -1;
        }
    }
    m->indices = (uint32_t *)rd_array(r, ni, sizeof(uint32_t), 4);
    if (r->error) return;
    m->index_count = m->indices ? ni : 0;
    for (i = 0; i < m->index_count; i++) m->indices[i] = rd_u32(r);
}

static void rd_mesh(reader *r, tc_mesh *m, int v2, uint32_t words) {
    rd_surface(r, &m->surface, v2);
    rd_geometry(r, m, words);
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
    for (i = 0; i < level->ed.part_count; i++) free_mesh(&level->ed.parts[i].mesh);
    tc_free(level->ed.parts);
    for (i = 0; i < level->ed.anim_count; i++) {
        tc_free(level->ed.anims[i].name);
        tc_free(level->ed.anims[i].transforms);
        tc_free(level->ed.anims[i].shown);
    }
    tc_free(level->ed.anims);
    memset(level, 0, sizeof *level);
}

tc_result tc_level_load(tc_level *level, const uint8_t *data, size_t size) {
    reader rr, *r = &rr;
    uint32_t i, k, n, words;
    int v2;
    tc_ed *ed;

    memset(level, 0, sizeof *level);
    level->light_set_count = 1;
    level->ed.static_perso = -1;
    rr.data = data; rr.size = data ? size : 0; rr.offset = 0; rr.error = TC_OK;

    if (!have(r, 4)) return TC_ERR_BAD_MAGIC;
    if (memcmp(data, "TTLV", 4) != 0) return TC_ERR_BAD_MAGIC;
    r->offset = 4;
    level->version = rd_u32(r);
    if (r->error) return r->error;
    if (level->version < 1 || level->version > 8) return TC_ERR_UNSUPPORTED_VERSION;
    v2 = level->version >= 2;
    if (level->version >= 4) level->light_set_count = rd_u32(r);
    if (level->version >= 5) (void)rd_u32(r); /* play_offset: for seeking tools; read sequentially here */
    if (level->version >= 7) {
        level->fog_near = rd_f32(r);
        level->fog_far = rd_f32(r);
        level->fog_rgb[0] = rd_f32(r);
        level->fog_rgb[1] = rd_f32(r);
        level->fog_rgb[2] = rd_f32(r);
    }
    if (level->version >= 8) {
        level->lightmap_atlas = rd_string(r);
        if (level->lightmap_atlas && !level->lightmap_atlas[0]) { tc_free(level->lightmap_atlas); level->lightmap_atlas = NULL; }
    }
    words = level->version >= 8 ? 13 : level->version >= 4 ? 11 : level->version >= 3 ? 9 : 8; /* vertex size in 4-byte words */

    n = rd_u32(r);
    level->textures = (char **)rd_array(r, n, sizeof(char *), 2);
    if (level->textures) level->texture_count = n;
    for (i = 0; i < level->texture_count && !r->error; i++) level->textures[i] = rd_string(r);

    n = rd_u32(r);
    level->meshes = (tc_mesh *)rd_array(r, n, sizeof(tc_mesh), 16);
    if (level->meshes) level->mesh_count = n;
    for (i = 0; i < level->mesh_count && !r->error; i++) rd_mesh(r, &level->meshes[i], v2, words);
    if (!v2 || r->error) goto done;

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
            rd_mesh(r, &p->parts[k].mesh, v2, words);
        }
        rd_samples(r, (uint32_t)p->sample_count * p->channel_count, &p->transforms, &p->shown);
    }
    if (level->version < 5 || r->error) goto done;

    n = rd_u32(r);
    level->collision = (tc_vec3 *)rd_array(r, n, 3 * sizeof(tc_vec3), 36);
    if (level->collision) level->collision_triangle_count = n;
    for (i = 0; i < 3 * level->collision_triangle_count; i++) {
        level->collision[i].x = rd_f32(r);
        level->collision[i].y = rd_f32(r);
        level->collision[i].z = rd_f32(r);
    }
    if (level->version >= 6) {
        tc_floor *table;
        uint32_t floor_count = rd_u16(r);
        (void)rd_u16(r);
        table = (tc_floor *)rd_array(r, floor_count, sizeof(tc_floor), 8);
        for (i = 0; i < floor_count && table; i++) {
            table[i].grip = rd_f32(r);
            table[i].rebound = rd_f32(r);
        }
        level->collision_floors = (tc_floor *)rd_array(r, level->collision_triangle_count, sizeof(tc_floor), 2);
        for (i = 0; i < level->collision_triangle_count && level->collision_floors; i++) {
            uint16_t k = rd_u16(r);
            if (k < floor_count && table) level->collision_floors[i] = table[k];
            else { level->collision_floors[i].grip = 1; level->collision_floors[i].rebound = 1; }
        }
        if (level->collision_triangle_count % 2) (void)rd_u16(r);
        tc_free(table);
    }
    if (rd_u32(r) != 1 || r->error) goto done;
    ed = &level->ed;
    rd_mat34(r, &ed->start);
    ed->static_perso = rd_i32(r);
    n = rd_u32(r);
    ed->parts = (tc_ed_part *)rd_array(r, n, sizeof(tc_ed_part), 28);
    if (ed->parts) ed->part_count = n;
    for (i = 0; i < ed->part_count && !r->error; i++) {
        ed->parts[i].object = rd_u16(r);
        (void)rd_u16(r);
        rd_mesh(r, &ed->parts[i].mesh, v2, words);
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
    if (!r->error) level->has_ed = 1;

done:
    if (r->error) {
        tc_result e = r->error;
        tc_level_free(level);
        return e;
    }
    return TC_OK;
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
    case TC_ERR_BAD_MAGIC: return "not a .ttlv file (bad magic)";
    case TC_ERR_UNSUPPORTED_VERSION: return "unsupported .ttlv version";
    case TC_ERR_TRUNCATED: return "truncated .ttlv file";
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
