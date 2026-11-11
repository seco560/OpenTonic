/* Vehicles (tcx_vehicle.h): the port's own toy car and Ed's hops in and out of it. */
#include "../tc_internal.h"
#include <math.h>
#include <string.h>
#include "tonic/custom/tcx_vehicle.h"

/* ---- the car's axes ---- */

static tc_vec3 front_of(float heading) { return tc_v3(sinf(heading), -cosf(heading), 0); }

/* Body -> world: local X = the car's left, Y = its back (the front is -Y, as every perso's), Z = up (tilted). */
static tc_mat34 body_matrix(tc_vec3 position, float heading, tc_vec3 up) {
    tc_vec3 f = front_of(heading), F, X, Y;
    tc_mat34 m;
    F = tc_v3_normalize(tc_v3_sub(f, tc_v3_scale(up, tc_v3_dot(f, up))));
    if (tc_v3_length_sq(F) < 1e-6f) F = f;
    Y = tc_v3_scale(F, -1);
    X = tc_v3_cross(Y, up);
    m.m[0] = X.x, m.m[1] = Y.x, m.m[2] = up.x, m.m[3] = position.x;
    m.m[4] = X.y, m.m[5] = Y.y, m.m[6] = up.y, m.m[7] = position.y;
    m.m[8] = X.z, m.m[9] = Y.z, m.m[10] = up.z, m.m[11] = position.z;
    return m;
}

/* A point in the car's flat axes (heading only) -> world. */
static tc_vec3 flat_point(tc_vec3 position, float heading, float x, float y, float z) {
    float c = cosf(heading), s = sinf(heading);
    return tc_v3(position.x + c * x - s * y, position.y + s * x + c * y, position.z + z);
}

/* ---- ground and walls ---- */

typedef struct ground_probe { int hits; float z[TCX_VEHICLE_MAX_WHEELS]; bool hit[TCX_VEHICLE_MAX_WHEELS]; } ground_probe;

/* Each wheel's ray, from TCX_VEHICLE_STEP above the car's ground down to `below` under it: the highest floor-like face. */
static void probe_ground(const tcx_vehicle *c, tc_collision *world, float below, ground_probe *g) {
    const tcx_vehicle_data *d = c->data;
    uint32_t i;
    g->hits = 0;
    for (i = 0; i < d->wheel_count; i++) {
        const tcx_vehicle_wheel *w = &d->wheels[i];
        tc_vec3 o = flat_point(c->position, c->heading, w->centre.x, w->centre.y, TCX_VEHICLE_STEP);
        tc_hit hit;
        float reach = TCX_VEHICLE_STEP + below, z = -INFINITY;
        /* the first face down may be a wall's edge: look on past steep faces */
        while (reach > 0 && tc_collision_raycast(world, o, tc_v3(0, 0, -1), reach, &hit)) {
            if (fabsf(hit.normal.z) > 0.5f) {
                z = o.z - hit.distance;
                break;
            }
            o.z -= hit.distance + 0.01f;
            reach -= hit.distance + 0.01f;
        }
        g->hit[i] = isfinite(z);
        g->z[i] = z;
        if (g->hit[i]) g->hits++;
    }
}

typedef struct wall_push {
    tc_vec3 centre;
    float radius;
    tc_vec3 push;
    tc_vec3 normal;   /* the deepest contact's, horizontal */
    float depth;
} wall_push;

static void wall_tri(const tc_tri *t, void *user) {
    wall_push *w = (wall_push *)user;
    tc_vec3 p, d, n;
    float dist, depth;
    if (fabsf(t->normal.z) >= 0.6f) return; /* floors and ceilings are the wheels' business */
    p = tc_closest_point_on_triangle(t->a, t->b, t->c, w->centre);
    d = tc_v3_sub(w->centre, p);
    dist = tc_v3_length(d);
    if (dist >= w->radius) return;
    n = tc_v3(d.x, d.y, 0);
    if (tc_v3_length_sq(n) < 1e-8f) { /* the centre on the face: out along its normal, towards where it came from */
        n = tc_v3(t->normal.x, t->normal.y, 0);
        if (tc_v3_dot(n, tc_v3_sub(w->centre, t->a)) < 0) n = tc_v3_scale(n, -1);
    }
    n = tc_v3_normalize(n);
    depth = w->radius - dist;
    w->centre = tc_v3_add(w->centre, tc_v3_scale(n, depth));
    w->push = tc_v3_add(w->push, tc_v3_scale(n, depth));
    if (depth > w->depth) w->depth = depth, w->normal = n;
}

