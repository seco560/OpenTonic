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

/* How near the drawn persos' surfaces come to point p (Ed's excluded), up to `within`: the camera's near plane needs its
 * radius clear all round, not only along the line the sight ray follows (a slanted surface beside the eye). */
static float perso_clearance(const tc_viewer *v, tc_vec3 p, float within) {
    float best = within;
    uint32_t i, k;
    for (i = 0; i < v->draw_count; i++) {
        const tc_view_draw *dr = &v->draws[i];
        const tc_mesh *m;
        const float *s;
        tc_vec3 c;
        float scale;
        if (dr->mesh < v->perso_first || dr->mesh >= v->ed_first) continue;
        if (dr->flags & (TC_FLAG_BILLBOARD | TC_FLAG_WATER | TC_FLAG_TRANSPARENT)) continue;
        if (v->play && tcx_sight_skips(v->play->custom, dr->mesh)) continue;
        m = v->meshes[dr->mesh];
        s = v->mesh_sphere + 4 * dr->mesh;
        c = model_point(&dr->model, s);
        scale = fmaxf(tc_v3_length(tc_v3(dr->model.m[0], dr->model.m[1], dr->model.m[2])),
                      fmaxf(tc_v3_length(tc_v3(dr->model.m[4], dr->model.m[5], dr->model.m[6])),
                            tc_v3_length(tc_v3(dr->model.m[8], dr->model.m[9], dr->model.m[10]))));
        if (tc_v3_distance(c, p) > s[3] * scale + best) continue;
        for (k = dr->first_index; k + 2 < dr->first_index + dr->index_count && k + 2 < m->index_count; k += 3) {
            tc_vec3 a = model_point(&dr->model, m->vertices[m->indices[k]].position);
            tc_vec3 b = model_point(&dr->model, m->vertices[m->indices[k + 1]].position);
            tc_vec3 q = model_point(&dr->model, m->vertices[m->indices[k + 2]].position);
            float d = tc_v3_distance(tc_closest_point_on_triangle(a, b, q, p), p);
            if (d < best) best = d;
        }
    }
    return best;
}

#define SIGHT_STEP 0.1f      /* how far in at a time the eye comes while a perso's surface is within its radius */

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
    { /* port (docs/BUGS.md "Camera Inside Persos"): where the eye would be (its radius short of what the ray met), a
       * surface beside it within the radius: in towards the pivot until clear (a slanted shoulder, a sleeve) */
        float at = fminf(block, len), dist = len > 0 ? at : 0;
        tc_vec3 dir = tc_v3_scale(d, 1 / len);
        while (dist - TC_CAM_RADIUS > end + SIGHT_STEP &&
               perso_clearance(v, tc_v3_add(from, tc_v3_scale(dir, dist - TC_CAM_RADIUS)), TC_CAM_RADIUS) < TC_CAM_RADIUS * 0.9f)
            dist -= SIGHT_STEP;
        if (dist < at) block = dist;
    }
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

/* The camera's dsgvar k as Ed's copy holds it (the cast's directors and characters write it there), 0 / false none. A
 * value that is not a number (what a primitive the port does not model returns: cavedoc's ruby lifts once took their
 * shot's elevation from such a FUNC, and the follow kept it for good) counts as none. */
static float cam_f(const tc_ed_ai *e, uint32_t k) {
    float x = tc_ai_other_dsgvar_float(&e->ai, (uint32_t)e->camera, k);
    return isfinite(x) ? x : 0;
}
static bool cam_v(const tc_ed_ai *e, uint32_t k, tc_vec3 *out) {
    return tc_ai_other_dsgvar_vec(&e->ai, (uint32_t)e->camera, k, out) && isfinite(out->x) && isfinite(out->y) &&
           isfinite(out->z);
}

/* Ed as the directed shots aim at him: his origin as drawn, and the aim height (the follow's record +0x24 offset, which
 * B8 keeps; B22 clears it: the rail aims at his origin). */
static tc_vec3 ed_drawn(const tc_viewer_play *p) {
    return p->use_ai ? tc_v3(p->pose_draw.m[12], p->pose_draw.m[13], p->pose_draw.m[14]) : p->main.player.position;
}

/* The eye of a fixed or rail shot looking at `at`, turned round it by the player's turn (tc_viewer.shot.look_*: yaw as
 * the follow's, rise lifts the eye); a turned eye stays this side of the level's surfaces (pulled in along the line
 * from the look point, as the follow's walls pull its eye in). */
