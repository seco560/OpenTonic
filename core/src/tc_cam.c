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

/* The offset from the pivot of a camera at yaw (tc_camera convention: looking from the camera to the pivot), elevation
 * (rad above the pivot) and distance; and the yaw of an offset. */
static tc_vec3 offset_of(float yaw, float elevation, float distance) {
    float h = cosf(elevation) * distance;
    return tc_v3(-sinf(yaw) * h, -cosf(yaw) * h, sinf(elevation) * distance);
}

void tc_cam_reset(tc_cam *c) {
    bool closeup = c->closeup; /* the player's choice (5): kept, as the zoom is */
    memset(c, 0, sizeof(*c));
    c->closeup = closeup;
    c->since_look = 1e6f;
    c->watch = TC_CAM_WATCH_TIME;
}

void tc_cam_look(tc_cam *c, tc_camera *view, float yaw, float rise) {
    view->yaw = wrap(view->yaw + yaw);
    if (c->first_person) view->pitch = fmaxf(-1.4f, fminf(1.4f, view->pitch - rise)); /* looking, not orbiting */
    else c->rise = fmaxf(TC_CAM_RISE_MIN, fminf(TC_CAM_RISE_MAX, c->rise + rise));
    c->since_look = 0;
    c->recentering = false;
    c->swinging = false;
    c->swing_rate = 0;
    c->chase_rate = 0;
}

void tc_cam_recenter(tc_cam *c) {
    c->recentering = true;
    c->swinging = false;
    c->swing_rate = 0;
}

/* How far the camera's sphere gets from the pivot along dir (unit) before it meets the level, up to distance: the
 * sphere swept (the exe's sweep: faces it moves into) and a ray, both faces (the original's "can see" ray), less the
 * sphere's radius; the drawn persos by the viewer's sight test. Never past a surface; 0 when one is at the pivot. */
/* least (NULL: not wanted): port, how far out the eye has to be to be out of a perso the pivot is in (tc_cam_sight_fn),
 * 0 none; never more than the distance returned. */
static float clear_distance_r(const tc_cam_target *t, tc_vec3 pivot, tc_vec3 dir, float distance, float radius, float *least) {
    tc_collision *world = t->world;
    tc_ai_sphere_set body; /* one sphere at the origin */
    tc_mec_hit hit;
    tc_hit h;
    float d = distance, out = 0;
    if (least) *least = 0;
    if (distance <= 0) return distance;
    if (t->sight) { /* the drawn persos: the sphere's radius kept off them along the line */
        float inside = 0, reach = distance + radius;
        float f = t->sight(t->sight_user, pivot, tc_v3_add(pivot, tc_v3_scale(dir, reach)), &inside);
        if (f < 1) d = fmaxf(0, f * reach - radius);
        if (inside > 0) d = fmaxf(d, out = fminf(distance, inside + radius)); /* port: out of the perso the pivot is in */
    }
    if (!world) { if (least) *least = fminf(out, d); return d; }
    memset(&body, 0, sizeof(body));
    body.spheres[0].radius = radius;
    body.spheres[0].rebound = 1;
    body.spheres[0].grip = 1;
    body.count = 1;
    if (tc_mec_sweep(world, &body, 0, pivot, tc_v3_add(pivot, tc_v3_scale(dir, distance)), &hit))
        d = fminf(d, fmaxf(0, hit.t * distance - 0.02f)); /* just short of touching */
    if (tc_collision_raycast(world, pivot, dir, distance + radius, &h))
        d = fminf(d, fmaxf(0, h.distance - radius));
    if (least) *least = fminf(out, d);
    return d;
}

static float clear_distance(const tc_cam_target *t, tc_vec3 pivot, tc_vec3 dir, float distance) {
    return clear_distance_r(t, pivot, dir, distance, TC_CAM_RADIUS, NULL);
}

