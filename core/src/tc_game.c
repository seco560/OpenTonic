/* The game session (tc_game.h): making it, the frame's ticks, custom gameplay's frame, the HUD's tick, the scripts'
 * pictures, the sounds. Its other parts: tc_game_speech.c (lines, hint scenes), tc_game_portals.c (transitions, level
 * changes, arrivals). Moved out of the viewer (tc_viewer_play.c, tc_viewer.c) on 2026-11-19 as it was: the same steps
 * in the same order. */
#include "tc_internal.h"
#include <math.h>
#include <string.h>
#include "tonic/tc_game.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_ed_ai.h"
#include "tonic/custom/tcx.h"
#include "tc_prof_hooks.h"

/* tc_game.sight: the static meshes' triangles as drawn (engine space). Out of memory: left empty. */
static void build_sight(tc_collision *col, const tc_level *l) {
    uint32_t i, k, n = 0;
    tc_vec3 *v;
    for (i = 0; i < l->mesh_count; i++) n += l->meshes[i].index_count / 3;
    if (!n || !(v = (tc_vec3 *)tc_calloc((size_t)n * 3, sizeof(tc_vec3)))) return;
    n = 0;
    for (i = 0; i < l->mesh_count; i++) {
        const tc_mesh *m = &l->meshes[i];
        for (k = 0; k + 2 < m->index_count; k += 3) {
            uint32_t j;
            for (j = 0; j < 3; j++) {
                uint32_t x = m->indices[k + j];
                const float *q = x < m->vertex_count ? m->vertices[x].position : m->vertices[0].position;
                v[n * 3 + j] = tc_v3(q[0], q[1], q[2]);
            }
            n++;
        }
    }
    if (tc_collision_init(col, v, n * 3, NULL) != 0) memset(col, 0, sizeof(*col));
    tc_free(v);
}

tc_game *tc_game_create(const tc_game_desc *desc) {
    const tc_level *l = desc->level;
    const tc_game_carry *carry = desc->carry;
    tc_game *g;
    int idle;
    if (!l->has_ed || l->collision_triangle_count == 0) return NULL;
    idle = tc_ed_find_anim(&l->ed, "idle");
    if (idle < 0 || l->ed.anims[idle].sample_count == 0) return NULL;
    g = (tc_game *)tc_calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->level = l;
    g->ai_result = TC_OK;
    if (tc_collision_init(&g->world, l->collision, 3 * l->collision_triangle_count, l->collision_floors) != 0) {
        tc_free(g);
        return NULL;
    }
    tc_collision_set_sectors(&g->world, l->sectors.collision_sector, l->sectors.collision_count, l->sectors.count);
    build_sight(&g->sight, l);
    tc_main_init(&g->main, l, &g->world, desc->ai_data, desc->ai_size, desc->level_name, &g->ai_result);
    if (g->main.has_driver && l->cast.count) {
        tc_result r;
        /* Not through a portal: a new run, and no level has been visited in it (the level's saved values, tc_cast.h) */
        if (!carry) tc_cast_level_saves_clear();
        r = tc_cast_init(&g->cast, l, &g->main.driver.ai);
        g->has_cast = r == TC_OK;
        if (r != TC_OK) g->ai_result = r;
        g->cast.globals.speech_host = l->speech.dialogue_count != 0; /* tc_game_update_speech */
        if (l->speech.has_arrival) { /* a hint scene: where its own Ed would stand, turned to its speaker */
            uint32_t i;
            float h = g->main.player.heading;
            for (i = 0; i < g->cast.count; i++)
                if (g->cast.actors[i].entry->flags & TC_CAST_TALK) {
                    tc_vec3 to = tc_v3_sub(g->cast.actors[i].ai->position, l->speech.arrival);
                    h = atan2f(to.x, -to.y);
                }
            tc_player_teleport(&g->main.player, tc_v3_add(l->speech.arrival, tc_v3(0, 0, 0.3f)), h);
            g->main.player.start_position = l->speech.arrival; /* and back here after a death */
            g->main.player.start_heading = h;
        }
    }
    /* The persos' own collision (docs/ENGINE.md "Moving collision, read"): Ed's sweeps meet it where each perso is now
     * (the cast poses its actors' every tick), as his collide set says (the player's +0x34 / +0x35) */
    if (tc_obstacles_init(&g->obstacles, l) == TC_OK && g->obstacles.count) {
        g->world.obstacles = &g->obstacles;
        g->world.self_priority = l->pcol.main_priority;
        g->world.self_meets = (l->pcol.main_flags & TC_PCOL_MEETS_PERSOS) != 0;
        if (g->has_cast) tc_cast_set_obstacles(&g->cast, &g->obstacles);
    }
    if (g->has_cast) g->cast.world = &g->world;
    if (g->sight.tri_count) {
        if (g->has_cast) g->cast.sight = &g->sight;
        if (g->main.has_driver) g->main.driver.sight = &g->sight;
    }
    g->use_ai = g->main.has_driver;
    g->custom = tcx_play_create(l, desc->ai_data, desc->ai_size, &g->world, &g->obstacles, desc->perso_first);
    g->main.custom = g->custom;

    g->rng = desc->seed ? desc->seed : 0x7417u;
    g->sound_rng = g->rng ^ 0x50D5u;
    g->scene_exit = -1;
    tc_speech_init(&g->speech, &l->speech);
    tc_hud_init(&g->hud);
    tc_copy_text(g->level_name, sizeof(g->level_name), desc->level_name);
    if (carry) {
        tc_copy_text(g->arrival_place, sizeof(g->arrival_place), carry->place);
        tc_copy_text(g->arrival_camera_place, sizeof(g->arrival_camera_place), carry->camera_place);
    }
    return g;
}

