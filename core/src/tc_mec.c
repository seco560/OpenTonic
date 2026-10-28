#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_mec.h"

/* The fix's air environment 0x36b51c, shared by every sector of every level: g 9.81, density (+0x14) 1.3. Water
 * sectors list a second one (density 1000) for below their surface (docs/NOTES.md "Swimming"). */
tc_mec_env tc_mec_env_default(void) {
    tc_mec_env e = {9.81f, 1.3f, 1.0f};
    return e;
}

static bool box_holds(const tc_vec3 *b, tc_vec3 p) {
    return p.x >= b[0].x && p.y >= b[0].y && p.z >= b[0].z && p.x <= b[1].x && p.y <= b[1].y && p.z <= b[1].z;
}

static float box_volume(const tc_vec3 *b) {
    return (b[1].x - b[0].x) * (b[1].y - b[0].y) * (b[1].z - b[0].z);
}

/* The surface nearest in height among those whose boxes hold p. The exe asks the perso's sector; without sectors, a
 * .ttai DRYB dry box holding p that is smaller than every water box holding it stands for a dry sector there. */
float tc_mec_env_at(const tc_ai_model *m, tc_vec3 p, tc_mec_env *env) {
    const tc_ai_water *best = NULL;
    float best_d = 0, wet_v = INFINITY, depth;
    uint16_t i, k;
    for (i = 0; m && i < m->water_count; i++) {
        const tc_ai_water *w = &m->water[i];
        bool holds = false;
        for (k = 0; k < w->box_count; k++) {
            const tc_vec3 *b = &w->boxes[2 * k];
            if (!box_holds(b, p)) continue;
            holds = true;
            wet_v = fminf(wet_v, box_volume(b));
        }
        if (holds && (!best || fabsf(w->z - p.z) < best_d)) {
            best = w;
            best_d = fabsf(w->z - p.z);
        }
    }
    for (i = 0; best && m && i < m->dry_box_count; i++)
        if (box_holds(&m->dry_boxes[2 * i], p) && box_volume(&m->dry_boxes[2 * i]) < wet_v) best = NULL;
    *env = tc_mec_env_default();
    depth = best ? best->z - p.z : 0; /* FUN_0045d880: surface height - point height */
    if (depth <= 0) return 0;
    env->gravity = best->gravity;
    env->density = best->density;
    env->drag = best->drag;
    return depth;
}

float tc_mec_per_frame(float k, float dt) {
    if (!(k > 0 && k < 1) || !(dt > 0)) return k;
    return 1 - powf(1 - k, dt / TC_MEC_FRAME);
}

/* ---- Type 5: flight ----
 * Card fields used (Ed's 0x3c379c): +4 mass 80, +8 polar set 2, +0x10/+0x14/+0x18 lag times of attack / bank /
 * thrust 0.1 / 0.3 / 2 s, +0x24 thrust 2000, +0x28 wing area 5.5, +0x2C/+0x30 attack range 12 / -4 degrees,
 * +0x34/+0x38 bank range 85 / -85, +0x3C bank factor without a turn request 0, +0x44 top speed 20,
 * +0x48 launch speed 10, byte +0x4C roll sign 1. */

/* Lift coefficient by angle of attack (degrees), polar sets 1 and 2 (FUN_004caa9b). */
static float lift_coefficient(float a) { return a >= -10 && a <= 15 ? a * 0.0693f + 0.18f : 0; }

/* Lift-to-drag ratio (FUN_004caae9): 11 outside 0..15 degrees, rising to 22 at 1, an arc to 24 at 3, then down. */
static float lift_to_drag(float a) {
    if (a < 0 || a > 15) return 11;
    if (a <= 1) return a * 11 + 11;
    if (a <= 3) return sqrtf(fmaxf(4 - (3 - a) * (3 - a), 0)) + 22;
    return a * -1.08f + 27.24f;
}

/* FUN_004cac3c's sets: 0 and 3 are constants (DAT_004e7834.., not read yet), 1 = set 2 with drag x 10. */
static void polar(uint32_t set, float a, float *cl, float *ld) {
    *cl = lift_coefficient(a);
    *ld = lift_to_drag(a);
    if (set == 1) *ld *= 0.1f;
}

