/* The viewer's play session: input, Ed's ticks (his AI and the cast), the simple controller, custom gameplay. */
#include "tc_viewer_internal.h"

/* Ed's animation by .ttlv name with at least one sample, or -1. */
int tcv_ed_anim(const tc_level *l, const char *name) {
    int i = tc_ed_find_anim(&l->ed, name);
    return i >= 0 && l->ed.anims[i].sample_count > 0 ? i : -1;
}

/* NULL when the level has no Ed, no collision or no idle clip (or out of memory). */
tc_viewer_play *tcv_play_create(tc_viewer *v, const tc_viewer_desc *desc) {
    const tc_level *l = desc->level;
    tc_viewer_play *p;
    if (!level_has_ed(l) || tcv_ed_anim(l, "idle") < 0) return NULL;
    p = (tc_viewer_play *)tc_calloc(1, sizeof(*p));
    if (!p) return NULL;
    if (tc_collision_init(&p->world, l->collision, 3 * l->collision_triangle_count, l->collision_floors) != 0) {
        tc_free(p);
        return NULL;
    }
    tc_collision_set_sectors(&p->world, l->sectors.collision_sector, l->sectors.collision_count, l->sectors.count);
    tc_main_init(&p->main, l, &p->world, desc->ai_data, desc->ai_size, desc->level_name, &v->ai_result);
    if (p->main.has_driver && l->cast.count) {
        tc_result r;
        /* Not through a portal: a new run, and no level has been visited in it (the level's saved values, tc_cast.h) */
        if (!desc->carry) tc_cast_level_saves_clear();
        r = tc_cast_init(&p->cast, l, &p->main.driver.ai);
        p->has_cast = r == TC_OK;
        if (r != TC_OK) v->ai_result = r;
        p->cast.globals.speech_host = l->speech.dialogue_count != 0; /* update_speech */
        if (l->speech.has_arrival) { /* a hint scene: where its own Ed would stand, turned to its speaker */
            uint32_t i;
            float h = p->main.player.heading;
            for (i = 0; i < p->cast.count; i++)
                if (p->cast.actors[i].entry->flags & TC_CAST_TALK) {
                    tc_vec3 to = tc_v3_sub(p->cast.actors[i].ai->position, l->speech.arrival);
                    h = atan2f(to.x, -to.y);
                }
            tc_player_teleport(&p->main.player, tc_v3_add(l->speech.arrival, tc_v3(0, 0, 0.3f)), h);
            p->main.player.start_position = l->speech.arrival; /* and back here after a death */
            p->main.player.start_heading = h;
        }
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
    p->use_ai = p->main.has_driver;
    p->custom = tcx_play_create(l, &p->world, &p->obstacles, v->perso_first);
    return p;
}

void tcv_play_destroy(tc_viewer_play *p) {
    if (!p) return;
    if (p->has_cast) tc_cast_free(&p->cast);
    tc_main_free(&p->main);
    tcx_play_free(p->custom);
    tc_collision_free(&p->world);
    tc_obstacles_free(&p->obstacles);
    tc_free(p);
}

/* Distance to the nearest hit along the segment, or -1. */
static float raycast(tc_viewer_play *p, tc_vec3 origin, tc_vec3 dir, float max_distance) {
    tc_hit hit;
    return tc_collision_raycast(&p->world, origin, dir, max_distance, &hit) ? hit.distance : -1;
}

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

static float axis(const tc_viewer *v, tc_view_action pos, tc_view_action neg) {
    return (held(v, pos) ? 1.0f : 0.0f) - (held(v, neg) ? 1.0f : 0.0f);
}

/* Arrow keys, the right stick and the mouse turn the camera (both modes). */
/* The look keys, the right stick and the mouse turn the view, x look_sensitivity, reversed per axis as set. In play mode
 * they turn the play camera round Ed and raise or lower it (tc_cam_look). */
void tcv_look(tc_viewer *v, float dt) {
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
        v->looked = true; /* a scripted shot's camera turns with it (tc_viewer.shot.look_*) */
        v->look_yaw_frame += yaw;
        v->look_rise_frame += -pitch;
        tc_cam_look(&v->cam, c, yaw, -pitch); /* looking up: the camera lower, looking up at him */
        return;
    }
    c->yaw += yaw;
    c->pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT, c->pitch + pitch));
}

