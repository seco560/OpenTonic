/* The viewer module (tc_viewer.h): init / free, the frame, settings, the title readout. Its other parts:
 * tc_viewer_internal.h. */
#include "tc_viewer_internal.h"
#include "tonic/tc_save.h"

tc_result tc_viewer_init(tc_viewer *v, const tc_viewer_desc *desc) {
    const tc_level *l = desc->level;
    uint32_t i, k, n = 0, *piece_ranges = NULL;
    tc_vec3 lo, hi;
    float size;
    memset(v, 0, sizeof(*v));
    v->crosshair_effect = -1;
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
    v->shot.since_look = 1e6f; /* the player has not turned the camera yet */
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
    v->lighting = l->has_lighting ? TC_VL_GAME : TC_VL_PORT;
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
    tcv_measure_sky(v);
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
        for (i = 0; i < v->sectors->count && !v->sector_lit; i++)
            if (v->sectors->sectors[i].char_light_count) {
                v->sector_lit = (struct tc_viewer_char_lit *)tc_calloc(v->sectors->count, sizeof *v->sector_lit);
                if (!v->sector_lit) {
                    tc_viewer_free(v);
                    return TC_ERR_OUT_OF_MEMORY;
                }
            }
    }
    if (!tcv_split_transparent(v, &piece_ranges)) {
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
        bool ok = tcv_mesh_centres(v) && tcv_mesh_ranges(v, piece_ranges) && tcv_make_cells(v);
        tc_free(piece_ranges);
        if (!ok) {
            tc_viewer_free(v);
            return TC_ERR_OUT_OF_MEMORY;
        }
    }

    {
        tc_game_desc gd;
        memset(&gd, 0, sizeof(gd));
        gd.level = l;
        gd.ai_data = desc->ai_data;
        gd.ai_size = desc->ai_size;
        gd.level_name = desc->level_name;
        gd.carry = desc->carry;
        gd.seed = desc->seed;
        gd.perso_first = v->perso_first;
        v->game = tc_game_create(&gd);
        if (v->game) v->ai_result = v->game->ai_result;
    }
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
        /* each water mesh's surface (the one at its height as loaded): drawn where the scripts move that surface's height
         * (tc_ai_surface_order: the level's surface_z moved in place) */
        if (v->mesh_water && v->sectors->surface_count &&
            (v->surface_z0 = (float *)tc_alloc(sizeof(float) * v->sectors->surface_count)) != NULL &&
            (v->mesh_surface = (int16_t *)tc_alloc(sizeof(int16_t) * l->mesh_count)) != NULL) {
            memcpy(v->surface_z0, v->sectors->surface_z, sizeof(float) * v->sectors->surface_count);
            for (i = 0; i < l->mesh_count; i++)
                v->mesh_surface[i] = v->mesh_water[i] && l->meshes ? tcv_mesh_surface(v, &l->meshes[i]) : -1;
        }
    }
    v->ed_clip = tcv_ed_anim(l, "idle");
    v->rng = desc->seed ? desc->seed : 0x7417u; /* the light persos' seeds without a game (tc_game.rng with one) */
    {
        bool oom;
        v->light = tcv_light_create(v, l, &oom);
        v->light_scripted = v->light != NULL;
        if (oom) {
            tc_viewer_free(v);
            return TC_ERR_OUT_OF_MEMORY;
        }
    }
    if (l->perso_count) { /* the spawnable templates' entries: never drawn where the data places them (at the origin) */
        uint32_t k;
        v->spawn_prso = (uint8_t *)tc_calloc(l->perso_count, 1);
        if (!v->spawn_prso) { tc_viewer_free(v); return TC_ERR_OUT_OF_MEMORY; }
        for (k = 0; k < l->cast.count; k++)
            if ((l->cast.entries[k].flags & TC_CAST_SPAWN) && l->cast.entries[k].prso < l->perso_count)
                v->spawn_prso[l->cast.entries[k].prso] = 1;
    }
    if (tcv_ai_model(v) && !tcv_collect_effects(v, tcv_ai_model(v))) {
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
    if (v->game) tcv_toggle_play(v);
    if (desc->carry && desc->carry->vertical_fov > 0) v->camera.vertical_fov = desc->carry->vertical_fov;
    if (desc->carry && desc->carry->orbit_distance > 0) v->orbit_distance = desc->carry->orbit_distance;
    if (desc->carry) v->cam.closeup = desc->carry->closeup != 0;
    if (desc->carry && v->game) { /* the game takes the carry; the camera turns as it asks */
        tc_game_arrive(v->game, desc->carry);
        tcv_take_camera_requests(v);
    }
    /* Port (the user, 2026-11-15): every fresh start keeps the powers all on (TC_AI_POWERS_ALL: Tonic Viewer, Totalski
     * too); a new game (the main menu's NEW GAME) comes with its own carry (tc_save_start_new_game: no powers, a new game's
     * save values), and a level change carries them as they are. Until then a run starting at retail Totalski was the
     * game's start whatever opened it. */
    v->rec = desc->recorder;
    if (v->rec) tc_rec_begin(v->rec, v, desc);
    /* the meshes' bounding spheres, which culling needs (tc_viewer_set_aspect): made now, in the loading, not on the
     * first frame drawn (LibertyCity2001: millions of vertices). Levels with sectors make them when the camera first
     * asks (camera_sight), as before. */
    if (!v->sectors) tcv_mesh_spheres(v);
    /* and what occlusion culling needs (tc_viewer_set_occlusion): the meshes' boxes, the occluders */
    tcv_occ_init(v);
    return TC_OK;
}

