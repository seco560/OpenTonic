#include "tc_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "tonic/tc_viewer.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_anim.h"
#include "tonic/tc_ed_ai.h"
#include "tonic/tc_cast.h"
#include "tonic/tc_obstacle.h"

/* ---- play session ---- */

/* The tc_player points at its world and the AI driver at the player, so the session stays at one address. */
struct tc_viewer_play {
    tc_collision world;
    tc_obstacles obstacles;      /* the persos' own collision (.ttlv PCOL), met by Ed's sweeps (world.obstacles) */
    tc_player player;
    tc_ai_model model;
    bool has_model;
    /* A Special Edition level that switches walking / skiing (.ttai MODZ + ALTM): the skier's model, which one drives
     * Ed now (TC_AI_MODE_*), and which single-shot zones have fired (play_mode_zones). */
    tc_ai_model alt;
    bool has_alt;
    uint8_t mode;
    uint8_t zone_fired[64];
    tc_ed_ai driver;
    bool has_driver;
    tc_cast cast;                /* the level persos that run their own AI (.ttlv CAST), with Ed as the main actor */
    bool has_cast;
    bool use_ai;
    char level_name[128];        /* tc_ed_ai keeps a pointer to it */
    /* Ed's AI and mechanics step in fixed ticks of TC_ED_AI_TICK; drawing interpolates between the last two. */
    float tick_time;             /* seconds not yet stepped */
    bool have_pose;
    bool ski_shot;          /* the skier has ridden: the camera's shot is the ski follow (stands for the start trigger,
                               tc_viewer_camera_shot) */
    tc_mat4 pose_prev, pose_cur, pose_draw; /* Ed's model matrix after the last two ticks, and this frame's */
    int clip_prev, clip_cur;     /* level->ed.anims index after those ticks */
    float phase_prev, phase_cur;
    float rate_cur;              /* animation frames per second of the state after the last tick */
    bool loop_cur;
};

static bool level_has_ed(const tc_level *l) { return l->has_ed && l->collision_triangle_count > 0; }

/* Ed's animation by .ttlv name with at least one sample, or -1. */
static int ed_anim(const tc_level *l, const char *name) {
    int i = tc_ed_find_anim(&l->ed, name);
    return i >= 0 && l->ed.anims[i].sample_count > 0 ? i : -1;
}

/* NULL when the level has no Ed, no collision or no idle clip (or out of memory). */
static tc_viewer_play *play_create(tc_viewer *v, const tc_viewer_desc *desc) {
    const tc_level *l = desc->level;
    tc_viewer_play *p;
    tc_mat34 start;
    uint32_t i;
    int s;
    if (!level_has_ed(l) || ed_anim(l, "idle") < 0) return NULL;
    p = (tc_viewer_play *)tc_calloc(1, sizeof(*p));
    if (!p) return NULL;
    if (tc_collision_init(&p->world, l->collision, 3 * l->collision_triangle_count, l->collision_floors) != 0) {
        tc_free(p);
        return NULL;
    }
    tc_collision_set_sectors(&p->world, l->sectors.collision_sector, l->sectors.collision_count, l->sectors.count);
    start = l->ed.start;
    tc_player_init(&p->player, &p->world, tc_v3(start.m[3], start.m[7], start.m[11]), tc_heading_of(&start));
    tc_player_apply_floors(&p->player);
    /* Lengths of the one-shot clips the controller plays (jump, land). */
    for (i = 0; i < l->ed.anim_count; i++) {
        const tc_ed_anim *a = &l->ed.anims[i];
        if (a->sample_count == 0 || !a->name || a->loop != 0) continue;
        for (s = 0; s < TC_ANIM_COUNT; s++)
            if (strcmp(tc_anim_state_name((tc_anim_state)s), a->name) == 0)
                p->player.clip_length[s] = (float)a->sample_count / fmaxf(a->sample_rate, 1e-3f);
    }
    if (desc->level_name) {
        strncpy(p->level_name, desc->level_name, sizeof(p->level_name) - 1);
        p->level_name[sizeof(p->level_name) - 1] = 0;
    }
    if (desc->ai_data) {
        v->ai_result = tc_ai_model_load(&p->model, (const uint8_t *)desc->ai_data, desc->ai_size);
        p->has_model = v->ai_result == TC_OK;
        if (p->has_model) tc_ai_model_link(&p->model, l);
    }
    if (p->has_model && p->model.alt_data && p->model.mode_zone_count) {
        p->has_alt = tc_ai_model_load(&p->alt, p->model.alt_data, p->model.alt_size) == TC_OK;
        if (p->has_alt) tc_ai_model_link(&p->alt, l);
    }
    if (p->has_model) {
        tc_result r = tc_ed_ai_init(&p->driver, &p->model, &p->player, p->level_name);
        p->has_driver = r == TC_OK;
        if (r != TC_OK) v->ai_result = r;
        if (p->has_driver) tc_ed_ai_set_sectors(&p->driver, l->sectors.count ? &l->sectors : NULL);
        if (p->has_driver && p->has_alt) p->driver.walk.firm_floors = true;
    }
    if (p->has_driver && l->cast.count) {
        tc_result r = tc_cast_init(&p->cast, l, &p->driver.ai);
        p->has_cast = r == TC_OK;
        if (r != TC_OK) v->ai_result = r;
    }
    /* The persos' own collision (docs/ENGINE.md "Moving collision, read"): Ed's sweeps meet it where each perso is now
     * (the cast poses its actors' every tick), as his collide set says (the player's +0x34 / +0x35) */
    if (tc_obstacles_init(&p->obstacles, l) == TC_OK && p->obstacles.count) {
        p->world.obstacles = &p->obstacles;
        p->world.self_priority = l->pcol.main_priority;
        p->world.self_meets = (l->pcol.main_flags & TC_PCOL_MEETS_PERSOS) != 0;
        if (p->has_cast) tc_cast_set_obstacles(&p->cast, &p->obstacles);
    }
    if (p->has_cast) p->cast.world = &p->world;
    p->use_ai = p->has_driver;
    return p;
}

static void play_destroy(tc_viewer_play *p) {
    if (!p) return;
    if (p->has_cast) tc_cast_free(&p->cast);
    if (p->has_driver) tc_ed_ai_free(&p->driver);
    if (p->has_model) tc_ai_model_free(&p->model);
    if (p->has_alt) tc_ai_model_free(&p->alt);
    tc_collision_free(&p->world);
    tc_obstacles_free(&p->obstacles);
    tc_free(p);
}

/* ---- baked light sets ---- */

/* The level persos that set the light (tc_level.light_persos), run by the interpreter on whole milliseconds of the
 * game clock: sud0's day / night cycle, Land's lightning. */
struct tc_viewer_light {
    uint32_t count;
    tc_ai_model *models;
    tc_ai_perso *persos;
    double clock;          /* seconds they have run */
    uint64_t stepped_ms;   /* of it, stepped */
};

static void light_destroy(tc_viewer_light *l) {
    uint32_t i;
    if (!l) return;
    for (i = 0; i < l->count; i++) {
        tc_ai_free(&l->persos[i]);
        tc_ai_model_free(&l->models[i]);
    }
    tc_free(l->models);
    tc_free(l->persos);
    tc_free(l);
}

/* NULL when the level has none (or one set only); *oom on running out of memory. Persos whose scripts don't load
 * are left out. Each gets its own random stream from the viewer's. */
static tc_viewer_light *light_create(tc_viewer *v, const tc_level *l, bool *oom) {
    tc_viewer_light *lt;
    uint32_t i;
    *oom = false;
    if (!l->light_perso_count || v->light_set_count < 2) return NULL;
    lt = (tc_viewer_light *)tc_calloc(1, sizeof(*lt));
    if (lt) {
        lt->models = (tc_ai_model *)tc_calloc(l->light_perso_count, sizeof(tc_ai_model));
        lt->persos = (tc_ai_perso *)tc_calloc(l->light_perso_count, sizeof(tc_ai_perso));
    }
    if (!lt || !lt->models || !lt->persos) { light_destroy(lt); *oom = true; return NULL; }
    for (i = 0; i < l->light_perso_count; i++) {
        tc_result e = tc_ai_model_load_scripts(&lt->models[lt->count], l->light_persos[i].scripts, l->light_persos[i].size);
        if (e == TC_OK && (e = tc_ai_init(&lt->persos[lt->count], &lt->models[lt->count])) != TC_OK)
            tc_ai_model_free(&lt->models[lt->count]);
        if (e == TC_ERR_OUT_OF_MEMORY) { light_destroy(lt); *oom = true; return NULL; }
        if (e != TC_OK) continue;
        v->rng = v->rng * 1103515245u + 12345u;
        lt->persos[lt->count].rng = v->rng;
        lt->count++;
    }
    if (!lt->count) { light_destroy(lt); return NULL; }
    return lt;
}

static uint32_t light_mod(const tc_viewer *v, int32_t n) {
    int32_t c = (int32_t)v->light_set_count;
    return (uint32_t)(((n % c) + c) % c);
}

/* SetLightSet(n, duration, absolute), 0x449ad0: refused while a change runs; duration 0 switches at once (to n if
 * absolute, else n sets on); otherwise the target is n or n sets on, and the radiosity blends set by set (next =
 * current + 1) at sets to go / duration. */
static void set_light(tc_viewer *v, const tc_ai_light_call *c) {
    uint32_t target, to_go;
    if (v->light_rate > 0) return;
    target = light_mod(v, c->absolute ? c->set : (int32_t)v->light_set + c->set);
    if (c->duration_ms <= 0) {
        v->light_set = v->light_next = v->light_target = target;
        v->light_blend = 0;
        return;
    }
    to_go = (target + v->light_set_count - v->light_set) % v->light_set_count;
    if (!to_go) return;
    v->light_target = target;
    v->light_next = (v->light_set + 1) % v->light_set_count;
    v->light_blend = 0;
    v->light_rate = (float)to_go * 1000.0f / (float)c->duration_ms;
}

/* FUN_00449860's blend: on to the next set at 1, until the target. */
static void blend_light(tc_viewer *v, float dt) {
    if (v->light_rate <= 0) return;
    v->light_blend += v->light_rate * dt;
    while (v->light_blend >= 1) {
        v->light_set = v->light_next;
        v->light_blend -= 1;
        if (v->light_set == v->light_target) {
            v->light_blend = 0;
            v->light_rate = 0;
            return;
        }
        v->light_next = (v->light_next + 1) % v->light_set_count;
    }
}

static void update_light(tc_viewer *v, float dt) {
    tc_viewer_light *lt = v->light;
    if (lt && dt > 0) {
        uint64_t ms;
        uint32_t i, k;
        lt->clock += dt;
        ms = (uint64_t)(lt->clock * 1000) - lt->stepped_ms;
        if (ms > 0) {
            lt->stepped_ms += ms;
            for (i = 0; i < lt->count; i++) {
                tc_ai_perso *p = &lt->persos[i];
                tc_ai_step(p, (float)ms / 1000);
                for (k = 0; k < p->light_call_count && v->light_scripted; k++) set_light(v, &p->light_calls[k]);
            }
        }
    }
    blend_light(v, dt);
}

/* Distance to the nearest hit along the segment, or -1. */
static float raycast(tc_viewer_play *p, tc_vec3 origin, tc_vec3 dir, float max_distance) {
    tc_hit hit;
    return tc_collision_raycast(&p->world, origin, dir, max_distance, &hit) ? hit.distance : -1;
}

/* ---- portals and screen transitions ---- */

