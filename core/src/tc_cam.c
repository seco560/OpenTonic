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

static tc_vec3 lerp3(tc_vec3 a, tc_vec3 b, float t) { return tc_v3_add(a, tc_v3_scale(tc_v3_sub(b, a), t)); }

/* The yaw (tc_camera convention) of a camera at offset off from the pivot, looking at it; and back: the offset of a
 * camera at yaw, elevation (rad above the pivot) and distance. */
static float yaw_of(tc_vec3 off) { return atan2f(-off.x, -off.y); }
static tc_vec3 offset_of(float yaw, float elevation, float distance) {
    float h = cosf(elevation) * distance;
    return tc_v3(-sinf(yaw) * h, -cosf(yaw) * h, sinf(elevation) * distance);
}

/* p turned round the pivot by dyaw (about z) and delev (in its vertical plane, held within +-1.4 rad). */
static tc_vec3 orbit(tc_vec3 p, tc_vec3 pivot, float dyaw, float delev) {
    tc_vec3 off = tc_v3_sub(p, pivot);
    float len = tc_v3_length(off), h = sqrtf(off.x * off.x + off.y * off.y), elev;
    if (len < 1e-5f) return p;
    elev = fmaxf(-1.4f, fminf(1.4f, atan2f(off.z, h) + delev));
    return tc_v3_add(pivot, offset_of(wrap(yaw_of(off) + dyaw), elev, len));
}

static void classic_reset(tc_cam_classic *c);
static void classic_look(tc_cam_classic *c, tc_camera *view, float yaw, float rise);
static void classic_recenter(tc_cam_classic *c);
static void classic_update(tc_cam_classic *c, tc_camera *view, const tc_cam_target *t, float dt);

void tc_cam_reset(tc_cam *c) {
    bool faithful = c->faithful;
    memset(c, 0, sizeof(*c));
    c->faithful = faithful;
    classic_reset(&c->classic);
    c->since_look = 1e6f;
    c->stretch = -1;
    c->watch = TC_CAM_WATCH_TICKS;
}

void tc_cam_look(tc_cam *c, tc_camera *view, float yaw, float rise) {
    if (!c->faithful) {
        c->classic.first_person = c->first_person;
        classic_look(&c->classic, view, yaw, rise);
        return;
    }
    view->yaw = wrap(view->yaw + yaw);
    if (c->first_person) view->pitch = fmaxf(-1.4f, fminf(1.4f, view->pitch - rise)); /* looking, not orbiting */
    else {
        float r = fmaxf(TC_CAM_RISE_MIN, fminf(TC_CAM_RISE_MAX, c->rise + rise));
        if (c->placed) { /* at once: both ticks the frame is drawn between */
            c->body = orbit(c->body, c->pivot, yaw, r - c->rise);
            c->body_prev = orbit(c->body_prev, c->pivot, yaw, r - c->rise);
        }
        c->rise = r;
    }
    c->since_look = 0;
    c->recentering = false;
    c->swinging = false;
}

void tc_cam_recenter(tc_cam *c) {
    if (!c->faithful) { classic_recenter(&c->classic); return; }
    c->recentering = true;
    c->swinging = false;
}

/* How far the camera's sphere gets from the pivot along dir (unit) before it meets the level, up to distance: the
 * sphere swept (the exe's sweep: faces it moves into) and a ray, both faces (the original's "can see" ray), less the
 * sphere's radius. Never past a surface; 0 when one is at the pivot. */
