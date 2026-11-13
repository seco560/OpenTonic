/* The viewer's play camera: its update, the sight test against the drawn persos, the director's shot, its settings. */
#include "tc_viewer_internal.h"

static float camera_sight_test(tc_viewer *v, tc_vec3 from, tc_vec3 to, float *inside);

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

void tcv_mesh_spheres(tc_viewer *v) {
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
    /* the parts of the cast's actors without collision of their own */
    if ((v->mesh_ghost = (uint8_t *)tc_calloc(v->mesh_count ? v->mesh_count : 1, 1)) != NULL && v->play && v->play->has_cast) {
        uint32_t first = v->perso_first;
        for (i = 0; i < v->level->perso_count; first += v->level->persos[i].part_count, i++) {
            const tc_cast_actor *a = tc_cast_actor_of(&v->play->cast, i);
            for (k = 0; a && !a->obstacle && k < v->level->persos[i].part_count && first + k < v->mesh_count; k++)
                v->mesh_ghost[first + k] = 1;
        }
    }
}

#define SIGHT_FAR 30.0f      /* how long the rays are that count crossings (in or out of the persos) */

#define SIGHT_CROSSINGS 64

#define SIGHT_ROOM 0.85f     /* a gap between two solids the eye fits in: twice the camera's radius and a little */

/* The drawn persos' crossings of the ray from + d x t, t in 0..1: up to cap of them into ts (unordered), the count (of
 * all of them) returned. Persos only (not Ed, statics, pieces), and nothing see-through. ghosts: those of the cast's
 * actors Ed does not collide with (tc_viewer.mesh_ghost), else those of all the others. */
static uint32_t perso_crossings(const tc_viewer *v, bool ghosts, tc_vec3 from, tc_vec3 d, float *ts, uint32_t cap) {
    float len2 = tc_v3_dot(d, d);
    uint32_t i, k, n = 0;
    for (i = 0; i < v->draw_count; i++) {
        const tc_view_draw *dr = &v->draws[i];
        const tc_mesh *m;
        const float *s;
        tc_vec3 c, rel;
        float scale, r, along;
        if (dr->mesh < v->perso_first || dr->mesh >= v->ed_first) continue;
        if (dr->flags & (TC_FLAG_BILLBOARD | TC_FLAG_WATER | TC_FLAG_TRANSPARENT)) continue;
        if ((v->mesh_ghost && v->mesh_ghost[dr->mesh]) != ghosts) continue;
        if (v->play && tcx_sight_skips(v->play->custom, dr->mesh)) continue;
        m = v->meshes[dr->mesh];
        s = v->mesh_sphere + 4 * dr->mesh;
        c = model_point(&dr->model, s);
        scale = fmaxf(tc_v3_length(tc_v3(dr->model.m[0], dr->model.m[1], dr->model.m[2])),
                      fmaxf(tc_v3_length(tc_v3(dr->model.m[4], dr->model.m[5], dr->model.m[6])),
                            tc_v3_length(tc_v3(dr->model.m[8], dr->model.m[9], dr->model.m[10]))));
        r = s[3] * scale;
        rel = tc_v3_sub(c, from);
        along = fmaxf(0, fminf(1, tc_v3_dot(rel, d) / len2));
        if (tc_v3_length(tc_v3_sub(rel, tc_v3_scale(d, along))) > r) continue; /* the ray misses its sphere */
        TC_PROF_COUNT(TC_PC_SIGHT_TRIS, dr->index_count / 3);
        for (k = dr->first_index; k + 2 < dr->first_index + dr->index_count && k + 2 < m->index_count; k += 3) {
            float t = segment_tri_t(from, d, model_point(&dr->model, m->vertices[m->indices[k]].position),
                                    model_point(&dr->model, m->vertices[m->indices[k + 1]].position),
                                    model_point(&dr->model, m->vertices[m->indices[k + 2]].position));
            if (t > 1) continue;
            if (n < cap) ts[n] = t;
            n++;
        }
    }
    return n;
}

/* tc_cam_sight_fn over the persos as drawn last frame: the nearest crossing, as the exe's ray. Port, for the cast's
 * actors Ed does not collide with (they can stand in him, and the eye was put inside them): the crossings of a long ray,
 * in order, are where it goes into and out of them (a perso's parts are open shells that close together, so they are
 * counted as one). An odd count says `from` is inside one, which two more rays from it must agree with (a sheet crosses
 * once too). Then *inside is where that solid ends, and the share returned is up to the next thing after it. */
