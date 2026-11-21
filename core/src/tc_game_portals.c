/* The game session's screen transitions, level changes and arrivals (tc_game.h; docs/PORTALS.md). Moved out of
 * tc_viewer_portals.c and tc_viewer_debug.c on 2026-11-19 as it was; the overlay and the camera's moves stay the
 * viewer's (it takes them as tc_game_camera_request). */
#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_game.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_cam.h"

static const tc_ai_model *ed_model(const tc_game *g) { return g->main.has_driver ? g->main.driver.ai.model : NULL; }

/* The camera turned to Ed (where he is now) from `eye`, asked of the host. */
static void ask_aim(tc_game *g, tc_vec3 eye) {
    tc_game_camera_request *r;
    if (g->camera_request_count >= TC_GAME_MAX_CAMERA_REQUESTS) return;
    r = &g->camera_requests[g->camera_request_count++];
    memset(r, 0, sizeof(*r));
    r->kind = TC_GAME_CAMERA_AIM;
    r->eye = eye;
    r->at = tc_v3_add(g->main.player.position, tc_v3(0, 0, 1.1f));
}

static void ask_yaw(tc_game *g, float yaw) {
    tc_game_camera_request *r;
    if (g->camera_request_count >= TC_GAME_MAX_CAMERA_REQUESTS) return;
    r = &g->camera_requests[g->camera_request_count++];
    memset(r, 0, sizeof(*r));
    r->kind = TC_GAME_CAMERA_YAW;
    r->yaw = yaw;
}

void tc_game_start_transition(tc_game *g, bool reveal) {
    const tc_ai_model *m = ed_model(g);
    if (m && m->has_transitions && tc_transition_start(&g->transition, &m->transitions, reveal, &g->rng)) {
        g->phase = reveal ? TC_GP_REVEAL : TC_GP_COVER;
        return;
    }
    memset(&g->transition, 0, sizeof(g->transition));
    g->phase = reveal ? TC_GP_PLAY : TC_GP_BLACK;
    if (!reveal) g->main.driver.screen_covered = true;
}

void tc_game_arrive(tc_game *g, const tc_game_carry *c) {
    const tc_ai_model *m = ed_model(g);
    uint16_t i;
    if (!m) return;
    if (c->dsg_size && c->dsg_size == m->dsg.size && g->main.driver.ai.dsg) memcpy(g->main.driver.ai.dsg, c->dsg, c->dsg_size);
    if (c->clock_ms) g->main.driver.ai.time_ms = c->clock_ms; /* his timestamps' clock goes on (tc_game_carry.clock_ms) */
    if (c->has_progress && g->has_cast) { /* before the cast's first tick: its scripts' first tests see them */
        tc_ai_globals *gl = &g->cast.globals;
        gl->lives = c->lives;
        gl->powers = c->powers;
        gl->real_story = c->real_story != 0;
        memcpy(gl->values, c->values, sizeof gl->values);
    } else if (c->has_progress && !g->carry) { /* a level without a cast (an imported one: the SSX courses): nothing here
                                                * holds the progress, so it is kept for the way out (custom.py's arches) */
        tc_game_carry *keep = (tc_game_carry *)tc_calloc(1, sizeof(*keep));
        if (keep) {
            keep->has_progress = true;
            keep->lives = c->lives;
            keep->powers = c->powers;
            keep->real_story = c->real_story;
            memcpy(keep->values, c->values, sizeof keep->values);
            g->carry = keep;
        }
    }
    for (i = 0; c->place[0] && i < m->place_count; i++)
        if (m->places[i].name && tc_same_text(m->places[i].name, c->place))
            tc_player_teleport(&g->main.player, m->places[i].position, m->places[i].heading);
    for (i = 0; c->camera_place[0] && i < m->place_count; i++)
        if (m->places[i].name && tc_same_text(m->places[i].name, c->camera_place)) ask_aim(g, m->places[i].position);
    if (c->has_checkpoint) { /* a saved game: back where its save point left him (tc_save.h) */
        tc_vec3 at = tc_v3(c->checkpoint[0], c->checkpoint[1], c->checkpoint[2]);
        tc_player_teleport(&g->main.player, at, c->checkpoint_heading);
        if (g->has_cast) {
            g->cast.globals.has_checkpoint = true;
            g->cast.globals.checkpoint = at;
            g->cast.globals.checkpoint_heading = c->checkpoint_heading;
        }
        ask_yaw(g, c->checkpoint_heading); /* behind him (the follow camera's yaw is his heading) */
    }
    if (c->fix_dsg_size && g->has_cast) { /* the fix persos' dsgvars as the last level left them */
        uint32_t size;
        uint8_t *dsg = tc_cast_fix_dsg(&g->cast, 2, &size);
        if (dsg && size == c->fix_dsg_size) memcpy(dsg, c->fix_dsg, size);
    }
    if (c->hit_points_max > 0) {
        tc_ai_perso *a = &g->main.driver.ai;
        a->hit_points_max = c->hit_points_max;
        a->hit_points = c->hit_points > 0 && c->hit_points <= c->hit_points_max ? c->hit_points : c->hit_points_max;
    }
    tc_game_start_transition(g, true);
}

void tc_game_carry_fix(const tc_game *g, tc_game_carry *c) {
    uint32_t size = 0;
    uint8_t *dsg = g->has_cast ? tc_cast_fix_dsg((tc_cast *)&g->cast, 2, &size) : NULL;
    if (dsg && size <= TC_GAME_CARRY_FIX_DSG) {
        c->fix_dsg_size = size;
        memcpy(c->fix_dsg, dsg, size);
    } else if (g->carry && g->carry->fix_dsg_size) {
        if (g->carry != c) {
            c->fix_dsg_size = g->carry->fix_dsg_size;
            memcpy(c->fix_dsg, g->carry->fix_dsg, c->fix_dsg_size);
        }
    } else c->fix_dsg_size = 0;
}

