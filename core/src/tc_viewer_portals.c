/* The viewer's portals, level changes, screen transitions and overlays, the gunsight (docs/PORTALS.md). */
#include "tc_viewer_internal.h"

int tcv_same_text(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

void tcv_copy_text(char *dst, size_t n, const char *src) {
    size_t i;
    for (i = 0; src && src[i] && i + 1 < n; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* The distinct effect textures of the model's transitions, then the gunsight (tc_viewer_crosshair). */
bool tcv_collect_effects(tc_viewer *v, const tc_ai_model *m) {
    uint32_t i, k, j, n = 0;
    for (i = 0; m->has_transitions && i < 4; i++) n += m->transitions.effects[i].variant_count;
    v->effects = (const char **)tc_calloc(n + 1, sizeof(*v->effects));
    if (!v->effects) return false;
    for (i = 0; m->has_transitions && i < 4; i++)
        for (k = 0; k < m->transitions.effects[i].variant_count; k++) {
            const char *t = m->transitions.effects[i].variants[k];
            for (j = 0; j < v->effect_count && strcmp(v->effects[j], t) != 0; j++) {}
            if (t && j == v->effect_count) v->effects[v->effect_count++] = t;
        }
    v->crosshair_effect = (int32_t)v->effect_count;
    v->effects[v->effect_count++] = TC_VIEWER_CROSSHAIR_TEXTURE;
    return true;
}

/* Whether behaviour b of the model fires a projectile (a META 11 node in its schedule or rules). */
static bool behaviour_shoots(const tc_ai_model *m, uint32_t b) {
    const tc_ai_behaviour *be;
    int32_t k;
    if (!m || b >= (uint32_t)m->normal_count + m->reflex_count) return false;
    be = &m->behaviours[b];
    for (k = be->schedule >= 0 ? -1 : 0; k < (int32_t)be->script_count; k++) {
        uint32_t n = k < 0 ? m->scripts[be->schedule] : m->scripts[be->first_script + (uint32_t)k];
        for (; n < m->node_count && m->nodes[n].type != TC_AI_END; n++)
            if (m->nodes[n].type == TC_AI_META && m->nodes[n].param == 11) return true;
    }
    return false;
}

bool tc_viewer_crosshair(const tc_viewer *v, float aspect, tc_view_overlay *out) {
    const tc_ai_perso *a;
    float h = TC_VIEWER_CROSSHAIR_SIZE * 0.5f, w = aspect > 0 ? h / aspect : h;
    memset(out, 0, sizeof(*out));
    out->texture = -1;
    if (!v->playing || !v->play || !v->play->main.has_driver || !v->cam.first_person || v->crosshair_effect < 0 ||
        v->phase != TC_VP_PLAY)
        return false;
    a = &v->play->main.driver.ai;
    if (a->normal.behaviour == 0xFFFF || !behaviour_shoots(a->model, a->normal.behaviour)) return false;
    out->kind = TC_VO_SPRITE;
    out->texture = v->crosshair_effect;
    out->corners[0][0] = 0.5f - w, out->corners[0][1] = 0.5f - h;
    out->corners[1][0] = 0.5f + w, out->corners[1][1] = 0.5f - h;
    out->corners[2][0] = 0.5f - w, out->corners[2][1] = 0.5f + h;
    out->corners[3][0] = 0.5f + w, out->corners[3][1] = 0.5f + h;
    out->uv[1][0] = out->uv[3][0] = 1;
    out->uv[2][1] = out->uv[3][1] = 1;
    return true;
}

const tc_ai_model *tcv_ai_model(const tc_viewer *v) {
    return v->play && v->play->main.has_driver ? v->play->main.driver.ai.model : NULL;
}

/* Starts the cover or the reveal; without transition data, it is done at once. */
void tcv_start_transition(tc_viewer *v, bool reveal) {
    const tc_ai_model *m = tcv_ai_model(v);
    if (m && m->has_transitions && tc_transition_start(&v->transition, &m->transitions, reveal, &v->rng)) {
        v->phase = reveal ? TC_VP_REVEAL : TC_VP_COVER;
        return;
    }
    memset(&v->transition, 0, sizeof(v->transition));
    v->phase = reveal ? TC_VP_PLAY : TC_VP_BLACK;
    if (!reveal && v->play) v->play->main.driver.screen_covered = true;
}

/* The orbit camera looking at Ed from `eye` (PROC_8's camera position, PROC_29's camera place). */
static void aim_camera(tc_viewer *v, tc_vec3 eye) {
    tc_vec3 d = tc_v3_sub(tc_v3_add(v->play->main.player.position, tc_v3(0, 0, 1.1f)), eye);
    float h = sqrtf(d.x * d.x + d.y * d.y);
    if (h < 1e-3f) return;
    v->camera.yaw = atan2f(d.x, d.y);
    v->camera.pitch = fmaxf(-1.5f, fminf(1.5f, atan2f(d.z, h)));
}

/* Arriving through a portal: Ed's dsgvars from the last level, the arrival place, then the reveal. */
void tcv_arrive(tc_viewer *v, const tc_viewer_carry *c) {
    tc_viewer_play *p = v->play;
    const tc_ai_model *m = tcv_ai_model(v);
    uint16_t i;
    if (!m) return;
    if (c->dsg_size && c->dsg_size == m->dsg.size && p->main.driver.ai.dsg) memcpy(p->main.driver.ai.dsg, c->dsg, c->dsg_size);
    if (c->has_progress && p->has_cast) { /* before the cast's first tick: its scripts' first tests see them */
        tc_ai_globals *g = &p->cast.globals;
        g->lives = c->lives;
        g->powers = c->powers;
        memcpy(g->values, c->values, sizeof g->values);
    }
    for (i = 0; c->place[0] && i < m->place_count; i++)
        if (m->places[i].name && tcv_same_text(m->places[i].name, c->place))
            tc_player_teleport(&p->main.player, m->places[i].position, m->places[i].heading);
    for (i = 0; c->camera_place[0] && i < m->place_count; i++)
        if (m->places[i].name && tcv_same_text(m->places[i].name, c->camera_place)) aim_camera(v, m->places[i].position);
    tcv_start_transition(v, true);
}

/* After Ed's step: his portal events drive the phases (docs/PORTALS.md). */
void tcv_update_portals(tc_viewer *v, float dt) {
    tc_ed_ai *e;
    tc_ed_ai_events *ev;
    if (!v->play || !v->play->main.has_driver) return;
    e = &v->play->main.driver;
    ev = &e->events;
    if (ev->cover) {
        ev->cover = false;
        tcv_start_transition(v, false);
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
            tcv_start_transition(v, true);
        }
    }
    if (ev->level) {
        tc_viewer_carry *c = v->carry ? v->carry : (tc_viewer_carry *)tc_calloc(1, sizeof(*c));
        if (c) {
            const tc_ai_model *m = e->ai.model;
            /* A hint scene (docs/ENGINE.md "Walkable scenes"): Ed walks in himself, at his start there; its speaker's script
             * hands over to the main level. Without SPCH (an old export) it is passed by, as before. */
            const char *after = v->level->speech.dialogue_count ? NULL : tc_level_after_cutscene(ev->level);
            bool scene = tc_level_after_cutscene(ev->level) != NULL;
            tcv_copy_text(c->level, sizeof(c->level), after ? after : ev->level);
            tcv_copy_text(c->place, sizeof(c->place), scene ? NULL : ev->place);
            tcv_copy_text(c->camera_place, sizeof(c->camera_place), scene ? NULL : ev->camera_place);
            c->dsg_size = m->dsg.size <= TC_VIEWER_CARRY_DSG && e->ai.dsg ? m->dsg.size : 0;
            if (c->dsg_size) memcpy(c->dsg, e->ai.dsg, c->dsg_size);
            c->vertical_fov = v->camera.vertical_fov;
            c->orbit_distance = v->orbit_distance;
            c->closeup = v->cam.closeup;
            c->has_progress = v->play->has_cast;
            if (c->has_progress) tc_cast_level_save(&v->play->cast); /* the way out: the level's saved values */
            if (c->has_progress) {
                const tc_ai_globals *g = &v->play->cast.globals;
                c->lives = g->lives;
                c->powers = g->powers;
                memcpy(c->values, g->values, sizeof c->values);
            }
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