/* Three spheres along the body, their bottoms clear of kerbs: pushed out of the walls, the car with them, its speed into
 * them taken away (a little bounce). */
static void push_walls(tcx_vehicle *c, tc_collision *world) {
    const tcx_vehicle_data *d = c->data;
    float r = 0.48f * (d->box_hi.x - d->box_lo.x), zc = r + 0.3f;
    float cy = 0.5f * (d->box_lo.y + d->box_hi.y), half = 0.5f * (d->box_hi.y - d->box_lo.y), cx = 0.5f * (d->box_lo.x + d->box_hi.x);
    float ys[3];
    int k, pass, n = 0;
    if (r <= 0.05f) return;
    ys[n++] = cy;
    if (half - r > 0.1f) ys[n++] = cy - (half - r), ys[n++] = cy + (half - r);
    for (pass = 0; pass < 2; pass++)
        for (k = 0; k < n; k++) {
            wall_push w;
            w.centre = flat_point(c->position, c->heading, cx, ys[k], zc);
            w.radius = r;
            w.push = tc_v3(0, 0, 0);
            w.depth = 0;
            tc_collision_query(world, tc_v3_adds(w.centre, -r), tc_v3_adds(w.centre, r), wall_tri, &w);
            if (w.depth <= 0) continue;
            c->position = tc_v3_add(c->position, tc_v3(w.push.x, w.push.y, 0));
            {
                float vn = tc_v3_dot(c->velocity, w.normal);
                if (vn < 0) {
                    c->velocity = tc_v3_sub(c->velocity, tc_v3_scale(w.normal, vn * 1.2f));
                    c->yaw_rate *= 0.7f; /* a knock takes some of the spin */
                }
            }
        }
}

/* ---- the tick ---- */

static float approach(float x, float target, float step) {
    return x < target ? fminf(target, x + step) : fmaxf(target, x - step);
}

static void place_on_ground(tcx_vehicle *c, tc_collision *world) {
    ground_probe g;
    float sum = 0;
    uint32_t i;
    c->position.z += 2.0f; /* from above whatever it was put into */
    probe_ground(c, world, 8.0f, &g);
    for (i = 0; i < c->data->wheel_count; i++) if (g.hit[i]) sum += g.z[i];
    if (g.hits) c->position.z = sum / (float)g.hits, c->grounded = true;
    else c->position.z -= 2.0f;
}

/* The engine, brakes and drag along the car: the change of forward speed per second. */
static float along(float fwd, float throttle, float handbrake, bool grounded) {
    float a = 0, drag;
    if (!grounded) return 0;
    if (throttle > 0) {
        if (fwd < -0.5f) a = TCX_VEHICLE_BRAKE * throttle; /* braking out of reverse */
        else a = throttle * fminf(TCX_VEHICLE_ENGINE_ACCEL, TCX_VEHICLE_ENGINE_POWER / fmaxf(fwd, 1));
    } else if (throttle < 0) {
        if (fwd > 0.5f) a = TCX_VEHICLE_BRAKE * throttle;
        else if (fwd > -TCX_VEHICLE_MAX_REVERSE) a = TCX_VEHICLE_REVERSE_ACCEL * throttle;
    }
    drag = TCX_VEHICLE_AERO * fwd * fabsf(fwd) + (fabsf(fwd) > 0.05f ? TCX_VEHICLE_ROLL * (fwd > 0 ? 1 : -1) : 0);
    drag += handbrake * TCX_VEHICLE_HANDBRAKE * (fwd > 0 ? 1 : fwd < 0 ? -1 : 0);
    return a - drag;
}