static int same_text(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static void copy_text(char *dst, size_t n, const char *src) {
    size_t i;
    for (i = 0; src && src[i] && i + 1 < n; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* The distinct effect textures of the model's transitions. */
static bool collect_effects(tc_viewer *v, const tc_ai_model *m) {
    uint32_t i, k, j, n = 0;
    for (i = 0; i < 4; i++) n += m->transitions.effects[i].variant_count;
    if (!m->has_transitions || n == 0) return true;
    v->effects = (const char **)tc_calloc(n, sizeof(*v->effects));
    if (!v->effects) return false;
    for (i = 0; i < 4; i++)
        for (k = 0; k < m->transitions.effects[i].variant_count; k++) {
            const char *t = m->transitions.effects[i].variants[k];
            for (j = 0; j < v->effect_count && strcmp(v->effects[j], t) != 0; j++) {}
            if (t && j == v->effect_count) v->effects[v->effect_count++] = t;
        }
    return true;
}

static const tc_ai_model *ai_model(const tc_viewer *v) {
    return v->play && v->play->has_driver ? v->play->driver.ai.model : NULL;
}

/* Starts the cover or the reveal; without transition data, it is done at once. */
static void start_transition(tc_viewer *v, bool reveal) {
    const tc_ai_model *m = ai_model(v);
    if (m && m->has_transitions && tc_transition_start(&v->transition, &m->transitions, reveal, &v->rng)) {
        v->phase = reveal ? TC_VP_REVEAL : TC_VP_COVER;
        return;
    }
    memset(&v->transition, 0, sizeof(v->transition));
    v->phase = reveal ? TC_VP_PLAY : TC_VP_BLACK;
    if (!reveal && v->play) v->play->driver.screen_covered = true;
}

/* The orbit camera looking at Ed from `eye` (PROC_8's camera position, PROC_29's camera place). */
static void aim_camera(tc_viewer *v, tc_vec3 eye) {
    tc_vec3 d = tc_v3_sub(tc_v3_add(v->play->player.position, tc_v3(0, 0, 1.1f)), eye);
    float h = sqrtf(d.x * d.x + d.y * d.y);
    if (h < 1e-3f) return;
    v->camera.yaw = atan2f(d.x, d.y);
    v->camera.pitch = fmaxf(-1.5f, fminf(1.5f, atan2f(d.z, h)));
}

/* Arriving through a portal: Ed's dsgvars from the last level, the arrival place, then the reveal. */
static void arrive(tc_viewer *v, const tc_viewer_carry *c) {
    tc_viewer_play *p = v->play;
    const tc_ai_model *m = ai_model(v);
    uint16_t i;
    if (!m) return;
    if (c->dsg_size && c->dsg_size == m->dsg.size && p->driver.ai.dsg) memcpy(p->driver.ai.dsg, c->dsg, c->dsg_size);
    for (i = 0; c->place[0] && i < m->place_count; i++)
        if (m->places[i].name && same_text(m->places[i].name, c->place))
            tc_player_teleport(&p->player, m->places[i].position, m->places[i].heading);
    for (i = 0; c->camera_place[0] && i < m->place_count; i++)
        if (m->places[i].name && same_text(m->places[i].name, c->camera_place)) aim_camera(v, m->places[i].position);
    start_transition(v, true);
}

/* After Ed's step: his portal events drive the phases (docs/PORTALS.md). */
static void update_portals(tc_viewer *v, float dt) {
    tc_ed_ai *e;
    tc_ed_ai_events *ev;
    if (!v->play || !v->play->has_driver) return;
    e = &v->play->driver;
    ev = &e->events;
    if (ev->cover) {
        ev->cover = false;
        start_transition(v, false);
    }
    if (v->phase == TC_VP_COVER && tc_transition_step(&v->transition, dt)) {
        v->phase = TC_VP_BLACK;
        e->screen_covered = true;
    }
    if (ev->teleported) {
        ev->teleported = false;
        if (ev->camera_set) aim_camera(v, ev->camera);
        ev->camera_set = false;
        if (v->phase == TC_VP_BLACK) { /* a teleport behind the cover: reveal; one in plain view just happens */
            e->screen_covered = false;
            start_transition(v, true);
        }
    }
    if (ev->level) {
        tc_viewer_carry *c = v->carry ? v->carry : (tc_viewer_carry *)tc_calloc(1, sizeof(*c));
        if (c) {
            const tc_ai_model *m = e->ai.model;
            const char *after = tc_level_after_cutscene(ev->level);
            /* Cutscenes don't play yet: a portal into a cutscene level goes on to the level it hands over to, Ed at
             * its start. */
            copy_text(c->level, sizeof(c->level), after ? after : ev->level);
            copy_text(c->place, sizeof(c->place), after ? NULL : ev->place);
            copy_text(c->camera_place, sizeof(c->camera_place), after ? NULL : ev->camera_place);
            c->dsg_size = m->dsg.size <= TC_VIEWER_CARRY_DSG && e->ai.dsg ? m->dsg.size : 0;
            if (c->dsg_size) memcpy(c->dsg, e->ai.dsg, c->dsg_size);
            c->vertical_fov = v->camera.vertical_fov;
            c->orbit_distance = v->orbit_distance;
            c->closeup = v->cam.closeup;
            v->carry = c;
            v->phase = TC_VP_LOADING;
            v->loading_time = 0;
        }
        ev->level = ev->place = ev->camera_place = NULL;
    }
    if (v->phase == TC_VP_LOADING && (v->loading_time += dt) >= TC_VIEWER_LOADING_SECONDS) v->phase = TC_VP_PENDING;
    if (v->phase == TC_VP_REVEAL && tc_transition_step(&v->transition, dt)) v->phase = TC_VP_PLAY;
}

uint32_t tc_viewer_effect_count(const tc_viewer *v) { return v->effect_count; }

const char *tc_viewer_effect_texture(const tc_viewer *v, uint32_t i) { return i < v->effect_count ? v->effects[i] : NULL; }

const tc_viewer_carry *tc_viewer_pending_level(const tc_viewer *v) { return v->phase == TC_VP_PENDING ? v->carry : NULL; }

void tc_viewer_overlay(const tc_viewer *v, tc_view_overlay *out) {
    const tc_transition *t = &v->transition;
    memset(out, 0, sizeof(*out));
    out->texture = -1;
    if (v->picture_left > 0 && v->phase == TC_VP_PLAY) {
        out->kind = TC_VO_PICTURE;
        out->picture = v->picture;
        return;
    }
    switch (v->phase) {
    case TC_VP_BLACK: out->kind = TC_VO_BLACK; return;
    case TC_VP_LOADING: case TC_VP_PENDING:
        out->kind = TC_VO_LOADING;
        out->progress = fminf(1, v->loading_time / TC_VIEWER_LOADING_SECONDS);
        out->level = v->carry ? v->carry->level : NULL;
        return;
    case TC_VP_COVER: case TC_VP_REVEAL: {
        const char *name = t->effect && t->variant < t->effect->variant_count ? t->effect->variants[t->variant] : NULL;
        uint32_t i;
        for (i = 0; name && i < v->effect_count; i++)
            if (strcmp(v->effects[i], name) == 0) out->texture = (int32_t)i;
        if (t->logo) {
            out->kind = TC_VO_LOGO;
            tc_transition_logo_quad(t, out->corners, out->uv);
        } else {
            out->kind = TC_VO_MASK;
            out->threshold = t->value;
        }
        return;
    }
    default: return;
    }
}

/* ---- setup ---- */

static bool drawable(const tc_mesh *m) { return m->index_count > 0 && m->vertex_count > 0 && m->vertices && m->indices; }

/* ---- sector ranges (tc_viewer_draws draws only the shown sectors' part of a static mesh) ---- */

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

/* ---- transparent pieces (tc_viewer_draws sorts blended draws back to front) ---- */

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
static bool split_transparent(tc_viewer *v, uint32_t **piece_ranges) {
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
static bool mesh_centres(tc_viewer *v) {
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
static bool mesh_ranges(tc_viewer *v, const uint32_t *piece_ranges) {
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

/* ---- lighting (TC_VL_GAME) ---- */

/* The original's vertex light for a stored baked colour channel (TC_RADIOSITY) without lights. */
static float formula_light(uint32_t byte, float ambient, float diffuse) {
    return fminf(1, fmaxf(0, (TC_RADIOSITY(byte) / 256 * diffuse + ambient) * (512.0f / 255)));
}

/* Per baked set, the mean light of the sky's vertices (TC_SKY_ALPHA) under the formula: how lit the sky is, for the
 * water's sheen (sud0's day / night). 1 without a sky. */
static void measure_sky(tc_viewer *v) {
    const tc_level *l = v->level;
    double sum[3][3] = {{0}};
    uint32_t i, k, n = 0, set, ch;
    for (i = 0; i < l->mesh_count; i++) {
        const tc_mesh *m = &l->meshes[i];
        for (k = 0; k < m->vertex_count; k++) {
            uint32_t a = m->vertices[k].color[0] >> 24;
            if (a < 64 || a > 192) continue; /* not a sky's (TC_SKY_ALPHA) */
            n++;
            for (set = 0; set < 3; set++) {
                uint32_t c = m->vertices[k].color[set];
                if ((c >> 24) < 64) c = m->vertices[k].color[0]; /* the shaders' fallback to set 0 */
                for (ch = 0; ch < 3; ch++)
                    sum[set][ch] += formula_light((c >> (8 * ch)) & 0xFF, m->surface.ambient[ch], m->surface.diffuse[ch]);
            }
        }
    }
    for (set = 0; set < 3; set++)
        for (ch = 0; ch < 3; ch++) v->sky_light[set][ch] = n ? (float)(sum[set][ch] / n) : 1;
}

/* Ed's sector's lights and its fog at the camera (FUN_00451d80 pushes the lights of the sector the frame is drawn from;
 * FUN_00451e80 sets the fog of its env at the camera, FUN_00451c40), faded over TC_VIEW_LIGHT_FADE where the original
 * switches at once (the user's choice, 2026-10-30). */
/* The camera's sector (TC_VIEW_LIGHT_FADE's note): from Ed's head to the camera through the portals of his. */
static uint16_t camera_sector(const tc_viewer *v) {
    uint16_t s = tc_viewer_ed_sector(v);
    if (s == TC_SECTOR_NONE || !v->play) return s;
    return tc_sectors_cross(v->sectors, s, tc_v3_add(v->play->player.position, tc_v3(0, 0, 1.1f)), v->camera.position);
}

static void update_lighting(tc_viewer *v, float dt) {
    const tc_level *l = v->level;
    uint16_t s = v->light_sector = camera_sector(v), k;
    const tc_sector *sec = v->sectors && s < v->sectors->count ? &v->sectors->sectors[s] : NULL;
    float step = v->lights_started ? dt / TC_VIEW_LIGHT_FADE : 1, ease;
    const tc_sector_env *env = NULL;
    tc_sector_env level_fog;
    uint32_t i;
    if (!l->has_lighting) return;
    if (v->light_wanted) {
        memset(v->light_wanted, 0, l->light_count);
        for (k = 0; sec && k < sec->light_count; k++) v->light_wanted[sec->lights[k]] = 1;
        for (i = 0; i < l->light_count; i++) {
            float w = v->light_instant[i] ? (v->light_wanted[i] ? 1.0f : 0.0f)
                                          : v->light_weight[i] + (v->light_wanted[i] ? step : -step);
            v->light_weight[i] = w < 0 ? 0 : w > 1 ? 1 : w;
        }
    }
    k = sec ? tc_sectors_env_index(v->sectors, s, v->camera.position, NULL) : TC_SECTOR_NONE;
    if (k != TC_SECTOR_NONE && sec->envs[k].fog && sec->envs[k].fog_far > sec->envs[k].fog_near) env = &sec->envs[k];
    if (!sec && v->has_fog) { /* no sector known (or none in the level): the level's fog */
        level_fog.fog = 1;
        level_fog.fog_near = l->fog_near;
        level_fog.fog_far = l->fog_far;
        memcpy(level_fog.fog_rgb, l->fog_rgb, sizeof(level_fog.fog_rgb));
        env = &level_fog;
    }
    if (env && v->sector_fog_strength <= 0) { /* fading in from none: its own values */
        v->sector_fog_near = env->fog_near;
        v->sector_fog_far = env->fog_far;
        memcpy(v->sector_fog_color, env->fog_rgb, sizeof(v->sector_fog_color));
    } else if (env) {
        ease = step >= 1 ? 1 : 1 - expf(-3 * step); /* most of the way within the fade */
        v->sector_fog_near += (env->fog_near - v->sector_fog_near) * ease;
        v->sector_fog_far += (env->fog_far - v->sector_fog_far) * ease;
        for (i = 0; i < 3; i++) v->sector_fog_color[i] += (env->fog_rgb[i] - v->sector_fog_color[i]) * ease;
    }
    v->sector_fog_strength += env ? step : -step;
    v->sector_fog_strength = v->sector_fog_strength < 0 ? 0 : v->sector_fog_strength > 1 ? 1 : v->sector_fog_strength;
    v->lights_started = true;
}

tc_result tc_viewer_init(tc_viewer *v, const tc_viewer_desc *desc) {
    const tc_level *l = desc->level;
    uint32_t i, k, n = 0, *piece_ranges = NULL;
    tc_vec3 lo, hi;
    float size;
    memset(v, 0, sizeof(*v));
    v->level = l;
    v->ai_result = TC_OK;
    v->animation_speed = 1;
    v->gamma = 1;
    v->fog_enabled = true;
    v->fly_speed = 10;
    v->turn_speed = 1.6f;
    v->look_sensitivity = 1;
    v->orbit_distance = TC_CAM_DISTANCE;
    tc_cam_reset(&v->cam);
    v->ed_clip = -1;
    v->ed_state = -1;
    tc_camera_init(&v->camera);

    v->has_fog = l->fog_far > l->fog_near;
    if (v->has_fog) {
        v->fog_near = l->fog_near;
        v->fog_far = l->fog_far;
        memcpy(v->fog_color, l->fog_rgb, sizeof(v->fog_color));
    }
    v->light_set_count = l->light_set_count > 1 ? l->light_set_count : 1;
    /* The Special Edition's levels start in the port's lighting: its original data shows issues under the game's
     * formula, probably engine changes before release (docs/SE.md). The toggle still offers it. */
    v->lighting = l->has_lighting && l->edition != TC_EDITION_SPECIAL ? TC_VL_GAME : TC_VL_PORT;
    if (l->light_count) {
        v->light_weight = (float *)tc_calloc(l->light_count, sizeof(float));
        v->light_wanted = (uint8_t *)tc_calloc(l->light_count, 1);
        v->light_instant = (uint8_t *)tc_calloc(l->light_count, 1);
        if (!v->light_weight || !v->light_wanted || !v->light_instant) {
            tc_viewer_free(v);
            return TC_ERR_OUT_OF_MEMORY;
        }
        for (i = 0; i < l->light_count; i++)
            for (k = 0; k < 3; k++)
                if (l->lights[i].color[k] < 0 || l->lights[i].color[k] > 1) v->light_instant[i] = 1;
    }
    v->light_sector = TC_SECTOR_NONE;
    measure_sky(v);
    for (i = 0; i < l->sectors.count; i++)
        for (k = 0; k < l->sectors.sectors[i].env_count; k++) {
            const tc_sector_env *e = &l->sectors.sectors[i].envs[k];
            v->has_sector_fog |= e->fog && e->fog_far > e->fog_near && e->fog_near < 10000;
        }

    v->sectors = l->sectors.count ? &l->sectors : NULL;
    v->sector_mode = TC_VS_SECTOR; /* the user's default (2026-10-30): Ed's sector and its list; K cycles */
    v->ed_sector.sector = TC_SECTOR_NONE;
    if (v->sectors) {
        v->sector_shown = (uint8_t *)tc_calloc(v->sectors->count, 1);
        v->sector_water = (float *)tc_calloc(v->sectors->count, sizeof(float));
        if (!v->sector_shown || !v->sector_water) {
            tc_viewer_free(v);
            return TC_ERR_OUT_OF_MEMORY;
        }
    }
    if (!split_transparent(v, &piece_ranges)) {
        tc_free(piece_ranges);
        tc_viewer_free(v);
        return TC_ERR_OUT_OF_MEMORY;
    }
    v->mesh_count = l->mesh_count;
    for (i = 0; i < l->perso_count; i++) v->mesh_count += l->persos[i].part_count;
    if (l->has_ed) v->mesh_count += l->ed.part_count;
    v->mesh_count += v->piece_count;
    if (v->mesh_count) {
        v->meshes = (const tc_mesh **)tc_calloc(v->mesh_count, sizeof(*v->meshes));
        if (!v->meshes) {
            tc_free(piece_ranges);
            tc_viewer_free(v);
            return TC_ERR_OUT_OF_MEMORY;
        }
    }
    for (i = 0; i < l->mesh_count; i++) {
        v->meshes[n++] = &l->meshes[i];
        if (!drawable(&l->meshes[i])) continue;
        v->stats.meshes++;
        v->stats.triangles += l->meshes[i].index_count / 3;
    }
    v->perso_first = n;
    for (i = 0; i < l->perso_count; i++) {
        const tc_perso *p = &l->persos[i];
        uint32_t parts = 0;
        for (k = 0; k < p->part_count; k++) {
            v->meshes[n++] = &p->parts[k].mesh;
            if (p->sample_count == 0 || !drawable(&p->parts[k].mesh)) continue;
            parts++;
            v->stats.triangles += p->parts[k].mesh.index_count / 3;
        }
        if (parts == 0) continue;
        v->stats.persos++;
        v->stats.parts += parts;
    }
    v->ed_first = n;
    if (l->has_ed)
        for (i = 0; i < l->ed.part_count; i++) v->meshes[n++] = &l->ed.parts[i].mesh;
    v->piece_first = n;
    for (i = 0; i < v->piece_count; i++) v->meshes[n++] = &v->pieces[i];
    {
        bool ok = mesh_centres(v) && mesh_ranges(v, piece_ranges);
        tc_free(piece_ranges);
        if (!ok) {
            tc_viewer_free(v);
            return TC_ERR_OUT_OF_MEMORY;
        }
    }

    v->play = play_create(v, desc);
    if (v->sectors && v->mesh_ranges && l->mesh_count) {
        /* Water materials: static meshes (the .ttlv groups them by material) holding some of a water surface's
         * triangles (a range of a water group: the geometry the sectors list as their surfaces). The renderers add the
         * sheen to their upward-facing fragments only (the falls and pool sides share the rainbow material). */
        v->mesh_water = (uint8_t *)tc_calloc(l->mesh_count, 1);
        for (i = 0; v->mesh_water && i < l->mesh_count; i++)
            for (k = 0; k < v->mesh_ranges[2 * i + 1]; k++) {
                uint16_t g = v->ranges[v->mesh_ranges[2 * i] + k].group;
                if (g < v->sectors->group_count && (v->sectors->groups[g].flags & TC_GROUP_WATER)) v->mesh_water[i] = 1;
            }
    }
    v->ed_clip = ed_anim(l, "idle");
    v->rng = desc->seed ? desc->seed : 0x7417u;
    {
        bool oom;
        v->light = light_create(v, l, &oom);
        v->light_scripted = v->light != NULL;
        if (oom) {
            tc_viewer_free(v);
            return TC_ERR_OUT_OF_MEMORY;
        }
    }
    if (ai_model(v) && !collect_effects(v, ai_model(v))) {
        tc_viewer_free(v);
        return TC_ERR_OUT_OF_MEMORY;
    }

    tc_level_bounds(l, &lo, &hi);
    size = tc_v3_length(tc_v3_sub(hi, lo));
    /* Start in the middle: levels are rooms or sit inside a sky dome, so outside the bounds sees nothing. */
    v->camera.position = tc_v3_scale(tc_v3_add(lo, hi), 0.5f);
    v->camera.yaw = 0;
    v->camera.pitch = -0.2f;
    v->camera.far_z = fmaxf(2000, size * 3);
    v->fly_speed = fmaxf(2, size / 40);
    /* Start as Ed when the level has him (Tab still switches to free-fly). */
    if (v->play) tc_viewer_toggle_play(v);
    if (desc->carry && desc->carry->vertical_fov > 0) v->camera.vertical_fov = desc->carry->vertical_fov;
    if (desc->carry && desc->carry->orbit_distance > 0) v->orbit_distance = desc->carry->orbit_distance;
    if (desc->carry) v->cam.closeup = desc->carry->closeup != 0;
    if (desc->carry && v->play) arrive(v, desc->carry);
    return TC_OK;
}

void tc_viewer_free(tc_viewer *v) {
    uint32_t i;
    play_destroy(v->play);
    light_destroy(v->light);
    tc_free((void *)v->effects);
    tc_free(v->carry);
    tc_free((void *)v->meshes);
    tc_free(v->mesh_water);
    tc_free(v->draws);
    tc_free(v->mesh_sphere);
    tc_free(v->blended);
    for (i = 0; i < v->piece_count; i++) {
        tc_free(v->pieces[i].vertices);
        tc_free(v->pieces[i].indices);
    }
    tc_free(v->pieces);
    tc_free(v->piece_origin);
    tc_free(v->split);
    tc_free(v->mesh_centre);
    tc_free(v->mesh_extent);
    tc_free(v->blended_depth);
    tc_free(v->sector_shown);
    tc_free(v->sector_water);
    tc_free(v->mesh_ranges);
    tc_free(v->ranges);
    tc_free(v->light_weight);
    tc_free(v->light_wanted);
    tc_free(v->light_instant);
    memset(v, 0, sizeof(*v));
}

uint32_t tc_viewer_mesh_count(const tc_viewer *v) { return v->mesh_count; }

const tc_mesh *tc_viewer_mesh(const tc_viewer *v, uint32_t i) { return i < v->mesh_count ? v->meshes[i] : NULL; }

/* ---- input ---- */

void tc_viewer_set_action(tc_viewer *v, tc_view_action a, bool down) {
    if ((unsigned)a >= TC_VA_COUNT) return;
    if (down) v->held |= 1u << a;
    else v->held &= ~(1u << a);
}

void tc_viewer_release_all(tc_viewer *v) {
    v->held = 0;
    v->move_x = v->move_y = v->look_x = v->look_y = 0;
}

void tc_viewer_mouse_look(tc_viewer *v, float dx, float dy) {
    v->mouse_dx += dx;
    v->mouse_dy += dy;
}

/* Radial dead zone; rescale: the rest maps to 0..1 (a slight push starts slowly), else it stays as is (length <= 1). */
static void dead_zone(float x, float y, bool rescale, float *ox, float *oy) {
    float len = sqrtf(x * x + y * y), k;
    if (len <= TC_VIEWER_STICK_DEAD_ZONE) {
        *ox = *oy = 0;
        return;
    }
    k = rescale ? fminf(1, (len - TC_VIEWER_STICK_DEAD_ZONE) / (1 - TC_VIEWER_STICK_DEAD_ZONE)) / len : 1 / fmaxf(len, 1);
    *ox = x * k;
    *oy = y * k;
}

void tc_viewer_set_move_stick(tc_viewer *v, float x, float y) { dead_zone(x, y, false, &v->move_x, &v->move_y); }

void tc_viewer_set_look_stick(tc_viewer *v, float x, float y) { dead_zone(x, y, true, &v->look_x, &v->look_y); }

static bool held(const tc_viewer *v, tc_view_action a) { return (v->held >> a) & 1u; }

static float axis(const tc_viewer *v, tc_view_action pos, tc_view_action neg) {
    return (held(v, pos) ? 1.0f : 0.0f) - (held(v, neg) ? 1.0f : 0.0f);
}

/* Arrow keys, the right stick and the mouse turn the camera (both modes). */
/* The look keys, the right stick and the mouse turn the view, x look_sensitivity, reversed per axis as set. In play mode
 * they turn the play camera round Ed and raise or lower it (tc_cam_look); not during the demo (its own camera). */
static void look(tc_viewer *v, float dt) {
    tc_camera *c = &v->camera;
    float yaw_rate = fmaxf(-1, fminf(1, axis(v, TC_VA_LOOK_RIGHT, TC_VA_LOOK_LEFT) + v->look_x));
    float pitch_rate = fmaxf(-1, fminf(1, axis(v, TC_VA_LOOK_UP, TC_VA_LOOK_DOWN) + v->look_y));
    float yaw = (yaw_rate * v->turn_speed * dt + v->mouse_dx * 0.003f) * v->look_sensitivity;
    float pitch = (pitch_rate * v->turn_speed * dt - v->mouse_dy * 0.003f) * v->look_sensitivity;
    v->mouse_dx = v->mouse_dy = 0;
    if (yaw == 0 && pitch == 0) return;
    if (v->invert_x) yaw = -yaw;
    if (v->invert_y) pitch = -pitch;
    if (v->playing && v->play) {
        if (!v->demo) tc_cam_look(&v->cam, c, yaw, -pitch); /* looking up: the camera lower, looking up at him */
        return;
    }
    c->yaw += yaw;
    c->pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT, c->pitch + pitch));
}

/* Free-fly: W/S (the stick's y) fly along the view direction (pitch included); R/F stay world up/down. Keys fly at
 * full speed; the stick in proportion to how far it is pushed. */
static void fly(tc_viewer *v, float dt) {
    tc_camera *c = &v->camera;
    tc_vec3 move;
    float len;
    look(v, dt);
    move = tc_v3_add(tc_v3_scale(tc_camera_forward(c), axis(v, TC_VA_FORWARD, TC_VA_BACK) + v->move_y),
                     tc_v3_scale(tc_camera_right(c), axis(v, TC_VA_RIGHT, TC_VA_LEFT) + v->move_x));
    move.z += axis(v, TC_VA_UP, TC_VA_DOWN);
    len = tc_v3_length(move);
    if (len > 0) {
        float speed = v->fly_speed * (held(v, TC_VA_FAST) ? 4.0f : 1.0f) * dt;
        c->position = tc_v3_add(c->position, tc_v3_scale(move, speed / fmaxf(len, 1)));
    }
}

/* Simple controller: WASD and the left stick as a world-space XY direction relative to the camera's yaw (length <= 1). */
static tc_vec2 move_direction(const tc_viewer *v) {
    float yaw = v->camera.yaw, fwd = axis(v, TC_VA_FORWARD, TC_VA_BACK) + v->move_y;
    float side = axis(v, TC_VA_RIGHT, TC_VA_LEFT) + v->move_x;
    tc_vec2 m = tc_v2(sinf(yaw) * fwd + cosf(yaw) * side, cosf(yaw) * fwd - sinf(yaw) * side);
    float len = tc_v2_length(m);
    return len > 1 ? tc_v2(m.x / len, m.y / len) : m;
}

/* Ed's AI: the game's own inputs (1 << TC_AI_IN_*). The direction keys (the scripts make them camera-relative),
 * Fast = SpeedUp (on a keyboard Ed runs; SpeedUp makes him walk), Jump, Action (stick), Shoot, Lateral. */
static uint32_t ai_inputs(const tc_viewer *v) {
    static const struct { tc_view_action a; int bit; } map[] = {
        {TC_VA_FORWARD, TC_AI_IN_UP}, {TC_VA_BACK, TC_AI_IN_DOWN}, {TC_VA_LEFT, TC_AI_IN_LEFT},
        {TC_VA_RIGHT, TC_AI_IN_RIGHT}, {TC_VA_FAST, TC_AI_IN_SPEEDUP}, {TC_VA_JUMP, TC_AI_IN_JUMP},
        {TC_VA_ACTION, TC_AI_IN_ACTION}, {TC_VA_SHOOT, TC_AI_IN_SHOOT}, {TC_VA_LATERAL, TC_AI_IN_LATERAL},
    };
    uint32_t r = 0;
    size_t i;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (held(v, map[i].a)) r |= 1u << map[i].bit;
    if (v->cam.first_person) r &= (1u << TC_AI_IN_ACTION) | (1u << TC_AI_IN_SHOOT); /* he stands while looking */
    return r;
}

/* ---- play mode ---- */

void tc_viewer_toggle_play(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    if (!p) return;
    v->playing = !v->playing;
    if (!v->playing) {
        v->has_exit_camera = true;
        v->exit_camera = v->camera.position;
        return;
    }
    if (v->has_exit_camera && tc_v3_distance(v->exit_camera, v->camera.position) > 1) {
        tc_vec3 o = v->camera.position;
        float d = raycast(p, o, tc_v3(0, 0, -1), 2000);
        tc_vec3 ground = d >= 0 ? tc_v3_sub(o, tc_v3(0, 0, d)) : o;
        tc_player_teleport(&p->player, tc_v3_add(ground, tc_v3(0, 0, 0.3f)), TC_PI - v->camera.yaw);
    }
    v->camera.yaw = TC_PI - p->player.heading;  /* camera forward (sin yaw, cos yaw) = Ed's front (sin h, -cos h) */
    v->camera.pitch = -TC_CAM_ELEVATION;
    tc_cam_reset(&v->cam);  /* following him, placed at once */
}

void tc_viewer_toggle_ai(tc_viewer *v) {
    if (!tc_viewer_has_ai(v)) return;
    v->play->use_ai = !v->play->use_ai;
    v->play->have_pose = false; /* the tick blending starts over from where Ed is */
    v->play->tick_time = 0;
    v->ed_state = -1;
    v->ed_phase = 0;
}

/* ---- demo ---- */

/* What the autopilot is doing (tc_viewer.demo_act). */
enum { DEMO_RUN, DEMO_WALK, DEMO_PAUSE, DEMO_HOP, DEMO_LEAP, DEMO_POGO, DEMO_SWING, DEMO_SHOOT, DEMO_SIDESTEP };
#define DEMO_BUTTONS ((1u << TC_VA_JUMP) | (1u << TC_VA_ACTION) | (1u << TC_VA_SHOOT) | (1u << TC_VA_LATERAL))
#define DEMO_DROP 4.0f /* the least drop below Ed he goes for (units) */

static float demo_random(tc_viewer *v) {
    v->rng = v->rng * 1103515245u + 12345u;
    return (float)((v->rng >> 8) & 0xFFFFu) / 65536.0f;
}

static float demo_between(tc_viewer *v, float lo, float hi) { return lo + (hi - lo) * demo_random(v); }

static float wrap_angle(float a) {
    a = fmodf(a + TC_PI, 2 * TC_PI);
    return (a < 0 ? a + 2 * TC_PI : a) - TC_PI;
}

static tc_vec3 demo_dir(float a) { return tc_v3(sinf(a), cosf(a), 0); }

/* Free way from Ed's chest along a, up to max. */
static float demo_free(tc_viewer *v, float a, float max) {
    tc_viewer_play *p = v->play;
    float d = raycast(p, tc_v3_add(p->player.position, tc_v3(0, 0, 1.0f)), demo_dir(a), max);
    return d < 0 ? max : d;
}

/* How far the ground at dist along a lies below Ed's feet; -1 when the way there is blocked or there is no ground
 * within 80 (the level's edge: not worth jumping into). */
static float demo_drop(tc_viewer *v, float a, float dist) {
    tc_viewer_play *p = v->play;
    tc_vec3 chest = tc_v3_add(p->player.position, tc_v3(0, 0, 1.2f));
    float d;
    if (raycast(p, chest, demo_dir(a), dist) >= 0) return -1;
    d = raycast(p, tc_v3_add(chest, tc_v3_scale(demo_dir(a), dist)), tc_v3(0, 0, -1), 80);
    return d < 0 ? -1 : d - 1.2f;
}

/* A drop of DEMO_DROP or more round Ed, up to 12 units away where the way is clear: its depth and way (a), nearer and
 * deeper ones first; -1 when there is none. */
static float demo_find_drop(tc_viewer *v, float *a) {
    float best = -1, best_score = -1e9f;
    int k, n;
    for (k = 0; k < 12; k++) {
        float b = v->demo_aim + (float)k * (TC_PI / 6), free = demo_free(v, b, 12.5f);
        for (n = 1; n <= 3 && free > 4.0f * (float)n; n++) {
            float d = demo_drop(v, b, 4.0f * (float)n);
            if (d >= DEMO_DROP && fminf(d, 20) - 0.8f * 4.0f * (float)n > best_score) {
                best = d, best_score = fminf(d, 20) - 0.8f * 4.0f * (float)n, *a = b;
                break; /* the nearest edge that way */
            }
        }
    }
    return best;
}

/* The most open way round Ed, small turns from the aim preferred (either side first at random). */
static float demo_open_way(tc_viewer *v) {
    float best = v->demo_aim + TC_PI, best_score = -1e9f, sign = demo_random(v) < 0.5f ? -1.0f : 1.0f;
    int k;
    for (k = 1; k <= 7; k++) {
        float off = sign * (k & 1 ? 1.0f : -1.0f) * (float)((k + 1) / 2) * (TC_PI / 4);
        float score = demo_free(v, v->demo_aim + off, 10) - fabsf(off) * 0.8f;
        if (score > best_score) best_score = score, best = v->demo_aim + off;
    }
    return wrap_angle(best);
}

static void demo_start(tc_viewer *v, int act) {
    v->demo_act = act;
    v->demo_stage = 0;
    v->demo_act_time = 0;
    v->demo_mark = 0;
    v->demo_pace = 1;
    switch (act) {
    case DEMO_RUN: v->demo_act_length = demo_between(v, 3, 8); break;
    case DEMO_WALK: v->demo_act_length = demo_between(v, 2, 4); v->demo_pace = 0.45f; break;
    case DEMO_PAUSE:
        v->demo_act_length = demo_between(v, 1.2f, 2.8f);
        v->demo_pace = 0;
        v->demo_glance = (demo_random(v) < 0.5f ? -1 : 1) * demo_between(v, 0.5f, 1.3f);
        v->demo_glance_time = v->demo_act_length;
        break;
    case DEMO_HOP:
        v->demo_act_length = 1.5f;
        v->demo_mark = demo_between(v, 0.15f, 0.4f); /* the jump held this long: higher */
        v->demo_flap = demo_random(v) < 0.45f ? 1 : 2; /* often a double jump: a try at flying */
        v->demo_flap_after = 0.35f;
        break;
    case DEMO_LEAP: v->demo_act_length = 4; break; /* to reach the edge; then as long as he is in the air */
    case DEMO_POGO: v->demo_act_length = demo_between(v, 4, 7); v->demo_flap = 2; break;
    case DEMO_SWING: case DEMO_SHOOT: v->demo_act_length = 0.9f; break;
    case DEMO_SIDESTEP:
        v->demo_act_length = demo_between(v, 0.8f, 1.4f);
        v->demo_side = demo_random(v) < 0.5f ? -1 : 1;
        break;
    default: break;
    }
}

/* What next: a drop in sight first, now and then; else a random act (fewer on skis, in the water or without his AI). */
static void demo_pick(tc_viewer *v) {
    static const struct { int act, weight; } acts[] = {
        {DEMO_RUN, 30}, {DEMO_WALK, 8}, {DEMO_PAUSE, 8}, {DEMO_HOP, 16}, {DEMO_POGO, 12}, {DEMO_SWING, 8},
        {DEMO_SHOOT, 6}, {DEMO_SIDESTEP, 6},
    };
    tc_viewer_play *p = v->play;
    uint32_t card = p->has_driver ? p->driver.card_type : 0;
    bool plain = !p->has_driver || card == 1 || card == 2; /* simple controller, swimming, skiing */
    float r, total = 0, best, best_a = 0;
    size_t i;
    if (!p->player.grounded) { demo_start(v, DEMO_RUN); return; } /* in the air: carry on */
    if (!plain && v->demo_leap_wait <= 0) {
        best = demo_find_drop(v, &best_a);
        if (best >= DEMO_DROP && demo_random(v) < 0.75f) {
            v->demo_aim = wrap_angle(best_a);
            demo_start(v, DEMO_LEAP);
            v->demo_flap = demo_random(v) < (best >= 6 ? 0.85f : 0.4f) ? 1 : 2; /* high: he tries to fly */
            v->demo_flap_after = 0.45f;
            return;
        }
    }
    if (plain) { demo_start(v, demo_random(v) < 0.2f ? DEMO_HOP : demo_random(v) < 0.15f ? DEMO_WALK : DEMO_RUN); return; }
    for (i = 0; i < sizeof(acts) / sizeof(acts[0]); i++) total += (float)acts[i].weight;
    r = demo_random(v) * total;
    for (i = 0; i < sizeof(acts) / sizeof(acts[0]) - 1 && r >= (float)acts[i].weight; i++) r -= (float)acts[i].weight;
    demo_start(v, acts[i].act);
    if (acts[i].act == DEMO_RUN && demo_random(v) < 0.15f) { /* a look aside on the way */
        v->demo_glance = (demo_random(v) < 0.5f ? -1 : 1) * demo_between(v, 0.4f, 0.9f);
        v->demo_glance_time = demo_between(v, 1.5f, 2.5f);
    }
}

void tc_viewer_set_demo(tc_viewer *v, bool on, uint32_t seed) {
    v->demo = on && v->play != NULL;
    if (seed) v->rng = seed;
    v->move_x = v->move_y = v->look_x = v->look_y = 0;
    v->held &= ~DEMO_BUTTONS;
    tc_cam_reset(&v->cam); /* its own camera while on; following Ed after it */
    if (!v->demo) return;
    if (!v->playing) tc_viewer_toggle_play(v);
    v->demo_placed = v->play->player.placed - 1; /* the first step starts it */
}

bool tc_viewer_demo(const tc_viewer *v) { return v->demo; }

/* One frame of the autopilot (tc_viewer_set_demo): the stick and buttons as a player would, and the camera behind Ed. */
static void demo_step(tc_viewer *v, float dt) {
    tc_viewer_play *p = v->play;
    tc_player *pl = &p->player;
    uint32_t card = p->has_driver ? p->driver.card_type : 0, buttons = 0;
    bool ground = pl->grounded, flying = card == 5, footing = ground || card == 1, wander; /* footing: swimming too */
    float fwd = 0, side = 0, rel, face, pitch, t, k;
    if (pl->placed != v->demo_placed) { /* started, respawned or moved: start over from where he faces */
        v->demo_placed = pl->placed;
        v->demo_aim = v->demo_facing = v->camera.yaw = wrap_angle(TC_PI - pl->heading);
        v->demo_turn = 0;
        v->demo_next_turn = 2;
        v->demo_probe = v->demo_check = 0.75f;
        v->demo_last = pl->position;
        v->demo_glance_time = 0;
        v->demo_leap_wait = 3;
        v->demo_air = 0;
        v->demo_flap = 0;
        v->demo_jump_free = 1;
        demo_start(v, DEMO_RUN);
        v->demo_act_length = 2;
    }
    v->demo_act_time += dt;
    t = v->demo_act_time;
    v->demo_leap_wait -= dt;
    v->demo_glance_time -= dt;
    if (!ground) v->demo_air += dt;
    else { /* on the ground: a new fall decides again, unless this act decided already */
        v->demo_air = 0;
        if (v->demo_act != DEMO_HOP && v->demo_act != DEMO_LEAP && v->demo_act != DEMO_POGO) v->demo_flap = 0;
    }
    if ((v->demo_next_turn -= dt) <= 0) { /* the aim's drift: straight on about as often as a curve */
        v->demo_next_turn = demo_between(v, 2, 5);
        v->demo_turn = demo_random(v) < 0.4f ? 0 : demo_between(v, -0.5f, 0.5f);
    }

    wander = v->demo_act == DEMO_RUN || v->demo_act == DEMO_WALK || v->demo_act == DEMO_POGO;
    switch (v->demo_act) {
    case DEMO_RUN: case DEMO_WALK:
        v->demo_aim += v->demo_turn * dt;
        fwd = v->demo_pace;
        break;
    case DEMO_PAUSE: break;
    case DEMO_HOP:
        fwd = 1;
        if (t < v->demo_mark) buttons |= 1u << TC_VA_JUMP;
        if (t > 0.5f && ground) t = v->demo_act_time = v->demo_act_length; /* landed */
        break;
    case DEMO_LEAP:
        fwd = 1;
        if (v->demo_stage == 0) { /* running at the drop: jump at its edge (or off it, running) */
            float drop = demo_drop(v, v->demo_aim, 1.0f);
            if (!ground) v->demo_stage = 2;
            else if (drop < 0 || drop > 2) v->demo_stage = 1, v->demo_mark = t;
        }
        if (v->demo_stage == 1) {
            if (t - v->demo_mark < 0.3f) buttons |= 1u << TC_VA_JUMP;
            if (!ground) v->demo_stage = 2;
            else if (t - v->demo_mark > 1) v->demo_act_length = 0; /* the jump didn't leave the ground */
        }
        if (v->demo_stage == 2) {
            v->demo_act_length = t + 1; /* as long as he is in the air */
            if (ground) v->demo_act_length = 0, v->demo_leap_wait = demo_between(v, 5, 12);
            else if (t > 25) v->demo_act_length = 0;
        }
        break;
    case DEMO_POGO: /* jump, Action in the air (B89 -> B23: on the pogo, B92), bounce on forward; Action again gets off
                     * (B92's ButtonJustPressed(action) -> B27) */
        if (t < 0.3f) buttons |= 1u << TC_VA_JUMP;
        if (t > 0.55f && t < 0.85f) buttons |= 1u << TC_VA_ACTION;
        if (t > 1.2f) { v->demo_aim += v->demo_turn * dt; fwd = 0.8f; }
        if (t > v->demo_act_length - 0.5f && t < v->demo_act_length - 0.2f) buttons |= 1u << TC_VA_ACTION;
        break;
    case DEMO_SWING: case DEMO_SHOOT:
        fwd = 0.7f;
        if (t > 0.1f && t < 0.3f) buttons |= 1u << (v->demo_act == DEMO_SWING ? TC_VA_ACTION : TC_VA_SHOOT);
        break;
    case DEMO_SIDESTEP:
        buttons |= 1u << TC_VA_LATERAL;
        side = 0.9f * v->demo_side;
        break;
    default: break;
    }

    if (wander && footing && (v->demo_probe -= dt) <= 0) { /* look ahead: a drop to go for, or a wall to turn off before */
        float drop = demo_drop(v, v->demo_aim, 2.5f), a = v->demo_aim;
        bool leaps = v->demo_act != DEMO_POGO && v->demo_leap_wait <= 0 && p->has_driver && card == 0;
        v->demo_probe = 0.3f;
        if (leaps && drop < DEMO_DROP && demo_random(v) < 0.3f) drop = demo_find_drop(v, &a); /* now and then all round */
        if (leaps && drop >= DEMO_DROP && demo_random(v) < 0.6f) {
            v->demo_aim = wrap_angle(a);
            demo_start(v, DEMO_LEAP);
            v->demo_flap = demo_random(v) < (drop >= 6 ? 0.85f : 0.4f) ? 1 : 2;
            v->demo_flap_after = 0.45f;
        } else if (demo_free(v, v->demo_aim, 3) < 2.5f || (drop >= DEMO_DROP && demo_random(v) < 0.7f)) {
            v->demo_aim = demo_open_way(v);
        }
    }
    if ((v->demo_check -= dt) <= 0) { /* stuck against something: the most open way, and a hop */
        float dx = pl->position.x - v->demo_last.x, dy = pl->position.y - v->demo_last.y;
        if ((fwd > 0 || v->demo_act == DEMO_SIDESTEP) && footing && dx * dx + dy * dy < 0.25f && t > 0.75f) {
            v->demo_aim = demo_open_way(v);
            if (v->demo_act != DEMO_POGO) { /* on the pogo only the turn: getting off takes its Action press */
                demo_start(v, DEMO_HOP);
                v->demo_flap = 2;
            }
        }
        v->demo_check = 0.75f;
        v->demo_last = pl->position;
    }
    /* Falling a long way: he often tries to fly (Jump in the air: B89 -> B26), decided once per fall. */
    if (!ground && !flying && card != 1 && card != 2 && p->has_driver) {
        if (v->demo_flap == 0 && v->demo_air > 0.6f && pl->velocity.z < -5)
            v->demo_flap = demo_random(v) < 0.75f ? 1 : 2, v->demo_flap_after = v->demo_air;
        if (v->demo_flap == 1 && v->demo_air >= v->demo_flap_after && !(buttons & (1u << TC_VA_JUMP))
            && v->demo_jump_free >= 0.1f) { /* a fresh press: let go for a few of the AI's ticks first */
            v->demo_flap = 3; /* pressed: held for a moment below */
            v->demo_mark = v->demo_air;
        }
    }
    if (v->demo_flap == 3) {
        if (v->demo_air - v->demo_mark < 0.3f && !ground) buttons |= 1u << TC_VA_JUMP;
        else v->demo_flap = 2;
    }
    if (flying) { /* the glide: the stick left alone trims it; a gentle bank with the drift */
        fwd = 0;
        side = fmaxf(-0.6f, fminf(0.6f, v->demo_turn * 1.5f));
        v->demo_aim = v->demo_facing;
    }
    if (v->demo_act_time >= v->demo_act_length) demo_pick(v);
    v->demo_aim = wrap_angle(v->demo_aim);

    /* The camera behind him: after the way he moves (where he faces when about still, or side-stepping), smoothed;
     * a little higher over drops and in flight. Not the look stick: it turns faster than a player's would need to. */
    face = TC_PI - pl->heading;
    if (v->demo_act != DEMO_SIDESTEP && pl->velocity.x * pl->velocity.x + pl->velocity.y * pl->velocity.y > 4)
        face = atan2f(pl->velocity.x, pl->velocity.y);
    v->demo_facing = wrap_angle(v->demo_facing + wrap_angle(face - v->demo_facing) * (1 - expf(-dt / 0.25f)));
    k = 1 - expf(-dt / (flying ? 0.35f : 0.7f)); /* a banking glide turns steadily: keep up closer */
    v->camera.yaw += wrap_angle(v->demo_facing + (v->demo_glance_time > 0 ? v->demo_glance : 0) - v->camera.yaw) * k;
    pitch = flying || v->demo_act == DEMO_LEAP ? -0.5f : v->demo_act == DEMO_PAUSE ? -0.2f : -0.3f;
    v->camera.pitch += (pitch - v->camera.pitch) * (1 - expf(-dt / 0.8f));

    /* The stick is camera-relative: towards the aim from where the camera looks. */
    rel = wrap_angle(v->demo_aim - v->camera.yaw);
    v->move_x = flying ? side : side + fwd * sinf(rel);
    v->move_y = flying ? 0 : fwd * cosf(rel);
    v->look_x = v->look_y = 0;
    v->held = (v->held & ~DEMO_BUTTONS) | buttons;
    v->demo_jump_free = buttons & (1u << TC_VA_JUMP) ? 0 : v->demo_jump_free + dt;
}

bool tc_viewer_can_play(const tc_viewer *v) { return v->play != NULL; }

bool tc_viewer_has_ai(const tc_viewer *v) { return v->play && v->play->has_driver; }

bool tc_viewer_ai_active(const tc_viewer *v) { return v->playing && tc_viewer_has_ai(v) && v->play->use_ai; }

bool tc_viewer_status_live(const tc_viewer *v) {
    return tc_viewer_ai_active(v) || (v->light && v->light_scripted) || v->sector_mode != TC_VS_ALL;
}

const tc_player *tc_viewer_player(const tc_viewer *v) { return v->play ? &v->play->player : NULL; }

const tc_cast *tc_viewer_cast(const tc_viewer *v) { return v->play && v->play->has_cast ? &v->play->cast : NULL; }

/* The simple controller at a portal (the ski levels' finish, or I pressed anywhere): when one captures, Ed faces it and
 * plays the step-in (state 228's animation, as the AI's walk-in does), then the cover; once the screen is covered the
 * portal fires as with the AI (its dsgvar condition set as Ed's B106 would). Capture-only portals (the holes) need the
 * player's scripts and are left alone. */
static void simple_portals(tc_viewer *v, float dt) {
    tc_viewer_play *p = v->play;
    tc_ed_ai *e = &p->driver;
    const tc_ai_model *m = e->ai.model;
    if (v->walk_in == 0) {
        const tc_ai_portal *g;
        char name[16];
        if (v->phase != TC_VP_PLAY) return;
        tc_ed_ai_run_portals(e, dt);
        e->events.cover = false; /* the step-in comes first */
        if (e->portal < 0) return;
        g = &m->portals[e->portal];
        p->player.heading = atan2f(g->position.x - p->player.position.x, -(g->position.y - p->player.position.y));
        snprintf(name, sizeof(name), "s%u", m->state_count > 228 ? (unsigned)m->states[228].anim : 0u);
        v->walk_clip = m->state_count > 228 ? ed_anim(v->level, name) : -1;
        v->ed_phase = 0;
        v->walk_in = 1;
        if (v->walk_clip < 0) {
            v->walk_in = 2;
            start_transition(v, false);
        }
        return;
    }
    if (v->walk_in == 2 && v->phase == TC_VP_BLACK && e->portal >= 0) {
        const tc_ai_portal *g = &m->portals[e->portal];
        if (g->fire == 0) tc_ai_dsgvar_set_int(&e->ai, g->fire_dsg, g->fire_value);
        e->screen_covered = true;
        tc_ed_ai_run_portals(e, dt);
    }
    if (v->walk_in == 2 && e->portal < 0) v->walk_in = 0;
}

/* A rotation's quaternion (x, y, z, w) from a model matrix's orthonormal axes (columns m[0..2], m[4..6], m[8..10]). */
static void axes_quat(const tc_mat4 *a, float q[4]) {
    const float *m = a->m;
    float tr = m[0] + m[5] + m[10], s;
    if (tr > 0) {
        s = sqrtf(tr + 1) * 2;
        q[3] = s / 4, q[0] = (m[6] - m[9]) / s, q[1] = (m[8] - m[2]) / s, q[2] = (m[1] - m[4]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        s = sqrtf(1 + m[0] - m[5] - m[10]) * 2;
        q[3] = (m[6] - m[9]) / s, q[0] = s / 4, q[1] = (m[4] + m[1]) / s, q[2] = (m[8] + m[2]) / s;
    } else if (m[5] > m[10]) {
        s = sqrtf(1 + m[5] - m[0] - m[10]) * 2;
        q[3] = (m[8] - m[2]) / s, q[0] = (m[4] + m[1]) / s, q[1] = s / 4, q[2] = (m[9] + m[6]) / s;
    } else {
        s = sqrtf(1 + m[10] - m[0] - m[5]) * 2;
        q[3] = (m[1] - m[4]) / s, q[0] = (m[8] + m[2]) / s, q[1] = (m[9] + m[6]) / s, q[2] = s / 4;
    }
}

/* Between two of Ed's poses: the position linearly, the rotation along the shorter way (normalised quaternion lerp). A
 * jump of more than 5 units (a teleport, a respawn) is not interpolated. */
static tc_mat4 pose_lerp(const tc_mat4 *a, const tc_mat4 *b, float t) {
    float qa[4], qb[4], q[4], l, sign;
    tc_mat4 r = *b;
    int k;
    float dx = b->m[12] - a->m[12], dy = b->m[13] - a->m[13], dz = b->m[14] - a->m[14];
    if (dx * dx + dy * dy + dz * dz > 25) return *b;
    axes_quat(a, qa);
    axes_quat(b, qb);
    sign = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3] < 0 ? -1.f : 1.f;
    for (k = 0; k < 4; k++) q[k] = qa[k] * (1 - t) + qb[k] * sign * t;
    l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l > 0) for (k = 0; k < 4; k++) q[k] /= l;
    r.m[0] = 1 - 2 * (q[1] * q[1] + q[2] * q[2]), r.m[1] = 2 * (q[0] * q[1] + q[2] * q[3]), r.m[2] = 2 * (q[0] * q[2] - q[1] * q[3]);
    r.m[4] = 2 * (q[0] * q[1] - q[2] * q[3]), r.m[5] = 1 - 2 * (q[0] * q[0] + q[2] * q[2]), r.m[6] = 2 * (q[1] * q[2] + q[0] * q[3]);
    r.m[8] = 2 * (q[0] * q[2] + q[1] * q[3]), r.m[9] = 2 * (q[1] * q[2] - q[0] * q[3]), r.m[10] = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    r.m[12] = a->m[12] + dx * t, r.m[13] = a->m[13] + dy * t, r.m[14] = a->m[14] + dz * t;
    return r;
}

/* Ed's drawn pose and animation after a tick. */
static void capture_pose(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    char name[16];
    int i;
    snprintf(name, sizeof(name), "s%u", (unsigned)tc_ed_ai_anim(&p->driver));
    i = ed_anim(v->level, name);
    p->pose_cur = tc_ed_ai_model_matrix(&p->driver);
    p->clip_cur = i >= 0 ? i : ed_anim(v->level, "idle");
    p->phase_cur = tc_ed_ai_frame(&p->driver);
    p->loop_cur = tc_ed_ai_loops(&p->driver);
    p->rate_cur = (float)p->driver.ai.frame_rate;
    if (!p->have_pose) {
        p->pose_prev = p->pose_cur, p->clip_prev = p->clip_cur, p->phase_prev = p->phase_cur;
        p->have_pose = true;
    }
}

/* A Special Edition level's walk / ski switches (tc_ai_mode_zone, docs/SE.md "Sliding in the Special Edition"): with
 * Ed's origin in a zone whose requirement holds, the player goes to the other model (walking Ed: the level's .ttai,
 * the skier: its ALTM). Its position, heading and speed stay (the zone may move him first, as the SE trigger does);
 * the new driver starts in its model's start behaviour (the skier's waits for Jump, which tc_ed_ai's auto start
 * presses on Totalski). Port choice: the SE's own triggers force SE Ed's ski behaviour; retail Ed has none, so the
 * retail skier takes over. One zone per tick. */
unsigned tc_viewer_play_mode(const tc_viewer *v) { return v->play ? v->play->mode : TC_AI_MODE_WALK; }
const tc_ed_ai *tc_viewer_driver(const tc_viewer *v) { return v->play && v->play->has_driver ? &v->play->driver : NULL; }

static void play_mode_zones(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    const tc_level *l = v->level;
    uint32_t i;
    /* The SE's walk / ski switch. With a cast (SE Totalski's pickups and World since 2026-11-05) the cast is handed
     * the new driver: it holds links into the old one's AI (tc_cast_detach_main / tc_cast_set_main). */
    if (!p->has_alt || !p->has_driver) return;
    for (i = 0; i < p->model.mode_zone_count; i++) {
        const tc_ai_mode_zone *z = &p->model.mode_zones[i];
        tc_ai_model *next = z->mode == TC_AI_MODE_SKI ? &p->alt : &p->model;
        if (z->mode == p->mode || (i < 64 && z->once && p->zone_fired[i])) continue;
        if (z->require && z->require != p->mode + 1) continue;
        if (!tc_ai_zone_contains(z->inverse, z->prim, z->prim_count, p->player.position)) continue;
        if (z->teleport == 1) {
            p->player.position = z->vector;
            p->player.velocity = tc_v3(0, 0, 0);
            p->player.placed++;
        } else if (z->teleport == 2) {
            p->player.position = tc_v3_add(p->player.position, z->vector);
            p->player.placed++;
        }
        if (p->has_cast) tc_cast_detach_main(&p->cast);
        tc_ed_ai_free(&p->driver);
        p->has_driver = tc_ed_ai_init(&p->driver, next, &p->player, p->level_name) == TC_OK;
        if (!p->has_driver) { p->use_ai = false; return; }
        tc_ed_ai_set_sectors(&p->driver, l->sectors.count ? &l->sectors : NULL);
        if (p->has_cast) tc_cast_set_main(&p->cast, &p->driver.ai);
        p->mode = z->mode;
        p->driver.walk.firm_floors = p->mode == TC_AI_MODE_WALK;
        if (p->mode == TC_AI_MODE_WALK) p->ski_shot = false;
        if (i < 64) p->zone_fired[i] = 1;
        return;
    }
}

/* Ed's AI and mechanics in ticks of TC_ED_AI_TICK (the game's 30 per second), then this frame's pose between the
 * last two ticks. The scripts see last frame's camera eye (it makes the direction keys camera-relative). */
/* A picture the cast's scripts asked for since the last look (tc_ai_globals.vignette): taken as tc_viewer.picture
 * ("Credits\\Credit06.bmp" -> "Credits/Credit06"), shown for its time. */
static bool take_picture(tc_viewer *v) {
    const tc_ai_globals *g = &v->play->cast.globals;
    char *dot, *slash;
    size_t k;
    if (g->vignette_serial == v->picture_serial) return false;
    v->picture_serial = g->vignette_serial;
    if (!g->vignette[0] || !g->vignette_ms) return false;
    for (k = 0; g->vignette[k] && k + 1 < sizeof(v->picture); k++) v->picture[k] = g->vignette[k] == '\\' ? '/' : g->vignette[k];
    v->picture[k] = 0;
    dot = strrchr(v->picture, '.');
    slash = strrchr(v->picture, '/');
    if (dot && (!slash || dot > slash)) *dot = 0;
    v->picture_left = (float)g->vignette_ms / 1000;
    return true;
}

static void step_ai(tc_viewer *v, float dt) {
    tc_viewer_play *p = v->play;
    float t;
    int ticks = 0;
    if (!p->have_pose) capture_pose(v);
    v->sound_count = 0;
    p->tick_time += dt;
    while (p->tick_time >= TC_ED_AI_TICK) {
        uint32_t n;
        p->tick_time -= TC_ED_AI_TICK;
        if (++ticks > 8) { p->tick_time = 0; break; } /* a long stall: drop the backlog */
        p->pose_prev = p->pose_cur, p->clip_prev = p->clip_cur, p->phase_prev = p->phase_cur;
        bool still = v->cam.first_person || v->director; /* the director's shot: Ed waits, as the SE parks its Ed */
        play_mode_zones(v);
        if (!p->has_driver) break;
        /* the exe's frame (docs/ENGINE.md): the run-first persos (platforms, lifts) move before the rest, so Ed rides
         * and collides with them where they are this tick; then Ed, then the other persos */
        if (p->has_cast) {
            const tc_ai_state *st = &p->driver.ai.model->states[p->driver.ai.state];
            p->cast.main_carrier = p->driver.walk.carrier;
            p->cast.main_mass = st->card != TC_AI_NONE16 ? tc_ai_card_f32(&p->driver.ai.model->cards[st->card], 4) : 0;
            tc_cast_step_first(&p->cast, TC_ED_AI_TICK, v->sectors, p->driver.sector.sector);
        }
        tc_ed_ai_step(&p->driver, TC_ED_AI_TICK, v->director ? 0 : ai_inputs(v), still ? 0 : v->move_x,
                      still ? 0 : v->move_y, v->camera.position);
        if (p->driver.events.restarted) {
            p->driver.events.restarted = false;
            if (p->has_cast) tc_cast_respawn(&p->cast);
            memset(p->zone_fired, 0, sizeof(p->zone_fired)); /* the SE reloads the level: its triggers fire again */
        }
        for (n = 0; n < p->driver.ai.sound_count && v->sound_count < TC_AI_MAX_SOUNDS; n++)
            v->sounds[v->sound_count++] = p->driver.ai.sounds[n];
        if (p->has_cast) { /* after Ed, on his clock and from his sector */
            tc_cast_step_rest(&p->cast, TC_ED_AI_TICK, v->sectors, p->driver.sector.sector);
            for (n = 0; n < p->cast.sounds_out_count && v->sound_count < TC_AI_MAX_SOUNDS; n++)
                v->sounds[v->sound_count++] = p->cast.sounds_out[n];
        }
        capture_pose(v);
        if (p->has_cast && take_picture(v)) { /* the game holds while it shows (0x40fa20 waits in the main loop) */
            p->tick_time = 0;
            break;
        }
    }
    t = p->tick_time / TC_ED_AI_TICK;
    p->pose_draw = pose_lerp(&p->pose_prev, &p->pose_cur, t);
    v->ed_clip = p->clip_cur;
    v->ed_state = -1;
    v->ed_loop = p->loop_cur;
    v->ed_phase = p->phase_cur;
    if (p->clip_cur >= 0) {
        float count = (float)v->level->ed.anims[p->clip_cur].sample_count, d = p->phase_cur - p->phase_prev;
        if (d < 0 && p->loop_cur) d += count; /* wrapped round the loop */
        /* Only a loop wraps: an ended state holds its last frame at phase == count (the portal step-in, Ed hidden),
         * which wrapped to 0 drew him whole again where the step-in began. */
        if (p->clip_prev == p->clip_cur && d >= 0 && d < count / 2)
            v->ed_phase = p->loop_cur ? fmodf(p->phase_prev + d * t, count) : p->phase_prev + d * t;
        else /* a new clip this tick: the drawing runs a tick behind, so start it from its frame 0 at the tick's rate
              * (showing phase_cur at once jumped a tick ahead, then held it for the rest of the tick) */
            v->ed_phase = fmaxf(0, p->phase_cur - (1 - t) * p->rate_cur * TC_ED_AI_TICK);
    }
}

/* The camera's sight test against the persos drawn last frame (tc_cam_sight_fn): their parts, both faces, not sprites,
 * water or blended ones (smoke, glass), nor Ed. The original's camera tests the persos of the sectors on its line by
 * their drawn geometry (FUN_004a3920 -> FUN_00484210); here every drawn perso, culled by its parts' bounding spheres. */
static float segment_tri_t(tc_vec3 o, tc_vec3 d, tc_vec3 a, tc_vec3 b, tc_vec3 c) {
    tc_vec3 e1 = tc_v3_sub(b, a), e2 = tc_v3_sub(c, a), p = tc_v3_cross(d, e2), s, q;
    float det = tc_v3_dot(e1, p), inv, u, w, t;
    if (fabsf(det) < 1e-12f) return 2;
    inv = 1 / det;
    s = tc_v3_sub(o, a);
    u = tc_v3_dot(s, p) * inv;
    if (u < 0 || u > 1) return 2;
    q = tc_v3_cross(s, e1);
    w = tc_v3_dot(d, q) * inv;
    if (w < 0 || u + w > 1) return 2;
    t = tc_v3_dot(e2, q) * inv;
    return t >= 0 && t <= 1 ? t : 2;
}

static tc_vec3 model_point(const tc_mat4 *m, const float *p) {
    const float *a = m->m;
    return tc_v3(a[0] * p[0] + a[4] * p[1] + a[8] * p[2] + a[12], a[1] * p[0] + a[5] * p[1] + a[9] * p[2] + a[13],
                 a[2] * p[0] + a[6] * p[1] + a[10] * p[2] + a[14]);
}

static void mesh_spheres(tc_viewer *v) {
    uint32_t i, k;
    if (v->mesh_sphere || !(v->mesh_sphere = (float *)tc_alloc(sizeof(float) * 4 * (v->mesh_count ? v->mesh_count : 1))))
        return;
    for (i = 0; i < v->mesh_count; i++) {
        const tc_mesh *m = v->meshes[i];
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}, r = 0, *s = v->mesh_sphere + 4 * i;
        int j;
        for (k = 0; m && k < m->vertex_count; k++)
            for (j = 0; j < 3; j++) lo[j] = fminf(lo[j], m->vertices[k].position[j]), hi[j] = fmaxf(hi[j], m->vertices[k].position[j]);
        for (j = 0; j < 3; j++) s[j] = m && m->vertex_count ? (lo[j] + hi[j]) / 2 : 0;
        for (k = 0; m && k < m->vertex_count; k++) {
            const float *q = m->vertices[k].position;
            float dx = q[0] - s[0], dy = q[1] - s[1], dz = q[2] - s[2];
            r = fmaxf(r, dx * dx + dy * dy + dz * dz);
        }
        s[3] = sqrtf(r);
    }
}

static float camera_sight(void *user, tc_vec3 from, tc_vec3 to) {
    tc_viewer *v = (tc_viewer *)user;
    tc_vec3 d = tc_v3_sub(to, from);
    float best = 1, len2 = tc_v3_dot(d, d);
    uint32_t i, k;
    if (len2 < 1e-10f || !v->draws) return 1;
    mesh_spheres(v);
    if (!v->mesh_sphere) return 1;
    for (i = 0; i < v->draw_count; i++) {
        const tc_view_draw *dr = &v->draws[i];
        const tc_mesh *m;
        const float *s;
        tc_vec3 c, rel;
        float scale, r, along;
        if (dr->mesh < v->perso_first || dr->mesh >= v->ed_first) continue; /* persos only (not Ed, statics, pieces) */
        if (dr->flags & (TC_FLAG_BILLBOARD | TC_FLAG_WATER | TC_FLAG_TRANSPARENT)) continue;
        m = v->meshes[dr->mesh];
        s = v->mesh_sphere + 4 * dr->mesh;
        c = model_point(&dr->model, s);
        scale = fmaxf(tc_v3_length(tc_v3(dr->model.m[0], dr->model.m[1], dr->model.m[2])),
                      fmaxf(tc_v3_length(tc_v3(dr->model.m[4], dr->model.m[5], dr->model.m[6])),
                            tc_v3_length(tc_v3(dr->model.m[8], dr->model.m[9], dr->model.m[10]))));
        r = s[3] * scale;
        rel = tc_v3_sub(c, from);
        along = fmaxf(0, fminf(1, tc_v3_dot(rel, d) / len2));
        if (tc_v3_length(tc_v3_sub(rel, tc_v3_scale(d, along))) > r) continue; /* the segment misses its sphere */
        for (k = dr->first_index; k + 2 < dr->first_index + dr->index_count && k + 2 < m->index_count; k += 3) {
            float t = segment_tri_t(from, d, model_point(&dr->model, m->vertices[m->indices[k]].position),
                                    model_point(&dr->model, m->vertices[m->indices[k + 1]].position),
                                    model_point(&dr->model, m->vertices[m->indices[k + 2]].position));
            if (t < best) best = t;
        }
    }
    return best;
}

/* The director's shot (tc_viewer.director): camera mode 6 on a target, from the cast's scripts (tc_ai_globals). fresh:
 * Jump, Action or Shoot pressed this frame (ends it: port choice). Returns whether the shot is on. */
static bool update_director(tc_viewer *v, bool fresh) {
    tc_viewer_play *p = v->play;
    const tc_ai_globals *g;
    tc_vec3 target, d;
    bool want;
    if (!p->has_cast) return v->director = false;
    g = &p->cast.globals;
    if (g->cam_serial != v->director_serial) { /* a new camera write: the scripts direct again */
        v->director_serial = g->cam_serial;
        v->director_released = false;
    }
    want = g->cam_mode == 6 && g->cam_target != 0 && !v->director_released && !v->demo &&
           tc_cast_position(&p->cast, g->cam_target, &target);
    if (want && v->director && fresh) {
        want = false;
        v->director_released = true;
    }
    if (want && !v->director) { /* mode 6 stays where the camera is: in the level data, before it has moved */
        v->director = true;
        v->director_saved_fov = v->camera.vertical_fov;
        if (!g->cam_perso || !tc_cast_placed_position(&p->cast, g->cam_perso, &v->director_eye))
            v->director_eye = v->camera.position;
    } else if (!want && v->director) {
        v->director = false;
        v->camera.vertical_fov = v->director_saved_fov;
        v->cam.placed = false; /* the follow, placed afresh behind Ed */
    }
    if (!v->director) return false;
    if (g->cam_focal > 0) v->camera.vertical_fov = tc_camera_vfov_from_hfov_43(g->cam_focal);
    v->camera.position = v->director_eye;
    d = tc_v3_sub(target, v->director_eye);
    if (tc_v3_length(d) > 1e-4f) {
        v->camera.yaw = atan2f(d.x, d.y);
        v->camera.pitch = atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y));
    }
    return true;
}

