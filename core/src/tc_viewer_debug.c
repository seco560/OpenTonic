/* The viewer's debug menu (tc_debug.h; F4) and the HUD's glue (tc_hud.h). */
#include "tc_viewer_internal.h"

/* The game's state the menu shows and sets: the cast's (Ed's scripts share it), else Ed's own, else none. */
static tc_ai_globals *debug_globals(tc_viewer *v) {
    tc_game *p = v->game;
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

/* The HUD is the game's (tc_game.hud): a level that can't be played has none. */
void tc_viewer_hud_waits(tc_viewer *v, bool on) {
    if (on) REC_DOOR(TC_RD_HUD, 0, 0);
    if (v->game) v->game->hud_waits = on;
}

tc_result tc_viewer_set_hud(tc_viewer *v, const void *data, size_t size) {
    tc_hud *h = v->game ? &v->game->hud : NULL;
    tc_result r;
    if (!h) return TC_OK; /* nothing to show it over */
    r = tc_hud_load(h, data, size);
    if (r == TC_OK && v->level && v->level->edition == 0) tc_viewer_hud_waits(v, true);
    /* outside the retail game (the Special Edition's levels; the imported ones, which have no cast): its extensions */
    h->left_only = v->level && (v->level->edition != 0 || !v->game->has_cast);
    return r;
}

void tc_viewer_set_hud_auto_hide(tc_viewer *v, bool on) {
    REC_DOOR(TC_RD_HUD_AUTO_HIDE, on ? 1 : 0, 0);
    if (v->game) v->game->hud.auto_hide = on;
}

bool tc_viewer_hud_auto_hide(const tc_viewer *v) { return v->game && v->game->hud.auto_hide; }

bool tc_viewer_hud(tc_viewer *v, float aspect, tc_view_hud *out) {
    tc_game *p = v->game;
    tc_hud *hud;
    uint8_t points[TC_AI_HUD_SLOTS] = {0}, max[TC_AI_HUD_SLOTS] = {0};
    const tc_ai_globals *g;
    uint32_t k;
    if (!p) return false;
    hud = &p->hud;
    if (!hud->loaded || !v->playing || !p->main.has_driver || !(p->has_cast || hud->left_only) || !p->use_ai || v->tape)
        return false;
    if (tc_game_hud_covered(p)) return false;
    /* Port (the user, 2026-11-13, a stand-in for a photo mode): no HUD while paused, live (P) or in a paused playback
     * (tc_replay_pause): both set the pause's look round. It comes back as it was. The game menu's pause shows it, as
     * the original's does (2026-11-19: game_menu_hud in tc_viewer.c slides it in). */
    if ((v->paused || v->inspecting) && !v->game_menu) return false;
    g = p->has_cast ? &p->cast.globals : NULL;
    for (k = 0; g && k < TC_AI_HUD_SLOTS; k++) { /* a boss's life bar: its perso's hit points (GAM_00418d70) */
        const tc_ai_hud_slot *s = &g->hud[k];
        int32_t hp = 0, top = 0;
        if (!s->active || s->kind != TC_AI_HUD_BOSS) continue;
        if (!tc_cast_hit_points(&p->cast, s->value > 0 ? s->value : s->owner, &hp, &top)) continue;
        points[k] = (uint8_t)(hp < 0 ? 0 : hp > 255 ? 255 : hp);
        max[k] = (uint8_t)(top < 0 ? 0 : top > 255 ? 255 : top);
    }
    tc_hud_build(hud, g, points, max, true, aspect > 0 ? aspect : 4.0f / 3);
    { /* custom gameplay's start text (tcx.h: the SSX courses have no scripts to show the game's), in FontLips as
       * Totalski's scripts show it */
        tc_ai_hud_slot extra;
        if (tcx_hud_slot(p->custom, &extra)) {
            extra.font = tc_hud_font_rank(hud, "FontLips");
            tc_hud_add_slot(hud, &extra, aspect > 0 ? aspect : 4.0f / 3);
        }
    }
    if (!hud->quad_count) return false;
    out->rgba = hud->data.atlas;
    out->width = hud->data.atlas_width;
    out->height = hud->data.atlas_height;
    out->quads = hud->quads;
    out->count = hud->quad_count;
    return true;
}

tc_result tc_viewer_set_survey(tc_viewer *v, const void *data, size_t size) {
    return tc_debug_load_survey(&v->debug, data, size);
}

/* "Reload the level": a level change into this level, at Ed's start, with the progress as the menu left it (the
 * portals' way out: tc_viewer_pending_level; tc_game_arrive hands the progress to the new viewer's cast). */
static void debug_reload(tc_viewer *v) {
    tc_game_view view;
    if (!v->game) return;
    view = tcv_game_view(v);
    tc_game_reload(v->game, &view);
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
    if (!v->debug.open || !tc_debug_render(&v->debug, debug_globals(v), v->game ? v->game->main.level_name : "")) return false;
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