/* One substep of the tyres on the ground: the forward and sideways speeds and the yaw rate (see the header). */
static void tyres(tcx_vehicle *c, float throttle, float handbrake, float slope, float dt) {
    tc_vec3 f = front_of(c->heading), left = tc_v3(cosf(c->heading), sinf(c->heading), 0);
    float fwd = tc_v3_dot(c->velocity, f), lat = tc_v3_dot(c->velocity, left);
    float steer_left = -c->steer, a = c->to_front, b = c->to_rear, acc = along(fwd, throttle, handbrake, true);
    float before = fwd, kin, kin_yaw, kin_lat;
    fwd += (acc + TCX_VEHICLE_GRAVITY * slope) * dt; /* downhill the body's Y axis' z is positive */
    /* friction stops it rather than rolling it back and forth */
    if (throttle == 0 && ((before > 0 && fwd < 0) || (before < 0 && fwd > 0))) fwd = 0;
    /* the bicycle's (no slip): its share, 1 below TCX_VEHICLE_KINEMATIC_SPEED and in reverse, 0 from the tyres' speed */
    kin = fminf(1, fmaxf(0, (TCX_VEHICLE_DYNAMIC_SPEED - fwd) / (TCX_VEHICLE_DYNAMIC_SPEED - TCX_VEHICLE_KINEMATIC_SPEED)));
    kin_yaw = fwd / c->wheelbase * tanf(steer_left);
    kin_lat = lat * expf(-(12.0f - 9.0f * handbrake) * dt);
    if (kin >= 1) { /* slow or reversing: rolls where the wheels point */
        c->yaw_rate = kin_yaw;
        lat = kin_lat;
    } else {
        float share_f = b / (a + b), share_r = a / (a + b);
        float grip_f = TCX_VEHICLE_GRIP * share_f * TCX_VEHICLE_FRONT_GRIP, grip_r = TCX_VEHICLE_GRIP * share_r;
        float slip_f = atan2f(lat + a * c->yaw_rate, fwd) - steer_left, slip_r = atan2f(lat - b * c->yaw_rate, fwd);
        float drive = throttle > 0 ? fmaxf(0, acc) : 0, force_f, force_r;
        grip_r *= 1 - (1 - TCX_VEHICLE_HANDBRAKE_REAR) * handbrake;
        drive *= TCX_VEHICLE_DRIVE_SLIP; /* the drive is the rear's: the friction circle, kept mild */
        grip_r = sqrtf(fmaxf(grip_r * grip_r * 0.09f, grip_r * grip_r - drive * drive));
        force_f = fmaxf(-grip_f, fminf(grip_f, -TCX_VEHICLE_CORNERING * share_f * slip_f));
        force_r = fmaxf(-grip_r, fminf(grip_r, -TCX_VEHICLE_CORNERING * TCX_VEHICLE_REAR_STIFFNESS * share_r * slip_r));
        c->yaw_rate += (a * force_f - b * force_r) / (TCX_VEHICLE_INERTIA * a * b) * dt;
        lat += (force_f + force_r) * dt;
        if (kin > 0) { /* between the two speeds: drawn towards the bicycle's as it slows, at a rate (not a jump) */
            float k = 1 - expf(-8.0f * kin * dt);
            c->yaw_rate += (kin_yaw - c->yaw_rate) * k;
            lat += (kin_lat - lat) * k;
        }
    }
    c->velocity.x = f.x * fwd + left.x * lat;
    c->velocity.y = f.y * fwd + left.y * lat;
    c->heading = remainderf(c->heading + c->yaw_rate * dt, 2 * TC_PI);
}

/* The body's springs, a tick on: towards the lean the accelerations ask for (none in the air). */
static void body_springs(tcx_vehicle *c, float accel_along, float accel_across, float dt) {
    const float w = TCX_VEHICLE_BODY_FREQ, z = TCX_VEHICLE_BODY_DAMP;
    float want_pitch = c->grounded ? fmaxf(-0.12f, fminf(0.12f, TCX_VEHICLE_BODY_PITCH * accel_along)) : 0;
    float want_roll = c->grounded ? fmaxf(-0.12f, fminf(0.12f, -TCX_VEHICLE_BODY_ROLL * accel_across)) : 0;
    c->pitch_prev = c->pitch, c->roll_prev = c->roll, c->heave_prev = c->heave;
    c->pitch_rate += (w * w * (want_pitch - c->pitch) - 2 * z * w * c->pitch_rate) * dt;
    c->roll_rate += (w * w * (want_roll - c->roll) - 2 * z * w * c->roll_rate) * dt;
    c->heave_rate += (w * w * (0 - c->heave) - 2 * z * w * c->heave_rate) * dt;
    c->pitch = fmaxf(-0.2f, fminf(0.2f, c->pitch + c->pitch_rate * dt));
    c->roll = fmaxf(-0.2f, fminf(0.2f, c->roll + c->roll_rate * dt));
    c->heave = fmaxf(-0.25f, fminf(0.25f, c->heave + c->heave_rate * dt));
}