bool tc_viewer_directed(const tc_viewer *v) { return v->director; }

static void update_play(tc_viewer *v, float dt, bool fresh) {
    tc_viewer_play *p = v->play;
    const tc_level *l = v->level;
    const tc_ed_anim *a;
    float rate;
    int i;
    look(v, dt);
    if (p->use_ai) {
        step_ai(v, v->paused ? 0 : dt); /* Ed's own scripts, at the game's tick */
    } else {
        tc_player_input in;
        bool walking_in = v->walk_in != 0;
        in.move = walking_in || v->cam.first_person ? tc_v2(0, 0) : move_direction(v);
        /* Shift runs; the stick runs when pushed most of the way (0.8: the simple controller has only walk and run). */
        in.run = !walking_in && (held(v, TC_VA_FAST) || v->move_x * v->move_x + v->move_y * v->move_y > 0.64f);
        in.jump = !walking_in && held(v, TC_VA_JUMP);
        tc_player_step(&p->player, dt, &in);
        if (p->has_driver) simple_portals(v, dt);
    }
    /* The play camera (tc_cam.h): round Ed as drawn, turned by the player; against every surface of the level (no sector
     * row: the camera must not see through anything). */
    {
        tc_cam_target t;
        const uint8_t *solid = p->world.solid;
        t.position = p->use_ai ? tc_v3(p->pose_draw.m[12], p->pose_draw.m[13], p->pose_draw.m[14]) : p->player.position;
        t.behind = TC_PI - p->player.heading; /* camera forward (sin yaw, cos yaw) = Ed's front (sin h, -cos h) */
        if (p->use_ai && p->ski_shot) /* his facing as drawn (blended between his AI's ticks, as his position is), not
                                       * the last tick's: the ski follow holds within 0.05 rad of it and stepped at 30 Hz
                                       * with it */
            t.behind -= remainderf(atan2f(p->pose_draw.m[1], p->pose_draw.m[0]) - atan2f(p->pose_cur.m[1], p->pose_cur.m[0]),
                                   2 * TC_PI); /* behind = pi - heading: a heading change counts negative */
        t.speed = tc_player_horizontal_speed(&p->player);
        t.velocity = p->player.velocity;
        t.distance = v->orbit_distance;
        t.manual = v->demo;
        t.situation = tc_viewer_camera_situation(v);
        /* riding on a sliding stage (the skier is the player there: the cast's main alias; Ed's own slides on the walking
         * levels use the same card and keep the walking camera, his scripts setting the shot to 0): tc_viewer_camera_shot */
        if (p->use_ai && p->has_driver && p->driver.card_type == 2 && v->level->cast.main_alias != 0) p->ski_shot = true;
        t.shot = tc_viewer_camera_shot(v);
        t.world = &p->world;
        t.sight = camera_sight; /* the persos as drawn last frame */
        t.sight_user = v;
        p->world.solid = NULL;
        if (!update_director(v, fresh)) tc_cam_update(&v->cam, &v->camera, &t, dt);
        p->world.solid = solid;
    }

    if (p->use_ai) return; /* the AI's state clock picked the frame (step_ai) */
    if (v->walk_in == 1) { /* the step-in: once through at its own rate, then the cover */
        a = &l->ed.anims[v->walk_clip];
        v->ed_clip = v->walk_clip;
        v->ed_state = -1;
        v->ed_loop = false;
        v->ed_phase = tc_phase_advance(v->ed_phase, dt * a->sample_rate * (v->paused ? 0 : v->animation_speed),
                                       a->sample_count, false);
        if (v->ed_phase >= (float)a->sample_count - 1) {
            v->walk_in = 2;
            start_transition(v, false);
        }
        return;
    }
    /* Clip from the movement state; walk/run play at the rate that matches Ed's ground speed. */
    if ((int)p->player.animation != v->ed_state) {
        v->ed_state = (int)p->player.animation;
        i = ed_anim(l, tc_anim_state_name(p->player.animation));
        v->ed_clip = i >= 0 ? i : ed_anim(l, "idle");
        v->ed_phase = 0;
    }
    v->ed_loop = v->ed_clip < 0 || l->ed.anims[v->ed_clip].loop != 0;
    if (v->ed_clip < 0) return;
    a = &l->ed.anims[v->ed_clip];
    rate = a->ground_speed > 0 ? a->sample_rate * tc_player_horizontal_speed(&p->player) / a->ground_speed
                               : a->sample_rate;
    v->ed_phase = tc_phase_advance(v->ed_phase, dt * (rate / 4) * (v->paused ? 0 : v->animation_speed),
                                   a->sample_count, a->loop != 0);
}