void tc_viewer_free(tc_viewer *v) {
    uint32_t i;
    if (v->rec) tc_rec_end(v->rec, v);
    tcv_tape_free(v);
    tc_debug_free(&v->debug);
    tc_game_free(v->game);
    tcv_light_destroy(v->light);
    tc_free((void *)v->effects);
    tc_free(v->spawn_prso);
    tc_free((void *)v->meshes);
    tc_free(v->mesh_water);
    tc_free(v->mesh_surface);
    tc_free(v->piece_surface);
    tc_free(v->surface_z0);
    tc_free(v->draws);
    tc_free(v->mesh_sphere);
    tc_free(v->mesh_ghost);
    tc_free(v->blended);
    tc_free(v->culled);
    tcv_occ_free(v);
    for (i = 0; v->celled && v->mesh_cells && v->level && i < v->level->mesh_count; i++)
        if (v->mesh_cells[2 * i + 1]) tc_free(v->celled[i].indices);
    tc_free(v->celled);
    tc_free(v->mesh_cells);
    tc_free(v->cells);
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
    tc_free(v->sector_lit);
    tc_free(v->light_wanted);
    tc_free(v->light_instant);
    memset(v, 0, sizeof(*v));
}

uint32_t tc_viewer_sounds(const tc_viewer *v, const uint16_t **events) {
    static const uint16_t none[1] = {0};
    *events = v->game ? v->game->sounds : none;
    return v->game ? v->game->sound_count : 0;
}

const tc_sound_source *tc_viewer_sound_sources(const tc_viewer *v) {
    static const tc_sound_source none[1];
    return v->game ? v->game->sound_sources : none;
}

tc_sound_listener tcv_listener(const tc_viewer *v) {
    tc_sound_listener l;
    l.position = v->camera.position;
    l.right = tc_camera_right(&v->camera);
    l.front = tc_camera_forward(&v->camera);
    l.sector = v->sectors ? v->light_sector : TC_SECTOR_NONE; /* the camera's (tc_viewer_light.c camera_sector) */
    l.valid = true;
    return l;
}