void tc_game_free(tc_game *g) {
    if (!g) return;
    tc_speech_free(&g->speech);
    tc_hud_free(&g->hud);
    if (g->has_cast) tc_cast_free(&g->cast);
    tc_main_free(&g->main);
    tcx_play_free(g->custom);
    tc_collision_free(&g->world);
    tc_collision_free(&g->sight);
    tc_obstacles_free(&g->obstacles);
    tc_free(g->carry);
    tc_free(g);
}

tc_game_main_info tc_game_main(const tc_game *g) {
    tc_game_main_info r;
    const tc_cast_actor *a = g->has_cast ? tc_cast_character(&g->cast) : NULL;
    r.kind = a ? TC_GAME_MAIN_PERSO : !tc_main_riding(&g->main) ? TC_GAME_MAIN_ED : TC_GAME_MAIN_CHARACTER;
    r.actor = a;
    r.position = a ? a->ai->position : g->main.player.position;
    r.sector = tc_main_sector(&g->main);
    if (a && g->main.sectors && a->sector.sector != TC_SECTOR_NONE) r.sector = a->sector.sector;
    return r;
}

void tc_game_push_sound(tc_game *g, uint16_t event, const tc_sound_source *s) {
    if (g->sound_count >= TC_AI_MAX_SOUNDS) return;
    if (s) g->sound_sources[g->sound_count] = *s;
    else memset(&g->sound_sources[g->sound_count], 0, sizeof(g->sound_sources[0])); /* flat */
    g->sounds[g->sound_count++] = event;
}

/* The events that play one of their clips at random (.ttlv SPIK: Ed's cries and cheers, the swamp's animals): the
 * clip this time, or nothing (the exe's sound module picks as it plays the event; its own random numbers are not read:
 * the session's own stream, seeded with the level, so a replay hears the same). */
void tc_game_pick_sounds(tc_game *g) {
    uint32_t i, n = 0;
    if (!g->level->pick_count) return;
    for (i = 0; i < g->sound_count; i++) {
        int32_t e = tc_level_pick_sound(g->level, g->sounds[i], &g->sound_rng);
        if (e < 0) continue;
        g->sounds[n] = (uint16_t)e;
        g->sound_sources[n] = g->sound_sources[i];
        n++;
    }
    g->sound_count = n;
}

void tc_game_begin_frame(tc_game *g, bool action_fresh) {
    g->sound_count = 0;
    if (action_fresh) g->talk_press = true;
}

bool tc_game_picture_hold(tc_game *g, float dt, bool fresh) {
    if (g->picture_left <= 0) return false;
    g->picture_left = fresh ? 0 : fmaxf(0, g->picture_left - dt);
    return true;
}

