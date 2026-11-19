/* The viewer's side of portals and level changes (docs/PORTALS.md; the game's: tc_game_portals.c): the screen
 * transitions' overlays and textures, the camera moves the game asks for, the gunsight. */
#include "tc_viewer_internal.h"

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
    if (!v->playing || !v->game || !v->game->main.has_driver || !v->cam.first_person || v->crosshair_effect < 0 ||
        v->game->phase != TC_GP_PLAY)
        return false;
    a = &v->game->main.driver.ai;
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
    return v->game && v->game->main.has_driver ? v->game->main.driver.ai.model : NULL;
}

tc_game_view tcv_game_view(const tc_viewer *v) {
    tc_game_view w;
    w.vertical_fov = v->camera.vertical_fov;
    w.orbit_distance = v->orbit_distance;
    w.closeup = v->cam.closeup;
    return w;
}

/* The orbit camera looking at `at` (Ed's place when the game asked, 1.1 up) from `eye` (PROC_8's camera position,
 * PROC_29's camera place). */
static void aim_camera(tc_viewer *v, tc_vec3 eye, tc_vec3 at) {
    tc_vec3 d = tc_v3_sub(at, eye);
    float h = sqrtf(d.x * d.x + d.y * d.y);
    if (h < 1e-3f) return;
    tcv_camera_settle(v);
    v->camera.yaw = atan2f(d.x, d.y);
    v->camera.pitch = fmaxf(-1.5f, fminf(1.5f, atan2f(d.z, h)));
}

void tcv_take_camera_requests(tc_viewer *v) {
    tc_game *g = v->game;
    uint32_t k;
    if (!g) return;
    for (k = 0; k < g->camera_request_count; k++) {
        const tc_game_camera_request *r = &g->camera_requests[k];
        if (r->kind == TC_GAME_CAMERA_AIM) aim_camera(v, r->eye, r->at);
        else if (r->kind == TC_GAME_CAMERA_YAW) {
            tcv_camera_settle(v);
            v->camera.yaw = r->yaw;
        }
    }
    g->camera_request_count = 0;
}

/* After Ed's step: the game's portals and transitions (tc_game_update_portals), then the camera moves they asked for. */
void tcv_update_portals(tc_viewer *v, float dt) {
    tc_game_view view;
    if (!v->game) return;
    view = tcv_game_view(v);
    tc_game_update_portals(v->game, dt, &view);
    tcv_take_camera_requests(v);
}

uint32_t tc_viewer_effect_count(const tc_viewer *v) { return v->effect_count; }

const char *tc_viewer_effect_texture(const tc_viewer *v, uint32_t i) { return i < v->effect_count ? v->effects[i] : NULL; }

const tc_viewer_carry *tc_viewer_pending_level(const tc_viewer *v) { return v->game ? tc_game_pending_level(v->game) : NULL; }

void tc_viewer_overlay(const tc_viewer *v, tc_view_overlay *out) {
    const tc_game *g = v->game;
    const tc_transition *t;
    memset(out, 0, sizeof(*out));
    out->texture = -1;
    if (!g) return;
    t = &g->transition;
    if (g->picture_left > 0 && g->phase == TC_GP_PLAY) {
        out->kind = TC_VO_PICTURE;
        out->picture = g->picture;
        return;
    }
    switch (g->phase) {
    case TC_GP_BLACK: out->kind = TC_VO_BLACK; return;
    case TC_GP_LOADING: case TC_GP_PENDING:
        out->kind = TC_VO_LOADING;
        out->progress = fminf(1, g->loading_time / TC_GAME_LOADING_SECONDS);
        out->level = g->carry ? g->carry->level : NULL;
        return;
    case TC_GP_COVER: case TC_GP_REVEAL: {
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
