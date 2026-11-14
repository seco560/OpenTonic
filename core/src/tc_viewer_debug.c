/* The viewer's debug menu (tc_debug.h; F4) and the HUD's glue (tc_hud.h). */
#include "tc_viewer_internal.h"

/* The game's state the menu shows and sets: the cast's (Ed's scripts share it), else Ed's own, else none. */
static tc_ai_globals *debug_globals(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    if (!p || !p->main.has_driver) return NULL;
    if (p->has_cast) return &p->cast.globals;
    return p->main.driver.ai.globals ? p->main.driver.ai.globals : &p->main.driver.ai.own_globals;
}

/* The held actions as the menu's buttons: the move keys, the look keys and the left stick all steer it. */
static uint32_t debug_buttons(const tc_viewer *v) {
    uint32_t b = 0;
    if (held(v, TC_VA_FORWARD) || held(v, TC_VA_LOOK_UP) || v->move_y > 0.5f) b |= TC_DEBUG_UP;
    if (held(v, TC_VA_BACK) || held(v, TC_VA_LOOK_DOWN) || v->move_y < -0.5f) b |= TC_DEBUG_DOWN;
    if (held(v, TC_VA_LEFT) || held(v, TC_VA_LOOK_LEFT) || v->move_x < -0.5f) b |= TC_DEBUG_LEFT;
    if (held(v, TC_VA_RIGHT) || held(v, TC_VA_LOOK_RIGHT) || v->move_x > 0.5f) b |= TC_DEBUG_RIGHT;
    if (held(v, TC_VA_JUMP) || held(v, TC_VA_ACTION)) b |= TC_DEBUG_OK;
    if (held(v, TC_VA_FAST)) b |= TC_DEBUG_FAST;
    return b;
}

void tc_viewer_toggle_debug(tc_viewer *v) {
    if (v->tape) return; /* an original demo: nothing of the game runs */
    REC_DOOR(TC_RD_DEBUG, 0, 0);
    tc_debug_set_open(&v->debug, !v->debug.open, debug_buttons(v));
    v->skip_held = v->held & SKIP_MASK; /* what is held now is no fresh press for the game */
}

bool tc_viewer_debug_open(const tc_viewer *v) { return v->debug.open; }

void tc_viewer_hud_waits(tc_viewer *v, bool on) {
    if (on) REC_DOOR(TC_RD_HUD, 0, 0);
    v->hud_waits = on;
}

tc_result tc_viewer_set_hud(tc_viewer *v, const void *data, size_t size) {
    tc_result r = tc_hud_load(&v->hud, data, size);
    if (r == TC_OK && v->level && v->level->edition == 0) tc_viewer_hud_waits(v, true);
    /* outside the retail game (the Special Edition's levels; the imported ones, which have no cast): its extensions */
    v->hud.left_only = v->level && (v->level->edition != 0 || !(v->play && v->play->has_cast));
    return r;
}

void tc_viewer_set_hud_auto_hide(tc_viewer *v, bool on) { v->hud.auto_hide = on; }

bool tc_viewer_hud_auto_hide(const tc_viewer *v) { return v->hud.auto_hide; }