bool tc_viewer_sound_mix(const tc_viewer *v, const tc_sound_source *s, float event_volume, bool dolby, tc_sound_mix *out) {
    int vol = 127, pan = 64, rear = 0;
    if (s->kind != TC_SOUND_FLAT) {
        tc_sound_listener l = tcv_listener(v);
        if (s->emitter == TC_SOUND_EMITTER_POINT || !v->game || !v->game->has_cast || v->tape) {
            /* a place, or the main actor without a cast (at his own rolloff: the default one, Ed has none) */
            tc_vec3 at = s->emitter == TC_SOUND_EMITTER_POINT ? s->at
                       : v->game ? v->game->main.player.position : l.position;
            tc_vec3 off = tc_v3_sub(at, l.position);
            vol = tc_sound_volume(tc_sound_distance(off), NULL);
            pan = tc_sound_pan(off, l.right);
            rear = tc_sound_dolby(off, l.front);
        } else if (tc_cast_sound_lost(&v->game->cast, s))
            vol = -1; /* never to be heard: no voice for it */
        else
            vol = tc_cast_sound_place(&v->game->cast, s, &l, &pan, &rear);
    }
    if (vol < 0) {
        out->left = out->right = out->rear = 0;
        return false;
    }
    tc_sound_gains(vol, pan, rear, event_volume, dolby, out);
    return true;
}

/* Ed's sector after this frame's moves: his AI's, else tracked here for the simple controller. */
static void update_sectors(tc_viewer *v) {
    tc_game *p = v->game;
    if (!v->sectors || !p) return;
    if (p->main.has_driver) tc_ed_ai_track_sector(&p->main.driver);
    else tc_sector_follow(&v->ed_sector, v->sectors, p->main.player.position, p->main.player.placed);
}

uint16_t tc_viewer_ed_sector(const tc_viewer *v) {
    const tc_cast_actor *a;
    if (!v->sectors || !v->game) return TC_SECTOR_NONE;
    if (v->game->has_cast && (a = tc_cast_character(&v->game->cast)) != NULL && a->sector.sector != TC_SECTOR_NONE)
        return a->sector.sector; /* a chameleon holds the player: its sector is drawn, lit, heard */
    return v->game->main.has_driver ? v->game->main.driver.sector.sector : v->ed_sector.sector;
}

static void update_frame(tc_viewer *v, float dt) {
    const uint32_t skip_mask = SKIP_MASK;
    tc_game *g = v->game;
    bool fresh, action_fresh;
    double prof;
    if (g) tc_game_begin_frame(g, false);
    if (v->debug.open) { /* the game holds behind the menu */
        tcv_debug_frame(v, dt);
        if (v->debug.open) return;
    }
    fresh = (v->held & skip_mask & ~v->skip_held) != 0;
    action_fresh = (v->held & (1u << TC_VA_ACTION) & ~v->skip_held) != 0;
    v->skip_held = v->held & skip_mask;
    if (g) tc_game_begin_frame(g, action_fresh);
    if (g && tc_game_picture_hold(g, dt, fresh)) return; /* a scripted picture: the game holds (port: a fresh press skips it) */
    if (g && (g->phase == TC_GP_LOADING || g->phase == TC_GP_PENDING)) tcv_look(v, 0); /* the level is about to change: Ed waits */
    else if (v->playing) tcv_update_play(v, dt, fresh);
    else tcv_fly(v, dt);
    prof = TC_PROF_BEGIN();
    update_sectors(v);
    tcv_update_lighting(v, dt);
    TC_PROF_END(TC_PZ_LIGHTING, prof);
    prof = TC_PROF_BEGIN();
    tcv_update_portals(v, v->paused ? 0 : dt);
    tcv_update_light(v, v->paused ? 0 : dt); /* the game clock, as Ed's AI */
    TC_PROF_END(TC_PZ_PORTALS, prof);
    if (!v->paused) v->animation_time += dt * v->animation_speed;
}