static float camera_sight(void *user, tc_vec3 from, tc_vec3 to, float *inside) {
    double prof = TC_PROF_BEGIN();
    float r = camera_sight_test((tc_viewer *)user, from, to, inside);
    TC_PROF_END(TC_PZ_SIGHT, prof);
    return r;
}

static float camera_sight_test(tc_viewer *v, tc_vec3 from, tc_vec3 to, float *inside) {
    static const float other[2][3] = {{0.9f, -0.37f, 0.22f}, {-0.41f, 0.88f, -0.24f}};
    tc_vec3 d = tc_v3_sub(to, from);
    float len2 = tc_v3_dot(d, d), len, far_len, ts[SIGHT_CROSSINGS], skip[1], end = 0, block = INFINITY, solid = INFINITY;
    uint32_t n, k, j;
    *inside = 0;
    if (len2 < 1e-10f || !v->draws) return 1;
    tcv_mesh_spheres(v);
    if (!v->mesh_sphere) return 1;
    len = sqrtf(len2);
    n = perso_crossings(v, false, from, d, ts, SIGHT_CROSSINGS);
    for (k = 0; k < n && k < SIGHT_CROSSINGS; k++) solid = fminf(solid, ts[k] * len);
    far_len = fmaxf(len, SIGHT_FAR);
    n = perso_crossings(v, true, from, tc_v3_scale(d, far_len / len), ts, SIGHT_CROSSINGS);
    if (n > SIGHT_CROSSINGS) n = SIGHT_CROSSINGS - ((n ^ SIGHT_CROSSINGS) & 1); /* too many: the nearest, the parity kept */
    for (k = 1; k < n; k++) { /* in order */
        float t = ts[k];
        for (j = k; j > 0 && ts[j - 1] > t; j--) ts[j] = ts[j - 1];
        ts[j] = t;
    }
    for (k = 0; k < n; k++) ts[k] *= far_len;
    k = 0;
    if ((n & 1) &&
        (perso_crossings(v, true, from, tc_v3(other[0][0] * SIGHT_FAR, other[0][1] * SIGHT_FAR, other[0][2] * SIGHT_FAR), skip, 0) & 1) &&
        (perso_crossings(v, true, from, tc_v3(other[1][0] * SIGHT_FAR, other[1][1] * SIGHT_FAR, other[1][2] * SIGHT_FAR), skip, 0) & 1)) {
        /* inside: out at ts[0], then on through whatever follows too closely for the eye to fit between */
        end = ts[0];
        for (k = 1; k + 1 < n && ts[k] < end + SIGHT_ROOM; k += 2) end = ts[k + 1];
        if (end > len || solid < end + SIGHT_ROOM) end = 0, k = 0; /* no room beyond it within reach: as the exe's ray */
    }
    if (k < n) block = ts[k];
    *inside = end;
    block = fminf(block, solid);
    return block < len ? block / len : 1;
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
    want = g->cam_mode == 6 && g->cam_target != 0 && !v->director_released &&
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

void tcv_update_camera(tc_viewer *v, float dt, bool fresh) {
    tc_viewer_play *p = v->play;
    double prof = TC_PROF_BEGIN();
    /* The play camera (tc_cam.h): round Ed as drawn, turned by the player; against every surface of the level (no sector
     * row: the camera must not see through anything). */
    {
        tc_cam_target t;
        const uint8_t *solid = p->world.solid;
        t.position = p->use_ai ? tc_v3(p->pose_draw.m[12], p->pose_draw.m[13], p->pose_draw.m[14]) : p->main.player.position;
        t.behind = TC_PI - p->main.player.heading; /* camera forward (sin yaw, cos yaw) = Ed's front (sin h, -cos h) */
        if (p->use_ai && p->ski_shot) /* his facing as drawn (blended between his AI's ticks, as his position is), not
                                       * the last tick's: the ski follow holds within 0.05 rad of it and stepped at 30 Hz
                                       * with it */
            t.behind -= remainderf(atan2f(p->pose_draw.m[1], p->pose_draw.m[0]) - atan2f(p->main.pose_cur.m[1], p->main.pose_cur.m[0]),
                                   2 * TC_PI); /* behind = pi - heading: a heading change counts negative */
        t.speed = v->inspecting ? 0 : tc_player_horizontal_speed(&p->main.player); /* paused he stands, whatever he was doing */
        t.velocity = v->inspecting ? tc_v3(0, 0, 0) : p->main.player.velocity;
        t.distance = v->orbit_distance;
        t.situation = tc_viewer_camera_situation(v);
        /* riding on a sliding stage (the skier is the player there: the cast's main alias; Ed's own slides on the walking
         * levels use the same card and keep the walking camera, his scripts setting the shot to 0): tc_viewer_camera_shot */
        if (p->use_ai && p->main.has_driver && p->main.driver.dyn.card_type == 2 && v->level->cast.main_alias != 0) p->ski_shot = true;
        t.shot = tc_viewer_camera_shot(v);
        t.chase = 0;
        tc_main_camera_target(&p->main, &t); /* a character in the slot (the car) says where to look */
        t.world = &p->world;
        t.sight = camera_sight; /* the persos as drawn last frame */
        t.sight_user = v;
        p->world.solid = NULL;
        tcx_camera_obstacles(p->custom, false); /* custom gameplay's cars don't stop the camera */
        if (!update_director(v, fresh)) tc_cam_update(&v->cam, &v->camera, &t, dt);
        tcx_camera_obstacles(p->custom, true);
        p->world.solid = solid;
    }
    TC_PROF_END(TC_PZ_CAMERA, prof);
}

void tc_viewer_paused_look(tc_viewer *v, float dt) {
    tcv_look(v, dt);
    if (v->playing && v->play) tcv_update_camera(v, dt, false);
}

void tc_viewer_recenter_camera(tc_viewer *v) {
    REC_DOOR(TC_RD_RECENTER, 0, 0);
    if (v->playing) tc_cam_recenter(&v->cam);
}

void tc_viewer_toggle_closeup(tc_viewer *v) {
    REC_DOOR(TC_RD_CLOSEUP, 0, 0);
    if (v->playing) v->cam.closeup = !v->cam.closeup;
}

void tc_viewer_toggle_first_person(tc_viewer *v) {
    REC_DOOR(TC_RD_FIRST_PERSON, 0, 0);
    if (!v->playing || !v->play) return;
    v->cam.first_person = !v->cam.first_person;
    v->camera.yaw = TC_PI - v->play->main.player.heading; /* entering: looking the way he faces; leaving: behind him */
    v->camera.pitch = v->cam.first_person ? 0 : -TC_CAM_ELEVATION;
    v->cam.placed = false;
}

int tc_viewer_camera_shot(const tc_viewer *v) {
    const tc_viewer_play *p = v->play;
    int shot;
    if (!p || !p->use_ai || !p->main.has_driver || p->main.driver.camera < 0) return -1;
    shot = (int)tc_ai_other_dsgvar_float(&p->main.driver.ai, (uint32_t)p->main.driver.camera, TC_AI_CAMERA_SHOT_DSG);
    /* The sliding stages' start trigger (Totalski's YBA_GenD_DepartMap: once the skier is in his riding behaviour, the
     * camera's dsg39 := 5, then it removes itself) is not in the cast: the converter reduces it to the port's ski start
     * (PROC 6, tools/ttre/cast.py). Its write stands here: once he has ridden (his skiing card, type 2), the ski follow,
     * unless the scripts pick a shot of their own (5 or 6). Only there: the skier is the player (the cast's main alias). */
    if (p->ski_shot && shot != TC_CAM_SHOT_SKI && shot != TC_CAM_SHOT_FIXED) shot = TC_CAM_SHOT_SKI;
    return shot;
}

int tc_viewer_camera_situation(const tc_viewer *v) {
    const tc_viewer_play *p = v->play;
    if (!p || !p->use_ai || !p->main.has_driver || p->main.driver.camera < 0) return -1;
    return (int)tc_ai_other_dsgvar_float(&p->main.driver.ai, (uint32_t)p->main.driver.camera, 40);
}

static void set_look(tc_viewer *v, float sensitivity, bool invert_x, bool invert_y) {
    v->look_sensitivity = sensitivity > 0 ? fmaxf(0.25f, fminf(4, sensitivity)) : 1;
    v->invert_x = invert_x;
    v->invert_y = invert_y;
}

void tc_viewer_set_look(tc_viewer *v, float sensitivity, bool invert_x, bool invert_y) {
    REC_DOOR(TC_RD_SET_LOOK, (invert_x ? 1 : 0) | (invert_y ? 2 : 0), sensitivity);
    set_look(v, sensitivity, invert_x, invert_y);
}

void tc_viewer_step_look(tc_viewer *v, int dir) {
    float s = v->look_sensitivity * (dir > 0 ? 1.25f : 0.8f);
    REC_DOOR(TC_RD_STEP_LOOK, dir, 0);
    if (fabsf(s - 1) < 0.01f) s = 1; /* the steps land back on 1 exactly */
    set_look(v, s, v->invert_x, v->invert_y);
}

void tc_viewer_toggle_invert_y(tc_viewer *v) {
    REC_DOOR(TC_RD_INVERT_Y, 0, 0);
    v->invert_y = !v->invert_y;
}