static void look_from(tc_viewer *v, tc_vec3 eye, tc_vec3 at) {
    tc_vec3 d = tc_v3_sub(at, eye);
    float len = tc_v3_length(d);
    if (len > 1e-4f && (v->shot.look_yaw != 0 || v->shot.look_rise != 0)) {
        float yaw = atan2f(d.x, d.y) + v->shot.look_yaw;
        float pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT,
                                                       atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y)) - v->shot.look_rise));
        tc_vec3 dir = tc_v3(sinf(yaw) * cosf(pitch), cosf(yaw) * cosf(pitch), sinf(pitch)), back = tc_v3_scale(dir, -1);
        tc_hit hit;
        float reach = len;
        const uint8_t *solid = v->play->world.solid;
        v->play->world.solid = NULL; /* every surface (as the follow's) */
        if (tc_collision_raycast(&v->play->world, at, back, len, &hit)) reach = fmaxf(0.3f, hit.distance - TC_CAM_RADIUS);
        v->play->world.solid = solid;
        eye = tc_v3_add(at, tc_v3_scale(back, reach));
        d = tc_v3_sub(at, eye);
    }
    v->camera.position = eye;
    v->cam.hide_target = false; /* away from him: he is drawn */
    if (tc_v3_length(d) > 1e-4f) {
        v->camera.yaw = atan2f(d.x, d.y);
        v->camera.pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT, atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y))));
    }
}

/* The fixed shot: the camera's shot 6 (camera reflex 2 -> B8, then B7 every tick; its dsgvars as the scripts that pick it
 * write them, TC_AI_CAMERA_FIXED_*). B8: with dsg89 1 it moves to the place dsg92 (B7's Action_Camera_GoToPosition,
 * META 30: the camera's own mechanics carry it until within 0.1), else it is put there; it looks at its target perso
 * (dsg91 1: Ed) or at the point dsg93, which with dsg95 1 it eases to (dsg2 += (dsg93 - dsg2) / dsg94 per tick). Port:
 * the move to the place is an ease (TC_VIEW_FIXED_GLIDE_TIME: the camera's mechanics' speed is not read). */
#define TC_VIEW_FIXED_GLIDE_TIME 0.35f
static bool update_fixed(tc_viewer *v, const tc_ed_ai *e, float dt) {
    tc_vec3 place, point, target;
    bool glide, at_ed, ease;
    float steps;
    if (!cam_v(e, TC_AI_CAMERA_FIXED_PLACE, &place)) return false;
    glide = cam_f(e, TC_AI_CAMERA_FIXED_GLIDE) == 1;
    at_ed = cam_f(e, TC_AI_CAMERA_FIXED_TARGET) == 1;
    ease = cam_f(e, TC_AI_CAMERA_FIXED_EASE) == 1;
    steps = cam_f(e, TC_AI_CAMERA_FIXED_STEPS);
    if (!cam_v(e, TC_AI_CAMERA_FIXED_POINT, &point)) point = place;
    target = tc_v3_add(ed_drawn(v->play), tc_v3(0, 0, TC_CAM_AIM_HEIGHT));
    if (!v->shot.on) { /* B8: from where the camera is, or put at the place; the look starts at its point */
        v->shot.eye = glide ? v->camera.position : place;
        v->shot.look = at_ed ? target : point;
    }
    if (glide) v->shot.eye = tc_v3_add(v->shot.eye, tc_v3_scale(tc_v3_sub(place, v->shot.eye),
                                                                  1 - expf(-dt / TC_VIEW_FIXED_GLIDE_TIME)));
    else v->shot.eye = place;
    if (at_ed) v->shot.look = target;
    else if (ease && steps > 1) /* 1 / steps of the way per camera tick (30 a second) */
        v->shot.look = tc_v3_add(v->shot.look, tc_v3_scale(tc_v3_sub(point, v->shot.look),
                                                             1 - powf(1 - 1 / steps, dt * 30)));
    else v->shot.look = point;
    look_from(v, v->shot.eye, v->shot.look);
    return true;
}

/* The rail: shot 4 (camera B22 -> B21). The eye on the segment dsg82 -> dsg83 at Ed's place along it (his origin
 * projected, held within the ends, plus dsg85 of its length), eased there by dsg84 (at most 10: (1.95 - 0.15 dsg84) / 9
 * of the way per tick; 0: at once), mode 3: looking at him. A segment of no length ends the shot (B21: dsg39 := 0). */