uint32_t tc_viewer_sounds(const tc_viewer *v, const uint16_t **events) {
    *events = v->sounds;
    return v->sound_count;
}

/* Ed's sector after this frame's moves: his AI's, else tracked here for the simple controller. */
static void update_sectors(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    if (!v->sectors || !p) return;
    if (p->has_driver) tc_ed_ai_track_sector(&p->driver);
    else tc_sector_follow(&v->ed_sector, v->sectors, p->player.position, p->player.placed);
}

uint16_t tc_viewer_ed_sector(const tc_viewer *v) {
    if (!v->sectors || !v->play) return TC_SECTOR_NONE;
    return v->play->has_driver ? v->play->driver.sector.sector : v->ed_sector.sector;
}


void tc_viewer_update(tc_viewer *v, float dt) {
    const uint32_t skip_mask = (1u << TC_VA_JUMP) | (1u << TC_VA_ACTION) | (1u << TC_VA_SHOOT);
    bool fresh = (v->held & skip_mask & ~v->skip_held) != 0;
    v->skip_held = v->held & skip_mask;
    v->sound_count = 0;
    if (v->picture_left > 0) { /* a scripted picture: the game holds (port: a fresh press skips it) */
        v->picture_left = fresh ? 0 : fmaxf(0, v->picture_left - dt);
        return;
    }
    if (v->phase == TC_VP_LOADING || v->phase == TC_VP_PENDING) look(v, 0); /* the level is about to change: Ed waits */
    else if (v->playing) {
        if (v->demo) demo_step(v, dt);
        update_play(v, dt, fresh);
    }
    else fly(v, dt);
    update_sectors(v);
    update_lighting(v, dt);
    update_portals(v, v->paused ? 0 : dt);
    update_light(v, v->paused ? 0 : dt); /* the game clock, as Ed's AI */
    if (!v->paused) v->animation_time += dt * v->animation_speed;
}