static float clear_distance(const tc_cam_target *t, tc_vec3 pivot, tc_vec3 dir, float distance) {
    tc_collision *world = t->world;
    tc_ai_sphere_set body; /* one sphere at the origin */
    tc_mec_hit hit;
    tc_hit h;
    float d = distance;
    if (distance <= 0) return distance;
    if (t->sight) { /* the drawn persos: the sphere's radius kept off them along the line */
        float reach = distance + TC_CAM_RADIUS, f = t->sight(t->sight_user, pivot, tc_v3_add(pivot, tc_v3_scale(dir, reach)));
        if (f < 1) d = fmaxf(0, f * reach - TC_CAM_RADIUS);
    }
    if (!world) return d;
    memset(&body, 0, sizeof(body));
    body.spheres[0].radius = TC_CAM_RADIUS;
    body.spheres[0].rebound = 1;
    body.spheres[0].grip = 1;
    body.count = 1;
    if (tc_mec_sweep(world, &body, 0, pivot, tc_v3_add(pivot, tc_v3_scale(dir, distance)), &hit))
        d = fminf(d, fmaxf(0, hit.t * distance - 0.02f)); /* just short of touching */
    if (tc_collision_raycast(world, pivot, dir, distance + TC_CAM_RADIUS, &h))
        d = fminf(d, fmaxf(0, h.distance - TC_CAM_RADIUS));
    return d;
}

/* The preset for Ed's situation: distance (before the front stretch), elevation, the stretch in front of him. */
static void preset(const tc_cam *c, const tc_cam_target *t, float *distance, float *elevation, float *stretch) {
    float zoom = t->distance / TC_CAM_DISTANCE;
    *distance = t->distance, *elevation = TC_CAM_ELEVATION, *stretch = TC_CAM_FRONT_STRETCH;
    if (t->situation == TC_CAM_SIT_DYING) *distance = TC_CAM_DYING_DISTANCE * zoom, *stretch = 1;
    else if (t->situation == TC_CAM_SIT_FLIGHT)
        *distance = TC_CAM_FLIGHT_DISTANCE * zoom, *elevation = TC_CAM_FLIGHT_ELEVATION, *stretch = 1;
    else if (c->closeup)
        *distance = TC_CAM_CLOSEUP_DISTANCE * zoom, *elevation = TC_CAM_CLOSEUP_ELEVATION, *stretch = TC_CAM_CLOSEUP_STRETCH;
}

/* The jump's height hold, from Ed's situation (TC_CAM_SIT_JUMP): on at the jump with the camera's height as it is; off
 * when he is back to normal, flies, or his origin is 3 below his take-off pivot less 0.2 (B17: Ed's z < dsg74 - 3). */
static void hold_height(tc_cam *c, const tc_cam_target *t, tc_vec3 pivot) {
    int s = t->situation;
    if (s == TC_CAM_SIT_JUMP && !c->holding) {
        c->holding = true;
        c->hold_z = c->body.z;
        c->hold_ref = pivot.z - 0.2f;
    }
    if (c->holding && ((s != TC_CAM_SIT_JUMP && s != TC_CAM_SIT_FALL) || t->position.z < c->hold_ref - 3))
        c->holding = false;
}

/* The watchdog's search (FUN_004a5400 / 004a4f20): the wanted offset behind Ed turned by k pi / 4. The most open one
 * (the camera's sphere reaching furthest from the pivot) wins, and among the fully open ones the nearest round to where
 * the camera is; it must beat the eye's reach now by 1. The port's choice: the exe sorts by a score not decoded yet
 * (FUN_004a4660) and validates by its can-go / can-see rays (FUN_004a4bb0), then cuts. */
static void search(tc_cam *c, const tc_cam_target *t, float distance, float elevation) {
    float cur = yaw_of(tc_v3_sub(c->body, c->pivot)), best_turn = 1e9f, best_clear = c->reach + 1, best_yaw = 0;
    bool found = false;
    int k;
    for (k = 0; k < 8; k++) {
        float yaw = wrap(t->behind + k * TC_CAM_SEARCH_STEP), turn = fabsf(wrap(yaw - cur)), len, clear;
        tc_vec3 off = offset_of(yaw, elevation, distance);
        if (turn < 0.05f) continue; /* not where it already is */
        len = tc_v3_length(off);
        clear = fminf(len, clear_distance(t, c->pivot, tc_v3_scale(off, 1 / len), len) + 0.01f);
        /* the most open; among the fully open ones, the nearest round */
        if (clear > best_clear + 0.01f || (clear >= best_clear - 0.01f && turn < best_turn))
            best_clear = clear, best_turn = turn, best_yaw = yaw, found = true;
    }
    if (found) {
        c->swinging = true;
        c->swing_yaw = best_yaw;
        c->swing_time = 0;
    }
}