void tc_viewer_update(tc_viewer *v, float dt) {
    double prof = TC_PROF_BEGIN();
    if (v->rec) tc_rec_frame_in(v->rec, v, dt);
    update_frame(v, dt);
    if (v->game) tc_game_pick_sounds(v->game); /* the events that play one of their clips at random (.ttlv SPIK) */
    if (v->rec) tc_rec_frame_out(v->rec, v, dt);
    TC_PROF_END(TC_PZ_UPDATE, prof);
}

void tc_viewer_step_speed(tc_viewer *v, int dir) {
    REC_DOOR(TC_RD_STEP_SPEED, dir, 0);
    static const float steps[] = {0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 0.75f, 1, 1.5f, 2};
    const int count = (int)(sizeof(steps) / sizeof(steps[0]));
    int i = 5, s;
    for (s = 0; s < count; s++)
        if (steps[s] >= v->animation_speed - 0.001f) { i = s; break; }
    i += dir > 0 ? 1 : -1;
    v->animation_speed = steps[i < 0 ? 0 : i >= count ? count - 1 : i];
}

void tc_viewer_toggle_pause(tc_viewer *v) {
    REC_DOOR(TC_RD_TOGGLE_PAUSE, 0, 0);
    v->paused = !v->paused;
    tcv_set_inspecting(v, v->paused && v->playing);
}

void tc_viewer_step_gamma(tc_viewer *v, bool up) {
    REC_DOOR(TC_RD_STEP_GAMMA, up, 0);
    v->gamma = fmaxf(0.5f, fminf(2.5f, v->gamma * (up ? 1.1f : 1 / 1.1f)));
}

void tc_viewer_set_game_menu(tc_viewer *v, bool on) {
    uint32_t h;
    if (on == v->game_menu) return;
    REC_DOOR(TC_RD_GAME_MENU, on ? 1 : 0, 0);
    if (on) {
        if (v->paused) tc_viewer_toggle_pause(v); /* out of photo mode first: its camera comes back */
        v->paused = true;
        tcv_set_inspecting(v, v->playing);
        /* The drift starts from the very view the game was paused on (the user, 2026-11-15: a zoom of its own made the
         * first frame jump): the camera and its zoom stay, and the turn and the rise ease in from rest (tcv_look). A
         * way to turn and to rise first of its own, from where the game is (not the viewer's rng: the transitions draw
         * it). */
        h = (uint32_t)(v->animation_time * 1000.0f) * 2654435761u ^ v->draw_count * 40503u;
        h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
        v->game_menu = true;
        v->game_menu_time = 0;
        v->game_menu_turn = h & 1 ? 1.0f : -1.0f;
        v->game_menu_phase = h & 2 ? 0.5f * TC_PI : -0.5f * TC_PI; /* cos(0.31 t -+ pi/2): no rise at the start */
    } else {
        v->game_menu = false;
        v->paused = false;
        /* port (the user, 2026-11-16): play goes on from where the drift left the camera, not back to the paused view
         * (the follow carries on from there; the drift's rise eases back as Ed moves). The zoom never changed. */
        tcv_camera_settle(v);
        v->inspecting = false;
    }
}

void tc_viewer_set_view(tc_viewer *v, float gamma, float animation_speed, bool fog, uint32_t lighting) {
    static const float steps[] = {0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 0.75f, 1, 1.5f, 2};
    int s, best = 5;
    for (s = 0; s < 8; s++)
        if (fabsf(steps[s] - animation_speed) < fabsf(steps[best] - animation_speed)) best = s;
    if (!(gamma == gamma)) gamma = 1;
    REC_DOOR(TC_RD_SET_VIEW, (fog ? 1 : 0) | (int32_t)((lighting & 7) << 1) | (best << 4), gamma);
    v->gamma = fmaxf(0.5f, fminf(2.5f, gamma));
    v->animation_speed = steps[best];
    v->fog_enabled = fog;
    if (v->level->has_lighting && lighting < TC_VL_COUNT) v->lighting = (tc_view_lighting)lighting;
}