/* ---- settings ---- */

void tc_viewer_step_speed(tc_viewer *v, int dir) {
    static const float steps[] = {0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 0.75f, 1, 1.5f, 2};
    const int count = (int)(sizeof(steps) / sizeof(steps[0]));
    int i = 5, s;
    for (s = 0; s < count; s++)
        if (steps[s] >= v->animation_speed - 0.001f) { i = s; break; }
    i += dir > 0 ? 1 : -1;
    v->animation_speed = steps[i < 0 ? 0 : i >= count ? count - 1 : i];
}

void tc_viewer_toggle_pause(tc_viewer *v) { v->paused = !v->paused; }

void tc_viewer_recenter_camera(tc_viewer *v) {
    if (v->playing && !v->demo) tc_cam_recenter(&v->cam);
}

void tc_viewer_toggle_closeup(tc_viewer *v) {
    if (v->playing && !v->demo) v->cam.closeup = !v->cam.closeup;
}

void tc_viewer_toggle_first_person(tc_viewer *v) {
    if (!v->playing || v->demo || !v->play) return;
    v->cam.first_person = !v->cam.first_person;
    v->camera.yaw = TC_PI - v->play->player.heading; /* entering: looking the way he faces; leaving: behind him */
    v->camera.pitch = v->cam.first_person ? 0 : -TC_CAM_ELEVATION;
    v->cam.placed = false;
}