/* The preset for Ed's situation: distance (before the front stretch), elevation, the stretch in front of him. */
static void preset(const tc_cam *c, const tc_cam_target *t, float *distance, float *elevation, float *stretch) {
    float zoom = t->distance / TC_CAM_DISTANCE;
    *distance = t->distance, *elevation = TC_CAM_ELEVATION, *stretch = TC_CAM_FRONT_STRETCH;
    if (t->shot_distance > 0 && isfinite(t->shot_distance) && isfinite(t->shot_elevation)) { /* a director's follow (shots 2, 7, 8) */
        *distance = t->shot_distance * zoom, *elevation = t->shot_elevation, *stretch = 1;
        return;
    }
    if (t->shot == TC_CAM_SHOT_SKI && !c->closeup) { /* the slides: further back as he speeds up, lower, no stretch */
        *distance = (TC_CAM_SKI_DISTANCE + fminf(TC_CAM_SKI_PULLBACK_MAX, t->speed * TC_CAM_SKI_PULLBACK)) * zoom;
        *elevation = TC_CAM_SKI_ELEVATION, *stretch = 1;
    } else if (t->situation == TC_CAM_SIT_DYING) *distance = TC_CAM_DYING_DISTANCE * zoom, *stretch = 1;
    else if (t->situation == TC_CAM_SIT_FLIGHT)
        *distance = TC_CAM_FLIGHT_DISTANCE * zoom, *elevation = TC_CAM_FLIGHT_ELEVATION, *stretch = 1;
    else if (c->closeup)
        *distance = TC_CAM_CLOSEUP_DISTANCE * zoom, *elevation = TC_CAM_CLOSEUP_ELEVATION, *stretch = TC_CAM_CLOSEUP_STRETCH;
}

/* The jump's height hold, from Ed's situation (TC_CAM_SIT_JUMP): on at the jump with the rig's height as it is (not the
 * eye's: a wall may have pulled that in towards his head); off when he is back to normal, flies, or his origin is 3
 * below his take-off pivot less 0.2 (B17: Ed's z < dsg74 - 3). */
static void hold_height(tc_cam *c, const tc_cam_target *t) {
    int s = t->situation;
    if (s == TC_CAM_SIT_JUMP && !c->holding) {
        c->holding = true;
        c->hold_z = c->rig.z + c->hold_offset; /* the rig as it stood last frame (a hold may still be easing off) */
        c->hold_ref = c->pivot.z - 0.2f;
    }
    if (c->holding && ((s != TC_CAM_SIT_JUMP && s != TC_CAM_SIT_FALL) || t->position.z < c->hold_ref - 3))
        c->holding = false;
}

/* The watchdog's search (FUN_004a5400 / 004a4f20): the wanted offset behind Ed turned by k pi / 4. The most open one
 * (the camera's sphere reaching furthest from the pivot) wins, and among the fully open ones the nearest round to where
 * the camera is; it must beat the eye's reach now by 1. The port's choice: the exe sorts by a score not decoded yet
 * (FUN_004a4660) and validates by its can-go / can-see rays (FUN_004a4bb0), then cuts. */
static void search(tc_cam *c, const tc_camera *view, const tc_cam_target *t, tc_vec3 base, float distance,
                   float elevation) {
    float best_turn = 1e9f, best_clear = c->reach + 1, best_yaw = 0;
    bool found = false;
    int k;
    for (k = 0; k < 8; k++) {
        float yaw = wrap(t->behind + k * TC_CAM_SEARCH_STEP), turn = fabsf(wrap(yaw - view->yaw)), clear;
        tc_vec3 off = offset_of(yaw, elevation, distance);
        if (turn < 0.05f) continue; /* not where it already is */
        clear = fminf(distance, clear_distance(t, base, tc_v3_scale(off, 1 / distance), distance) + 0.01f);
        /* the most open; among the fully open ones, the nearest round */
        if (clear > best_clear + 0.01f || (clear >= best_clear - 0.01f && turn < best_turn))
            best_clear = clear, best_turn = turn, best_yaw = yaw, found = true;
    }
    if (found) {
        c->swinging = true;
        c->swing_yaw = best_yaw;
        c->swing_time = 0;
        c->swing_rate = c->turn_rate; /* from the way it turns now (the rope's drift): no jolt */
    }
}