static bool update_rail(tc_viewer *v, const tc_ed_ai *e, float dt) {
    tc_vec3 a, b, ab, ed = ed_drawn(v->play), point;
    float len2, s, smooth;
    if (!cam_v(e, 82, &a) || !cam_v(e, 83, &b)) return false;
    ab = tc_v3_sub(b, a);
    len2 = tc_v3_dot(ab, ab);
    if (len2 <= 0) return false;
    s = tc_v3_dot(tc_v3_sub(ed, a), ab) / len2;
    s = fminf(1, fmaxf(0, s)) + cam_f(e, 85);
    point = tc_v3_add(a, tc_v3_scale(ab, s));
    smooth = fminf(10, cam_f(e, 84));
    if (!v->shot.on) v->shot.eye = smooth != 0 ? v->camera.position : point;
    if (smooth != 0) {
        float k = (1.95f - 0.15f * smooth) / 9;
        k = k <= 0 ? 0 : k >= 1 ? 1 : 1 - powf(1 - k, dt * 30);
        v->shot.eye = tc_v3_add(v->shot.eye, tc_v3_scale(tc_v3_sub(point, v->shot.eye), k));
    } else v->shot.eye = point;
    look_from(v, v->shot.eye, ed);
    return true;
}

/* The directors' follows, shots 2, 7 and 8 (tc_cam_target.shot_*): 2 (camera B5 -> B28) the follow at the base distance
 * (dsg76 + dsg77) / 2 and the camera's elevation dsg78; 7 (B10 -> B9) and 8 (B20 -> B19) at dsg102 (at least 1.6) and
 * elevation dsg103, the camera on the side of Ed the direction dsg106 gives (SetCameraPivotOffset op 100: FUN_004a1310
 * takes -it for his front): 7 the director's dsg100, turned round at the start when dsg104 is 1 and Ed faces along it;
 * 8 dsg100 while Ed faces along dsg107, its mirror in dsg107 (dsg108) while he faces against it, each switch after
 * 600 ms of facing the other way (the camera's flag word 2 not read: its 600 ms default). */
static void director_follow(tc_viewer *v, const tc_ed_ai *e, int shot, tc_cam_target *t, float dt) {
    tc_vec3 dir, facing = tc_v3(sinf(v->play->main.player.heading), -cosf(v->play->main.player.heading), 0);
    if (shot == TC_CAM_SHOT_NEAR) {
        float lo = cam_f(e, 76), hi = cam_f(e, 77);
        if (lo + hi <= 0) return;
        t->shot_distance = (lo + hi) / 2;
        t->shot_elevation = cam_f(e, 78);
        return;
    }
    if (!cam_v(e, 100, &dir)) return;
    dir.z = 0;
    if (tc_v3_length(dir) < 1e-4f) return;
    dir = tc_v3_normalize(dir);
    t->shot_distance = fmaxf(1.6f, cam_f(e, 102));
    t->shot_elevation = cam_f(e, 103);
    if (shot == TC_CAM_SHOT_AXIS) {
        if (!v->shot.on) v->shot.axis_flip = cam_f(e, 104) == 1 && facing.x * dir.x + facing.y * dir.y > 0;
        if (v->shot.axis_flip) dir = tc_v3_scale(dir, -1);
    } else { /* the mirror */
        tc_vec3 m;
        float along;
        if (!cam_v(e, 107, &m)) m = tc_v3(0, 0, 0);
        along = facing.x * m.x + facing.y * m.y;
        if (!v->shot.on) { /* B20: its state by the way he faces now, its clock from now */
            v->shot.mirror_state = along >= 0;
            v->shot.mirror_since = 0;
        }
        if ((v->shot.mirror_state == 0 && along >= 0) || (v->shot.mirror_state == 1 && along < 0)) v->shot.mirror_since = 0;
        else if ((v->shot.mirror_since += dt) >= 0.6f) {
            v->shot.mirror_state = !v->shot.mirror_state;
            v->shot.mirror_since = 0;
        }
        if (v->shot.mirror_state) { /* dsg108: dsg100 mirrored in dsg107 (B20's formula) */
            tc_vec3 r = tc_v3(dir.x * m.y * m.y - 2 * dir.y * m.x * m.y - dir.x * m.x * m.x,
                              dir.y * m.x * m.x - 2 * dir.x * m.x * m.y - dir.y * m.y * m.y, 0);
            if (tc_v3_length(r) > 1e-4f) dir = tc_v3_normalize(r);
        }
    }
    t->shot_axis = true;
    t->shot_yaw = atan2f(-dir.x, -dir.y) + v->shot.look_yaw; /* the camera at Ed + dir: looking along -dir; the player's
                                                               * turn kept on top */
}