int tc_viewer_camera_shot(const tc_viewer *v) {
    const tc_viewer_play *p = v->play;
    int shot;
    if (!p || !p->use_ai || !p->has_driver || p->driver.camera < 0) return -1;
    shot = (int)tc_ai_other_dsgvar_float(&p->driver.ai, (uint32_t)p->driver.camera, TC_AI_CAMERA_SHOT_DSG);
    /* The sliding stages' start trigger (Totalski's YBA_GenD_DepartMap: once the skier is in his riding behaviour, the
     * camera's dsg39 := 5, then it removes itself) is not in the cast: the converter reduces it to the port's ski start
     * (PROC 6, tools/ttre/cast.py). Its write stands here: once he has ridden (his skiing card, type 2), the ski follow,
     * unless the scripts pick a shot of their own (5 or 6). Only there: the skier is the player (the cast's main alias). */
    if (p->ski_shot && shot != TC_CAM_SHOT_SKI && shot != TC_CAM_SHOT_FIXED) shot = TC_CAM_SHOT_SKI;
    return shot;
}

int tc_viewer_camera_situation(const tc_viewer *v) {
    const tc_viewer_play *p = v->play;
    if (!p || !p->use_ai || !p->has_driver || p->driver.camera < 0) return -1;
    return (int)tc_ai_other_dsgvar_float(&p->driver.ai, (uint32_t)p->driver.camera, 40);
}