uint32_t tc_viewer_save_serial(const tc_viewer *v) {
    return v->game && v->game->has_cast ? v->game->cast.globals.save_serial : 0;
}

uint8_t *tc_save_make(tc_viewer *v, const char *name, uint64_t time, size_t *size) {
    tc_game_view view;
    *size = 0;
    if (!v->game || v->tape) return NULL; /* nothing to save, or not a game: a demo tape */
    view = tcv_game_view(v);
    return tc_game_save_make(v->game, &view, name, time, size);
}

void tc_viewer_step_fov(tc_viewer *v, int dir) {
    REC_DOOR(TC_RD_STEP_FOV, dir, 0);
    /* in the original's measure, horizontal at 4:3: 86 degrees by default, 5 degree steps, 50-120 */
    float deg = roundf(tc_camera_hfov_43(v->camera.vertical_fov) * 180 / TC_PI) + (dir > 0 ? 5.0f : -5.0f);
    v->camera.vertical_fov = tc_camera_vfov_from_hfov_43(fmaxf(50, fminf(120, deg)) * TC_PI / 180);
}

/* The player's field of view: the camera's, or the one a director's shot (tc_viewer_camera.c) will give back. */
static float *player_fov(tc_viewer *v) { return v->director ? &v->director_saved_fov : &v->camera.vertical_fov; }

void tc_viewer_set_fov(tc_viewer *v, float degrees) {
    if (!(degrees == degrees)) degrees = 86;
    REC_DOOR(TC_RD_SET_FOV, 0, degrees);
    *player_fov(v) = tc_camera_vfov_from_hfov_43(fmaxf(50, fminf(120, degrees)) * TC_PI / 180);
}

float tc_viewer_fov(const tc_viewer *v) {
    float f = v->director ? v->director_saved_fov : v->camera.vertical_fov;
    return roundf(tc_camera_hfov_43(f) * 180 / TC_PI);
}

void tc_viewer_toggle_fog(tc_viewer *v) {
    REC_DOOR(TC_RD_FOG, 0, 0);
    v->fog_enabled = !v->fog_enabled;
}

void tc_viewer_cycle_lighting(tc_viewer *v) {
    REC_DOOR(TC_RD_LIGHTING, 0, 0);
    if (v->level->has_lighting) v->lighting = (tc_view_lighting)((v->lighting + 1) % TC_VL_COUNT);
}

void tc_viewer_cycle_sectors(tc_viewer *v) {
    REC_DOOR(TC_RD_SECTORS, 0, 0);
    if (v->sectors) v->sector_mode = (tc_view_sectors)((v->sector_mode + 1) % TC_VS_COUNT);
}

void tc_viewer_wheel(tc_viewer *v, float notches) {
    float step = powf(1.1f, notches);
    REC_DOOR(TC_RD_WHEEL, 0, notches);
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
    if (v->playing && tc_viewer_has_ai(v) && !v->game->use_ai) APPEND("%s", ", simple controller");
    if (v->playing && v->cam.closeup) APPEND("%s", ", close-up");
    if (v->playing && v->cam.first_person) APPEND("%s", ", first person");
    if (v->playing && v->director) APPEND("%s", ", scripted camera");
    {
        char custom[64];
        if (v->playing && v->game && tc_main_status(&v->game->main, custom, sizeof custom)) APPEND("%s", custom);
    }
    if (v->paused) APPEND("%s", ", paused");
    if (v->playing && v->game && v->game->has_cast && v->game->use_ai)
        APPEND(", cast %u/%u awake, lives %u, HP %d", (unsigned)v->game->cast.awake, (unsigned)v->game->cast.count,
               (unsigned)v->game->cast.globals.lives, (int)v->game->main.driver.ai.hit_points);
#undef APPEND
    return n;
}