bool tc_viewer_hud(tc_viewer *v, float aspect, tc_view_hud *out) {
    tc_viewer_play *p = v->play;
    uint8_t points[TC_AI_HUD_SLOTS] = {0}, max[TC_AI_HUD_SLOTS] = {0};
    const tc_ai_globals *g;
    uint32_t k;
    if (!v->hud.loaded || !v->playing || !p || !p->main.has_driver || !(p->has_cast || v->hud.left_only) || !p->use_ai ||
        v->tape)
        return false;
    if (tcv_hud_covered(v)) return false;
    /* Port (the user, 2026-11-13, a stand-in for a photo mode): no HUD while paused, live (P) or in a paused playback
     * (tc_replay_pause): both set the pause's look round. It comes back as it was. */
    if (v->paused || v->inspecting) return false;
    g = p->has_cast ? &p->cast.globals : NULL;
    for (k = 0; g && k < TC_AI_HUD_SLOTS; k++) { /* a boss's life bar: its perso's hit points (GAM_00418d70) */
        const tc_ai_hud_slot *s = &g->hud[k];
        int32_t hp = 0, top = 0;
        if (!s->active || s->kind != TC_AI_HUD_BOSS) continue;
        if (!tc_cast_hit_points(&p->cast, s->value > 0 ? s->value : s->owner, &hp, &top)) continue;
        points[k] = (uint8_t)(hp < 0 ? 0 : hp > 255 ? 255 : hp);
        max[k] = (uint8_t)(top < 0 ? 0 : top > 255 ? 255 : top);
    }
    if (!tc_hud_build(&v->hud, g, points, max, true, aspect > 0 ? aspect : 4.0f / 3)) return false;
    out->rgba = v->hud.data.atlas;
    out->width = v->hud.data.atlas_width;
    out->height = v->hud.data.atlas_height;
    out->quads = v->hud.quads;
    out->count = v->hud.quad_count;
    return true;
}

tc_result tc_viewer_set_survey(tc_viewer *v, const void *data, size_t size) {
    return tc_debug_load_survey(&v->debug, data, size);
}

/* "Reload the level": a level change into this level, at Ed's start, with the progress as the menu left it (the
 * portals' way out: tc_viewer_pending_level; arrive() hands the progress to the new viewer's cast). */
static void debug_reload(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    tc_viewer_carry *c;
    const tc_ai_globals *g = debug_globals(v);
    if (!p || !p->main.level_name[0] || v->phase == TC_VP_LOADING || v->phase == TC_VP_PENDING) return;
    c = v->carry ? v->carry : (tc_viewer_carry *)tc_calloc(1, sizeof(*c));
    if (!c) return;
    memset(c, 0, sizeof(*c));
    if (p->has_cast) tc_cast_level_save(&p->cast); /* as on any way out of the level */
    tcv_copy_text(c->level, sizeof(c->level), p->main.level_name);
    c->vertical_fov = v->camera.vertical_fov;
    c->orbit_distance = v->orbit_distance;
    c->closeup = v->cam.closeup;
    if (g) {
        c->has_progress = 1;
        c->lives = g->lives;
        c->powers = g->powers;
        memcpy(c->values, g->values, sizeof c->values);
    }
    v->carry = c;
    v->loading_time = 0;
    v->phase = p->main.has_driver ? TC_VP_LOADING : TC_VP_PENDING; /* update_portals runs the loading screen's time */
}

void tcv_debug_frame(tc_viewer *v, float dt) {
    const uint32_t b = debug_buttons(v);
    tc_debug_input(&v->debug, b, dt, debug_globals(v));
    v->mouse_dx = v->mouse_dy = 0; /* the mouse does not turn the camera behind the menu */
    v->skip_held = v->held & SKIP_MASK;
    if (v->debug.reload) {
        v->debug.reload = false;
        tc_debug_set_open(&v->debug, false, b);
        debug_reload(v);
    }
}

bool tc_viewer_debug_panel(tc_viewer *v, float aspect, tc_view_speech_panel *out) {
    const float shape = (float)TC_DEBUG_PANEL_W / (float)TC_DEBUG_PANEL_H;
    float h = 0.92f, w;
    if (!v->debug.open || !tc_debug_render(&v->debug, debug_globals(v), v->play ? v->play->main.level_name : "")) return false;
    if (aspect <= 0) aspect = 4.0f / 3;
    w = h * shape / aspect;
    if (w > 0.96f) w = 0.96f, h = w * aspect / shape; /* a narrow window: fit the width, keep the shape */
    out->rgba = v->debug.pixels;
    out->width = TC_DEBUG_PANEL_W;
    out->height = TC_DEBUG_PANEL_H;
    out->version = v->debug.version;
    out->rect[0] = 0.5f - w / 2;
    out->rect[1] = 0.5f - h / 2;
    out->rect[2] = 0.5f + w / 2;
    out->rect[3] = 0.5f + h / 2;
    return true;
}