static uint32_t card_u32(const tc_ai_card *c, uint32_t offset) {
    return offset >= 4 && offset / 4 - 1 < c->word_count ? c->words[offset / 4 - 1] : 0;
}

/* FUN_004c9afb: x += (target - x) / lag x dt. */
static float lag(float x, float target, float lag_time, float dt) {
    if (lag_time == 0) lag_time = 0.001f;
    return x + (target - x) / lag_time * dt;
}

static tc_vec3 rotate_about(tc_vec3 v, tc_vec3 axis, float angle);

/* FUN_004a9993 + the roll (FUN_004a9588 / 004a8e65 in FUN_004aea7c): the frame along v, rolled about its x by roll.
 * Too slow to have a direction (|v|^2 <= 1e-4, DAT_004e7240): the frame stays (the exe rebuilds it from the model's
 * axes and rolls it again; with Ed's roll byte that is last frame's frame). */
static void fly_frame(tc_mec_fly *f, tc_vec3 v, float roll) {
    float v2 = tc_v3_dot(v, v), c = cosf(roll), s = sinf(roll);
    tc_vec3 forward, side, up;
    if (v2 <= 1e-4f) return;
    forward = tc_v3_div(v, sqrtf(v2));
    side = tc_v3(-forward.y, forward.x, 0);
    if (tc_v3_length(side) < 1e-4f) side = tc_v3(0, 1, 0); /* straight up or down: the exe's heading 0 + pi / 2 */
    side = tc_v3_div(side, tc_v3_length(side));
    up = tc_v3_cross(forward, side);
    f->frame_x = forward;
    f->frame_y = tc_v3_add(tc_v3_scale(side, c), tc_v3_scale(up, s));   /* M x Rx(roll): columns y, z */
    f->frame_z = tc_v3_sub(tc_v3_scale(up, c), tc_v3_scale(side, s));
}

void tc_mec_fly_enter(tc_mec_fly *f, const tc_ai_card *card, tc_vec3 forward, tc_vec3 *velocity) {
    float thrust = f->thrust, thrust_lagged = f->thrust_lagged; /* not reset by the exe */
    memset(f, 0, sizeof(*f));
    f->thrust = thrust;
    f->thrust_lagged = thrust_lagged;
    *velocity = tc_v3_scale(forward, tc_ai_card_f32(card, 0x48));
    f->frame_x = forward;
    f->frame_y = tc_v3(-forward.y, forward.x, 0);
    f->frame_z = tc_v3(0, 0, 1);
    fly_frame(f, *velocity, 0);
}

void tc_mec_fly_request(tc_mec_fly *f, const tc_ai_card *card, int mode, const float *proposal, float dt,
                        const tc_vec3 *velocity) {
    float attack = f->attack, bank = f->bank, thrust = 0;
    switch (mode) {
    case 1: attack -= proposal[0x18]; break;
    case 2: attack += proposal[0x18]; break;
    case 3: bank -= proposal[0xB]; break;
    case 4: bank += proposal[0xB]; break;
    case 10: thrust += tc_ai_card_f32(card, 0x24); break;
    case 21: /* a force of vector x mass / dt (FUN_004c9bad) for this step: the velocity changes by the vector */
        f->pending_dv = tc_v3_add(f->pending_dv, tc_v3(proposal[1], proposal[2], proposal[3]));
        break;
    default: bank *= tc_ai_card_f32(card, 0x3C); break;
    }
    attack = fminf(attack, tc_ai_card_f32(card, 0x2C));
    attack = fmaxf(attack, tc_ai_card_f32(card, 0x30));
    bank = fminf(bank, tc_ai_card_f32(card, 0x34));
    bank = fmaxf(bank, tc_ai_card_f32(card, 0x38));
    f->attack_lagged = lag(f->attack_lagged, attack * TC_PI / 180, tc_ai_card_f32(card, 0x10), dt);
    f->bank_lagged = lag(f->bank_lagged, bank * TC_PI / 180, tc_ai_card_f32(card, 0x14), dt);
    f->thrust_lagged = lag(f->thrust_lagged, thrust, tc_ai_card_f32(card, 0x18), dt);
    if (f->thrust_lagged > 0.7f * tc_ai_card_f32(card, 0x24)) thrust = 0;
    f->attack = attack;
    f->bank = bank;
    f->thrust = thrust;
    fly_frame(f, *velocity, (card_u32(card, 0x4C) & 0xFF) ? f->bank_lagged : -f->bank_lagged);
}