void tc_game_update_portals(tc_game *g, float dt, const tc_game_view *view) {
    tc_ed_ai *e;
    tc_ed_ai_events *ev;
    if (!g->main.has_driver) return;
    e = &g->main.driver;
    ev = &e->events;
    if (ev->cover) {
        ev->cover = false;
        tc_game_start_transition(g, false);
    }
    if (g->phase == TC_GP_COVER && tc_transition_step(&g->transition, dt)) {
        g->phase = TC_GP_BLACK;
        e->screen_covered = true;
    }
    if (ev->teleported) {
        ev->teleported = false;
        if (ev->camera_set) ask_aim(g, ev->camera);
        ev->camera_set = false;
        if (g->phase == TC_GP_BLACK) { /* a teleport behind the cover: reveal; one in plain view just happens */
            e->screen_covered = false;
            tc_game_start_transition(g, true);
        }
    }
    if (ev->level) {
        tc_game_carry *c = g->carry ? g->carry : (tc_game_carry *)tc_calloc(1, sizeof(*c));
        if (c) {
            const tc_ai_model *m = e->ai.model;
            /* A hint scene (docs/ENGINE.md "Walkable scenes"): Ed walks in himself, at his start there; its speaker's script
             * hands over to the main level. Without SPCH (an old export) it is passed by, as before. */
            const char *after = g->level->speech.dialogue_count ? NULL : tc_level_after_cutscene(ev->level);
            bool scene = tc_level_after_cutscene(ev->level) != NULL;
            tc_copy_text(c->level, sizeof(c->level), after ? after : ev->level);
            tc_copy_text(c->place, sizeof(c->place), scene ? NULL : ev->place);
            tc_copy_text(c->camera_place, sizeof(c->camera_place), scene ? NULL : ev->camera_place);
            c->dsg_size = m->dsg.size <= TC_GAME_CARRY_DSG && e->ai.dsg ? m->dsg.size : 0;
            if (c->dsg_size) memcpy(c->dsg, e->ai.dsg, c->dsg_size);
            c->clock_ms = e->ai.time_ms;
            c->vertical_fov = view->vertical_fov;
            c->orbit_distance = TC_CAM_PORTAL_DISTANCE; /* not the wheel's zoom: a fixed close start (tc_cam.h) */
            c->closeup = view->closeup;
            /* without a cast: the progress it arrived with, if any (kept by tc_game_arrive) */
            if (g->has_cast) c->has_progress = true;
            if (g->has_cast) tc_cast_level_save(&g->cast); /* the way out: the level's saved values */
            if (g->has_cast) {
                const tc_ai_globals *gl = &g->cast.globals;
                c->lives = gl->lives;
                c->powers = gl->powers;
                c->real_story = gl->real_story;
                memcpy(c->values, gl->values, sizeof c->values);
            }
            c->has_checkpoint = 0; /* a saved game's arrival only (tc_save.h) */
            /* Ed's hit points and their maximum go on (his std game is the fix's: the exe fills them only at a new game,
             * GAM_StdGame_004237e0 with DAT_008c1767), and so do the fix persos' dsgvars (tc_game_carry_fix) */
            c->hit_points = e->ai.hit_points;
            c->hit_points_max = e->ai.hit_points_max;
            tc_game_carry_fix(g, c);
            g->carry = c;
            g->phase = TC_GP_LOADING;
            g->loading_time = 0;
        }
        ev->level = ev->place = ev->camera_place = NULL;
    }
    if (g->phase == TC_GP_LOADING && (g->loading_time += dt) >= TC_GAME_LOADING_SECONDS) g->phase = TC_GP_PENDING;
    if (g->phase == TC_GP_REVEAL && tc_transition_step(&g->transition, dt)) g->phase = TC_GP_PLAY;
}

/* The game's state the debug menu shows and sets: the cast's (Ed's scripts share it), else Ed's own, else none. */
static const tc_ai_globals *progress(const tc_game *g) {
    if (!g->main.has_driver) return NULL;
    if (g->has_cast) return &g->cast.globals;
    return g->main.driver.ai.globals ? g->main.driver.ai.globals : &g->main.driver.ai.own_globals;
}

void tc_game_reload(tc_game *g, const tc_game_view *view) {
    tc_game_carry *c;
    const tc_ai_globals *gl = progress(g);
    if (!g->main.level_name[0] || g->phase == TC_GP_LOADING || g->phase == TC_GP_PENDING) return;
    c = g->carry ? g->carry : (tc_game_carry *)tc_calloc(1, sizeof(*c));
    if (!c) return;
    memset(c, 0, sizeof(*c));
    if (g->has_cast) tc_cast_level_save(&g->cast); /* as on any way out of the level */
    tc_copy_text(c->level, sizeof(c->level), g->main.level_name);
    c->vertical_fov = view->vertical_fov;
    c->orbit_distance = view->orbit_distance;
    c->closeup = view->closeup;
    if (gl) {
        c->has_progress = 1;
        c->lives = gl->lives;
        c->powers = gl->powers;
        c->real_story = gl->real_story;
        memcpy(c->values, gl->values, sizeof c->values);
    }
    g->carry = c;
    g->loading_time = 0;
    g->phase = g->main.has_driver ? TC_GP_LOADING : TC_GP_PENDING; /* tc_game_update_portals runs the loading screen's time */
}

const tc_game_carry *tc_game_pending_level(const tc_game *g) { return g->phase == TC_GP_PENDING ? g->carry : NULL; }
