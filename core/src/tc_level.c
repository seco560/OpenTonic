#include "tc_internal.h"
#include "tonic/tc_level.h"
#include "tonic/tc_alloc.h"
#include "tc_reader.h"
#include "tonic/custom/tcx.h"
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

static void free_speech(tc_speech_data *s) {
    uint32_t i;
    for (i = 0; i < s->dialogue_count; i++) {
        tc_free(s->dialogues[i].phonemes);
        tc_free(s->dialogues[i].text);
        tc_free(s->dialogues[i].events);
    }
    tc_free(s->dialogues);
    tc_free(s->glyphs);
    tc_free(s->atlas);
}

void tc_level_free(tc_level *level) {
    uint32_t i, k;
    if (!level) return;
    free_speech(&level->speech);
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
    for (i = 0; level->cast.families && i < level->cast.family_count; i++) {
        tc_cast_family *f = &level->cast.families[i];
        for (k = 0; f->states && k < f->state_count; k++) {
            tc_free(f->states[k].transitions);
            tc_free(f->states[k].prohibited);
            tc_free(f->states[k].sounds);
        }
        for (k = 0; f->anims && k < f->anim_count; k++) {
            tc_free(f->anims[k].transforms);
            tc_free(f->anims[k].shown);
        }
        tc_free(f->states);
        tc_free(f->anims);
        tc_free(f->cards);
        tc_zde_set_free(&f->zde);
        tc_free(f->root);
        tc_free(f->root_first);
        tc_free(f->root_rows);
        tc_free(f->body);
        tc_free(f->bodies);
    }
    tcx_level_free(level->custom);
    level->custom = NULL;
    tc_free(level->cast.families);
    tc_free(level->cast.cards);
    for (i = 0; level->cast.entries && i < level->cast.count; i++) {
        tc_free(level->cast.entries[i].scripts);
        tc_free(level->cast.entries[i].tables);
        tc_free(level->cast.entries[i].zones);
    }
    tc_free(level->cast.entries);
    tc_free(level->cast.main_zones.zones);
    for (i = 0; level->cast.ghosts && i < level->cast.ghost_count; i++) tc_free(level->cast.ghosts[i].dsg);
    tc_free(level->cast.ghosts);
    for (i = 0; level->pcol.persos && i < level->pcol.count; i++) {
        tc_free(level->pcol.persos[i].objects);
        tc_free(level->pcol.persos[i].pose);
    }
    tc_free(level->pcol.persos);
    tc_free(level->pcol.prims);
    tc_free(level->lights);
    tc_free(level->ways.waypoints);
    tc_free(level->ways.persos);
    tc_free(level->ways.ways);
    tc_free(level->ways.points);
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

/* .ttlv CHLT (after LITE): per sector, the lights that reach only its characters and water. */
static void read_chlt(tc_level *level, reader *r) {
    uint32_t i, k;
    uint16_t ns = rd_u16(r);
    (void)rd_u16(r);
    for (i = 0; i < ns && !r->error; i++) {
        tc_sector *sec = i < level->sectors.count ? &level->sectors.sectors[i] : NULL;
        uint16_t n = rd_u16(r);
        if (sec) sec->char_lights = (uint16_t *)rd_array(r, n, sizeof(uint16_t), 2);
        for (k = 0; k < n; k++) {
            uint16_t l = rd_u16(r);
            if (sec && sec->char_lights && l < level->light_count) sec->char_lights[sec->char_light_count++] = l;
        }
    }
}

/* n zones of a CAST record into e->zones. */
static void read_zones(tc_cast_entry *e, reader *r) {
    uint32_t k, j;
    e->zones = (tc_cast_zone *)rd_array(r, e->zone_count, sizeof(tc_cast_zone), 4);
    if (!e->zones) e->zone_count = 0;
    for (k = 0; k < e->zone_count && !r->error; k++) {
        tc_cast_zone *z = &e->zones[k];
        uint16_t n;
        z->index = rd_u16(r);
        n = rd_u16(r);
        for (j = 0; j < n && !r->error; j++) {
            tc_cast_zone_prim q;
            q.kind = rd_u32(r);
            q.a.x = rd_f32(r), q.a.y = rd_f32(r), q.a.z = rd_f32(r);
            q.b.x = rd_f32(r), q.b.y = rd_f32(r), q.b.z = rd_f32(r);
            if (z->count < TC_CAST_ZONE_PRIMS) z->prims[z->count++] = q;
        }
    }
}

/* .ttlv CAST (tools/ttre/cast.py). Families and entries past what was allocated are dropped (out of memory). */
static void read_cast(tc_cast_data *c, reader *r) {
    uint32_t i, k, j;
    c->main_actor = rd_u16(r);
    c->family_count = rd_u16(r);
    c->count = rd_u16(r);
    c->main_alias = rd_u16(r);
    c->main_zones.zone_count = rd_u16(r);
    c->world = rd_u16(r); /* 0 in files from before 2026-11-05 */
    read_zones(&c->main_zones, r);
    c->ghost_count = rd_u16(r);
    (void)rd_u16(r);
    c->ghosts = (struct tc_cast_ghost *)rd_array(r, c->ghost_count, sizeof(*c->ghosts), 20);
    if (!c->ghosts) c->ghost_count = 0;
    for (i = 0; i < c->ghost_count && !r->error; i++) {
        struct tc_cast_ghost *g = &c->ghosts[i];
        uint32_t size;
        g->id = rd_u16(r);
        (void)rd_u16(r);
        g->position.x = rd_f32(r), g->position.y = rd_f32(r), g->position.z = rd_f32(r);
        size = rd_u32(r);
        if (!have(r, size)) { r->error = TC_ERR_TRUNCATED; break; }
        g->dsg = (uint8_t *)tc_alloc(size ? size : 1);
        if (!g->dsg) { r->error = TC_ERR_OUT_OF_MEMORY; break; }
        memcpy(g->dsg, r->data + r->offset, size);
        g->size = size;
        r->offset += size;
    }
    c->families = (tc_cast_family *)rd_array(r, c->family_count, sizeof(tc_cast_family), 4);
    if (!c->families) c->family_count = 0;
    for (i = 0; i < c->family_count && !r->error; i++) {
        tc_cast_family *f = &c->families[i];
        f->state_count = rd_u16(r);
        f->anim_count = rd_u16(r);
        f->states = (tc_cast_state *)rd_array(r, f->state_count, sizeof(tc_cast_state), 16);
        if (!f->states) f->state_count = 0;
        for (k = 0; k < f->state_count && !r->error; k++) {
            tc_cast_state *s = &f->states[k];
            s->next = rd_i16(r);
            s->frames = rd_u16(r);
            s->speed = rd_u8(r);
            s->flags = rd_u8(r);
            s->repeats = rd_u8(r);
            (void)rd_u8(r);
            s->anim = rd_u16(r);
            s->transition_count = rd_u16(r);
            s->prohibited_count = rd_u16(r);
            s->sound_count = rd_u16(r);
            s->transitions = (uint16_t *)rd_array(r, 2u * s->transition_count, 2, 2);
            for (j = 0; s->transitions && j < 2u * s->transition_count; j++) s->transitions[j] = rd_u16(r);
            s->prohibited = (uint16_t *)rd_array(r, s->prohibited_count, 2, 2);
            for (j = 0; s->prohibited && j < s->prohibited_count; j++) s->prohibited[j] = rd_u16(r);
            s->sounds = (tc_cast_sound *)rd_array(r, s->sound_count, sizeof(tc_cast_sound), 8);
            for (j = 0; s->sounds && j < s->sound_count; j++) {
                s->sounds[j].frame = rd_u16(r);
                s->sounds[j].event = rd_u16(r);
                s->sounds[j].first = rd_u8(r);
                s->sounds[j].period = rd_u8(r);
                (void)rd_u16(r);
            }
            if ((s->transition_count && !s->transitions) || (s->prohibited_count && !s->prohibited) ||
                (s->sound_count && !s->sounds))
                s->transition_count = s->prohibited_count = s->sound_count = 0;
            if (s->anim != 0xFFFF && s->anim >= f->anim_count) s->anim = 0xFFFF;
        }
        f->anims = (tc_cast_anim *)rd_array(r, f->anim_count, sizeof(tc_cast_anim), 8);
        if (!f->anims) f->anim_count = 0;
        for (k = 0; k < f->anim_count && !r->error; k++) {
            tc_cast_anim *a = &f->anims[k];
            a->channel_count = rd_u16(r);
            a->frame_count = rd_u16(r);
            a->rate = rd_f32(r);
            rd_samples(r, (uint32_t)a->frame_count * a->channel_count, &a->transforms, &a->shown);
            if (!a->transforms || !a->shown) a->frame_count = 0;
        }
    }
    c->entries = (tc_cast_entry *)rd_array(r, c->count, sizeof(tc_cast_entry), 76);
    if (!c->entries) c->count = 0;
    for (i = 0; i < c->count && !r->error; i++) {
        tc_cast_entry *e = &c->entries[i];
        uint32_t size;
        e->family_type = 0xFFFF; /* CMEC's, when there */
        e->id = rd_u16(r);
        e->prso = rd_u16(r);
        e->family = rd_u16(r);
        e->sector = rd_u16(r);
        e->initial_state = rd_u16(r);
        e->hit_points = rd_u8(r);
        e->hit_points_max = rd_u8(r);
        e->active = rd_u8(r);
        e->dead = rd_u8(r);
        e->mechanics = rd_u8(r);
        e->animation = rd_u8(r);
        e->reinit = rd_u8(r);
        e->flags = rd_u8(r);
        (void)rd_u16(r);
        e->radius2 = rd_f32(r);
        rd_mat34(r, &e->world);
        e->zone_count = rd_u16(r);
        (void)rd_u16(r);
        if (e->zone_count) {
            for (k = 0; k < 12; k++) e->zone_inverse[k] = rd_f32(r);
            read_zones(e, r);
        }
        size = rd_u32(r);
        if (!have(r, size)) break;
        e->scripts = (uint8_t *)tc_alloc(size ? size : 1);
        if (!e->scripts) { r->error = TC_ERR_OUT_OF_MEMORY; break; }
        memcpy(e->scripts, r->data + r->offset, size);
        e->size = size;
        r->offset += size + (4 - size % 4) % 4;
        if (r->offset > r->size) { r->error = TC_ERR_TRUNCATED; break; }
        if (e->flags & TC_CAST_TABLES) {
            size = rd_u32(r);
            if (!have(r, size)) break;
            e->tables = (uint8_t *)tc_alloc(size ? size : 1);
            if (!e->tables) { r->error = TC_ERR_OUT_OF_MEMORY; break; }
            memcpy(e->tables, r->data + r->offset, size);
            e->tables_size = size;
            r->offset += size + (4 - size % 4) % 4;
            if (r->offset > r->size) r->error = TC_ERR_TRUNCATED;
        }
        if (e->family >= c->family_count) e->size = 0; /* not run */
    }
}

void tc_zde_set_read(tc_zde_set *z, reader *r) {
    uint32_t i, k, n = 0;
    size_t start;
    memset(z, 0, sizeof(*z));
    z->zone_count = rd_u16(r);
    z->state_count = rd_u16(r);
    z->zones = (tc_zde_zone *)rd_array(r, z->zone_count, sizeof(tc_zde_zone), 4);
    if (!z->zones) z->zone_count = 0;
    start = r->offset; /* first pass: count the primitives */
    for (i = 0; i < z->zone_count && !r->error; i++) {
        uint16_t c = rd_u16(r);
        (void)rd_u16(r);
        if (!have(r, (size_t)c * 32)) { r->error = TC_ERR_TRUNCATED; return; }
        r->offset += (size_t)c * 32;
        n += c;
    }
    r->offset = start;
    z->prims = (tc_zde_prim *)tc_calloc(n ? n : 1, sizeof(tc_zde_prim));
    if (!z->prims) { r->error = TC_ERR_OUT_OF_MEMORY; return; }
    for (i = 0; i < z->zone_count && !r->error; i++) {
        uint16_t c = rd_u16(r);
        (void)rd_u16(r);
        z->zones[i].first = z->prim_count;
        z->zones[i].count = c;
        for (k = 0; k < c; k++) {
            tc_zde_prim *p = &z->prims[z->prim_count++];
            p->kind = (uint8_t)rd_u32(r);
            p->a.x = rd_f32(r), p->a.y = rd_f32(r), p->a.z = rd_f32(r);
            p->b.x = rd_f32(r), p->b.y = rd_f32(r), p->b.z = rd_f32(r);
            p->type = rd_u16(r);
            (void)rd_u16(r);
        }
    }
    z->active = (uint32_t *)rd_array(r, z->state_count, 4, 4);
    if (!z->active) z->state_count = 0;
    for (i = 0; i < z->state_count; i++) z->active[i] = rd_u32(r);
}

void tc_zde_set_free(tc_zde_set *z) {
    if (!z) return;
    tc_free(z->zones);
    tc_free(z->prims);
    tc_free(z->active);
    memset(z, 0, sizeof(*z));
}

/* .ttlv CMEC (tools/ttre/cast.py mech_chunk): after CAST. */
static void read_cmec(tc_cast_data *c, reader *r) {
    uint32_t i, k, n_fam;
    c->card_count = rd_u16(r);
    n_fam = rd_u16(r);
    c->cards = (tc_ai_card *)rd_array(r, c->card_count, sizeof(tc_ai_card), 8);
    if (!c->cards) c->card_count = 0;
    for (i = 0; i < c->card_count && !r->error; i++) {
        tc_ai_card *d = &c->cards[i];
        uint32_t n;
        d->type = rd_u32(r);
        n = rd_u32(r);
        d->word_count = n < 19 ? n : 19;
        for (k = 0; k < n; k++) {
            uint32_t w = rd_u32(r);
            if (k < 19) d->words[k] = w;
        }
    }
    for (i = 0; i < n_fam && !r->error; i++) {
        uint16_t n = rd_u16(r);
        tc_cast_family *f = i < c->family_count ? &c->families[i] : NULL;
        uint16_t *row = (uint16_t *)rd_array(r, n, 2, 2);
        for (k = 0; row && k < n; k++) {
            row[k] = rd_u16(r);
            if (row[k] != 0xFFFF && row[k] >= c->card_count) row[k] = 0xFFFF;
        }
        if (f && row && n == f->state_count) f->cards = row;
        else tc_free(row);
    }
    if (r->offset < r->size) { /* the entries' own sweep bodies */
        uint16_t n = rd_u16(r);
        (void)rd_u16(r);
        for (i = 0; i < n && !r->error; i++) {
            float rad = rd_f32(r);
            uint8_t prio = rd_u8(r), meets = rd_u8(r);
            uint16_t ftype = rd_u16(r); /* older files: 0 there */
            if (i < c->count) {
                c->entries[i].own_radius = rad;
                c->entries[i].own_priority = prio;
                c->entries[i].own_meets = meets;
                c->entries[i].family_type = ftype;
            }
        }
    }
    if (r->offset < r->size) { /* the walkers: root motion and ZDM bodies per family (tools/ttre/cast.py walk_block) */
        uint16_t n = rd_u16(r);
        (void)rd_u16(r);
        for (i = 0; i < n && !r->error; i++) {
            tc_cast_family scratch, *f = i < c->family_count ? &c->families[i] : &scratch;
            uint32_t rows = rd_u32(r), j;
            memset(&scratch, 0, sizeof(scratch));
            if (f == &scratch) return; /* more families than CAST's: a mismatch, stop */
            f->root = (tc_vec3 *)rd_array(r, rows, sizeof(tc_vec3), 12);
            if (rows && !f->root) return;
            f->root_count = rows;
            for (j = 0; j < rows; j++) {
                f->root[j].x = rd_f32(r);
                f->root[j].y = rd_f32(r);
                f->root[j].z = rd_f32(r);
            }
            f->root_first = (uint32_t *)tc_calloc(f->state_count ? f->state_count : 1, sizeof(uint32_t));
            f->root_rows = (uint16_t *)tc_calloc(f->state_count ? f->state_count : 1, sizeof(uint16_t));
            f->body = (uint16_t *)tc_calloc(f->state_count ? f->state_count : 1, sizeof(uint16_t));
            if (!f->root_first || !f->root_rows || !f->body) { r->error = TC_ERR_OUT_OF_MEMORY; return; }
            for (j = 0; j < f->state_count; j++) {
                f->root_first[j] = rd_u32(r);
                f->root_rows[j] = rd_u16(r);
                f->body[j] = rd_u16(r);
                if ((uint64_t)f->root_first[j] + f->root_rows[j] > rows) f->root_rows[j] = 0;
            }
            f->body_count = rd_u16(r);
            (void)rd_u16(r);
            f->bodies = (tc_ai_sphere_set *)rd_array(r, f->body_count, sizeof(tc_ai_sphere_set), 4);
            if (!f->bodies) f->body_count = 0;
            for (j = 0; j < f->body_count && !r->error; j++) {
                tc_ai_sphere_set *b = &f->bodies[j];
                uint16_t m = rd_u16(r), q;
                (void)rd_u16(r);
                for (q = 0; q < m; q++) {
                    tc_ai_sphere sp;
                    memset(&sp, 0, sizeof(sp));
                    sp.center.x = rd_f32(r);
                    sp.center.y = rd_f32(r);
                    sp.center.z = rd_f32(r);
                    sp.radius = rd_f32(r);
                    sp.flags = rd_u16(r);
                    sp.point = rd_u16(r) == 5;
                    sp.rebound = rd_f32(r);
                    sp.grip = rd_f32(r);
                    if (b->count < TC_AI_MAX_SPHERES) b->spheres[b->count++] = sp;
                }
            }
            for (j = 0; j < f->state_count; j++)
                if (f->body[j] != 0xFFFF && f->body[j] >= f->body_count) f->body[j] = 0xFFFF;
        }
    }
    if (r->offset < r->size) { /* the ZDE zones per family (ai.py zde_block) */
        uint16_t n = rd_u16(r);
        (void)rd_u16(r);
        for (i = 0; i < n && !r->error; i++) {
            tc_zde_set z;
            tc_zde_set_read(&z, r);
            if (i < c->family_count && z.state_count == c->families[i].state_count) c->families[i].zde = z;
            else tc_zde_set_free(&z);
        }
    }
}

/* .ttlv SPCH (tools/ttre/speech.py). */
static void read_speech(tc_speech_data *s, reader *r) {
    uint32_t i, k, nd, ng;
    size_t atlas;
    nd = rd_u32(r);
    ng = rd_u32(r);
    s->spacing = rd_f32(r);
    s->atlas_w = rd_u16(r);
    s->atlas_h = rd_u16(r);
    for (k = 0; k < 4; k++) s->panel[k] = rd_u8(r);
    s->has_arrival = rd_u32(r) != 0;
    s->arrival.x = rd_f32(r), s->arrival.y = rd_f32(r), s->arrival.z = rd_f32(r);
    atlas = (size_t)s->atlas_w * s->atlas_h * 4;
    s->atlas = (uint8_t *)rd_array(r, (uint32_t)atlas, 1, 1);
    for (i = 0; s->atlas && i < atlas; i++) s->atlas[i] = rd_u8(r);
    s->glyphs = (tc_glyph *)rd_array(r, ng, sizeof(tc_glyph), 40);
    if (s->glyphs) s->glyph_count = ng;
    for (i = 0; i < s->glyph_count && !r->error; i++) {
        tc_glyph *g = &s->glyphs[i];
        g->code = rd_u16(r);
        (void)rd_u16(r);
        g->step = rd_f32(r), g->advance = rd_f32(r), g->shift = rd_f32(r);
        g->half_w = rd_f32(r), g->half_h = rd_f32(r);
        g->u0 = rd_f32(r), g->v0 = rd_f32(r), g->u1 = rd_f32(r), g->v1 = rd_f32(r);
    }
    s->dialogues = (tc_dialogue *)rd_array(r, nd, sizeof(tc_dialogue), 38);
    if (s->dialogues) s->dialogue_count = nd;
    for (i = 0; i < s->dialogue_count && !r->error; i++) {
        tc_dialogue *d = &s->dialogues[i];
        uint32_t text;
        uint8_t flags;
        d->frames = rd_u16(r);
        d->rate = rd_u16(r);
        d->voice = rd_u16(r);
        flags = rd_u8(r);
        d->channel_count = rd_u8(r);
        for (k = 0; k < TC_DIALOGUE_CHANNELS; k++) d->channels[k] = rd_u8(r);
        text = rd_u16(r);
        d->event_count = rd_u16(r);
        for (k = 0; k < 4; k++) d->window[k] = rd_u32(r);
        d->phonemes = (uint8_t *)rd_array(r, (uint32_t)d->frames * 3, 1, 1);
        for (k = 0; d->phonemes && k < (uint32_t)d->frames * 3; k++) d->phonemes[k] = rd_u8(r);
        if (text) {
            if (!have(r, text)) { r->error = TC_ERR_TRUNCATED; return; }
            d->text = (char *)tc_calloc(text + 1u, 1);
            if (!d->text) { r->error = TC_ERR_OUT_OF_MEMORY; return; }
            for (k = 0; k < text; k++) d->text[k] = (char)rd_u8(r);
        }
        (void)flags;
        d->events = (tc_dialogue_event *)rd_array(r, d->event_count, sizeof(tc_dialogue_event), 8);
        for (k = 0; d->events && k < d->event_count; k++) {
            d->events[k].frame = rd_u32(r);
            d->events[k].state = rd_u32(r);
        }
    }
}

/* .ttlv WAYS (tools/ttre/ai.py ways_chunk). */
static void read_ways(tc_ways *w, reader *r) {
    uint32_t i, k, nw, np;
    nw = rd_u16(r);
    np = rd_u16(r);
    w->waypoints = (tc_waypoint *)rd_array(r, nw, sizeof(tc_waypoint), 20);
    if (w->waypoints) w->waypoint_count = (uint16_t)nw;
    for (i = 0; i < w->waypoint_count && !r->error; i++) {
        tc_waypoint *p = &w->waypoints[i];
        p->position.x = rd_f32(r);
        p->position.y = rd_f32(r);
        p->position.z = rd_f32(r);
        p->radius = rd_f32(r);
        p->father = rd_u16(r);
        (void)rd_u16(r);
    }
    w->persos = (tc_perso_ways *)rd_array(r, np, sizeof(tc_perso_ways), 4);
    if (!w->persos) return;
    w->perso_count = (uint16_t)np;
    { /* every way and every point takes 2 bytes at least: the rest of the chunk bounds both */
        size_t left = r->size > r->offset ? (r->size - r->offset) / 2 + 1 : 1;
        w->ways = (tc_way *)tc_calloc(left, sizeof(tc_way));
        w->points = (uint16_t *)tc_calloc(left, sizeof(uint16_t));
        if (!w->ways || !w->points) { r->error = TC_ERR_OUT_OF_MEMORY; return; }
    }
    for (i = 0; i < np && !r->error; i++) {
        tc_perso_ways *p = &w->persos[i];
        size_t start = r->offset;
        p->perso = rd_u16(r);
        p->count = rd_u16(r);
        p->first = w->way_count;
        for (k = 0; k < p->count && !r->error; k++) {
            uint16_t n = rd_u16(r), j;
            if (!have(r, (size_t)n * 2)) { r->error = TC_ERR_TRUNCATED; return; }
            w->ways[w->way_count].first = w->point_count;
            w->ways[w->way_count].count = n;
            for (j = 0; j < n; j++) {
                uint16_t q = rd_u16(r);
                w->points[w->point_count++] = q <= w->waypoint_count ? q : 0;
            }
            w->way_count++;
        }
        if ((r->offset - start) % 4) r->offset += 4 - (r->offset - start) % 4;
        if (r->offset > r->size) r->error = TC_ERR_TRUNCATED;
    }
}

const tc_perso_ways *tc_ways_of(const tc_ways *w, int32_t perso) {
    uint16_t i;
    for (i = 0; w && i < w->perso_count; i++)
        if ((int32_t)w->persos[i].perso == perso) return &w->persos[i];
    return NULL;
}

/* .ttlv PCOL (tools/ttre/ttlv.py perso_collision). */
static void read_pcol(tc_pcol *c, reader *r) {
    uint32_t i, k, j, total = 0;
    size_t start = r->offset;
    c->count = rd_u16(r);
    c->main_priority = rd_u8(r);
    c->main_flags = rd_u8(r);
    /* first pass: count the primitives, to allocate them in one array */
    for (i = 0; i < c->count && !r->error; i++) {
        uint16_t no, np;
        r->offset += 8 + 48;
        no = rd_u16(r);
        np = rd_u16(r);
        for (k = 0; k < no && !r->error; k++) {
            uint16_t n;
            (void)rd_u16(r);
            n = rd_u16(r);
            total += n;
            if (!have(r, (size_t)n * 48)) break;
            r->offset += (size_t)n * 48;
        }
        if (!have(r, (size_t)np * 52)) break;
        r->offset += (size_t)np * 52;
    }
    if (r->error) return;
    r->offset = start + 4;
    c->persos = (tc_pcol_perso *)rd_array(r, c->count, sizeof(tc_pcol_perso), 60);
    c->prims = (tc_pcol_prim *)rd_array(r, total, sizeof(tc_pcol_prim), 48);
    if (!c->persos || (total && !c->prims)) { c->count = 0; return; }
    c->prim_count = 0;
    for (i = 0; i < c->count && !r->error; i++) {
        tc_pcol_perso *p = &c->persos[i];
        p->id = rd_u16(r);
        p->prso = rd_u16(r);
        p->sector = rd_u16(r);
        p->priority = rd_u8(r);
        p->flags = rd_u8(r);
        rd_mat34(r, &p->world);
        p->object_count = rd_u16(r);
        p->pose_count = rd_u16(r);
        p->objects = (tc_pcol_object *)rd_array(r, p->object_count, sizeof(tc_pcol_object), 4);
        p->pose = (tc_pcol_pose *)rd_array(r, p->pose_count, sizeof(tc_pcol_pose), 52);
        if ((p->object_count && !p->objects) || (p->pose_count && !p->pose)) { p->object_count = p->pose_count = 0; break; }
        for (k = 0; k < p->object_count && !r->error; k++) {
            tc_pcol_object *o = &p->objects[k];
            tc_vec3 lo = tc_v3(INFINITY, INFINITY, INFINITY), hi = tc_v3(-INFINITY, -INFINITY, -INFINITY);
            uint16_t n;
            o->object = rd_u16(r);
            n = rd_u16(r);
            o->first = c->prim_count;
            for (j = 0; j < n && !r->error && c->prim_count < total; j++) {
                tc_pcol_prim *q = &c->prims[c->prim_count++];
                float v[9];
                uint32_t m;
                q->kind = rd_u8(r);
                q->sound = rd_u8(r);
                q->collide_id = rd_u16(r);
                q->floor.grip = rd_f32(r);
                q->floor.rebound = rd_f32(r);
                for (m = 0; m < 9; m++) v[m] = rd_f32(r);
                q->a = tc_v3(v[0], v[1], v[2]);
                q->b = tc_v3(v[3], v[4], v[5]);
                q->c = tc_v3(v[6], v[7], v[8]);
                if (q->kind == TC_PCOL_TRIANGLE) {
                    tc_vec3 nn = tc_v3_cross(tc_v3_sub(q->b, q->a), tc_v3_sub(q->c, q->a));
                    float len = tc_v3_length(nn);
                    q->n = len > 1e-12f ? tc_v3_div(nn, len) : tc_v3(0, 0, 1);
                    lo = tc_v3_min(lo, tc_v3_min(q->a, tc_v3_min(q->b, q->c)));
                    hi = tc_v3_max(hi, tc_v3_max(q->a, tc_v3_max(q->b, q->c)));
                } else if (q->kind == TC_PCOL_SPHERE) {
                    q->r = v[3];
                    q->b = q->c = tc_v3(0, 0, 0);
                    lo = tc_v3_min(lo, tc_v3_adds(q->a, -q->r));
                    hi = tc_v3_max(hi, tc_v3_adds(q->a, q->r));
                } else {
                    lo = tc_v3_min(lo, q->a);
                    hi = tc_v3_max(hi, q->b);
                }
            }
            o->count = c->prim_count - o->first;
            if (o->count) {
                o->centre = tc_v3_scale(tc_v3_add(lo, hi), 0.5f);
                o->radius = 0.5f * tc_v3_length(tc_v3_sub(hi, lo));
            }
        }
        for (k = 0; k < p->pose_count && !r->error; k++) {
            p->pose[k].object = rd_u16(r);
            (void)rd_u16(r);
            rd_mat34(r, &p->pose[k].m);
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
    level->data_hash = chunks_hash(&cs);

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
        e = tc_sectors_read(&level->sectors, r, level->meshes, level->mesh_count, level->perso_count,
                            level->collision_triangle_count);
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

    if (chunk_need(&cs, "CAST", r)) {
        read_cast(&level->cast, r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
    }

    if (chunk_need(&cs, "CMEC", r)) { /* optional: the cast's cards (absent: the cast runs no mechanics) */
        read_cmec(&level->cast, r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
    }

    if (chunk_need(&cs, "PCOL", r)) { /* optional: older files have the persos' collision in COLL */
        read_pcol(&level->pcol, r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
    }

    if (chunk_need(&cs, "WAYS", r)) { /* optional: the waypoints and ways (absent: the way primitives do nothing) */
        read_ways(&level->ways, r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
    }

    if (chunk_need(&cs, "SPCH", r)) { /* optional: what the characters say (absent: their lines end at once) */
        read_speech(&level->speech, r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
    }

    if (chunk_need(&cs, "EDIT", r)) { /* optional: the game version the level comes from (absent: retail) */
        level->edition = rd_u32(r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
    }

    if (chunk_need(&cs, "VEHI", r)) { /* optional: custom gameplay's (core/src/custom, tcx.h), read there */
        if ((e = tcx_level_read(level, "VEHI", r->data, r->size)) != TC_OK) goto fail;
        r->offset = r->size;
    }
    if (chunk_need(&cs, "SKIA", r)) { /* optional: custom gameplay's too */
        if ((e = tcx_level_read(level, "SKIA", r->data, r->size)) != TC_OK) goto fail;
        r->offset = r->size;
    }

    if (chunk_need(&cs, "LITE", r)) {
        read_lite(level, r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
        level->has_lighting = 1;
    }

    if (chunk_need(&cs, "CHLT", r)) { /* optional: lights for characters only (absent: none, as on retail) */
        read_chlt(level, r);
        if ((e = chunk_end(r)) != TC_OK) goto fail;
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
            if (level->meshes[i].surface.flags & TCX_FLAG_ROUND_CAMERA) break; /* a custom sky: not a place (tcx.h) */
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