/* Port (the user, 2026-11-13): no HUD over a covered screen or the loading screen (the frontends draw it last, over
 * the overlay); it comes back, sliding in, with the reveal. */
bool tc_game_hud_covered(const tc_game *g) {
    return g->phase == TC_GP_BLACK || g->phase == TC_GP_LOADING || g->phase == TC_GP_PENDING;
}

int tc_game_custom_frame(tc_game *g, const tc_game_input *gi, float dt) {
    tcx_host h;
    tcx_events out;
    tc_character_input in;
    double prof;
    bool held, given_back = false;
    if (!g->custom) return TC_GAME_CUSTOM_NONE;
    prof = TC_PROF_BEGIN();
    h.player = &g->main.player;
    h.held = gi->held;
    h.move_x = gi->move_x, h.move_y = gi->move_y;
    h.paused = gi->paused;
    h.held_back = gi->director || g->speech_held || gi->inspecting || g->phase != TC_GP_PLAY;
    h.may_start = !gi->first_person;
    h.at_start = false;
    h.covered = false;
    if (g->main.has_driver) {
        const tc_ed_ai *e = &g->main.driver;
        /* waiting behind the start text the custom layer shows (.ttai AUTO 2; tc_main sets wait_start) */
        h.at_start = g->use_ai && e->auto_start && e->wait_start && e->ai.model->auto_start == TC_AI_AUTO_UNLESS_TEXT;
        h.covered = e->screen_covered;
        h.may_start = h.may_start && g->phase == TC_GP_PLAY;
    }
    /* the cars; Ed getting into one hands it the main character's slot (tc_main.h) */
    if (tcx_update(g->custom, &h, dt, &out)) tc_main_take(&g->main, tcx_character(), g->custom);
    if (g->main.has_driver && out.cover) { /* a course's finish: its way back, as a portal's (tc_game_update_portals) */
        g->main.driver.events.cover = true;
        g->main.driver.screen_covered = false;
    }
    if (g->main.has_driver && out.level) {
        g->main.driver.events.level = out.level;
        g->main.driver.events.place = out.place;
        g->main.driver.events.camera_place = out.camera_place;
    }
    held = false;
    if (tc_main_riding(&g->main)) {
        in.ed = &g->main.player;
        in.held = h.held;
        in.move_x = h.move_x, in.move_y = h.move_y;
        in.paused = h.paused;
        in.held_back = h.held_back;
        in.animation_speed = gi->animation_speed;
        in.ed_clip = gi->ed_clip, in.ed_phase = gi->ed_phase;
        held = tc_main_frame(&g->main, &in, dt);
        if (!held) { /* back on his feet, main again: his AI's tick blending starts over there */
            g->main.have_pose = false;
            g->tick_time = 0;
            given_back = true;
        }
    }
    TC_PROF_END(TC_PZ_CUSTOM, prof);
    return held ? TC_GAME_CUSTOM_HELD : given_back ? TC_GAME_CUSTOM_GIVEN_BACK : TC_GAME_CUSTOM_NONE;
}

/* Ed's scripts' buttons this tick: the host's (tc_game_input.ai_inputs) less the presses something else had. */
static uint32_t ai_inputs(const tc_game *g, const tc_game_input *gi) {
    uint32_t r = gi->ai_inputs;
    if (tcx_holds_action(g->custom)) r &= ~(1u << TC_AI_IN_ACTION); /* that press was custom gameplay's */
    if (tcx_holds_shoot(g->custom)) r &= ~(1u << TC_AI_IN_SHOOT);   /* ... and this one */
    if (g->talk_held) r &= ~(1u << TC_AI_IN_ACTION); /* that press was the speaker's (tc_game_step) */
    return r;
}

/* A picture the cast's scripts asked for since the last look (tc_ai_globals.vignette): taken as tc_game.picture
 * ("Credits\\Credit06.bmp" -> "Credits/Credit06"), shown for its time. */