static void tick(tcx_vehicle *c, tc_collision *world, const tcx_vehicle_input *in) {
    const tcx_vehicle_data *d = c->data;
    const float dt = TCX_VEHICLE_TICK;
    tc_vec3 f = front_of(c->heading);
    float fwd = tc_v3_dot(c->velocity, f), fall = c->velocity.z;
    float throttle = in ? fmaxf(-1, fminf(1, in->throttle)) : 0, steer = in ? fmaxf(-1, fminf(1, in->steer)) : 0;
    bool handbrake = in && in->handbrake, hop = in && in->hop;
    /* the lock narrows with speed, and opens up again while it slides (to catch it) */
    float sliding = fminf(1, fabsf(atan2f(tc_v3_dot(c->velocity, tc_v3(cosf(c->heading), sinf(c->heading), 0)),
                                          fmaxf(1, fabsf(fwd)))) / TCX_VEHICLE_SLIDE_LOCK);
    float want = steer * TCX_VEHICLE_STEER_MAX / (1 + (1 - sliding) * fabsf(fwd) / TCX_VEHICLE_STEER_FADE);
    bool was_grounded = c->grounded;
    ground_probe g;
    uint32_t i;
    int k;

    c->position_prev = c->position, c->heading_prev = c->heading, c->up_prev = c->up;
    c->spin_prev = c->spin, c->steer_prev = c->steer;
    c->steer = approach(c->steer, want, TCX_VEHICLE_STEER_RATE * (fabsf(want) < fabsf(c->steer) ? 2 : 1) * dt);

    c->handbrake = approach(c->handbrake, handbrake ? 1.0f : 0.0f,
                            dt / (handbrake ? TCX_VEHICLE_HANDBRAKE_ON : TCX_VEHICLE_HANDBRAKE_OFF));
    if (c->grounded) {
        tc_mat34 body = body_matrix(c->position, c->heading, c->up);
        for (k = 0; k < TCX_VEHICLE_SUBSTEPS; k++) tyres(c, throttle, c->handbrake, body.m[9], dt / TCX_VEHICLE_SUBSTEPS);
        if (hop && !c->hop_held) { /* the hop: up, the nose a little first */
            c->velocity.z = TCX_VEHICLE_HOP;
            c->grounded = false;
            c->pitch_rate += 1.2f;
            c->heave_rate += 1.0f;
        }
    } else {
        c->velocity.z -= TCX_VEHICLE_GRAVITY * dt;
        c->yaw_rate *= expf(-0.5f * dt); /* it keeps turning in the air, a little less */
        c->heading = remainderf(c->heading + c->yaw_rate * dt, 2 * TC_PI);
    }
    c->hop_held = hop;
    c->position = tc_v3_add(c->position, tc_v3_scale(c->velocity, dt));
    push_walls(c, world);

    /* the ground under the wheels: held to it within TCX_VEHICLE_SNAP below (and up kerbs to TCX_VEHICLE_STEP), else falling */
    probe_ground(c, world, TCX_VEHICLE_SNAP + fmaxf(0, -c->velocity.z * dt), &g);
    if (g.hits) {
        float sum = 0, ground;
        for (i = 0; i < d->wheel_count; i++) if (g.hit[i]) sum += g.z[i];
        ground = sum / (float)g.hits;
        if (c->grounded ? c->position.z - ground <= TCX_VEHICLE_SNAP + 0.01f : c->position.z <= ground + 0.01f) {
            c->position.z = ground;
            c->velocity.z = 0;
            c->grounded = true;
        } else {
            c->grounded = false;
        }
    } else {
        c->grounded = false;
    }

    /* the body: pitched by the change of forward speed, leaning against the turn (sideways acceleration = speed x yaw
     * rate), dropped on its springs by a landing */
    if (c->grounded && !was_grounded && fall < -2) c->heave_rate += 0.03f * fall, c->pitch_rate -= 0.01f * fall;
    {
        float now = tc_v3_dot(c->velocity, front_of(c->heading));
        body_springs(c, (now - c->forward_prev) / dt, now * c->yaw_rate, dt);
        c->forward_prev = now;
    }

    /* the tilt: the plane of the wheels' floors (front against back, left against right) */
    {
        tc_vec3 want_up = tc_v3(0, 0, 1);
        float rate = 1.5f;
        if (c->grounded) {
            tc_vec3 fr = tc_v3(0, 0, 0), bk = fr, lf = fr, rt = fr;
            int nf = 0, nb = 0, nl = 0, nr = 0;
            float cy = 0.5f * (d->box_lo.y + d->box_hi.y), cx = 0.5f * (d->box_lo.x + d->box_hi.x);
            for (i = 0; i < d->wheel_count; i++) {
                tc_vec3 p;
                if (!g.hit[i]) continue;
                p = flat_point(c->position, c->heading, d->wheels[i].centre.x, d->wheels[i].centre.y, 0);
                p.z = g.z[i];
                if (d->wheels[i].centre.y < cy) fr = tc_v3_add(fr, p), nf++; else bk = tc_v3_add(bk, p), nb++;
                if (d->wheels[i].centre.x > cx) lf = tc_v3_add(lf, p), nl++; else rt = tc_v3_add(rt, p), nr++;
            }
            if (nf && nb && nl && nr) {
                tc_vec3 F = tc_v3_sub(tc_v3_scale(fr, 1.0f / nf), tc_v3_scale(bk, 1.0f / nb));
                tc_vec3 L = tc_v3_sub(tc_v3_scale(lf, 1.0f / nl), tc_v3_scale(rt, 1.0f / nr));
                tc_vec3 U = tc_v3_normalize(tc_v3_cross(F, L));
                if (U.z > 0.5f) want_up = U;
            }
            rate = 12;
        }
        c->up = tc_v3_normalize(tc_v3_add(c->up, tc_v3_scale(tc_v3_sub(want_up, c->up), 1 - expf(-rate * dt))));
    }

    /* the wheels roll with the ground speed */
    if (d->wheel_count && d->wheels[0].radius > 0.01f) {
        c->spin += tc_v3_dot(c->velocity, front_of(c->heading)) / d->wheels[0].radius * dt;
        c->spin = remainderf(c->spin, 2 * TC_PI);
    }

    c->air_time = c->grounded ? 0 : c->air_time + dt;
    if (c->air_time > TCX_VEHICLE_FALL_RESET) { /* off the world: back to the start */
        c->position = c->start_position;
        c->heading = c->start_heading;
        c->velocity = tc_v3(0, 0, 0);
        c->yaw_rate = 0;
        c->up = tc_v3(0, 0, 1);
        c->air_time = 0;
        c->grounded = false;
        place_on_ground(c, world);
        c->position_prev = c->position, c->heading_prev = c->heading, c->up_prev = c->up;
    }
}