tc_vec3 tc_mec_fly_velocity(tc_mec_fly *f, const tc_ai_card *card, tc_mec_env env, tc_vec3 velocity, float dt) {
    float mass = tc_ai_card_f32(card, 0x04), area = tc_ai_card_f32(card, 0x28), top = tc_ai_card_f32(card, 0x44);
    float v2 = tc_v3_dot(velocity, velocity), cl, ld, lift, drag, speed;
    tc_vec3 force;
    if (mass <= 0) return tc_v3(0, 0, 0);
    /* FUN_004c478d: weight; lift rho |v|^2 S CL(attack) along the frame's z, drag lift / (L/D) against its x. */
    force = tc_v3(0, 0, -mass * env.gravity);
    if (env.density > 0) {
        polar(card_u32(card, 0x08), f->attack, &cl, &ld);
        lift = env.density * v2 * area * cl;
        drag = ld != 0 ? lift / ld : 0;
        /* No lift that would push a steeply climbing (diving) glider further over the vertical. */
        if ((f->frame_x.z > 0.9f && lift > 0) || (f->frame_x.z < -0.9f && lift < 0)) lift = 0;
        force = tc_v3_add(force, tc_v3_scale(f->frame_z, lift));
        force = tc_v3_sub(force, tc_v3_scale(f->frame_x, drag));
    }
    velocity = tc_v3_add(velocity, f->pending_dv);
    f->pending_dv = tc_v3(0, 0, 0);
    velocity = tc_v3_add(velocity, tc_v3_scale(force, dt / mass));
    speed = tc_v3_length(velocity);
    if (top > 0 && speed > top) velocity = tc_v3_scale(velocity, top / speed);
    return velocity;
}

void tc_mec_fly_model_axes(const tc_mec_fly *f, const tc_ai_card *card, tc_vec3 *x, tc_vec3 *y, tc_vec3 *z) {
    tc_vec3 fx = f->frame_x, fy = f->frame_y, up = tc_v3(0, 0, 1);
    if (!(card_u32(card, 0x4C) & 0xFF)) {
        /* FUN_004b7a22 + FUN_004b7121: the rotation taking the frame's z onto up, applied to its x and y */
        tc_vec3 axis = tc_v3_cross(f->frame_z, up);
        float s = tc_v3_length(axis), c = tc_v3_dot(f->frame_z, up);
        if (s > 1e-6f) {
            float angle = atan2f(s, c);
            axis = tc_v3_div(axis, s);
            fx = rotate_about(fx, axis, angle);
            fy = rotate_about(fy, axis, angle);
        }
        *x = fy; *y = up; *z = fx;
        return;
    }
    *x = fy; *y = f->frame_z; *z = fx;
}

/* ---- Type 1: swimming ----
 * Card fields used (Ed's main swim card 0x3c3430): +4 mass 990, +0x10 / +0x14 / +0x18 lags of polar angle / heading
 * / thrust 0.1 s, +0x28 body size 1, +0x2C volume factor 1, +0x34 depth over which the volume ramps 10, +0x38 polar
 * decay in mode 0 0.99, +0x3C / +0x40 polar range 160 / 1 deg, +0x44 polar offset 0. */
void tc_mec_swim_enter(tc_mec_swim *s, float heading) {
    float h = heading - TC_PI / 2; /* tc_player's front (sin a, -cos a) = (cos h, sin h) */
    memset(s, 0, sizeof(*s));
    s->heading_lagged = h;
    s->heading = h * 180 / TC_PI;
}

