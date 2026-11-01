#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_cam.h"
#include "tonic/tc_mec.h"

/* ---- the view ---- */

void tc_camera_init(tc_camera *c) {
    c->position = tc_v3(0, 0, 0);
    c->yaw = 0;
    c->pitch = 0;
    c->vertical_fov = TC_CAMERA_FOV;
    c->near_z = 0.05f;
    c->far_z = 2000;
}

tc_vec3 tc_camera_forward(const tc_camera *c) {
    return tc_v3(sinf(c->yaw) * cosf(c->pitch), cosf(c->yaw) * cosf(c->pitch), sinf(c->pitch));
}

tc_vec3 tc_camera_right(const tc_camera *c) { return tc_v3(cosf(c->yaw), -sinf(c->yaw), 0); }

static tc_mat4 look_at(tc_vec3 eye, tc_vec3 center, tc_vec3 up) {
    tc_vec3 f = tc_v3_normalize(tc_v3_sub(center, eye));
    tc_vec3 s = tc_v3_normalize(tc_v3_cross(f, up));
    tc_vec3 u = tc_v3_cross(s, f);
    tc_mat4 r = {{s.x, u.x, -f.x, 0,
                  s.y, u.y, -f.y, 0,
                  s.z, u.z, -f.z, 0,
                  -tc_v3_dot(s, eye), -tc_v3_dot(u, eye), tc_v3_dot(f, eye), 1}};
    return r;
}

/* Reversed depth (TC_DEPTH_REVERSED): 1 at near, 0 at far, tested GREATER_OR_EQUAL as the original's driver does
 * (GliDX6vr.dll sets D3DRENDERSTATE_ZFUNC 7, GREATEREQUAL, at 0x10012fe1: depth grows towards the camera, ties go to the
 * later draw). With a float depth buffer that also spreads the precision evenly over distance: the standard mapping
 * (near 0.05, LESS) z-fought decals 0.01 off their wall from ~100 units (Agent XYZ's eyes on his newspaper, sud0's
 * doors; docs/NOTES.md "Depth"). Clip space z 0..1 (Metal, SDL_GPU). */
static tc_mat4 perspective(float fov_y, float aspect, float near_z, float far_z) {
    float y = 1 / tanf(fov_y / 2), x = y / aspect, k = near_z / (far_z - near_z);
    tc_mat4 r = {{x, 0, 0, 0,
                  0, y, 0, 0,
                  0, 0, k, -1,
                  0, 0, k * far_z, 0}};
    return r;
}

float tc_camera_hfov_43(float vertical_fov) { return 2 * atanf(tanf(vertical_fov / 2) * (4.0f / 3)); }
float tc_camera_vfov_from_hfov_43(float hfov) { return 2 * atanf(tanf(hfov / 2) * 0.75f); }

float tc_camera_fov_y(const tc_camera *c, float aspect) {
    if (aspect >= 4.0f / 3 || aspect <= 0) return c->vertical_fov;
    return 2 * atanf(tanf(tc_camera_hfov_43(c->vertical_fov) / 2) / aspect); /* narrower: the horizontal held */
}

tc_mat4 tc_camera_view_projection(const tc_camera *c, float aspect) {
    tc_mat4 p = perspective(tc_camera_fov_y(c, aspect), aspect, c->near_z, c->far_z);
    tc_mat4 v = look_at(c->position, tc_v3_add(c->position, tc_camera_forward(c)), tc_v3(0, 0, 1));
    return tc_mat4_mul(&p, &v);
}

/* ---- the play camera ---- */

static float wrap(float a) {
    a = fmodf(a + TC_PI, 2 * TC_PI);
    return a < 0 ? a + TC_PI : a - TC_PI;
}

/* The share of the way to close this frame for a first-order lag of time constant tau (frame-rate independent). */
static float ease(float dt, float tau) { return dt <= 0 ? 0 : 1 - expf(-dt / tau); }

void tc_cam_reset(tc_cam *c) {
    c->since_look = 1e6f;
    c->rise = 0;
    c->recentering = false;
    c->closeup = false;
    c->first_person = false;
    c->stretch = -1;
    c->rope_x = c->rope_y = 0;
    c->pitch = -TC_CAM_ELEVATION;
    c->distance = -1;
    c->reach = -1;
    c->pull = 0;
    c->holding = false;
    c->hold_z = c->hold_ref = c->hold_offset = 0;
}