/* One tick of the camera perso (TC_CAM_TICK): target, wanted spot, the mover, collision, the look-at lag, the watchdog. */
static void tick(tc_cam *c, const tc_cam_target *t) {
    tc_vec3 avg = tc_v3(0, 0, 0), pivot, off, want, delta, next, dir;
    float distance, elevation, stretch, yaw, len, lo, hi, step, sdt, dl, clear, v;
    int i, n;
    bool pulled;
    /* Ed's position history and its average (GAM_DynamHistPosition_0042af40, GAM_0042b730): with 1/30 s ticks the 0.4 s
     * window holds all 8 samples. A jump of more than 5 in a tick (a portal, a respawn) starts afresh. */
    if (c->history_count > 0 && tc_v3_length(tc_v3_sub(t->position, c->history[0])) > 5) c->placed = false;
    if (!c->placed) c->history_count = 0;
    for (i = TC_CAM_HISTORY - 1; i > 0; i--) c->history[i] = c->history[i - 1];
    c->history[0] = t->position;
    if (c->history_count < TC_CAM_HISTORY) c->history_count++;
    n = c->history_count;
    for (i = 0; i < n && i * TC_CAM_TICK < TC_CAM_AVERAGE_WINDOW; i++) avg = tc_v3_add(avg, c->history[i]);
    avg = tc_v3_scale(avg, 1.0f / (float)i);
    pivot = tc_v3_add(avg, tc_v3(0, 0, TC_CAM_AIM_HEIGHT));
    c->pivot = pivot;
    /* the camera's speed: 2 x Ed's averaged speed within the record's range (FUN_004a2a10, GAM_0042b5b0) */
    v = n > 1 ? tc_v3_length(tc_v3_sub(c->history[0], c->history[n - 1])) / ((float)(n - 1) * TC_CAM_TICK) : 0;
    c->speed = fmaxf(TC_CAM_SPEED_MIN, fminf(TC_CAM_SPEED_MAX, 2 * v));
    preset(c, t, &distance, &elevation, &stretch);
    if (!c->placed) { /* placed behind him at once */
        c->body = c->body_prev = tc_v3_add(pivot, offset_of(t->behind, elevation + c->rise, distance));
        c->look = c->look_prev = pivot;
        c->reach = 1e9f; /* the first tick's collision sets it */
        c->step_last = 0;
        c->stretch = 1;
        c->holding = false;
        c->hold_offset = 0;
        c->placed = true;
    }
    /* the wanted spot: the camera's own direction from the pivot (the lazy follow), the preset's elevation (the
     * player's height change on it), the distance into the band, stretched in front of him */
    off = tc_v3_sub(c->body, pivot);
    yaw = off.x * off.x + off.y * off.y > 1e-6f ? yaw_of(off) : t->behind;
    stretch = 1 + (stretch - 1) * fabsf(wrap(yaw - t->behind)) / TC_PI;
    c->stretch = c->stretch < 0 ? stretch : c->stretch + (stretch - c->stretch) * ease(TC_CAM_TICK, TC_CAM_STRETCH_TIME);
    len = tc_v3_length(off);
    lo = fmaxf(0.1f, distance - TC_CAM_BAND) * c->stretch, hi = (distance + TC_CAM_BAND) * c->stretch;
    want = tc_v3_add(pivot, offset_of(yaw, fmaxf(-1.4f, fminf(1.4f, elevation + c->rise)), fmaxf(lo, fminf(hi, len))));
    {   /* the jump's hold: the wanted height stays; letting go eases back to the follow's */
        bool held = c->holding;
        hold_height(c, t, pivot);
        if (c->holding || held) c->hold_offset = c->hold_z - want.z;
        else c->hold_offset -= c->hold_offset * ease(TC_CAM_TICK, 0.35f);
        want.z += c->hold_offset;
    }
    /* the mover (DNM_RequestType3_004b3810 -> FUN_004b29fa, FUN_004b2e4b) */
    delta = tc_v3_sub(want, c->body);
    dl = tc_v3_length(delta);
    sdt = c->speed * TC_CAM_TICK;
    step = fminf(dl, sdt);
    if (dl < sdt * (TC_CAM_EASE - dl)) step = fminf(step, dl / (TC_CAM_EASE - dl)); /* easing in near it */
    if (c->holding) step = dl; /* the hold keeps the height exactly (PROC 188 / 190: a band on the camera's z) */
    else step = fminf(step, c->step_last + sdt / TC_CAM_ACCEL); /* speeding up gradually */
    next = dl > 1e-6f ? tc_v3_add(c->body, tc_v3_scale(delta, step / dl)) : c->body;
    c->step_last = tc_v3_length(tc_v3_sub(next, c->body));
    c->body_prev = c->body;
    c->body = next;
    /* the eye: never past a surface seen from the pivot; in at once, out at the mover's pace */
    off = tc_v3_sub(next, pivot);
    len = tc_v3_length(off);
    dir = len > 1e-5f ? tc_v3_scale(off, 1 / len) : offset_of(yaw, 0, 1);
    clear = clear_distance(t, pivot, dir, len);
    pulled = clear < len - 1e-3f;
    {   /* what the wall takes off the rig's distance: more at once, less at the mover's pace */
        float pull = len - clear, old = c->reach >= 1e8f ? pull : fmaxf(0, c->reach_len - c->reach);
        if (pull < old) {
            float gap = old - pull, out = fminf(gap, sdt);
            if (gap < sdt * (TC_CAM_EASE - gap)) out = fminf(out, gap / (TC_CAM_EASE - gap));
            pull = old - out;
        }
        c->pull_prev = c->reach >= 1e8f ? pull : c->pull;
        c->pull = pull;
        c->reach = fmaxf(0, len - pull);
        c->reach_len = len;
    }
    /* the look-at point (FUN_004b514b) */
    c->look_prev = c->look;
    c->look = tc_v3_add(c->look, tc_v3_scale(tc_v3_sub(pivot, c->look), 1 / TC_CAM_LOOK_LAG));
    /* a swing ends once the eye is clear of Ed again (the target was a direction of the moment) */
    if (c->swinging && c->reach > TC_CAM_SWING_NEAR + 1) c->swinging = false;
    /* the watchdog: pulled nearer than the second range's minimum, look for an open spot round him */
    if (--c->watch <= 0) {
        c->watch = TC_CAM_WATCH_TICKS;
        if (pulled && c->reach < fminf(TC_CAM_SWING_NEAR, distance - TC_CAM_NEAR_RANGE) && !c->recentering && !c->swinging &&
            c->since_look >= TC_CAM_GRACE && t->world)
            search(c, t, distance, elevation);
    }
}