void tc_mec_swim_request(tc_mec_swim *s, const tc_ai_card *card, int mode, const float *proposal, float dt) {
    float polar = s->polar, heading = s->heading, thrust = s->thrust, speed = proposal[0], p, h;
    float offset = tc_ai_card_f32(card, 0x44) * TC_PI / 180;
    switch (mode) {
    case 0: polar *= tc_ai_card_f32(card, 0x38); break;
    case 1: polar += proposal[0x18]; break;
    case 2: polar -= proposal[0x18]; break;
    case 3: heading += proposal[0xB]; break;
    case 4: heading -= proposal[0xB]; break;
    case 10: thrust += speed; break;
    case 20: {
        tc_vec3 d = tc_v3(proposal[7], proposal[8], proposal[9]);
        float n = tc_v3_length(d), xy;
        if (n > 0) d = tc_v3_div(d, n);
        xy = sqrtf(d.x * d.x + d.y * d.y);
        if (n == 0) { polar = 0; heading = 0; }
        else if (xy == 0) { heading = 0; polar = d.z < 0 ? 180 : 0; }
        else {
            heading = acosf(fminf(1, fmaxf(-1, d.x / xy))) * 180 / TC_PI;
            if (d.y < 0) heading = -heading;
            polar = acosf(fminf(1, fmaxf(-1, d.z))) * 180 / TC_PI;
        }
        thrust += speed;
        break;
    }
    default: break;
    }
    polar = fminf(polar, tc_ai_card_f32(card, 0x3C));
    polar = fmaxf(polar, tc_ai_card_f32(card, 0x40));
    p = polar * TC_PI / 180;
    h = heading * TC_PI / 180;
    s->polar_lagged = lag(s->polar_lagged, p, tc_ai_card_f32(card, 0x10), dt);
    s->heading_lagged = lag(s->heading_lagged, h, tc_ai_card_f32(card, 0x14), dt);
    s->thrust_lagged = lag(s->thrust_lagged, thrust, tc_ai_card_f32(card, 0x18), dt);
    if (s->thrust_lagged > 0.7f * speed) thrust = 0;
    s->polar = polar;
    s->heading = heading;
    s->thrust = thrust;
    /* the thrust pushes along the target direction (the frame uses the lagged one) */
    s->thrust_force = tc_v3_scale(tc_v3(cosf(h) * sinf(offset + p), sinf(h) * sinf(offset + p), cosf(offset + p)),
                                  s->thrust_lagged);
}

tc_vec3 tc_mec_swim_velocity(tc_mec_swim *s, const tc_ai_card *card, const tc_ai_model *m, tc_vec3 position,
                             tc_vec3 velocity, float dt, float *depth) {
    float mass = tc_ai_card_f32(card, 0x04), size = tc_ai_card_f32(card, 0x28), factor = tc_ai_card_f32(card, 0x2C);
    float ramp = tc_ai_card_f32(card, 0x34), half = 0.5f * size, s2 = size * size, s3 = s2 * size, d, volume = 0, speed;
    tc_mec_env env;
    tc_vec3 force;
    if (mass <= 0) return velocity;
    *depth = tc_mec_env_at(m, tc_v3_sub(position, tc_v3(0, 0, half)), &env);
    d = *depth - half;
    force = tc_v3(0, 0, env.gravity > 0 ? -mass * env.gravity : 0);
    if (d >= half) volume = d > ramp ? s3 * factor : (s3 * factor - s3) / (ramp != 0 ? ramp : 1) * d + s3;
    if (d > -half && d < half) volume = (half + d) * s2;
    force.z += env.density * env.gravity * volume;
    speed = tc_v3_length(velocity);
    if (env.density > 0 && d >= -half)
        force = tc_v3_sub(force, tc_v3_scale(velocity, env.density * fminf(speed, 10) * s2 * env.drag));
    force = tc_v3_add(force, s->thrust_force);
    s->thrust_force = tc_v3(0, 0, 0);
    return tc_v3_add(velocity, tc_v3_scale(force, dt / mass));
}

void tc_mec_swim_model_axes(const tc_mec_swim *s, const tc_ai_card *card, tc_vec3 *x, tc_vec3 *y, tc_vec3 *z) {
    float p = s->polar_lagged + tc_ai_card_f32(card, 0x44) * TC_PI / 180, h = s->heading_lagged;
    float sh = sinf(h), ch = cosf(h), sp = sinf(p), cp = cosf(p);
    *x = tc_v3(-sh, ch, 0);                 /* Rz(h + pi / 2) Rx(p): columns */
    *y = tc_v3(-ch * cp, -sh * cp, sp);
    *z = tc_v3(ch * sp, sh * sp, cp);
}