void tc_cam_look(tc_cam *c, tc_camera *view, float yaw, float rise) {
    view->yaw = wrap(view->yaw + yaw);
    if (c->first_person) view->pitch = fmaxf(-1.4f, fminf(1.4f, view->pitch - rise)); /* looking, not orbiting */
    else c->rise = fmaxf(TC_CAM_RISE_MIN, fminf(TC_CAM_RISE_MAX, c->rise + rise));
    c->since_look = 0;
    c->recentering = false;
}

void tc_cam_recenter(tc_cam *c) { c->recentering = true; }

/* How far the camera's sphere gets from the pivot along dir (unit) before it meets the level, up to distance. */
static float clear_distance(tc_collision *world, tc_vec3 pivot, tc_vec3 dir, float distance) {
    tc_ai_sphere_set body; /* one sphere at the origin */
    tc_mec_hit hit;
    if (!world || distance <= 0) return distance;
    memset(&body, 0, sizeof(body));
    body.spheres[0].radius = TC_CAM_RADIUS;
    body.spheres[0].rebound = 1;
    body.spheres[0].grip = 1;
    body.count = 1;
    if (!tc_mec_sweep(world, &body, 0, pivot, tc_v3_add(pivot, tc_v3_scale(dir, distance)), &hit)) return distance;
    return hit.t * distance - 0.02f; /* just short of touching */
}

/* The jump's height hold, from Ed's situation (TC_CAM_SIT_JUMP): on at the jump with the camera's height as it is; off
 * when he is back to normal, flies, or his origin is 3 below his take-off pivot less 0.2 (B17: Ed's z < dsg74 - 3). */
static void hold_height(tc_cam *c, const tc_camera *view, const tc_cam_target *t, tc_vec3 pivot) {
    int s = t->situation;
    if (s == TC_CAM_SIT_JUMP && !c->holding && c->reach >= 0) {
        c->holding = true;
        c->hold_z = view->position.z;
        c->hold_ref = pivot.z - 0.2f;
    }
    if (c->holding && ((s != TC_CAM_SIT_JUMP && s != TC_CAM_SIT_FALL) || t->position.z < c->hold_ref - 3))
        c->holding = false;
}