/* Free-fly: W/S (the stick's y) fly along the view direction (pitch included); R/F stay world up/down. Keys fly at
 * full speed; the stick in proportion to how far it is pushed. */
void tcv_fly(tc_viewer *v, float dt) {
    tc_camera *c = &v->camera;
    tc_vec3 move;
    float len;
    tcv_look(v, dt);
    move = tc_v3_add(tc_v3_scale(tc_camera_forward(c), axis(v, TC_VA_FORWARD, TC_VA_BACK) + v->move_y),
                     tc_v3_scale(tc_camera_right(c), axis(v, TC_VA_RIGHT, TC_VA_LEFT) + v->move_x));
    move.z += axis(v, TC_VA_UP, TC_VA_DOWN);
    len = tc_v3_length(move);
    if (len > 0) {
        float speed = v->fly_speed * (held(v, TC_VA_FAST) ? 4.0f : 1.0f) * dt;
        c->position = tc_v3_add(c->position, tc_v3_scale(move, speed / fmaxf(len, 1)));
    }
}

/* Into or out of a pause's camera. */
void tcv_set_inspecting(tc_viewer *v, bool on) {
    if (on == v->inspecting) return;
    v->inspecting = on;
    if (on) {
        v->inspect_camera = v->camera;
        v->inspect_cam = v->cam;
        v->inspect_orbit = v->orbit_distance;
    } else {
        v->camera = v->inspect_camera;
        v->cam = v->inspect_cam;
        v->orbit_distance = v->inspect_orbit;
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
    if (v->play && tcx_holds_action(v->play->custom)) r &= ~(1u << TC_AI_IN_ACTION); /* that press was custom gameplay's */
    return r;
}

/* Before Ed's step: custom gameplay's frame (the cars move; Action gets him in or out). True while it holds him: he is not
 * stepped, and drawn as it says. */
static bool update_custom(tc_viewer *v, float dt) {
    tc_viewer_play *p = v->play;
    tcx_host h;
    tc_character_input in;
    double prof;
    bool held;
    if (!p->custom) return false;
    prof = TC_PROF_BEGIN();
    h.player = &p->main.player;
    h.held = v->held;
    h.move_x = v->move_x, h.move_y = v->move_y;
    h.paused = v->paused;
    h.held_back = v->director || v->speech_held || v->inspecting || v->phase != TC_VP_PLAY;
    h.may_start = !v->cam.first_person;
    /* the cars; Ed getting into one hands it the main character's slot (tc_main.h) */
    if (tcx_update(p->custom, &h, dt)) tc_main_take(&p->main, tcx_character(), p->custom);
    held = false;
    if (!tc_main_is_ed(&p->main)) {
        in.ed = &p->main.player;
        in.held = h.held;
        in.move_x = h.move_x, in.move_y = h.move_y;
        in.paused = h.paused;
        in.held_back = h.held_back;
        in.animation_speed = v->animation_speed;
        in.ed_clip = v->ed_clip, in.ed_phase = v->ed_phase;
        held = tc_main_frame(&p->main, &in, dt);
        if (!held) { /* back on his feet, main again: his AI's tick blending starts over there */
            p->main.have_pose = false;
            p->tick_time = 0;
            v->ed_state = -1;
            v->ed_phase = 0;
        }
    }
    TC_PROF_END(TC_PZ_CUSTOM, prof);
    if (!held) return false;
    v->ed_clip = p->main.rider.clip;
    v->ed_phase = p->main.rider.phase;
    v->ed_loop = p->main.rider.loop;
    v->ed_state = -1;
    return true;
}

void tcv_toggle_play(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    if (!p) return;
    v->playing = !v->playing;
    if (!v->playing) {
        tc_main_abort(&p->main); /* out of the car at once: Ed main again */
        v->has_exit_camera = true;
        v->exit_camera = v->camera.position;
        return;
    }
    if (v->has_exit_camera && tc_v3_distance(v->exit_camera, v->camera.position) > 1) {
        tc_vec3 o = v->camera.position;
        float d = raycast(p, o, tc_v3(0, 0, -1), 2000);
        tc_vec3 ground = d >= 0 ? tc_v3_sub(o, tc_v3(0, 0, d)) : o;
        tc_player_teleport(&p->main.player, tc_v3_add(ground, tc_v3(0, 0, 0.3f)), TC_PI - v->camera.yaw);
    }
    v->camera.yaw = TC_PI - p->main.player.heading;  /* camera forward (sin yaw, cos yaw) = Ed's front (sin h, -cos h) */
    v->camera.pitch = -TC_CAM_ELEVATION;
    tc_cam_reset(&v->cam);  /* following him, placed at once */
}

void tc_viewer_toggle_play(tc_viewer *v) {
    REC_DOOR(TC_RD_TOGGLE_PLAY, 0, 0);
    tcv_set_inspecting(v, false);
    tcv_toggle_play(v);
}

void tc_viewer_toggle_ai(tc_viewer *v) {
    REC_DOOR(TC_RD_TOGGLE_AI, 0, 0);
    if (!tc_viewer_has_ai(v)) return;
    v->play->use_ai = !v->play->use_ai;
    v->play->main.have_pose = false; /* the tick blending starts over from where Ed is */
    v->play->tick_time = 0;
    v->ed_state = -1;
    v->ed_phase = 0;
}

bool tc_viewer_can_play(const tc_viewer *v) { return v->play != NULL; }

const struct tcx_play *tc_viewer_custom(const tc_viewer *v) { return v->play ? v->play->custom : NULL; }

bool tc_viewer_has_ai(const tc_viewer *v) { return v->play && v->play->main.has_driver; }

bool tc_viewer_ai_active(const tc_viewer *v) { return v->playing && tc_viewer_has_ai(v) && v->play->use_ai; }

bool tc_viewer_status_live(const tc_viewer *v) {
    return tc_viewer_ai_active(v) || (v->light && v->light_scripted) || v->sector_mode != TC_VS_ALL;
}

const tc_player *tc_viewer_player(const tc_viewer *v) { return v->play ? &v->play->main.player : NULL; }

const tc_cast *tc_viewer_cast(const tc_viewer *v) { return v->play && v->play->has_cast ? &v->play->cast : NULL; }

/* The simple controller at a portal (the ski levels' finish, or I pressed anywhere): when one captures, Ed faces it and
 * plays the step-in (state 228's animation, as the AI's walk-in does), then the cover; once the screen is covered the
 * portal fires as with the AI (its dsgvar condition set as Ed's B106 would). Capture-only portals (the holes) need the
 * player's scripts and are left alone. */
static void simple_portals(tc_viewer *v, float dt) {
    tc_viewer_play *p = v->play;
    tc_ed_ai *e = &p->main.driver;
    const tc_ai_model *m = e->ai.model;
    if (v->walk_in == 0) {
        const tc_ai_portal *g;
        char name[16];
        if (v->phase != TC_VP_PLAY) return;
        tc_ed_ai_run_portals(e, dt);
        e->events.cover = false; /* the step-in comes first */
        if (e->portal < 0) return;
        g = &m->portals[e->portal];
        p->main.player.heading = atan2f(g->position.x - p->main.player.position.x, -(g->position.y - p->main.player.position.y));
        snprintf(name, sizeof(name), "s%u", m->state_count > 228 ? (unsigned)m->states[228].anim : 0u);
        v->walk_clip = m->state_count > 228 ? tcv_ed_anim(v->level, name) : -1;
        v->ed_phase = 0;
        v->walk_in = 1;
        if (v->walk_clip < 0) {
            v->walk_in = 2;
            tcv_start_transition(v, false);
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

/* A Special Edition level's walk / ski switches (tc_ai_mode_zone, docs/SE.md "Sliding in the Special Edition"): with
 * Ed's origin in a zone whose requirement holds, the player goes to the other model (walking Ed: the level's .ttai,
 * the skier: its ALTM). Its position, heading and speed stay (the zone may move him first, as the SE trigger does);
 * the new driver starts in its model's start behaviour (the skier's waits for Jump, which tc_ed_ai's auto start
 * presses on Totalski). Port choice: the SE's own triggers force SE Ed's ski behaviour; retail Ed has none, so the
 * retail skier takes over. One zone per tick. */
unsigned tc_viewer_play_mode(const tc_viewer *v) { return v->play ? v->play->main.mode : TC_AI_MODE_WALK; }

const tc_ed_ai *tc_viewer_driver(const tc_viewer *v) { return v->play && v->play->main.has_driver ? &v->play->main.driver : NULL; }

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

/* Port (the user, 2026-11-13): no HUD over a covered screen or the loading screen (the frontends draw it last, over
 * the overlay); it comes back, sliding in, with the reveal. */
bool tcv_hud_covered(const tc_viewer *v) {
    return v->phase == TC_VP_BLACK || v->phase == TC_VP_LOADING || v->phase == TC_VP_PENDING;
}

/* The HUD's tick (tc_hud.h), after the cast's: what the standard elements show comes from Ed and the save values. The
 * level's bonuses count on the World perso's save value and the one after it (FUN_0043bc00, GAM_00419490: the exe
 * reads them through perso 0x8c1744's inventory object). */
static void hud_tick(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    tc_ai_globals *g = &p->cast.globals;
    tc_hud_values now;
    uint16_t k;
    memset(&now, 0, sizeof(now));
    now.hit_points = p->main.driver.ai.hit_points;
    now.hit_points_max = p->main.driver.ai.hit_points_max;
    now.lives = g->lives;
    /* Port (the user, 2026-11-13: no ammunition counter before Ed has the peashooter): the bees show with power 8,
     * which Ed's own scripts test before Action takes the blowgun out (B86 / B87: Cond_TestPower(8) -> B72) and which
     * sud0's Doc gives. The exe's HUD has no such test (GAM_0041a720 only leaves the counter out at -1): there it comes
     * when the count changes, or always under two of the four display options (GAM_00411c50: masks 0x5b, 0xdf). */
    now.bees = (g->powers & 8) == 8 ? g->values[0] : -1;
    now.tings = now.spade = -1;
    for (k = 0; k < g->inv_count; k++)
        if (g->inv[k].id == v->level->cast.world && g->inv[k].slot >= 0 && g->inv[k].slot < 31) {
            now.tings = g->values[g->inv[k].slot] & 0xFF;
            now.spade = g->inv[k].slot + 1 < 31 ? g->values[g->inv[k].slot + 1] & 0xFF : -1;
        }
    now.doc_slot = g->hud_doc;
    now.doc_count = g->hud_doc >= 1 && g->hud_doc <= 6 ? g->values[g->hud_doc] : 0;
    now.doc_serial = g->hud_doc_serial;
    now.show_all = !v->hud.auto_hide;
    tc_hud_tick(&v->hud, g, &now);
    if (tcv_hud_covered(v)) tc_hud_away(&v->hud); /* they slide in again once the screen opens */
}

static void step_ai(tc_viewer *v, float dt) {
    tc_viewer_play *p = v->play;
    tc_main *m = &p->main;
    float t;
    int ticks = 0;
    double prof = TC_PROF_BEGIN();
    if (!m->have_pose) tc_main_capture(m);
    v->sound_count = 0;
    p->tick_time += dt;
    while (p->tick_time >= TC_ED_AI_TICK) {
        tc_main_input in;
        int mode_switch;
        bool ran;
        p->tick_time -= TC_ED_AI_TICK;
        if (++ticks > 8) { p->tick_time = 0; break; } /* a long stall: drop the backlog */
        bool held_back = v->director || v->speech_held;
        bool still = v->cam.first_person || held_back; /* the director's shot: Ed waits, as the SE parks its Ed */
        in.inputs = held_back ? 0 : ai_inputs(v);
        in.stick_x = still ? 0 : v->move_x;
        in.stick_y = still ? 0 : v->move_y;
        in.eye = v->camera.position;
        in.face = v->cam.first_person && !v->director;
        in.heading = TC_PI - v->camera.yaw;
        in.talk_press = v->talk_press;
        in.hud_waits = v->hud_waits;
        /* the game's tick (tc_main.h): the run-first persos, Ed, the rest */
        ran = tc_main_tick(m, p->has_cast ? &p->cast : NULL, &in, v->sounds, &v->sound_count, TC_AI_MAX_SOUNDS, &mode_switch);
        if (mode_switch < 0) p->use_ai = false;
        if (mode_switch > 0 && m->mode == TC_AI_MODE_WALK) p->ski_shot = false;
        if (!ran) break;
        if (p->has_cast) {
            v->talk_press = false; /* the cast had it */
            hud_tick(v);
        }
        if (p->has_cast && take_picture(v)) { /* the game holds while it shows (0x40fa20 waits in the main loop) */
            p->tick_time = 0;
            break;
        }
    }
    TC_PROF_END(TC_PZ_TICKS, prof);
    if (!tc_main_is_ed(m)) return; /* Ed is drawn as the character in the slot says (update_custom) */
    t = p->tick_time / TC_ED_AI_TICK;
    p->pose_draw = pose_lerp(&p->main.pose_prev, &p->main.pose_cur, t);
    v->ed_clip = p->main.clip_cur;
    v->ed_state = -1;
    v->ed_loop = p->main.loop_cur;
    v->ed_phase = p->main.phase_cur;
    if (p->main.clip_cur >= 0) {
        float count = (float)v->level->ed.anims[p->main.clip_cur].sample_count, d = p->main.phase_cur - p->main.phase_prev;
        if (d < 0 && p->main.loop_cur) d += count; /* wrapped round the loop */
        /* Only a loop wraps: an ended state holds its last frame at phase == count (the portal step-in, Ed hidden),
         * which wrapped to 0 drew him whole again where the step-in began. */
        if (p->main.clip_prev == p->main.clip_cur && d >= 0 && d < count / 2)
            v->ed_phase = p->main.loop_cur ? fmodf(p->main.phase_prev + d * t, count) : p->main.phase_prev + d * t;
        else /* a new clip this tick: the drawing runs a tick behind, so start it from its frame 0 at the tick's rate
              * (showing phase_cur at once jumped a tick ahead, then held it for the rest of the tick) */
            v->ed_phase = fmaxf(0, p->main.phase_cur - (1 - t) * p->main.rate_cur * TC_ED_AI_TICK);
    }
}

void tcv_update_play(tc_viewer *v, float dt, bool fresh) {
    tc_viewer_play *p = v->play;
    const tc_level *l = v->level;
    const tc_ed_anim *a;
    float rate;
    int i;
    if (v->tape) {
        tcv_tape_update(v, v->paused ? 0 : dt, fresh);
        return;
    }
    tcv_look(v, dt);
    if (update_custom(v, dt)) { /* a car holds the main character's slot: the game goes on round Ed riding it */
        if (p->use_ai) {
            step_ai(v, v->paused ? 0 : dt);
            if (!v->paused) {
                tcv_update_speech(v, dt, fresh);
                tcv_update_scene(v);
            }
        }
        tcv_update_camera(v, dt, fresh); /* the camera follows what it says */
        return;
    }
    if (p->use_ai) {
        step_ai(v, v->paused ? 0 : dt); /* Ed's own scripts, at the game's tick */
        if (!v->paused) {
            tcv_update_speech(v, dt, fresh);
            tcv_update_scene(v);
        }
    } else {
        tc_player_input in;
        bool walking_in = v->walk_in != 0;
        in.move = walking_in || v->cam.first_person ? tc_v2(0, 0) : move_direction(v);
        /* Shift runs; the stick runs when pushed most of the way (0.8: the simple controller has only walk and run). */
        in.run = !walking_in && (held(v, TC_VA_FAST) || v->move_x * v->move_x + v->move_y * v->move_y > 0.64f);
        in.jump = !walking_in && held(v, TC_VA_JUMP);
        double prof = TC_PROF_BEGIN();
        tc_player_step(&p->main.player, dt, &in);
        TC_PROF_END(TC_PZ_SIMPLE, prof);
        if (p->main.has_driver) simple_portals(v, dt);
    }
    tcv_update_camera(v, dt, fresh);

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
            tcv_start_transition(v, false);
        }
        return;
    }
    /* Clip from the movement state; walk/run play at the rate that matches Ed's ground speed. */
    if ((int)p->main.player.animation != v->ed_state) {
        v->ed_state = (int)p->main.player.animation;
        i = tcv_ed_anim(l, tc_anim_state_name(p->main.player.animation));
        v->ed_clip = i >= 0 ? i : tcv_ed_anim(l, "idle");
        v->ed_phase = 0;
    }
    v->ed_loop = v->ed_clip < 0 || l->ed.anims[v->ed_clip].loop != 0;
    if (v->ed_clip < 0) return;
    a = &l->ed.anims[v->ed_clip];
    rate = a->ground_speed > 0 ? a->sample_rate * tc_player_horizontal_speed(&p->main.player) / a->ground_speed
                               : a->sample_rate;
    v->ed_phase = tc_phase_advance(v->ed_phase, dt * (rate / 4) * (v->paused ? 0 : v->animation_speed),
                                   a->sample_count, a->loop != 0);
}