/* Turns the camera's last two ticks round the pivot towards yaw by the share k; true when it got within done (rad):
 * then exactly there. */
static bool turn_towards(tc_cam *c, float yaw, float k, float done) {
    float cur = yaw_of(tc_v3_sub(c->body, c->pivot)), d = wrap(yaw - cur);
    if (fabsf(d) < done) k = 1;
    c->body = orbit(c->body, c->pivot, d * k, 0);
    c->body_prev = orbit(c->body_prev, c->pivot, d * k, 0);
    return k == 1;
}

void tc_cam_update(tc_cam *c, tc_camera *view, const tc_cam_target *t, float dt) {
    tc_vec3 head = tc_v3_add(t->position, tc_v3(0, 0, TC_CAM_AIM_HEIGHT)), eye, look, off, f;
    float len, clear, alpha;
    int ticks = 0;
    if (!c->faithful) {
        if (!c->placed) c->classic.reach = -1, c->placed = true; /* placed afresh (a new level, first person left) */
        c->classic.closeup = c->closeup;
        c->classic.first_person = c->first_person;
        classic_update(&c->classic, view, t, dt);
        c->hide_target = c->classic.hide && !c->first_person;
        return;
    }
    c->since_look += dt;
    if (c->first_person && !t->manual) { /* at his head, where the player looks; nothing else */
        view->position = tc_v3_add(t->position, tc_v3(0, 0, TC_CAM_EYE_HEIGHT));
        c->placed = false; /* the follow is placed afresh afterwards */
        c->hide_target = false;
        return;
    }
    if (t->manual) { /* the demo's direction: only the position and the collision */
        float distance, elevation, stretch;
        preset(c, t, &distance, &elevation, &stretch);
        off = tc_v3_scale(tc_camera_forward(view), -distance);
        len = tc_v3_length(off);
        clear = clear_distance(t, head, tc_v3_scale(off, 1 / len), len);
        view->position = tc_v3_add(head, tc_v3_scale(off, clear / len));
        c->hide_target = clear < TC_CAM_HIDE_DISTANCE;
        c->placed = false;
        c->rise = 0;
        return;
    }
    if (!c->placed) {
        tick(c, t);
        c->clock = 0;
    }
    /* the player's part, between ticks: recentring, the swing to an open spot, the height change easing back */
    if (c->recentering) {
        float k = ease(dt, TC_CAM_RECENTER_TIME);
        c->rise -= c->rise * k;
        if (turn_towards(c, t->behind, k, 0.005f) && fabsf(c->rise) < 0.005f) c->rise = 0, c->recentering = false;
    } else if (c->swinging) {
        c->swing_time += dt;
        /* near enough, the follow takes over (Ed's own moves turn the camera too: chasing the last bit overshot) */
        if (fabsf(wrap(c->swing_yaw - yaw_of(tc_v3_sub(c->body, c->pivot)))) < 0.05f ||
            turn_towards(c, c->swing_yaw, ease(dt, TC_CAM_SWING_TIME), 0) || c->swing_time > 4 * TC_CAM_SWING_TIME)
            c->swinging = false;
    }
    if (t->speed > TC_CAM_MOVING_SPEED && c->since_look >= TC_CAM_GRACE) c->rise -= c->rise * ease(dt, TC_CAM_RELAX_TIME);
    c->clock += dt;
    while (c->clock >= TC_CAM_TICK) {
        c->clock -= TC_CAM_TICK;
        if (++ticks > 8) { c->clock = 0; break; } /* a long stall: drop the backlog */
        tick(c, t);
    }
    /* drawn between the last two ticks. The view looks the way the rig looks (from the rig at the lagging look-at
     * point, Ed's pivot held within TC_CAM_LOOK_OFF_MAX of its centre): steady however near a wall pulls the eye. The
     * eye is on the line from Ed as drawn to the rig, the wall's pull taken off, never past a surface. */
    alpha = fminf(1, c->clock / TC_CAM_TICK);
    {
        tc_vec3 rig = lerp3(c->body_prev, c->body, alpha), w, u;
        float pull = c->pull_prev + (c->pull - c->pull_prev) * alpha, lw, lu, a; /* the wall's, between the ticks */
        look = lerp3(c->look_prev, c->look, alpha);
        w = tc_v3_sub(head, rig), u = tc_v3_sub(look, rig);
        lw = tc_v3_length(w), lu = tc_v3_length(u);
        if (lw > 1e-4f) {
            w = tc_v3_scale(w, 1 / lw);
            u = lu > 1e-4f ? tc_v3_scale(u, 1 / lu) : w;
            a = acosf(fmaxf(-1, fminf(1, tc_v3_dot(u, w))));
            if (a > TC_CAM_LOOK_OFF_MAX) { /* along the great circle from Ed towards the look-at point */
                tc_vec3 perp = tc_v3_sub(u, tc_v3_scale(w, tc_v3_dot(u, w)));
                float lp = tc_v3_length(perp);
                if (lp > 1e-5f)
                    u = tc_v3_add(tc_v3_scale(w, cosf(TC_CAM_LOOK_OFF_MAX)), tc_v3_scale(perp, sinf(TC_CAM_LOOK_OFF_MAX) / lp));
            }
            f = u;
            len = fmaxf(0, lw - pull);
            eye = tc_v3_sub(head, tc_v3_scale(w, len));
            clear = clear_distance(t, head, tc_v3_scale(w, -1), len);
            if (clear < len) eye = tc_v3_sub(head, tc_v3_scale(w, clear)), len = clear;
        } else {
            eye = rig, len = 0;
            f = tc_camera_forward(view);
        }
    }
    c->hide_target = len < TC_CAM_HIDE_DISTANCE;
    view->position = eye;
    if (tc_v3_length(f) > 1e-5f) {
        view->yaw = atan2f(f.x, f.y);
        view->pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT, atan2f(f.z, sqrtf(f.x * f.x + f.y * f.y))));
    }
}