void tc_cam_update(tc_cam *c, tc_camera *view, const tc_cam_target *t, float dt) {
    tc_vec3 pivot = tc_v3_add(t->position, tc_v3(0, 0, TC_CAM_AIM_HEIGHT)), dir, want;
    float zoom = t->distance / TC_CAM_DISTANCE, distance = t->distance, elevation = TC_CAM_ELEVATION;
    float stretch = TC_CAM_FRONT_STRETCH, length, allowed, pull, off, pitch;
    bool first = c->reach < 0, held;
    tc_camera along;
    /* the preset */
    if (t->situation == TC_CAM_SIT_DYING) distance = TC_CAM_DYING_DISTANCE * zoom, stretch = 1;
    else if (t->situation == TC_CAM_SIT_FLIGHT)
        distance = TC_CAM_FLIGHT_DISTANCE * zoom, elevation = TC_CAM_FLIGHT_ELEVATION, stretch = 1;
    else if (c->closeup)
        distance = TC_CAM_CLOSEUP_DISTANCE * zoom, elevation = TC_CAM_CLOSEUP_ELEVATION, stretch = TC_CAM_CLOSEUP_STRETCH;
    c->distance = first || c->distance < 0 ? distance : c->distance + (distance - c->distance) * ease(dt, TC_CAM_DISTANCE_TIME);
    distance = c->distance;
    c->since_look += dt;
    { /* his velocity smoothed for the rope (it steps at the AI's 30 Hz and flickers along walls) */
        float k = first ? 1 : ease(dt, TC_CAM_ROPE_TIME);
        c->rope_x += (t->velocity.x - c->rope_x) * k;
        c->rope_y += (t->velocity.y - c->rope_y) * k;
    }
    if (c->first_person && !t->manual) { /* at his head, where the player looks; nothing else */
        view->position = tc_v3_add(t->position, tc_v3(0, 0, TC_CAM_EYE_HEIGHT));
        c->reach = -1; /* the follow is placed afresh afterwards */
        c->holding = false;
        c->hold_offset = 0;
        return;
    }
    if (!t->manual) {
        /* the orbit: the player's, eased back behind a moving Ed once the player leaves it alone (or recentres) */
        off = wrap(view->yaw - t->behind);
        if (first) {
            view->yaw = t->behind;
            c->pitch = -elevation;
        } else if (c->recentering) {
            float k = ease(dt, TC_CAM_RECENTER_TIME);
            view->yaw = wrap(view->yaw - off * k);
            c->rise -= c->rise * k;
            if (fabsf(wrap(view->yaw - t->behind)) < 0.005f && fabsf(c->rise) < 0.005f) {
                view->yaw = t->behind; /* there: exactly */
                c->rise = 0;
                c->recentering = false;
            }
        } else if (t->speed > TC_CAM_MOVING_SPEED && c->since_look >= TC_CAM_GRACE && c->distance > 0.1f) {
            /* the rope: Ed's speed across the view (along the camera's right) over the follow's distance turns it (not
             * over the distance a wall leaves: pulled in close, that turned it wildly) */
            float across = c->rope_x * cosf(view->yaw) - c->rope_y * sinf(view->yaw);
            across = across > 0 ? fmaxf(0, across - TC_CAM_ROPE_DEAD) : fminf(0, across + TC_CAM_ROPE_DEAD);
            view->yaw = wrap(view->yaw + TC_CAM_ROPE * across / c->distance * dt);
            c->rise -= c->rise * ease(dt, TC_CAM_RELAX_TIME);
        }
        c->pitch += (-elevation - c->pitch) * (first ? 1 : ease(dt, TC_CAM_FOLLOW_TIME));
        off = wrap(view->yaw - t->behind);
        stretch = 1 + (stretch - 1) * fabsf(off) / TC_PI;
        c->stretch = c->stretch < 0 || first ? stretch : c->stretch + (stretch - c->stretch) * ease(dt, TC_CAM_STRETCH_TIME);
        distance *= c->stretch;
        held = c->holding;
        hold_height(c, view, t, pivot);
        along = *view;
        along.pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT, c->pitch - c->rise));
        want = tc_v3_sub(pivot, tc_v3_scale(tc_camera_forward(&along), distance));
        if (c->holding || held) c->hold_offset = c->hold_z - want.z; /* letting go: from where it is */
        else c->hold_offset -= c->hold_offset * ease(dt, TC_CAM_FOLLOW_TIME); /* let go: eased back to the follow's */
        want.z += c->hold_offset;
    } else {
        c->pitch = view->pitch; /* the demo's own direction; the follow picks up from here */
        c->rise = 0;
        c->holding = false;
        c->hold_offset = 0;
        want = tc_v3_sub(pivot, tc_v3_scale(tc_camera_forward(view), distance));
    }
    dir = tc_v3_sub(want, pivot);
    length = tc_v3_length(dir);
    if (length < 1e-4f) { dir = tc_v3_scale(tc_camera_forward(view), -1); length = 0; }
    else dir = tc_v3_scale(dir, 1 / length);
    /* What the level takes off the distance: more at once, less eased (only the collision's part: the wanted distance
     * itself is followed exactly) */
    allowed = fmaxf(fminf(length, TC_CAM_MIN_DISTANCE), fminf(length, clear_distance(t->world, pivot, dir, length)));
    pull = length - allowed;
    if (first || pull > c->pull) c->pull = pull;               /* blocked: in at once */
    else c->pull += (pull - c->pull) * ease(dt, TC_CAM_RELEASE_TIME); /* clear: back out, gently */
    /* never more than there is (Ed walking at the camera shortens the distance under an older, larger pull: until
     * 2026-11-01 that made the reach negative, put the camera through him and re-placed it behind him the next frame) */
    c->pull = fminf(c->pull, length - fminf(length, TC_CAM_MIN_DISTANCE));
    c->reach = fmaxf(fminf(length, TC_CAM_MIN_DISTANCE), length - c->pull);
    view->position = tc_v3_add(pivot, tc_v3_scale(dir, c->reach));
    pitch = asinf(fmaxf(-1, fminf(1, -dir.z)));
    if (!t->manual) view->pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT, pitch));
}
