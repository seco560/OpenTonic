#include "tc_internal.h"
#include "tonic/tc_player.h"

static const char *const anim_names[TC_ANIM_COUNT] = {"idle", "walk", "run", "jump", "jump_air", "fall", "land"};

const char *tc_anim_state_name(tc_anim_state s) {
    return (unsigned)s < TC_ANIM_COUNT ? anim_names[s] : "idle";
}

static void reset_motion(tc_player *p) {
    p->velocity = tc_v3(0, 0, 0);
    p->grounded = false;
    p->air_time = 0;
    p->since_jump = INFINITY;
}

void tc_player_init(tc_player *p, tc_collision *world, tc_vec3 start, float heading) {
    int i;
    p->world = world;
    p->start_position = start;
    p->start_heading = heading;
    p->position = tc_v3_add(start, tc_v3(0, 0, 0.3f)); /* GPT placements sit a little into the floor */
    p->heading = heading;
    reset_motion(p);
    // Basic walking values for Ed: inferred from guesswork. Mechanics are for demonstration only.
    p->radius = 0.4f;
    p->height = 1.4f;
    p->walk_speed = 3.3f;
    p->run_speed = 9.9f;
    p->jump_speed = 10.1f;
    p->gravity = 13.37f;
    p->max_slope = 33 * TC_PI / 180;
    // "Sliding" Values: for skislope and topsecret. May be plugged directly into level or crudely decommented and rebuilt.
//    p->walk_speed = 10.1f;
//    p->run_speed = 20.2f;
//    p->jump_speed = 22.3f;
//    p->gravity = 11.2f;
//    p->max_slope = 10 * TC_PI / 180;
    p->snap_distance = 0.45f;
    p->animation = TC_ANIM_IDLE;
    p->animation_time = 0;
    for (i = 0; i < TC_ANIM_COUNT; i++) p->clip_length[i] = 0;
    p->clip_length[TC_ANIM_JUMP] = 0.3f;
    p->clip_length[TC_ANIM_LAND] = 0.5f;
    p->jump_held = false;
    p->land_timer = 0;
}

void tc_player_respawn(tc_player *p) {
    p->position = tc_v3_add(p->start_position, tc_v3(0, 0, 0.3f));
    p->heading = p->start_heading;
    reset_motion(p);
}

void tc_player_teleport(tc_player *p, tc_vec3 position, float heading) {
    p->position = position;
    p->heading = heading;
    reset_motion(p);
}

float tc_player_horizontal_speed(const tc_player *p) { return tc_v2_length(tc_v2(p->velocity.x, p->velocity.y)); }

typedef struct resolve_ctx { tc_player *p; tc_vec3 c; float off, cos_slope; bool touched; } resolve_ctx;

static void resolve_tri(const tc_tri *t, void *user) {
    resolve_ctx *x = (resolve_ctx *)user;
    tc_player *p = x->p;
    float r = p->radius;
    tc_vec3 q = tc_closest_point_on_triangle(t->a, t->b, t->c, x->c);
    tc_vec3 diff = tc_v3_sub(x->c, q), n;
    float d2 = tc_v3_length_sq(diff), dist, pen;
    if (!(d2 < r * r && d2 > 1e-12f)) return;
    dist = sqrtf(d2);
    n = tc_v3_div(diff, dist);
    pen = r - dist;
    if (n.z >= x->cos_slope) {
        p->position.z += fminf(pen / n.z, r); /* ground: straight up, no sliding down slopes */
        if (p->velocity.z < 0) p->velocity.z = 0;
        p->grounded = true;
    } else {
        float vn;
        p->position = tc_v3_add(p->position, tc_v3_scale(n, pen));
        vn = tc_v3_dot(p->velocity, n);
        if (vn < 0) p->velocity = tc_v3_sub(p->velocity, tc_v3_scale(n, vn)); /* slide along walls */
    }
    x->c = tc_v3_add(p->position, tc_v3(0, 0, x->off));
    x->touched = true;
}

/* Push both spheres out of every overlapping triangle (a few passes for corners). */
static void resolve(tc_player *p) {
    resolve_ctx x;
    float offsets[2];
    int pass, k;
    x.p = p;
    x.cos_slope = cosf(p->max_slope);
    offsets[0] = p->radius;
    offsets[1] = fmaxf(p->radius, p->height - p->radius);
    for (pass = 0; pass < 4; pass++) {
        x.touched = false;
        for (k = 0; k < 2; k++) {
            x.off = offsets[k];
            x.c = tc_v3_add(p->position, tc_v3(0, 0, x.off));
            tc_collision_query(p->world, tc_v3_adds(x.c, -p->radius), tc_v3_adds(x.c, p->radius), resolve_tri, &x);
        }
        if (!x.touched) break;
    }
}