/* ---- the classic camera (the fourth pass, 2026-11-01): the default; 9 switches to the fifth pass ---- */



static void classic_reset(tc_cam_classic *c) {
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

static void classic_look(tc_cam_classic *c, tc_camera *view, float yaw, float rise) {
    view->yaw = wrap(view->yaw + yaw);
    if (c->first_person) view->pitch = fmaxf(-1.4f, fminf(1.4f, view->pitch - rise)); /* looking, not orbiting */
    else c->rise = fmaxf(TC_CAM_RISE_MIN, fminf(TC_CAM_RISE_MAX, c->rise + rise));
    c->since_look = 0;
    c->recentering = false;
}

static void classic_recenter(tc_cam_classic *c) { c->recentering = true; }


/* The jump's height hold, from Ed's situation (TC_CAM_SIT_JUMP): on at the jump with the camera's height as it is; off
 * when he is back to normal, flies, or his origin is 3 below his take-off pivot less 0.2 (B17: Ed's z < dsg74 - 3). */
static void classic_hold_height(tc_cam_classic *c, const tc_camera *view, const tc_cam_target *t, tc_vec3 pivot) {
    int s = t->situation;
    if (s == TC_CAM_SIT_JUMP && !c->holding && c->reach >= 0) {
        c->holding = true;
        c->hold_z = view->position.z;
        c->hold_ref = pivot.z - 0.2f;
    }
    if (c->holding && ((s != TC_CAM_SIT_JUMP && s != TC_CAM_SIT_FALL) || t->position.z < c->hold_ref - 3))
        c->holding = false;
}

static void classic_update(tc_cam_classic *c, tc_camera *view, const tc_cam_target *t, float dt) {
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
        classic_hold_height(c, view, t, pivot);
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
    allowed = fmaxf(fminf(length, TC_CAM_CLASSIC_MIN), fminf(length, clear_distance(t, pivot, dir, length)));
    pull = length - allowed;
    if (first || pull > c->pull) c->pull = pull;               /* blocked: in at once */
    else c->pull += (pull - c->pull) * ease(dt, TC_CAM_RELEASE_TIME); /* clear: back out, gently */
    /* never more than there is (Ed walking at the camera shortens the distance under an older, larger pull: until
     * 2026-11-01 that made the reach negative, put the camera through him and re-placed it behind him the next frame) */
    c->pull = fminf(c->pull, length - fminf(length, TC_CAM_CLASSIC_MIN));
    c->reach = fmaxf(fminf(length, TC_CAM_CLASSIC_MIN), length - c->pull);
    c->hide = c->reach < 0.5f; /* inside his body (the old floor was 0.5: the eye in him or in walls) */
    view->position = tc_v3_add(pivot, tc_v3_scale(dir, c->reach));
    pitch = asinf(fmaxf(-1, fminf(1, -dir.z)));
    if (!t->manual) view->pitch = fmaxf(-TC_CAM_PITCH_LIMIT, fminf(TC_CAM_PITCH_LIMIT, pitch));
}
