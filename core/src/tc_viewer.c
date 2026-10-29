#include "tc_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "tonic/tc_viewer.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_anim.h"
#include "tonic/tc_ed_ai.h"

/* ---- camera ---- */

void tc_camera_init(tc_camera *c) {
    c->position = tc_v3(0, 0, 0);
    c->yaw = 0;
    c->pitch = 0;
    c->vertical_fov = 55 * TC_PI / 180;
    c->near_z = 0.05f;
    c->far_z = 2000;
}

tc_vec3 tc_camera_forward(const tc_camera *c) {
    return tc_v3(sinf(c->yaw) * cosf(c->pitch), cosf(c->yaw) * cosf(c->pitch), sinf(c->pitch));
}

tc_vec3 tc_camera_right(const tc_camera *c) { return tc_v3(cosf(c->yaw), -sinf(c->yaw), 0); }

static tc_mat4 look_at(tc_vec3 eye, tc_vec3 center, tc_vec3 up) {
    tc_vec3 f = tc_v3_normalize(tc_v3_sub(center, eye));
    tc_vec3 s = tc_v3_normalize(tc_v3_cross(f, up));
    tc_vec3 u = tc_v3_cross(s, f);
    tc_mat4 r = {{s.x, u.x, -f.x, 0,
                  s.y, u.y, -f.y, 0,
                  s.z, u.z, -f.z, 0,
                  -tc_v3_dot(s, eye), -tc_v3_dot(u, eye), tc_v3_dot(f, eye), 1}};
    return r;
}

static tc_mat4 perspective(float fov_y, float aspect, float near_z, float far_z) {
    float y = 1 / tanf(fov_y / 2), x = y / aspect, z = far_z / (near_z - far_z);
    tc_mat4 r = {{x, 0, 0, 0,
                  0, y, 0, 0,
                  0, 0, z, -1,
                  0, 0, z * near_z, 0}};
    return r;
}

tc_mat4 tc_camera_view_projection(const tc_camera *c, float aspect) {
    tc_mat4 p = perspective(c->vertical_fov, aspect, c->near_z, c->far_z);
    tc_mat4 v = look_at(c->position, tc_v3_add(c->position, tc_camera_forward(c)), tc_v3(0, 0, 1));
    return tc_mat4_mul(&p, &v);
}

/* ---- play session ---- */

/* The tc_player points at its world and the AI driver at the player, so the session stays at one address. */
struct tc_viewer_play {
    tc_collision world;
    tc_player player;
    tc_ai_model model;
    bool has_model;
    tc_ed_ai driver;
    bool has_driver;
    bool use_ai;
    char level_name[128];        /* tc_ed_ai keeps a pointer to it */
    /* Ed's AI and mechanics step in fixed ticks of TC_ED_AI_TICK; drawing interpolates between the last two. */
    float tick_time;             /* seconds not yet stepped */
    bool have_pose;
    tc_mat4 pose_prev, pose_cur, pose_draw; /* Ed's model matrix after the last two ticks, and this frame's */
    int clip_prev, clip_cur;     /* level->ed.anims index after those ticks */
    float phase_prev, phase_cur;
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
    if (p->has_model) {
        tc_result r = tc_ed_ai_init(&p->driver, &p->model, &p->player, p->level_name);
        p->has_driver = r == TC_OK;
        if (r != TC_OK) v->ai_result = r;
    }
    p->use_ai = p->has_driver;
    return p;
}