void tc_viewer_set_look(tc_viewer *v, float sensitivity, bool invert_x, bool invert_y) {
    v->look_sensitivity = sensitivity > 0 ? fmaxf(0.25f, fminf(4, sensitivity)) : 1;
    v->invert_x = invert_x;
    v->invert_y = invert_y;
}

void tc_viewer_step_look(tc_viewer *v, int dir) {
    float s = v->look_sensitivity * (dir > 0 ? 1.25f : 0.8f);
    if (fabsf(s - 1) < 0.01f) s = 1; /* the steps land back on 1 exactly */
    tc_viewer_set_look(v, s, v->invert_x, v->invert_y);
}

void tc_viewer_toggle_invert_y(tc_viewer *v) { v->invert_y = !v->invert_y; }

void tc_viewer_set_light_set(tc_viewer *v, uint32_t n) {
    if (n >= v->light_set_count) return;
    v->light_set = v->light_next = v->light_target = n;
    v->light_blend = v->light_rate = 0;
    v->light_scripted = false;
}

void tc_viewer_follow_light(tc_viewer *v) { v->light_scripted = v->light != NULL; }

void tc_viewer_step_gamma(tc_viewer *v, bool up) {
    v->gamma = fmaxf(0.5f, fminf(2.5f, v->gamma * (up ? 1.1f : 1 / 1.1f)));
}