static bool take_picture(tc_game *g) {
    const tc_ai_globals *gl = &g->cast.globals;
    char *dot, *slash;
    size_t k;
    if (gl->vignette_serial == g->picture_serial) return false;
    g->picture_serial = gl->vignette_serial;
    if (!gl->vignette[0] || !gl->vignette_ms) return false;
    for (k = 0; gl->vignette[k] && k + 1 < sizeof(g->picture); k++) g->picture[k] = gl->vignette[k] == '\\' ? '/' : gl->vignette[k];
    g->picture[k] = 0;
    dot = strrchr(g->picture, '.');
    slash = strrchr(g->picture, '/');
    if (dot && (!slash || dot > slash)) *dot = 0;
    g->picture_left = (float)gl->vignette_ms / 1000;
    return true;
}

/* The HUD's tick (tc_hud.h), after the cast's: what the standard elements show comes from Ed and the save values. The
 * level's bonuses count on the World perso's save value and the one after it (FUN_0043bc00, GAM_00419490: the exe
 * reads them through perso 0x8c1744's inventory object). */
static void hud_tick(tc_game *g) {
    tc_ai_globals *gl = &g->cast.globals;
    tc_hud_values now;
    uint16_t k;
    memset(&now, 0, sizeof(now));
    now.hit_points = g->main.driver.ai.hit_points;
    now.hit_points_max = g->main.driver.ai.hit_points_max;
    if (!g->has_cast) { /* an imported level (tc_hud.left_only): his life, and the lives it arrived with (tc_game_arrive) */
        now.lives = g->carry && g->carry->has_progress ? g->carry->lives : 0;
        now.bees = now.tings = now.spade = -1;
        now.show_all = !g->hud.auto_hide;
        tc_hud_tick(&g->hud, NULL, &now);
        if (tc_game_hud_covered(g)) tc_hud_away(&g->hud);
        return;
    }
    now.lives = gl->lives;
    /* Port (the user, 2026-11-13: no ammunition counter before Ed has the peashooter): the bees show with power 8,
     * which Ed's own scripts test before Action takes the blowgun out (B86 / B87: Cond_TestPower(8) -> B72) and which
     * sud0's Doc gives. The exe's HUD has no such test (GAM_0041a720 only leaves the counter out at -1): there it comes
     * when the count changes, or always under two of the four display options (GAM_00411c50: masks 0x5b, 0xdf). */
    now.bees = (gl->powers & 8) == 8 ? gl->values[0] : -1;
    now.tings = now.spade = -1;
    for (k = 0; k < gl->inv_count; k++)
        if (gl->inv[k].id == g->level->cast.world && gl->inv[k].slot >= 0 && gl->inv[k].slot < 31) {
            now.tings = gl->values[gl->inv[k].slot] & 0xFF;
            now.spade = gl->inv[k].slot + 1 < 31 ? gl->values[gl->inv[k].slot + 1] & 0xFF : -1;
        }
    now.doc_slot = gl->hud_doc;
    now.doc_count = gl->hud_doc >= 1 && gl->hud_doc <= 6 ? gl->values[gl->hud_doc] : 0;
    now.doc_serial = gl->hud_doc_serial;
    now.show_all = !g->hud.auto_hide;
    tc_hud_tick(&g->hud, gl, &now);
    if (tc_game_hud_covered(g)) tc_hud_away(&g->hud); /* they slide in again once the screen opens */
}

/* Port: a hint scene's way out (.ttlv SCNX; docs/ENGINE.md "Walkable scenes"). The speaker sends its scene's own Ed off
 * to a waypoint after the line (his GoToWaypoint walk, state 9); the real Ed plays the scene, so he walks there by his
 * own scripts, steered as a stick held towards it (relative to the camera as his scripts read it: from the eye to him),
 * the player's input held. Arrived (within TC_GAME_SCENE_EXIT_NEAR), he stands; the speaker's cover and level change
 * follow as its scripts time them. */