/* The orbit's direction this frame: placed, recentring, the watchdog's swing, or the rope (Ed's speed across the view
 * turns it, once the player has left it alone); the player's height change eases back while he moves. */
static void turn(tc_cam *c, tc_camera *view, const tc_cam_target *t, float dt, bool first) {
    if (first) {
        view->yaw = t->behind;
        return;
    }
    if (c->recentering) {
        float k = ease(dt, TC_CAM_RECENTER_TIME);
        view->yaw = wrap(view->yaw + wrap(t->behind - view->yaw) * k);
        c->rise -= c->rise * k;
        if (fabsf(wrap(view->yaw - t->behind)) < 0.005f && fabsf(c->rise) < 0.005f) {
            view->yaw = t->behind; /* there: exactly */
            c->rise = 0;
            c->recentering = false;
        }
        c->rope_gain = 0;
        return;
    }
    if (c->swinging) { /* critically damped: speeds up and slows down smoothly (a lag would start at full speed) */
        float d = wrap(c->swing_yaw - view->yaw), w = 1 / TC_CAM_SWING_TIME, h = fminf(dt, 1.0f / 30);
        c->swing_time += dt;
        c->swing_rate += (w * w * d - 2 * w * c->swing_rate) * h;
        view->yaw = wrap(view->yaw + c->swing_rate * h);
        /* near enough, the follow takes over (Ed's own moves turn the camera too: chasing the last bit overshot); once
         * the eye is clear of Ed again the spot has done its job */
        if (fabsf(d) < 0.05f || c->reach > TC_CAM_SWING_NEAR + 1 || c->swing_time > 8 * TC_CAM_SWING_TIME) {
            c->swinging = false;
            c->swing_rest = TC_CAM_SWING_REST;
        }
        c->rope_gain = 0;
    } else {
        if (c->swing_rate != 0) { /* the swing's speed dies out rather than stopping dead */
            float r = c->swing_rate * (1 - ease(dt, TC_CAM_SWING_TIME / 2));
            view->yaw = wrap(view->yaw + (c->swing_rate + r) * 0.5f * dt);
            c->swing_rate = fabsf(r) < 1e-3f ? 0 : r;
        }
        if (t->shot_axis && c->since_look >= TC_CAM_GRACE) {
            /* a director's axis (shots 7, 8): onto it, critically damped, whatever Ed does */
            float d = wrap(t->shot_yaw - view->yaw), w = 1 / TC_CAM_AXIS_TIME, h = fminf(dt, 1.0f / 30);
            c->chase_rate += (w * w * d - 2 * w * c->chase_rate) * h;
            view->yaw = wrap(view->yaw + c->chase_rate * h);
            c->rope_gain = 0;
        } else if (t->chase > 0 && t->speed > TC_CAM_MOVING_SPEED && c->since_look >= TC_CAM_GRACE) {
            /* the chase (a car's): in behind it, critically damped (from standing: no jolt as it starts); the rope's
             * gain starts over for when it ends */
            float d = wrap(t->behind - view->yaw), w = 1 / t->chase, h = fminf(dt, 1.0f / 30);
            c->chase_rate += (w * w * d - 2 * w * c->chase_rate) * h;
            view->yaw = wrap(view->yaw + c->chase_rate * h);
            c->rope_gain = 0;
        } else if (t->speed > TC_CAM_MOVING_SPEED && c->since_look >= TC_CAM_GRACE && c->distance > 0.1f) {
            /* the rope: Ed's speed across the view (along the camera's right) over the follow's distance (not over the
             * distance a wall leaves: pulled in close, that turned it wildly) */
            float across = c->rope_x * cosf(view->yaw) - c->rope_y * sinf(view->yaw);
            c->rope_gain += (1 - c->rope_gain) * ease(dt, TC_CAM_ROPE_ONSET);
            float dead = t->shot == TC_CAM_SHOT_SKI ? TC_CAM_SKI_ROPE_DEAD : TC_CAM_ROPE_DEAD;
            across = across > 0 ? fmaxf(0, across - dead) : fminf(0, across + dead);
            view->yaw = wrap(view->yaw + c->rope_gain * TC_CAM_ROPE * across / c->distance * dt);
        } else if (c->since_look < TC_CAM_GRACE) c->rope_gain = 0; /* the player has it: fade in afresh after */
        if (!(t->shot_axis && c->since_look >= TC_CAM_GRACE) &&
            !(t->chase > 0 && t->speed > TC_CAM_MOVING_SPEED && c->since_look >= TC_CAM_GRACE))
            c->chase_rate = 0;
    }
    if (t->speed > TC_CAM_MOVING_SPEED && c->since_look >= TC_CAM_GRACE) c->rise -= c->rise * ease(dt, TC_CAM_RELAX_TIME);
}