static void play_destroy(tc_viewer_play *p) {
    if (!p) return;
    if (p->has_driver) tc_ed_ai_free(&p->driver);
    if (p->has_model) tc_ai_model_free(&p->model);
    tc_collision_free(&p->world);
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

/* ---- transparent pieces (tc_viewer_draws sorts blended draws back to front) ---- */

/* Piece `group` of static mesh `origin`: its triangles (tri_group[t] == group) with their vertices compacted.
 * map: one UINT32_MAX per vertex of m on entry, restored on return. */
static bool add_piece(tc_viewer *v, const tc_mesh *m, uint32_t origin, const uint32_t *tri_group, uint32_t group,
                      uint32_t *map) {
    uint32_t t, k, nt = m->index_count / 3, ni = 0, nv = 0;
    tc_mesh *p;
    for (t = 0; t < nt; t++)
        if (tri_group[t] == group) ni += 3;
    if (v->piece_count == v->piece_cap) {
        uint32_t cap = v->piece_cap ? v->piece_cap * 2 : 64;
        tc_mesh *pieces = (tc_mesh *)tc_calloc(cap, sizeof(*pieces));
        uint32_t *origins = (uint32_t *)tc_calloc(cap, sizeof(*origins));
        if (!pieces || !origins) {
            tc_free(pieces);
            tc_free(origins);
            return false;
        }
        if (v->pieces) memcpy(pieces, v->pieces, v->piece_count * sizeof(*pieces));
        if (v->piece_origin) memcpy(origins, v->piece_origin, v->piece_count * sizeof(*origins));
        tc_free(v->pieces);
        tc_free(v->piece_origin);
        v->pieces = pieces;
        v->piece_origin = origins;
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
    for (t = 0; t < nt; t++) {
        if (tri_group[t] != group) continue;
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
    p->vertex_count = nv;
    v->piece_origin[v->piece_count++] = origin;
    return true;
}

/* The .ttlv holds one mesh per material for the whole level, so a transparent material (water, glass, the OMBRE
 * shadows) would sort as a single draw. Each transparent static is cut by a grid of TC_VIEW_PIECE_SIZE cells over
 * its triangles' centroids into pieces with their own vertices; one that fits a single cell stays whole. Triangles
 * with an index out of range (none in converted levels) go to the first piece. */
static bool split_transparent(tc_viewer *v) {
    const tc_level *l = v->level;
    uint32_t i, t, g;
    bool ok = true;
    if (!l->mesh_count) return true;
    v->split = (uint8_t *)tc_calloc(l->mesh_count, 1);
    if (!v->split) return false;
    for (i = 0; ok && i < l->mesh_count; i++) {
        const tc_mesh *m = &l->meshes[i];
        uint32_t nt = m->index_count / 3, groups = 0, *tri_group, *map;
        int32_t *cells;
        if (!(m->surface.flags & TC_FLAG_TRANSPARENT) || !drawable(m) || nt < 2) continue;
        tri_group = (uint32_t *)tc_calloc(nt, sizeof(*tri_group));
        cells = (int32_t *)tc_calloc((size_t)nt * 3, sizeof(*cells));
        map = (uint32_t *)tc_calloc(m->vertex_count, sizeof(*map));
        if (!tri_group || !cells || !map) {
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
                for (g = 0; ok && g < groups; g++) ok = add_piece(v, m, i, tri_group, g, map);
                v->split[i] = 1;
            }
        }
        tc_free(tri_group);
        tc_free(cells);
        tc_free(map);
    }
    return ok;
}

/* The centre of each transparent mesh's vertex bounds (mesh space): what blended draws sort by. */
static bool mesh_centres(tc_viewer *v) {
    uint32_t i, k;
    if (!v->mesh_count) return true;
    v->mesh_centre = (tc_vec3 *)tc_calloc(v->mesh_count, sizeof(*v->mesh_centre));
    if (!v->mesh_centre) return false;
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
    }
    return true;
}

tc_result tc_viewer_init(tc_viewer *v, const tc_viewer_desc *desc) {
    const tc_level *l = desc->level;
    uint32_t i, k, n = 0;
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
    v->orbit_distance = 4.5f;
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

    if (!split_transparent(v)) {
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
    if (!mesh_centres(v)) {
        tc_viewer_free(v);
        return TC_ERR_OUT_OF_MEMORY;
    }

    v->play = play_create(v, desc);
    if (ai_model(v) && ai_model(v)->water_count && l->mesh_count) {
        /* Water materials: static meshes (the .ttlv groups them by material) with a triangle lying flat (within 5 cm)
         * at a water plane's height, centred inside one of its sector boxes. The renderers add the sheen to their
         * upward-facing fragments only (the falls and pool sides share the rainbow material). */
        const tc_ai_model *am = ai_model(v);
        v->mesh_water = (uint8_t *)tc_calloc(l->mesh_count, 1);
        for (i = 0; v->mesh_water && i < l->mesh_count; i++) {
            const tc_mesh *m = &l->meshes[i];
            uint32_t k, w, b;
            for (k = 0; k + 2 < m->index_count && !v->mesh_water[i]; k += 3) {
                const float *q0 = m->vertices[m->indices[k]].position, *q1 = m->vertices[m->indices[k + 1]].position,
                            *q2 = m->vertices[m->indices[k + 2]].position;
                tc_vec3 c = tc_v3((q0[0] + q1[0] + q2[0]) / 3, (q0[1] + q1[1] + q2[1]) / 3, (q0[2] + q1[2] + q2[2]) / 3);
                for (w = 0; w < am->water_count && !v->mesh_water[i]; w++) {
                    const tc_ai_water *wp = &am->water[w];
                    if (fabsf(q0[2] - wp->z) > 0.05f || fabsf(q1[2] - wp->z) > 0.05f || fabsf(q2[2] - wp->z) > 0.05f) continue;
                    for (b = 0; b < wp->box_count; b++) {
                        const tc_vec3 *bx = &wp->boxes[2 * b];
                        if (c.x >= bx[0].x && c.y >= bx[0].y && c.x <= bx[1].x && c.y <= bx[1].y) { v->mesh_water[i] = 1; break; }
                    }
                }
            }
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
    tc_free(v->blended);
    for (i = 0; i < v->piece_count; i++) {
        tc_free(v->pieces[i].vertices);
        tc_free(v->pieces[i].indices);
    }
    tc_free(v->pieces);
    tc_free(v->piece_origin);
    tc_free(v->split);
    tc_free(v->mesh_centre);
    tc_free(v->blended_depth);
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
static void look(tc_viewer *v, float dt) {
    tc_camera *c = &v->camera;
    float yaw_rate = fmaxf(-1, fminf(1, axis(v, TC_VA_LOOK_RIGHT, TC_VA_LOOK_LEFT) + v->look_x));
    float pitch_rate = fmaxf(-1, fminf(1, axis(v, TC_VA_LOOK_UP, TC_VA_LOOK_DOWN) + v->look_y));
    float pitch;
    c->yaw += yaw_rate * v->turn_speed * dt + v->mouse_dx * 0.003f;
    pitch = c->pitch + pitch_rate * v->turn_speed * dt - v->mouse_dy * 0.003f;
    c->pitch = fmaxf(-1.5f, fminf(1.5f, pitch));
    v->mouse_dx = v->mouse_dy = 0;
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
    v->camera.pitch = -0.3f;
}

void tc_viewer_toggle_ai(tc_viewer *v) {
    if (!tc_viewer_has_ai(v)) return;
    v->play->use_ai = !v->play->use_ai;
    v->play->have_pose = false; /* the tick blending starts over from where Ed is */
    v->play->tick_time = 0;
    v->ed_state = -1;
    v->ed_phase = 0;
}

bool tc_viewer_can_play(const tc_viewer *v) { return v->play != NULL; }

bool tc_viewer_has_ai(const tc_viewer *v) { return v->play && v->play->has_driver; }

bool tc_viewer_ai_active(const tc_viewer *v) { return v->playing && tc_viewer_has_ai(v) && v->play->use_ai; }

bool tc_viewer_status_live(const tc_viewer *v) { return tc_viewer_ai_active(v) || (v->light && v->light_scripted); }

const tc_player *tc_viewer_player(const tc_viewer *v) { return v->play ? &v->play->player : NULL; }

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
    if (!p->have_pose) {
        p->pose_prev = p->pose_cur, p->clip_prev = p->clip_cur, p->phase_prev = p->phase_cur;
        p->have_pose = true;
    }
}

/* Ed's AI and mechanics in ticks of TC_ED_AI_TICK (the game's 30 per second), then this frame's pose between the
 * last two ticks. The scripts see last frame's camera eye (it makes the direction keys camera-relative). */
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
        tc_ed_ai_step(&p->driver, TC_ED_AI_TICK, ai_inputs(v), v->move_x, v->move_y, v->camera.position);
        for (n = 0; n < p->driver.ai.sound_count && v->sound_count < TC_AI_MAX_SOUNDS; n++)
            v->sounds[v->sound_count++] = p->driver.ai.sounds[n];
        capture_pose(v);
    }
    t = p->tick_time / TC_ED_AI_TICK;
    p->pose_draw = pose_lerp(&p->pose_prev, &p->pose_cur, t);
    v->ed_clip = p->clip_cur;
    v->ed_state = -1;
    v->ed_loop = p->loop_cur;
    v->ed_phase = p->phase_cur;
    if (p->clip_prev == p->clip_cur && p->clip_cur >= 0) {
        float count = (float)v->level->ed.anims[p->clip_cur].sample_count, d = p->phase_cur - p->phase_prev;
        if (d < 0 && p->loop_cur) d += count; /* wrapped round the loop */
        /* Only a loop wraps: an ended state holds its last frame at phase == count (the portal step-in, Ed hidden),
         * which wrapped to 0 drew him whole again where the step-in began. */
        if (d >= 0 && d < count / 2) v->ed_phase = p->loop_cur ? fmodf(p->phase_prev + d * t, count) : p->phase_prev + d * t;
    }
}

static void update_play(tc_viewer *v, float dt) {
    tc_viewer_play *p = v->play;
    const tc_level *l = v->level;
    const tc_ed_anim *a;
    tc_vec3 target, back;
    float d, hit, rate;
    int i;
    look(v, dt);
    if (p->use_ai) {
        step_ai(v, v->paused ? 0 : dt); /* Ed's own scripts, at the game's tick */
    } else {
        tc_player_input in;
        bool walking_in = v->walk_in != 0;
        in.move = walking_in ? tc_v2(0, 0) : move_direction(v);
        /* Shift runs; the stick runs when pushed most of the way (0.8: the simple controller has only walk and run). */
        in.run = !walking_in && (held(v, TC_VA_FAST) || v->move_x * v->move_x + v->move_y * v->move_y > 0.64f);
        in.jump = !walking_in && held(v, TC_VA_JUMP);
        tc_player_step(&p->player, dt, &in);
        if (p->has_driver) simple_portals(v, dt);
    }
    /* Third-person orbit around Ed's chest (as drawn); pulled in when the level is in the way. */
    target = p->use_ai ? tc_v3(p->pose_draw.m[12], p->pose_draw.m[13], p->pose_draw.m[14] + 1.1f)
                       : tc_v3_add(p->player.position, tc_v3(0, 0, 1.1f));
    back = tc_v3_scale(tc_camera_forward(&v->camera), -1);
    d = v->orbit_distance;
    hit = raycast(p, target, back, d + 0.3f);
    if (hit >= 0) d = fmaxf(0.4f, hit - 0.3f);
    v->camera.position = tc_v3_add(target, tc_v3_scale(back, d));

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

void tc_viewer_update(tc_viewer *v, float dt) {
    v->sound_count = 0;
    if (v->phase == TC_VP_LOADING || v->phase == TC_VP_PENDING) look(v, 0); /* the level is about to change: Ed waits */
    else if (v->playing) update_play(v, dt);
    else fly(v, dt);
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
    float deg = roundf(v->camera.vertical_fov * 180 / TC_PI) + (dir > 0 ? 5.0f : -5.0f);
    v->camera.vertical_fov = fmaxf(30, fminf(100, deg)) * TC_PI / 180;
}

void tc_viewer_toggle_fog(tc_viewer *v) { v->fog_enabled = !v->fog_enabled; }

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
           (double)(v->camera.vertical_fov * 180 / TC_PI), extra ? extra : "");
    if (v->light_set_count > 1 && v->light_rate > 0)
        APPEND(", light %u\xe2\x86\x92%u", (unsigned)v->light_set + 1, (unsigned)v->light_next + 1);
    else if (v->light_set_count > 1)
        APPEND(", light %u/%u", (unsigned)v->light_set + 1, (unsigned)v->light_set_count);
    if (v->light_set_count > 1 && v->light && !v->light_scripted) APPEND("%s", " held");
    if (v->has_fog) APPEND("%s", v->fog_enabled ? ", fog on" : ", fog off");
    if (!v->playing && tc_viewer_can_play(v)) APPEND("%s", ", free-fly");
    if (v->playing && tc_viewer_has_ai(v) && !v->play->use_ai) APPEND("%s", ", simple controller");
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

/* The blended draws back to front: farthest centre from the eye first, ties in list order (a stable insertion
 * sort: a few dozen to a few hundred draws). Left unsorted if the depth buffer can't grow. */
static void sort_blended(tc_viewer *v, uint32_t n) {
    uint32_t i, j;
    tc_vec3 eye = v->camera.position;
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
        const float *m = d->model.m;  /* column-major */
        tc_vec3 w = tc_v3(m[0] * c.x + m[4] * c.y + m[8] * c.z + m[12], m[1] * c.x + m[5] * c.y + m[9] * c.z + m[13],
                          m[2] * c.x + m[6] * c.y + m[10] * c.z + m[14]);
        v->blended_depth[i] = tc_v3_length_sq(tc_v3_sub(w, eye));
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

static void add_draw(tc_viewer *v, draw_list *dl, uint32_t mesh, const tc_mat4 *model) {
    const tc_mesh *m = v->meshes[mesh];
    tc_view_draw *d;
    if (!drawable(m)) return;
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
    d->model = *model;
}

const tc_view_draw *tc_viewer_draws(tc_viewer *v, uint32_t *count, uint32_t *opaque_count) {
    const tc_level *l = v->level;
    const tc_mat4 identity = tc_mat4_identity();
    draw_list dl = {0, 0};
    uint32_t i, k, first = v->perso_first;
    float t = v->animation_time;
    v->ed_draw_count = 0;

    for (i = 0; i < l->mesh_count; i++)
        if (!v->split || !v->split[i]) add_draw(v, &dl, i, &identity);  /* split ones: as their pieces */
    for (i = 0; i < v->piece_count; i++) add_draw(v, &dl, v->piece_first + i, &identity);

    for (i = 0; i < l->perso_count; first += l->persos[i].part_count, i++) {
        const tc_perso *p = &l->persos[i];
        uint32_t nch = p->channel_count, base;
        tc_mat4 world;
        tc_sample s;
        if (p->sample_count == 0 || (v->playing && (int32_t)i == l->ed.static_perso)) continue;
        /* Baked samples loop; blend each channel between neighbouring samples so motion is smooth at any display
         * rate and playback speed. Visibility follows the earlier sample. */
        world = tc_mat4_from34(&p->world);
        s = tc_sample_at_time(t, p->sample_rate, p->sample_count);
        base = s.s0 * nch;
        for (k = 0; k < p->part_count; k++) {
            const tc_part *part = &p->parts[k];
            tc_mat4 m, model;
            if (part->channel >= nch || p->shown[base + part->channel] != part->object) continue;
            /* Lerping full matrices is fine for samples this close together; skipped across a part swap. */
            m = tc_channel_matrix(p->transforms, p->shown, nch, s, part->channel);
            model = tc_mat4_mul(&world, &m);
            add_draw(v, &dl, first + k, &model);
        }
    }

    if (v->playing && v->play && v->ed_clip >= 0) {
        const tc_ed_anim *a = &l->ed.anims[v->ed_clip];
        tc_sample s = tc_sample_at_phase(v->ed_phase, a->sample_count, v->ed_loop);
        uint32_t nch = a->channel_count, ch;
        /* The skier's board tilts with the slope. */
        tc_mat4 model = v->play->use_ai && v->play->has_driver ? v->play->pose_draw
                                                               : tc_player_model_matrix(&v->play->player);
        for (ch = 0; ch < nch; ch++) {
            uint16_t obj = a->shown[s.s0 * nch + ch];
            tc_mat4 m, world;
            if (obj == TC_HIDDEN) continue;
            m = tc_channel_matrix(a->transforms, a->shown, nch, s, ch);
            world = tc_mat4_mul(&model, &m);
            for (k = 0; k < l->ed.part_count; k++)
                if (l->ed.parts[k].object == obj) {
                    add_draw(v, &dl, v->ed_first + k, &world);
                    v->ed_draw_count++;
                }
        }
    }

    *opaque_count = dl.count;
    sort_blended(v, dl.blended_count);
    if (dl.blended_count && reserve(&v->draws, &v->draw_cap, dl.count + dl.blended_count)) {
        memcpy(v->draws + dl.count, v->blended, dl.blended_count * sizeof(*v->draws));
        dl.count += dl.blended_count;
    }
    *count = dl.count;
    return v->draws;
}

void tc_viewer_frame(const tc_viewer *v, float aspect, tc_view_frame *out) {
    const tc_camera *c = &v->camera;
    tc_vec3 right = tc_camera_right(c);
    bool fog_on = v->fog_enabled && v->has_fog;
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
    if (v->has_fog) {
        out->fog_near = v->fog_near;
        out->fog_far = v->fog_far;
        memcpy(out->fog_color, v->fog_color, sizeof(out->fog_color));
    }
    out->eye = c->position;
    out->cam_right = right;
    out->cam_up = tc_v3_cross(right, tc_camera_forward(c));
    if (fog_on) {
        memcpy(out->clear_color, v->fog_color, sizeof(out->clear_color));
    } else {
        out->clear_color[0] = 0.08f;
        out->clear_color[1] = 0.09f;
        out->clear_color[2] = 0.14f;
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