/* ---- Type 2: sliding ----
 * Card fields used (the skier's riding card 0x3c398c): +4 mass 150, +0x14 top speed 70 (Action's card: 50),
 * +0x24 steering 90, +0x28 weathervane 0.2, +0x2C edge grip 350, +0x30 hold-down force when upside down 6000,
 * +0x34 gravity factor 4, +0x38 alignment to the surface per frame 0.6, +0x3C steering factor in the air 0.5,
 * +0x40 extra gravity factor in the air 2.5, +0x44 probe length 2, +0x48 probe cos 0.7986 (37 deg),
 * +0x4C minimum speed 0.001. Steep = normal z below cos 70 deg (DAT_004d30a0). */
#define SKI_STEEP 0.34202f

static tc_vec3 rotate_about(tc_vec3 v, tc_vec3 axis, float angle) { /* Rodrigues, axis unit */
    float c = cosf(angle), s = sinf(angle);
    return tc_v3_add(tc_v3_add(tc_v3_scale(v, c), tc_v3_scale(tc_v3_cross(axis, v), s)),
                     tc_v3_scale(axis, tc_v3_dot(axis, v) * (1 - c)));
}

static tc_vec3 without(tc_vec3 v, tc_vec3 n) { return tc_v3_sub(v, tc_v3_scale(n, tc_v3_dot(v, n))); }

void tc_mec_ski_enter(tc_mec_ski *s, float heading) {
    memset(s, 0, sizeof(*s));
    s->y = tc_v3(-sinf(heading), cosf(heading), 0);
    s->z = tc_v3(0, 0, 1);
    s->x = tc_v3_cross(s->y, s->z);
}

float tc_mec_ski_heading(const tc_mec_ski *s) { return atan2f(-s->y.x, s->y.y); }

void tc_mec_ski_request(tc_mec_ski *s, const tc_ai_card *card, int mode, const float *proposal, tc_mec_contact *c,
                        tc_vec3 velocity) {
    float steer = 0, a = tc_ai_card_f32(card, 0x24), b = tc_ai_card_f32(card, 0x28), edge = tc_ai_card_f32(card, 0x2C);
    float factor;
    tc_vec3 push = tc_v3(0, 0, 0), jump = tc_v3(0, 0, 0), side, planar;
    if (mode == 3 || mode == 4) steer = proposal[0xD];
    if (mode == 10) {
        push = tc_v3_add(tc_v3_scale(s->y, proposal[2]),
                         tc_v3_scale(tc_v3(proposal[7], proposal[8], proposal[9]), proposal[1]));
        if (proposal[3] != 0) {
            c->ground = c->wall = false;
            jump = tc_v3_scale(s->z, proposal[3]);
        }
    }
    /* mode 20 (turn toward [7..9]) is not used by the skier: not ported */
    if (!c->ground && !c->wall && !s->near_ground) {
        factor = tc_ai_card_f32(card, 0x3C);
        a *= factor;
        b = 0;
    } else {
        factor = c->ground ? c->ground_factor : 1;
    }
    steer *= factor;
    edge *= factor;
    planar = without(velocity, s->z);
    /* angular velocity: steering about the board's up axis, plus the weathervane that swings the front (-y) into
     * the direction of travel; the last request of the frame wins */
    s->omega = tc_v3_add(tc_v3_scale(s->z, steer * a), tc_v3_scale(tc_v3_cross(planar, s->y), b));
    side = tc_v3_scale(s->x, -tc_v3_dot(planar, s->x) * edge); /* the edges resist sideways slip */
    if (c->ground ? c->ground_normal.z < 0.5f : (c->wall && c->wall_normal.z < 0.5f)) {
        tc_vec3 n = c->ground ? c->ground_normal : c->wall_normal;
        push = without(push, n);
        side = without(side, n);
    }
    s->force = tc_v3_add(s->force, tc_v3_add(tc_v3_add(push, side), jump));
}