void tcx_vehicle_init(tcx_vehicle *c, const tcx_vehicle_data *d, const tc_mat34 *placement, tc_collision *world) {
    uint32_t i;
    float front = INFINITY, back = -INFINITY;
    const uint8_t *solid = world->solid;
    memset(c, 0, sizeof(*c));
    c->data = d;
    c->position = tc_v3(placement->m[3], placement->m[7], placement->m[11]);
    c->heading = atan2f(-placement->m[1], placement->m[5]); /* where local -Y points: (sin h, -cos h) */
    c->up = tc_v3(0, 0, 1);
    for (i = 0; i < d->wheel_count; i++) front = fminf(front, d->wheels[i].centre.y), back = fmaxf(back, d->wheels[i].centre.y);
    c->wheelbase = d->wheel_count >= 2 ? fmaxf(1, back - front) : 2.5f;
    { /* the centre of mass: the body's middle, kept between the axles */
        float cy = 0.5f * (d->box_lo.y + d->box_hi.y);
        if (d->wheel_count < 2) front = cy - 1.25f, back = cy + 1.25f;
        cy = fmaxf(front + 0.25f * c->wheelbase, fminf(back - 0.25f * c->wheelbase, cy));
        c->to_front = cy - front;
        c->to_rear = back - cy;
    }
    world->solid = NULL;
    place_on_ground(c, world);
    world->solid = solid;
    c->start_position = c->position;
    c->start_heading = c->heading;
    c->position_prev = c->position, c->heading_prev = c->heading, c->up_prev = c->up;
}