/* The scripted shots (2, 4, 6, 7, 8: the cast's directors and characters; docs/CAMERA.md "The directors' shots"). Port
 * (the user, 2026-11-13): the player turns the shot's camera (tc_viewer.shot.look_*: the eye round its look point, a
 * follow's direction), and takes it back from the shots that do not follow Ed (4, 6) with a fresh Jump, Action or
 * Shoot, until the scripts pick another shot or place. Returns whether the eye is the shot's (4, 6); a follow (2, 7,
 * 8) fills t. */
static bool scripted_shot(tc_viewer *v, int shot, tc_cam_target *t, float dt, bool fresh) {
    const tc_viewer_play *p = v->play;
    const tc_ed_ai *e = p->main.has_driver ? &p->main.driver : NULL;
    bool eye = shot == TC_CAM_SHOT_FIXED || shot == TC_CAM_SHOT_RAIL;
    bool follow = shot == TC_CAM_SHOT_NEAR || shot == TC_CAM_SHOT_AXIS || shot == TC_CAM_SHOT_MIRROR;
    tc_vec3 sig = tc_v3(0, 0, 0);
    bool on = false;
    if (e && e->camera >= 0 && (eye || follow)) { /* what the scripts gave: a new one takes the camera again */
        uint32_t k = shot == TC_CAM_SHOT_FIXED ? TC_AI_CAMERA_FIXED_PLACE : shot == TC_CAM_SHOT_RAIL ? 82u
                   : shot == TC_CAM_SHOT_NEAR ? 76u : 100u;
        if (!cam_v(e, k, &sig)) sig = tc_v3(cam_f(e, k), shot == TC_CAM_SHOT_NEAR ? cam_f(e, 77) : 0, 0);
    }
    if (shot != v->shot.seen || tc_v3_distance(sig, v->shot.sig) > 1e-3f) {
        v->shot.seen = shot;
        v->shot.sig = sig;
        v->shot.released = false;
        v->shot.look_yaw = v->shot.look_rise = 0;
        v->shot.on = false; /* entered afresh */
    }
    if (v->shot.on) { /* the player turns the shot's camera (port) */
        v->shot.look_yaw += v->look_yaw_frame;
        v->shot.look_rise = fmaxf(-1.2f, fminf(1.2f, v->shot.look_rise + v->look_rise_frame));
    }
    if (v->shot.on && eye && fresh) { /* a fresh Jump, Action or Shoot takes it back from a fixed or rail eye */
        v->shot.released = true;
        v->cam.placed = false; /* the follow, placed afresh behind Ed */
    }
    if (e && e->camera >= 0 && !v->shot.released) {
        if (shot == TC_CAM_SHOT_FIXED) on = update_fixed(v, e, dt);
        else if (shot == TC_CAM_SHOT_RAIL) on = update_rail(v, e, dt);
        else if (follow) {
            director_follow(v, e, shot, t, dt);
            on = t->shot_distance > 0;
        }
    }
    if (!on && v->shot.on && eye) v->cam.placed = false;
    v->shot.on = on;
    return on && eye;
}

bool tc_viewer_fixed_shot(const tc_viewer *v) { return v->shot.on; }

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
        t.shot_distance = t.shot_elevation = t.shot_yaw = 0;
        t.shot_axis = false;
        if (!update_director(v, fresh) && !scripted_shot(v, t.shot, &t, dt, fresh)) tc_cam_update(&v->cam, &v->camera, &t, dt);
        if (p->has_cast) { /* the current record, as the scripts' FUNC 59-66 read it: the follow's (camera B28's bands) */
            tc_ai_globals *g = &p->cast.globals;
            float d = v->cam.distance;
            g->cam_now[0] = d - 0.3f, g->cam_now[1] = d + 0.3f, g->cam_now[2] = d - 1.3f, g->cam_now[3] = d + 3;
            g->cam_now[4] = 0, g->cam_now[5] = 0.05f, g->cam_now[6] = v->cam.elevation, g->cam_now[7] = 0;
            g->has_cam_now = isfinite(d) && isfinite(v->cam.elevation);
        }
        v->looked = false;
        v->look_yaw_frame = v->look_rise_frame = 0;
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