static void substep(tc_player *p, float h, const tc_player_input *in, bool jump) {
    tc_vec2 move = in->move, target, vxy, dv;
    float accel, dl, mlen = tc_v2_length(move), plen;
    bool was_grounded;
    tc_vec3 d;
    int pieces, i;

    if (mlen > 1) { move.x /= mlen; move.y /= mlen; mlen = tc_v2_length(move); }
    target = tc_v2(move.x * (in->run ? p->run_speed : p->walk_speed), move.y * (in->run ? p->run_speed : p->walk_speed));
    vxy = tc_v2(p->velocity.x, p->velocity.y);
    accel = p->grounded ? 42.0f : 14.0f;
    dv = tc_v2(target.x - vxy.x, target.y - vxy.y);
    dl = tc_v2_length(dv);
    if (dl > accel * h) { vxy.x += dv.x / dl * accel * h; vxy.y += dv.y / dl * accel * h; }
    else { vxy.x += dv.x; vxy.y += dv.y; }
    p->velocity.x = vxy.x;
    p->velocity.y = vxy.y;
    if (mlen > 0.05f) {
        float want = atan2f(move.x, -move.y);
        float dh = fmodf(want - p->heading, 2 * TC_PI);
        if (dh > TC_PI) dh -= 2 * TC_PI;
        else if (dh < -TC_PI) dh += 2 * TC_PI;
        p->heading += dh * fminf(1, 14 * h);
    }

    if (jump && (p->grounded || p->air_time < 0.12f)) { /* small grace period after walking off a ledge */
        p->velocity.z = p->jump_speed;
        p->grounded = false;
        p->since_jump = 0;
        p->air_time = 0.2f;
    }
    was_grounded = p->grounded;
    p->velocity.z = fmaxf(p->velocity.z - p->gravity * h, -50);

    d = tc_v3_scale(p->velocity, h);
    plen = ceilf(tc_v3_length(d) / (p->radius * 0.5f));
    pieces = plen > 1 ? (plen < 1e6f ? (int)plen : 1000000) : 1;
    p->grounded = false;
    for (i = 0; i < pieces; i++) {
        p->position = tc_v3_add(p->position, tc_v3_div(d, (float)pieces));
        resolve(p);
    }
    if (!p->grounded && was_grounded && p->velocity.z <= 0) {
        tc_vec3 o = tc_v3_add(p->position, tc_v3(0, 0, p->radius));
        tc_hit hit;
        if (tc_collision_raycast(p->world, o, tc_v3(0, 0, -1), p->radius + p->snap_distance, &hit) &&
            fabsf(hit.normal.z) >= cosf(p->max_slope)) {
            p->position.z = o.z - hit.distance;
            p->velocity.z = 0;
            p->grounded = true;
        }
    }
    if (p->grounded) p->air_time = 0;
    else p->air_time += h;
    p->since_jump += h;
}

static void update_animation(tc_player *p, float dt, bool running) {
    tc_anim_state previous = p->animation;
    float speed = tc_player_horizontal_speed(p);
    if (p->grounded) {
        if (previous == TC_ANIM_FALL || previous == TC_ANIM_JUMP_AIR) p->land_timer = p->clip_length[TC_ANIM_LAND];
        p->land_timer -= dt;
        if (speed > 0.3f) {
            p->animation = running && speed > p->walk_speed * 1.1f ? TC_ANIM_RUN : TC_ANIM_WALK;
            p->land_timer = 0;
        } else {
            p->animation = p->land_timer > 0 ? TC_ANIM_LAND : TC_ANIM_IDLE;
        }
    } else if (p->since_jump < p->clip_length[TC_ANIM_JUMP] && p->velocity.z > 0) {
        p->animation = TC_ANIM_JUMP;
    } else if (p->velocity.z > 0 && p->since_jump < 5) {
        p->animation = TC_ANIM_JUMP_AIR;
    } else if (p->air_time > 0.2f) {
        p->animation = TC_ANIM_FALL;
    }
    p->animation_time = p->animation == previous ? p->animation_time + dt : 0;
}

void tc_player_step(tc_player *p, float dt, const tc_player_input *input) {
    bool jump_pressed = input->jump && !p->jump_held;
    bool want_jump = jump_pressed;
    float remaining = fminf(dt, 0.25f);
    p->jump_held = input->jump;
    while (remaining > 1e-6f) {
        float h = fminf(remaining, 1.0f / 120);
        substep(p, h, input, want_jump);
        want_jump = false;
        remaining -= h;
    }
    if (p->position.z < p->world->bounds_min.z - 20) tc_player_respawn(p); /* fell out of the level */
    update_animation(p, dt, input->run);
}

tc_mat4 tc_player_model_matrix(const tc_player *p) {
    float c = cosf(p->heading), s = sinf(p->heading);
    tc_mat4 m = {{c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, p->position.x, p->position.y, p->position.z, 1}};
    return m;
}

float tc_heading_of(const tc_mat34 *m) { return atan2f(-m->m[1], m->m[5]); }