void tc_cam_update(tc_cam *c, tc_camera *view, const tc_cam_target *t, float dt) {
    tc_vec3 pivot = tc_v3_add(t->position, tc_v3(0, 0, TC_CAM_AIM_HEIGHT)), dir, base;
    float distance, elevation, stretch, length, allowed, pull, k, least = 0;
    bool first;
    c->since_look += dt;
    if (c->first_person) { /* at his head, where the player looks; nothing else */
        view->position = tc_v3_add(t->position, tc_v3(0, 0, TC_CAM_EYE_HEIGHT));
        c->placed = false; /* the follow is placed afresh afterwards */
        c->hide_target = false;
        return;
    }
    if (c->placed && tc_v3_distance(pivot, c->pivot) > TC_CAM_TELEPORT) c->placed = false; /* a respawn, a portal */
    /* never kept: a value that is not a number (it would stay in the eased state for good) */
    if (c->placed && !(isfinite(c->distance) && isfinite(c->elevation) && isfinite(c->rise) && isfinite(view->yaw) &&
                       isfinite(c->height1) && isfinite(c->height2) && isfinite(c->stretch) && isfinite(c->pull)))
        c->placed = false;
    if (!isfinite(view->yaw)) view->yaw = t->behind;
    if (!isfinite(c->rise)) c->rise = 0;
    first = !c->placed;
    c->pivot = pivot;
    preset(c, t, &distance, &elevation, &stretch);
    if (first) {
        c->distance = distance;
        c->elevation = elevation;
        c->height1 = c->height2 = pivot.z;
        c->rope_x = t->velocity.x, c->rope_y = t->velocity.y;
        c->rope_gain = 1;
        c->pull = 0;
        c->holding = c->swinging = false;
        c->swing_rate = c->swing_rest = 0;
        c->hold_offset = c->hold_rate = 0;
        c->watch = TC_CAM_WATCH_TIME;
        c->placed = true;
    } else {
        c->distance += (distance - c->distance) * ease(dt, TC_CAM_DISTANCE_TIME);
        c->elevation += (elevation - c->elevation) * ease(dt, TC_CAM_FOLLOW_TIME);
        k = ease(dt, t->shot == TC_CAM_SHOT_SKI ? TC_CAM_SKI_HEIGHT_TIME : TC_CAM_HEIGHT_TIME); /* two lags: 2 h1 - h2
                                                                    follows a steady climb exactly (no lag) */
        c->height1 += (pivot.z - c->height1) * k;
        c->height2 += (c->height1 - c->height2) * k;
        k = ease(dt, t->shot == TC_CAM_SHOT_SKI ? TC_CAM_SKI_ROPE_TIME : TC_CAM_ROPE_TIME); /* his velocity steps at the
                                                                    AI's 30 Hz and flickers along walls */
        c->rope_x += (t->velocity.x - c->rope_x) * k;
        c->rope_y += (t->velocity.y - c->rope_y) * k;
    }
    base = tc_v3(pivot.x, pivot.y, 2 * c->height1 - c->height2); /* Ed's head, its height smoothed */

    /* the rig */
    {
        bool held = c->holding;
        float off, yaw0 = view->yaw;
        turn(c, view, t, dt, first);
        c->turn_rate = first || dt <= 0 ? 0 : wrap(view->yaw - yaw0) / dt;
        off = fabsf(wrap(view->yaw - t->behind));
        stretch = 1 + (stretch - 1) * off / TC_PI;
        if (first) c->stretch = c->stretch1 = stretch;
        else {
            k = ease(dt, TC_CAM_STRETCH_TIME);
            c->stretch1 += (stretch - c->stretch1) * k;
            c->stretch += (c->stretch1 - c->stretch) * k;
        }
        c->rig = tc_v3_add(base, offset_of(view->yaw, fmaxf(-1.4f, fminf(1.4f, c->elevation + c->rise)),
                                           c->distance * c->stretch));
        hold_height(c, t);
        if (c->holding || held) { /* held (and letting go): from where it is, the rate the rig moves under it */
            float o = c->hold_z - c->rig.z;
            c->hold_rate = held && dt > 0 ? (o - c->hold_offset) / dt : 0;
            c->hold_offset = o;
        } else if (c->hold_offset != 0 || c->hold_rate != 0) { /* let go: back to the follow's, gathering speed */
            float w = 2 / TC_CAM_FOLLOW_TIME, h = fminf(dt, 1.0f / 30);
            c->hold_rate += (-w * w * c->hold_offset - 2 * w * c->hold_rate) * h;
            c->hold_offset += c->hold_rate * h;
            if (fabsf(c->hold_offset) < 1e-4f && fabsf(c->hold_rate) < 1e-3f) c->hold_offset = c->hold_rate = 0;
        }
        c->rig.z += c->hold_offset;
    }

    /* the eye: on the line from Ed's head (its height smoothed) to the rig, the level's pull taken off: more at once,
     * less eased (only the collision's part: the rig itself is followed exactly) */
    dir = tc_v3_sub(c->rig, base);
    length = tc_v3_length(dir);
    if (length < 1e-4f) { dir = tc_v3_scale(tc_camera_forward(view), -1); length = 0; }
    else dir = tc_v3_scale(dir, 1 / length);
    allowed = fminf(length, clear_distance_r(t, base, dir, length, TC_CAM_RADIUS, &least));
    pull = length - allowed;
    if (first || pull > c->pull) c->pull = pull;                       /* blocked: in at once */
    else c->pull += (pull - c->pull) * ease(dt, TC_CAM_RELEASE_TIME);  /* clear: back out, gently */
    /* never more than there is (Ed walking at the camera shortens the distance under an older, larger pull: until
     * 2026-11-01 that put the camera through him) */
    c->pull = fminf(c->pull, length);
    if (least > 0) c->pull = fminf(c->pull, length - fminf(least, allowed)); /* port: out of a perso at once, not eased */
    c->reach = length - c->pull;
    c->hide_target = c->reach < TC_CAM_HIDE_DISTANCE;
    view->position = tc_v3_add(base, tc_v3_scale(dir, c->reach));
    view->pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT, asinf(fmaxf(-1, fminf(1, -dir.z)))));

    /* the watchdog: pulled in close by a wall, the player not turning it, look for an open side round him */
    c->swing_rest -= dt;
    c->watch -= dt;
    if (c->watch <= 0) {
        c->watch += TC_CAM_WATCH_TIME;
        if (c->watch <= 0) c->watch = TC_CAM_WATCH_TIME; /* a long stall */
        /* not on the slides: at his speed the swing turned the view three times as much and hid him no less */
        if (t->shot != TC_CAM_SHOT_SKI && t->world && c->pull > 1e-3f && c->reach < fminf(TC_CAM_SWING_NEAR, c->distance - TC_CAM_NEAR_RANGE) &&
            !c->recentering && !c->swinging && c->swing_rest <= 0 && c->since_look >= TC_CAM_GRACE)
            search(c, view, t, base, c->distance * c->stretch, c->elevation + c->rise);
    }
}