void tcx_vehicle_update(tcx_vehicle *c, tc_collision *world, const tcx_vehicle_input *in, float dt) {
    const uint8_t *solid = world->solid;
    int ticks = 0;
    world->solid = NULL; /* every sector's surfaces: the car keeps no sector of its own */
    c->tick_time += dt;
    while (c->tick_time >= TCX_VEHICLE_TICK) {
        c->tick_time -= TCX_VEHICLE_TICK;
        if (++ticks > 16) { c->tick_time = 0; break; } /* a long stall: drop the backlog */
        tick(c, world, in);
    }
    world->solid = solid;
}

/* ---- drawing ---- */

static float share(const tcx_vehicle *c) { return fminf(1, fmaxf(0, c->tick_time / TCX_VEHICLE_TICK)); }

tc_vec3 tcx_vehicle_drawn_position(const tcx_vehicle *c) {
    tc_vec3 d = tc_v3_sub(c->position, c->position_prev);
    if (tc_v3_length_sq(d) > 25) return c->position; /* put back at the start: no streak */
    return tc_v3_add(c->position_prev, tc_v3_scale(d, share(c)));
}

float tcx_vehicle_drawn_heading(const tcx_vehicle *c) {
    return c->heading_prev + remainderf(c->heading - c->heading_prev, 2 * TC_PI) * share(c);
}

tc_mat34 tcx_vehicle_world(const tcx_vehicle *c) {
    float t = share(c);
    tc_vec3 up = tc_v3_normalize(tc_v3_add(c->up_prev, tc_v3_scale(tc_v3_sub(c->up, c->up_prev), t)));
    return body_matrix(tcx_vehicle_drawn_position(c), tcx_vehicle_drawn_heading(c), up);
}

tc_vec3 tcx_vehicle_point(const tcx_vehicle *c, tc_vec3 local) {
    tc_mat34 m = tcx_vehicle_world(c);
    return tc_m34_point(&m, local);
}

float tcx_vehicle_speed(const tcx_vehicle *c) { return tc_v3_dot(c->velocity, front_of(c->heading)); }

/* The body on its springs (channel -> car): pitched about X (nose up), rolled about Y (left side down), lifted, about a
 * point between the wheels' centres. */
static tc_mat34 body_spring(const tcx_vehicle *c) {
    const tcx_vehicle_data *d = c->data;
    float t = share(c), pitch = c->pitch_prev + (c->pitch - c->pitch_prev) * t, roll = c->roll_prev + (c->roll - c->roll_prev) * t;
    float heave = c->heave_prev + (c->heave - c->heave_prev) * t;
    float cp = cosf(-pitch), sp = sinf(-pitch), cr = cosf(roll), sr = sinf(roll);
    tc_vec3 o = tc_v3(0.5f * (d->box_lo.x + d->box_hi.x), 0.5f * (d->box_lo.y + d->box_hi.y),
                      d->wheel_count ? d->wheels[0].centre.z : 0.5f * (d->box_lo.z + d->box_hi.z));
    tc_mat34 m;
    /* R = Rx(-pitch) x Ry(roll) */
    m.m[0] = cr, m.m[1] = 0, m.m[2] = sr;
    m.m[4] = sp * sr, m.m[5] = cp, m.m[6] = -sp * cr;
    m.m[8] = -cp * sr, m.m[9] = sp, m.m[10] = cp * cr;
    m.m[3] = o.x - (m.m[0] * o.x + m.m[1] * o.y + m.m[2] * o.z);
    m.m[7] = o.y - (m.m[4] * o.x + m.m[5] * o.y + m.m[6] * o.z);
    m.m[11] = o.z + heave - (m.m[8] * o.x + m.m[9] * o.y + m.m[10] * o.z);
    return m;
}