#define TC_GAME_SCENE_EXIT_NEAR 0.8f
#define TC_GAME_SCENE_EXIT_STICK 0.6f /* a walk, not a run */
static void scene_walk(tc_game *g, tc_main_input *in, float camera_yaw) {
    tc_vec3 ed = g->main.player.position, to = g->level->speech.exits[g->scene_exit].point, f;
    float dx = to.x - ed.x, dy = to.y - ed.y, d = sqrtf(dx * dx + dy * dy), fl;
    in->inputs = 0;
    in->face = false;
    in->stick_x = in->stick_y = 0;
    if (d < TC_GAME_SCENE_EXIT_NEAR) { g->scene_exit = -1; return; }
    f = tc_v3(ed.x - in->eye.x, ed.y - in->eye.y, 0);
    fl = sqrtf(f.x * f.x + f.y * f.y);
    if (fl < 0.5f) f = tc_v3(sinf(camera_yaw), cosf(camera_yaw), 0), fl = 1; /* the eye above him: its facing */
    f.x /= fl, f.y /= fl;
    dx /= d, dy /= d;
    in->stick_y = TC_GAME_SCENE_EXIT_STICK * (dx * f.x + dy * f.y);  /* forward: away from the eye */
    in->stick_x = TC_GAME_SCENE_EXIT_STICK * (dx * f.y - dy * f.x);  /* right of forward (fy, -fx) */
}

/* Ed's AI and mechanics and the cast in ticks of TC_ED_AI_TICK (the game's 30 per second). The scripts see last frame's
 * camera eye (it makes the direction keys camera-relative). */
void tc_game_step(tc_game *g, float dt, const tc_game_input *gi) {
    tc_main *m = &g->main;
    int ticks = 0;
    double prof = TC_PROF_BEGIN();
    if (!m->have_pose) tc_main_capture(m);
    g->sound_count = 0;
    if (g->has_cast) g->cast.listener = gi->listener; /* last frame's camera, as the exe's micro at the frame's start */
    g->tick_time += dt;
    /* Port: a fresh Action within reach of a waiting speaker talks to it, and only that. Ed's own scripts take the same
     * press as the blowgun's (B86 / B87: ButtonJustPressed(action) -> B72) in the tick the speaker wakes, so they do not
     * get it; nor while it stays held, or its first tick back would be a fresh press to them (tc_ai update_buttons). */
    if (!(gi->held & (1u << TC_VA_ACTION))) g->talk_held = false;
    else if (g->talk_press && !gi->director && tc_game_talk_prompt(g)) g->talk_held = true;
    while (g->tick_time >= TC_ED_AI_TICK) {
        tc_main_input in;
        int mode_switch;
        bool ran, held_back, still;
        g->tick_time -= TC_ED_AI_TICK;
        if (++ticks > 8) { g->tick_time = 0; break; } /* a long stall: drop the backlog */
        held_back = gi->director || g->speech_held;
        still = gi->first_person || held_back; /* the director's shot: Ed waits, as the SE parks its Ed */
        in.inputs = held_back ? 0 : ai_inputs(g, gi);
        in.stick_x = still ? 0 : gi->move_x;
        in.stick_y = still ? 0 : gi->move_y;
        in.eye = gi->eye;
        in.face = gi->first_person && !gi->director;
        in.heading = TC_PI - gi->yaw;
        if (g->scene_exit >= 0) scene_walk(g, &in, gi->yaw); /* a hint scene's way out: he walks off by himself */
        in.aim_pitch = in.face ? gi->pitch : 0; /* first person: shots go where the gunsight is, up and down too */
        in.talk_press = g->talk_press;
        in.hud_waits = g->hud_waits;
        /* the game's tick (tc_main.h): the run-first persos, Ed, the rest */
        ran = tc_main_tick(m, g->has_cast ? &g->cast : NULL, &in, g->sounds, g->sound_sources, &g->sound_count,
                           TC_AI_MAX_SOUNDS, &mode_switch);
        if (ran) g->tick_serial++;
        if (mode_switch < 0) g->use_ai = false;
        if (mode_switch > 0 && m->mode == TC_AI_MODE_WALK) g->walk_serial++;
        if (!ran) break;
        if (g->has_cast) {
            g->talk_press = false; /* the cast had it */
            hud_tick(g);
        } else if (g->hud.left_only) hud_tick(g); /* an imported level's stripped-down HUD */
        if (g->has_cast && take_picture(g)) { /* the game holds while it shows (0x40fa20 waits in the main loop) */
            g->tick_time = 0;
            break;
        }
    }
    TC_PROF_END(TC_PZ_TICKS, prof);
}