tc_vec3 tc_mec_ski_velocity(tc_mec_ski *s, const tc_ai_card *card, tc_mec_env env, const tc_mec_contact *c,
                            tc_collision *world, tc_vec3 feet, tc_vec3 velocity, float dt) {
    float mass = tc_ai_card_f32(card, 0x04), top = tc_ai_card_f32(card, 0x14), min_speed = tc_ai_card_f32(card, 0x4C);
    float align = tc_ai_card_f32(card, 0x38), speed, angle, d;
    tc_vec3 up = tc_v3(0, 0, 1), gravity, f, target = s->z, axis;
    bool airborne = !c->ground && !c->wall && !s->near_ground;
    if (mass <= 0 || dt <= 0) return velocity;
    gravity = tc_v3(0, 0, -mass * env.gravity * tc_ai_card_f32(card, 0x34));
    f = gravity;
    if (airborne) {
        f = tc_v3_scale(f, tc_ai_card_f32(card, 0x40));
        target = up;
        align = 0.05f;
    }
    s->near_ground = false;
    if (!c->ground && !c->wall) {
        /* probe below the board: align to the surface under it, and hold it down when riding upside down */
        tc_hit hit;
        tc_vec3 down = tc_v3_scale(s->z, -1);
        if (tc_v3_dot(velocity, velocity) > min_speed * min_speed &&
            tc_collision_raycast(world, tc_v3_add(feet, tc_v3_scale(s->z, 0.4f)), down, tc_ai_card_f32(card, 0x44) + 0.4f, &hit) &&
            hit.distance - 0.4f < tc_ai_card_f32(card, 0x44) && tc_v3_dot(s->z, hit.normal) > tc_ai_card_f32(card, 0x48)) {
            float hold = tc_v3_dot(up, down);
            if (hold > 0.65f) f = tc_v3_add(f, tc_v3_scale(hit.normal, -tc_ai_card_f32(card, 0x30) * hold));
            s->near_ground = true;
            target = hit.normal;
        }
    } else {
        tc_vec3 n = c->ground ? c->ground_normal : c->wall_normal;
        float grip = c->ground ? c->ground_grip : 1, g2 = tc_mec_per_frame(fminf(fmaxf(grip * grip, 0), 1), dt);
        if (tc_v3_dot(n, s->z) > 0.5f || tc_v3_dot(n, up) > 0.5f) target = n;
        f = tc_v3_sub(f, tc_v3_scale(n, tc_v3_dot(n, gravity)));      /* the surface carries the weight */
        f = tc_v3_add(f, tc_v3_scale(velocity, -mass * g2 / dt));   /* friction per 1/30 s: grip 1 stops dead, snow 0 none */
    }
    if ((c->ground && c->ground_normal.z < SKI_STEEP) || (!c->ground && c->wall && c->wall_normal.z < SKI_STEEP)) {
        f = without(f, c->ground ? c->ground_normal : c->wall_normal);
        s->near_ground = false;
    }
    /* turn z towards the surface (or up in the air), a fraction per 1/30 s */
    if (tc_v3_dot(velocity, velocity) > min_speed * min_speed) {
        axis = tc_v3_cross(s->z, target);
        d = tc_v3_length(axis);
        if (d > 1e-6f) {
            angle = acosf(fminf(fmaxf(tc_v3_dot(s->z, target), -1), 1));
            s->omega = tc_v3_add(s->omega, tc_v3_scale(tc_v3_div(axis, d), angle * tc_mec_per_frame(align, dt) / dt));
        }
    }
    velocity = tc_v3_add(velocity, tc_v3_scale(tc_v3_add(f, s->force), dt / mass));
    s->force = tc_v3(0, 0, 0);
    speed = tc_v3_length(velocity);
    if (top > 0 && speed > top) velocity = tc_v3_scale(velocity, top / speed);
    if (c->ground && c->ground_normal.z < SKI_STEEP) velocity = without(velocity, c->ground_normal);
    /* orientation += omega dt */
    speed = tc_v3_length(s->omega);
    if (speed * dt > 1e-7f) {
        axis = tc_v3_div(s->omega, speed);
        s->y = rotate_about(s->y, axis, speed * dt);
        s->z = rotate_about(s->z, axis, speed * dt);
    }
    s->z = tc_v3_div(s->z, tc_v3_length(s->z));
    s->y = without(s->y, s->z);
    s->y = tc_v3_div(s->y, fmaxf(tc_v3_length(s->y), 1e-6f));
    s->x = tc_v3_cross(s->y, s->z);
    return velocity;
}