void tc_viewer_step_fov(tc_viewer *v, int dir) {
    /* in the original's measure, horizontal at 4:3: 86 degrees by default, 5 degree steps, 50-120 */
    float deg = roundf(tc_camera_hfov_43(v->camera.vertical_fov) * 180 / TC_PI) + (dir > 0 ? 5.0f : -5.0f);
    v->camera.vertical_fov = tc_camera_vfov_from_hfov_43(fmaxf(50, fminf(120, deg)) * TC_PI / 180);
}

void tc_viewer_toggle_fog(tc_viewer *v) { v->fog_enabled = !v->fog_enabled; }

void tc_viewer_cycle_lighting(tc_viewer *v) {
    if (v->level->has_lighting) v->lighting = (tc_view_lighting)((v->lighting + 1) % TC_VL_COUNT);
}

void tc_viewer_cycle_sectors(tc_viewer *v) {
    if (v->sectors) v->sector_mode = (tc_view_sectors)((v->sector_mode + 1) % TC_VS_COUNT);
}

void tc_viewer_wheel(tc_viewer *v, float notches) {
    float step = powf(1.1f, notches);
    if (v->playing) v->orbit_distance = fmaxf(1.5f, fminf(25, v->orbit_distance / step));  /* camera zoom */
    else v->fly_speed = fmaxf(0.5f, fminf(500, v->fly_speed * step));                       /* fly speed */
}

size_t tc_viewer_status(const tc_viewer *v, const char *extra, char *out, size_t size) {
    size_t n = 0;
    int w;
    /* Appends while there is room; n keeps counting what would have been written. */
#define APPEND(...)                                                          \
    do {                                                                     \
        w = snprintf(out ? out + (n < size ? n : size) : NULL,               \
                     n < size ? size - n : 0, __VA_ARGS__);                  \
        if (w > 0) n += (size_t)w;                                           \
    } while (0)
    APPEND("anim %.2f\xc3\x97, gamma %.2f, FOV %.0f\xc2\xb0%s", (double)v->animation_speed, (double)v->gamma,
           (double)(tc_camera_hfov_43(v->camera.vertical_fov) * 180 / TC_PI), extra ? extra : "");
    if (fabsf(v->look_sensitivity - 1) > 0.001f) APPEND(", look %.0f %%", (double)(v->look_sensitivity * 100));
    if (v->invert_x || v->invert_y) APPEND(", invert %s", v->invert_x && v->invert_y ? "XY" : v->invert_x ? "X" : "Y");
    if (v->light_set_count > 1 && v->light_rate > 0)
        APPEND(", light %u\xe2\x86\x92%u", (unsigned)v->light_set + 1, (unsigned)v->light_next + 1);
    else if (v->light_set_count > 1)
        APPEND(", light %u/%u", (unsigned)v->light_set + 1, (unsigned)v->light_set_count);
    if (v->light_set_count > 1 && v->light && !v->light_scripted) APPEND("%s", " held");
    if (v->lighting == TC_VL_GAME ? v->has_sector_fog : v->has_fog)
        APPEND("%s", v->fog_enabled ? ", fog on" : ", fog off");
    if (v->lighting == TC_VL_PORT && v->level->has_lighting) APPEND("%s", ", old lighting");
    if (v->sectors && v->sector_mode != TC_VS_ALL)
        APPEND(", sector %s%s", tc_sectors_name(v->sectors, tc_viewer_ed_sector(v)),
               v->sector_mode == TC_VS_ALONE ? " alone" : "");
    if (!v->playing && tc_viewer_can_play(v)) APPEND("%s", ", free-fly");
    if (v->playing && tc_viewer_has_ai(v) && !v->play->use_ai) APPEND("%s", ", simple controller");
    if (v->playing && v->cam.closeup) APPEND("%s", ", close-up");
    if (v->playing && v->cam.first_person) APPEND("%s", ", first person");
    if (v->playing && v->director) APPEND("%s", ", scripted camera");
    if (v->playing && v->play && v->play->has_cast && v->play->use_ai)
        APPEND(", cast %u/%u awake, lives %u, HP %d", (unsigned)v->play->cast.awake, (unsigned)v->play->cast.count,
               (unsigned)v->play->cast.globals.lives, (int)v->play->driver.ai.hit_points);
#undef APPEND
    return n;
}

/* ---- per frame ---- */

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
    tc_view_draw *d;
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
}

/* A mesh whole, or with `shown` (NULL: all) the runs of its ranges whose group is shown, each run of consecutive shown
 * ranges under the same water plane (sector_water: a sector's geometry; none on the water surfaces' groups) one draw. */
static void add_mesh(tc_viewer *v, draw_list *dl, uint32_t mesh, const tc_mat4 *model, const uint8_t *shown,
                     bool water) {
    uint32_t n = v->mesh_ranges ? v->mesh_ranges[2 * mesh + 1] : 0, k, first = 0, count = 0;
    const tc_sector_range *r;
    float z = NAN, rz;
    if ((!shown && !water) || n == 0) {
        add_draw(v, dl, mesh, model, 0, v->meshes[mesh]->index_count, NAN);
        return;
    }
    r = &v->ranges[v->mesh_ranges[2 * mesh]];
    for (k = 0; k < n; k++) {
        if (shown && !tc_sectors_group_shown(v->sectors, r[k].group, shown)) continue;
        rz = water && r[k].group < v->sectors->count ? v->sector_water[r[k].group] : NAN;
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
static const uint8_t *shown_sectors(tc_viewer *v) {
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

const tc_view_draw *tc_viewer_draws(tc_viewer *v, uint32_t *count, uint32_t *opaque_count) {
    const tc_level *l = v->level;
    const tc_mat4 identity = tc_mat4_identity();
    draw_list dl = {0, 0};
    uint32_t i, k, first = v->perso_first;
    float t = v->animation_time;
    const uint8_t *shown = shown_sectors(v);
    bool perso_sectors = shown && v->sectors->perso_count == l->perso_count;
    bool water = water_planes(v);
    float z;
    uint16_t sec;
    v->ed_draw_count = 0;

    for (i = 0; i < l->mesh_count; i++)
        if (!v->split || !v->split[i]) add_mesh(v, &dl, i, &identity, shown, water); /* split ones: as their pieces */
    for (i = 0; i < v->piece_count; i++) add_mesh(v, &dl, v->piece_first + i, &identity, shown, water);

    for (i = 0; i < l->perso_count; first += l->persos[i].part_count, i++) {
        const tc_perso *p = &l->persos[i];
        uint32_t nch = p->channel_count, base;
        tc_mat4 world;
        tc_sample s;
        const tc_cast_actor *actor;
        if (p->sample_count == 0 || (v->playing && (int32_t)i == l->ed.static_perso)) continue;
        /* drawn with its sector (the sector's character list); unknown: always */
        if (perso_sectors && v->sectors->perso_sector[i] != TC_SECTOR_NONE &&
            !(shown[v->sectors->perso_sector[i]] & TC_SHOWN_SECTOR))
            continue;
        /* Baked samples loop; blend each channel between neighbouring samples so motion is smooth at any display
         * rate and playback speed. Visibility follows the earlier sample. */
        world = tc_mat4_from34(&p->world);
        sec = v->sectors && v->sectors->perso_count == l->perso_count ? v->sectors->perso_sector[i] : TC_SECTOR_NONE;
        z = water && sec < v->sectors->count ? v->sector_water[sec] : NAN; /* drawn in its sector's pass */
        actor = v->playing && v->play && v->play->has_cast && v->play->use_ai ? tc_cast_actor_of(&v->play->cast, i) : NULL;
        if (actor) { /* its own AI's state animation; parts keyed by object (channel numbering differs per animation) */
            const tc_cast_anim *an;
            tc_mat34 w34;
            uint32_t ch;
            if (!tc_cast_pose(&v->play->cast, actor, v->play->tick_time / TC_ED_AI_TICK, &w34, &an, &s)) continue;
            world = tc_mat4_from34(&w34);
            nch = an->channel_count;
            for (ch = 0; ch < nch; ch++) {
                uint16_t obj = an->shown[s.s0 * nch + ch];
                tc_mat4 m, model;
                if (obj == TC_HIDDEN || (ch < 64 && (actor->ai.hidden_channels >> ch) & 1u)) continue;
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
        /* The skier's board tilts with the slope. */
        tc_mat4 model = v->play->use_ai && v->play->has_driver ? v->play->pose_draw
                                                               : tc_player_model_matrix(&v->play->player);
        sec = tc_viewer_ed_sector(v);
        z = water && sec != TC_SECTOR_NONE && sec < v->sectors->count ? v->sector_water[sec] : NAN;
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

    *opaque_count = dl.count;
    sort_blended(v, dl.blended_count);
    v->draw_count = 0; /* set below, once the list is whole */
    if (dl.blended_count && reserve(&v->draws, &v->draw_cap, dl.count + dl.blended_count)) {
        memcpy(v->draws + dl.count, v->blended, dl.blended_count * sizeof(*v->draws));
        dl.count += dl.blended_count;
    }
    *count = dl.count;
    v->draw_count = dl.count;
    return v->draws;
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
        o->position[0] = L->position.x, o->position[1] = L->position.y, o->position[2] = L->position.z;
        o->position[3] = (float)L->type;
        o->direction[0] = L->direction.x, o->direction[1] = L->direction.y, o->direction[2] = L->direction.z;
        o->direction[3] = L->near_range;
        for (k = 0; k < 3; k++) o->color[k] = L->color[k] * w;
        o->color[3] = L->far_range;
    }
}

bool tc_viewer_shadow(const tc_viewer *v, tc_view_shadow *out) {
    tc_viewer_play *p = v->play;
    tc_vec3 o;
    tc_hit hit;
    float h, k, s;
    memset(out, 0, sizeof(*out));
    if (!p || !v->playing || v->ed_draw_count == 0) return false;
    if (p->use_ai && p->has_driver && (p->driver.card_type == 1 || p->driver.card_type == 7)) return false; /* swimming */
    /* Where he is drawn (between the AI's ticks), as the camera's target */
    o = p->use_ai ? tc_v3(p->pose_draw.m[12], p->pose_draw.m[13], p->pose_draw.m[14]) : p->player.position;
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