tc_mat34 tcx_vehicle_channel(const tcx_vehicle *c, uint16_t channel) {
    const tcx_vehicle_data *d = c->data;
    float t = share(c), spin, steer, cs, ss, ct, st;
    tc_mat34 m;
    uint32_t i;
    memset(&m, 0, sizeof(m));
    m.m[0] = m.m[5] = m.m[10] = 1;
    for (i = 0; i < d->wheel_count; i++) {
        const tcx_vehicle_wheel *w = &d->wheels[i];
        if (w->channel != channel) continue;
        spin = c->spin_prev + remainderf(c->spin - c->spin_prev, 2 * TC_PI) * t;
        steer = w->steers ? -(c->steer_prev + (c->steer - c->steer_prev) * t) : 0; /* right = turned towards -X */
        cs = cosf(spin), ss = sinf(spin), ct = cosf(steer), st = sinf(steer);
        /* Rz(steer) x Rx(spin): rolling forward (-Y) turns its top towards the front */
        m.m[0] = ct, m.m[1] = -st * cs, m.m[2] = st * ss, m.m[3] = w->centre.x;
        m.m[4] = st, m.m[5] = ct * cs, m.m[6] = -ct * ss, m.m[7] = w->centre.y;
        m.m[8] = 0, m.m[9] = ss, m.m[10] = cs, m.m[11] = w->centre.z;
        return m;
    }
    return body_spring(c); /* the body */
}

/* ---- the ride ---- */

bool tcx_ride_in_reach(const tcx_vehicle *c, tc_vec3 feet) {
    const tcx_vehicle_data *d = c->data;
    tc_vec3 p = tcx_vehicle_drawn_position(c), rel = tc_v3_sub(feet, p);
    float h = tcx_vehicle_drawn_heading(c);
    float lx = rel.x * cosf(h) + rel.y * sinf(h), ly = -rel.x * sinf(h) + rel.y * cosf(h); /* car axes */
    float dx = fmaxf(0, fmaxf(d->box_lo.x - lx, lx - d->box_hi.x)), dy = fmaxf(0, fmaxf(d->box_lo.y - ly, ly - d->box_hi.y));
    return dx * dx + dy * dy <= TCX_RIDE_REACH * TCX_RIDE_REACH && fabsf(rel.z) <= TCX_RIDE_HEIGHT;
}

/* Where he stands on it: over the middle of the body, on its top. */
static tc_vec3 roof(const tcx_vehicle_data *d) {
    return tc_v3(0.5f * (d->box_lo.x + d->box_hi.x), 0.5f * (d->box_lo.y + d->box_hi.y), d->box_hi.z);
}

/* How high the arc between feet a and b may rise above the higher of them: TCX_RIDE_HOP_HEIGHT, less what a ceiling over
 * either end leaves for his head (an overhang over the car). */
static float hop_lift(tc_collision *world, tc_vec3 a, tc_vec3 b) {
    const uint8_t *solid = world->solid;
    float top = fmaxf(a.z, b.z), lift = TCX_RIDE_HOP_HEIGHT;
    const tc_vec3 ends[2] = {a, b};
    int k;
    world->solid = NULL;
    for (k = 0; k < 2; k++) {
        tc_hit hit;
        tc_vec3 o = tc_v3(ends[k].x, ends[k].y, ends[k].z + 0.1f);
        if (tc_collision_raycast(world, o, tc_v3(0, 0, 1), TCX_RIDE_HOP_HEIGHT + TCX_RIDE_ED_HEIGHT + 1, &hit))
            lift = fminf(lift, o.z + hit.distance - TCX_RIDE_ED_HEIGHT - top);
    }
    world->solid = solid;
    return fmaxf(0, lift);
}

void tcx_ride_board(tcx_ride *r, int32_t vehicle, const tcx_vehicle *c, tc_vec3 feet, tc_collision *world) {
    tc_vec3 to = tcx_vehicle_point(c, c->data->seat);
    memset(r, 0, sizeof(*r));
    r->phase = TCX_RIDE_BOARDING;
    r->vehicle = vehicle;
    r->from = feet;
    r->to = c->data->seat;
    r->heading = atan2f(to.x - feet.x, -(to.y - feet.y));
    r->lift = hop_lift(world, feet, to);
}

bool tcx_ride_leave(tcx_ride *r, const tcx_vehicle *c, tc_collision *world) {
    const tcx_vehicle_data *d = c->data;
    const tc_vec3 top = roof(d);
    const float cx = top.x, cy = top.y;
    const tc_vec3 spots[4] = {tc_v3(d->box_hi.x + TCX_RIDE_DOOR, cy, 0), tc_v3(d->box_lo.x - TCX_RIDE_DOOR, cy, 0),
                              tc_v3(cx, d->box_hi.y + TCX_RIDE_DOOR, 0), tc_v3(cx, d->box_lo.y - TCX_RIDE_DOOR, 0)};
    const uint8_t *solid = world->solid;
    tc_vec3 base = tcx_vehicle_drawn_position(c);
    float h = tcx_vehicle_drawn_heading(c);
    int k;
    bool found = false;
    world->solid = NULL;
    for (k = 0; k < 4 && !found; k++) {
        tc_vec3 w = flat_point(base, h, spots[k].x, spots[k].y, 0), centre = flat_point(base, h, cx, cy, 0), dir;
        float len, z = -INFINITY;
        tc_hit hit;
        int j, blocked = 0;
        /* nothing in the way at his knees and his head */
        dir = tc_v3_sub(tc_v3(w.x, w.y, 0), tc_v3(centre.x, centre.y, 0));
        len = tc_v3_length(dir);
        if (len < 1e-3f) continue;
        dir = tc_v3_div(dir, len);
        for (j = 0; j < 2 && !blocked; j++) {
            tc_vec3 o = tc_v3(centre.x, centre.y, base.z + (j ? 1.8f : 0.6f));
            blocked = tc_collision_raycast(world, o, dir, len + 0.4f, &hit);
        }
        if (blocked) continue;
        /* a floor there, not far from the car's */
        if (tc_collision_raycast(world, tc_v3(w.x, w.y, base.z + 2.0f), tc_v3(0, 0, -1), 2.0f + TCX_RIDE_HEIGHT, &hit) &&
            fabsf(hit.normal.z) > 0.5f)
            z = base.z + 2.0f - hit.distance;
        if (!isfinite(z)) continue;
        r->to = tc_v3(w.x, w.y, z);
        found = true;
    }
    world->solid = solid;
    if (!found) return false;
    r->phase = TCX_RIDE_LEAVING;
    r->time = 0;
    r->from = tcx_vehicle_point(c, d->seat);
    r->heading = atan2f(r->to.x - r->from.x, -(r->to.y - r->from.y));
    r->lift = hop_lift(world, r->from, r->to);
    return true;
}

bool tcx_ride_step(tcx_ride *r, float dt) {
    if (r->phase != TCX_RIDE_BOARDING && r->phase != TCX_RIDE_LEAVING) return false;
    r->time += dt;
    if (r->time < TCX_RIDE_HOP) return false;
    r->phase = r->phase == TCX_RIDE_BOARDING ? TCX_RIDE_DRIVING : TCX_RIDE_NONE;
    r->time = 0;
    return true;
}

tc_mat34 tcx_ride_seat_matrix(const tcx_vehicle *c) {
    const tcx_vehicle_data *d = c->data;
    tc_mat34 w = tcx_vehicle_world(c), body = body_spring(c), seat;
    float k = d->rider_scale;
    memset(&seat, 0, sizeof(seat));
    seat.m[0] = seat.m[5] = seat.m[10] = k;
    seat.m[3] = d->seat.x, seat.m[7] = d->seat.y, seat.m[11] = d->seat.z;
    w = tc_m34_mul(&w, &body); /* he rides the body */
    return tc_m34_mul(&w, &seat);
}

void tcx_ride_pose(const tcx_ride *r, const tcx_vehicle *c, tc_vec3 *feet, float *heading, float *scale, float *done) {
    float s = fminf(1, fmaxf(0, r->time / TCX_RIDE_HOP)), lift, k = c->data->rider_scale;
    tc_vec3 a = r->from, b = r->phase == TCX_RIDE_BOARDING ? tcx_vehicle_point(c, r->to) : r->to;
    if (r->phase == TCX_RIDE_DRIVING) { /* in the seat, where the car is */
        *feet = tcx_vehicle_point(c, c->data->seat);
        *heading = tcx_vehicle_drawn_heading(c);
        *scale = k;
        *done = 1;
        return;
    }
    *scale = r->phase == TCX_RIDE_BOARDING ? 1 + (k - 1) * s : k + (1 - k) * s;
    lift = fmaxf(a.z, b.z) - 0.5f * (a.z + b.z) + r->lift; /* 4 lift s (1 - s) peaks at s = 1/2 */
    *feet = tc_v3(a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, a.z + (b.z - a.z) * s + 4 * lift * s * (1 - s));
    *heading = r->heading;
    *done = s;
}
