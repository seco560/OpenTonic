#include "tc_internal.h"
#include <ctype.h>
#include <string.h>
#include "tonic/tc_ed_ai.h"
#include "tonic/tc_alloc.h"

static bool same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

tc_result tc_ed_ai_init(tc_ed_ai *e, const tc_ai_model *model, tc_player *player, const char *level_name) {
    tc_result r = tc_ai_init(&e->ai, model);
    uint32_t i;
    e->player = player;
    e->camera = -1;
    e->run_scale = 2.0f;  /* run (state 10) 4.5 -> 9.0 u/s: a little under the old controller's 9.9 (user) */
    e->walk_scale = 1;    /* walk (state 1) 3.5 u/s, already close to the old 3.3 */
    e->card_type = TC_AI_NONE16;
    memset(&e->fly, 0, sizeof(e->fly));
    e->env = tc_mec_env_default();
    tc_mec_walk_reset(&e->walk);
    e->contacts = 0;
    e->auto_start = false;
    e->time = 0;
    e->trigger_wake = NULL;
    e->triggers_fired = 0;
    e->portal = -1;
    e->portal_firing = false;
    e->portal_op = 0;
    e->portal_wait = 0;
    e->portal_armed = NULL;
    e->screen_covered = false;
    memset(&e->events, 0, sizeof(e->events));
    if (r != TC_OK) return r;
    if (model->trigger_count) {
        e->trigger_wake = (float *)tc_calloc(model->trigger_count, sizeof(float));
        if (!e->trigger_wake) {
            tc_ai_free(&e->ai);
            return TC_ERR_OUT_OF_MEMORY;
        }
    }
    if (model->portal_count) {
        e->portal_armed = (uint8_t *)tc_calloc(model->portal_count, 1);
        if (!e->portal_armed) {
            tc_free(e->trigger_wake);
            tc_ai_free(&e->ai);
            return TC_ERR_OUT_OF_MEMORY;
        }
        for (i = 0; i < model->portal_count; i++) /* a portal the player starts in captures only once he has left it */
            e->portal_armed[i] = tc_v3_distance(player->position, model->portals[i].position) >= model->portals[i].radius;
    }
    e->ai.level_name = level_name;
    e->auto_start = level_name && (same_name(level_name, "totalski") || same_name(level_name, "skymap"));
    for (i = 0; i < model->other_count; i++)
        if (model->others[i].role == 1) e->camera = (int)i;
    return TC_OK;
}

void tc_ed_ai_free(tc_ed_ai *e) {
    tc_free(e->trigger_wake);
    e->trigger_wake = NULL;
    tc_free(e->portal_armed);
    e->portal_armed = NULL;
    tc_ai_free(&e->ai);
}

/* The model's proximity triggers (other persos' scripts reduced to their effect on Ed, see tc_ai_trigger).
 * trigger_wake: time the trigger may fire again; < 0 = fired with sleep 0, waiting for Ed to leave the radius. */
static void run_triggers(tc_ed_ai *e) {
    const tc_ai_model *m = e->ai.model;
    uint16_t i;
    for (i = 0; e->trigger_wake && i < m->trigger_count; i++) {
        const tc_ai_trigger *t = &m->triggers[i];
        bool inside = tc_v3_length_sq(tc_v3_sub(e->player->position, t->position)) < t->radius * t->radius;
        if (e->trigger_wake[i] < 0) {
            if (!inside) e->trigger_wake[i] = 0;
            continue;
        }
        if (!inside || e->time < e->trigger_wake[i]) continue;
        if (t->guard == TC_AI_NONE16 || tc_ai_dsgvar_float(&e->ai, t->guard) == 0) {
            tc_ai_force_behaviour(&e->ai, t->behaviour);
            e->triggers_fired++;
        }
        e->trigger_wake[i] = t->sleep_ms ? e->time + (float)t->sleep_ms / 1000 : -1;
    }
}

/* One portal op on the player (capture or fire). Returns false for a wait that has not elapsed. */
static bool portal_op(tc_ed_ai *e, const tc_ai_portal *g, const tc_ai_portal_op *o) {
    const tc_ai_model *m = e->ai.model;
    switch (o->op) {
    case TC_AI_POP_SET:
        if (o->kind == 1) tc_ai_dsgvar_set_float(&e->ai, o->a, o->v.x);
        else if (o->kind == 3) tc_ai_dsgvar_set_vec(&e->ai, o->a, o->v);
        else tc_ai_dsgvar_set_int(&e->ai, o->a, o->kind == 2 ? (int32_t)g->perso : (int32_t)o->b);
        break;
    case TC_AI_POP_FORCE: tc_ai_force_behaviour(&e->ai, o->a); break;
    case TC_AI_POP_FORCE_REFLEX: tc_ai_force_reflex(&e->ai, o->a); break;
    case TC_AI_POP_COVER: e->events.cover = true; e->screen_covered = false; break;
    case TC_AI_POP_WAIT:
        if (e->portal_wait <= 0) e->portal_wait = e->time + (float)o->b / 1000;
        if (e->time < e->portal_wait) return false;
        e->portal_wait = 0;
        break;
    case TC_AI_POP_TELEPORT: {
        float heading = e->player->heading;
        if (o->kind == 1) memcpy(&heading, &o->c, sizeof(heading));
        tc_player_teleport(e->player, o->v, heading);
        tc_mec_walk_reset(&e->walk);
        e->contacts = 0;
        e->events.teleported = true;
        break;
    }
    case TC_AI_POP_CAMERA: e->events.camera_set = true; e->events.camera = o->v; break;
    case TC_AI_POP_LEVEL:
        e->events.level = m->names[o->b];
        e->events.place = o->c != TC_AI_NONE ? m->names[o->c] : NULL;
        e->events.camera_place = o->c != TC_AI_NONE ? m->names[o->a] : NULL;
        break;
    default: break;
    }
    return true;
}

/* The portals (tc_ai_portal): capture, then the fire ops in order once the fire condition holds. */
static void run_portals(tc_ed_ai *e) {
    const tc_ai_model *m = e->ai.model;
    uint16_t i, k;
    for (i = 0; e->portal_armed && i < m->portal_count; i++)
        if (!e->portal_armed[i] && tc_v3_distance(e->player->position, m->portals[i].position) >= m->portals[i].radius)
            e->portal_armed[i] = 1;
    if (e->portal < 0) {
        for (i = 0; e->portal_armed && i < m->portal_count; i++) {
            const tc_ai_portal *g = &m->portals[i];
            bool ok = e->portal_armed[i] && tc_v3_distance(e->player->position, g->position) < g->radius;
            for (k = 0; ok && k < g->capture_count; k++)
                if (g->ops[k].op == TC_AI_POP_GUARD && (int32_t)tc_ai_dsgvar_float(&e->ai, g->ops[k].a) != (int32_t)g->ops[k].b)
                    ok = false;
            if (!ok) continue;
            for (k = 0; k < g->capture_count; k++) (void)portal_op(e, g, &g->ops[k]);
            e->portal_armed[i] = 0;
            if (g->fire == 2) break; /* capture only: the player's own scripts take it from there */
            e->portal = i;
            e->portal_firing = false;
            e->portal_op = 0;
            e->portal_wait = 0;
            e->portal_armed[i] = 0;
            break;
        }
        return;
    }
    {
        const tc_ai_portal *g = &m->portals[e->portal];
        if (!e->portal_firing) {
            if (g->fire == 1 ? !e->screen_covered
                             : (int32_t)tc_ai_dsgvar_float(&e->ai, g->fire_dsg) != g->fire_value)
                return;
            e->portal_firing = true;
        }
        while (e->portal_op < g->fire_count) {
            if (!portal_op(e, g, &g->ops[g->capture_count + e->portal_op])) return;
            e->portal_op++;
        }
        e->portal = -1;
    }
}

void tc_ed_ai_run_portals(tc_ed_ai *e, float dt) {
    e->time += dt;
    run_portals(e);
}

static tc_vec3 local_to_global(float heading, tc_vec3 v) {
    float c = cosf(heading), s = sinf(heading);
    return tc_v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

/* Units per frame Ed moves forward in state s, tuned. A start-up state (walk 9 -> 1, run 158 -> 10) has no cycle of
 * its own: it borrows the stride of the loop it leads into, so Ed does not stop while it plays. */
static float ground_per_frame(const tc_ed_ai *e, uint32_t s) {
    const tc_ai_model *m = e->ai.model;
    const tc_ai_state *st = &m->states[s];
    float g;
    if (st->ground <= 0 && st->next >= 0 && (uint32_t)st->next != s) {
        s = (uint32_t)st->next;
        st = &m->states[s];
    }
    g = st->ground;
    return g * (g * (st->speed ? st->speed : 30) > 4 ? e->run_scale : e->walk_scale);
}

static uint32_t player_contacts(const tc_player *p) {
    return (p->grounded ? TC_AI_CONTACT_GROUND : 0) | (p->against_wall ? TC_AI_CONTACT_WALL : 0);
}

/* The playing state's root motion (units per animation frame, Ed's axes) at the current frame, 0 without one. */
static tc_vec3 root_motion(const tc_ai_perso *a) {
    const tc_ai_state *st = &a->model->states[a->state];
    uint32_t f;
    if (!st->root_count) return tc_v3(0, 0, 0);
    f = a->state_frame > 0 ? (uint32_t)a->state_frame % st->root_count : 0;
    return a->model->root[st->root_first + f];
}

/* Card types 0 and 4 (tc_mec_walk; card NULL: a card type not decoded yet): what the request handlers make of the
 * AI's requests, then the integrator.
 * Ground speed: SetMoveDirection under mode 12 (proposal [7..9] x [10], capped by card +0x14), else the
 * animation's root motion x its frame rate. ProposalVecA: the jump's velocity (mode 7 replaces the old one). */
static void walk(tc_ed_ai *e, const tc_ai_card *card, const tc_ai_sphere_set *body, float dt) {
    tc_player *p = e->player;
    tc_ai_perso *a = &e->ai;
    const tc_ai_motion *m = &a->motion;
    tc_vec3 wanted = tc_v3(0, 0, 0), jump;
    const tc_vec3 *impulse = NULL;
    if (card && card->type == 4) tc_mec_walk_air(&e->walk);
    if (m->turning) p->heading += m->turn_speed * dt;
    if (m->velocity_set) p->velocity = m->velocity;
    if (m->speed_set) {
        jump = local_to_global(p->heading, tc_v3(m->proposal[1], m->proposal[2], m->proposal[3]));
        impulse = &jump;
        /* Taking off: an impulse away from the ground leaves it, as the type 4 handler's mode 7 does by clearing the
         * contact reports. Under a ground card (the pogo's launch, state 59) the stale report would still count as
         * ground next frame: full grip drops the velocity, the move is skipped and Ed hovers until the state ends. */
        if (tc_v3_dot(jump, e->walk.ground_normal) > 0) tc_mec_walk_air(&e->walk);
    }
    if (m->stop) {
        p->velocity = tc_v3(0, 0, 0);
    } else if (m->direction_set && m->mech_mode == 12) {
        float speed = m->proposal[10], cap = card ? tc_ai_card_f32(card, card->type == 0 ? 0x14 : 0x10) : 0;
        if (cap > 0 && fabsf(speed) > cap) speed = speed < 0 ? -cap : cap;
        wanted = tc_v3_scale(local_to_global(p->heading, tc_v3(m->proposal[7], m->proposal[8], m->proposal[9])), speed);
    } else {
        wanted = tc_v3_scale(local_to_global(p->heading, root_motion(a)), (float)a->frame_rate);
    }
    tc_mec_walk_step(&e->walk, card, e->env, p->world, body, p->heading, wanted, card && card->type == 4, impulse, dt,
                     &p->position, &p->velocity);
    e->contacts = tc_mec_walk_contacts(&e->walk) & 0x1F;
    p->grounded = (e->contacts & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) != 0;
    p->against_wall = (e->contacts & TC_AI_CONTACT_WALL) != 0;
    p->ground_normal = e->walk.ground_normal;
    p->wall_normal = e->walk.wall_normal;
    p->ground_grip = e->walk.ground_grip;
    if (p->position.z < p->world->bounds_min.z - 20) { /* fell out of the level */
        tc_player_respawn(p);
        tc_mec_walk_reset(&e->walk);
    }
}

/* The skier's move (FUN_004ab91d after its forces): the collide set's single sphere around his origin, swept with
 * no card (MEC_004bedd6 gets none: default contact classes, no step-up); when nothing was touched and last frame
 * had ground, the re-snap within 0.7 onto ground within 45.6 deg of up (cos 0.7). */
static void ski_move(tc_ed_ai *e, const tc_ai_sphere_set *body, tc_vec3 v, float dt) {
    tc_player *p = e->player;
    uint32_t had = e->walk.report, bits = tc_mec_collide_move(&e->walk, NULL, p->world, body, 0, dt, &p->position, &v);
    if (!bits && (had & TC_AI_CONTACT_GROUND)) tc_mec_resnap(&e->walk, p->world, body, 0, 0.7f, 0.7f, &p->position);
    p->velocity = v;
    e->contacts = tc_mec_walk_contacts(&e->walk) & 0x1F;
    p->grounded = (e->walk.report & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) != 0;
    p->against_wall = (e->walk.report & TC_AI_CONTACT_WALL) != 0;
    p->ground_normal = e->walk.ground_normal;
    p->wall_normal = e->walk.wall_normal;
    p->ground_grip = e->walk.ground_grip;
    if (p->position.z < p->world->bounds_min.z - 20) {
        tc_player_respawn(p);
        tc_mec_walk_reset(&e->walk);
    }
}

/* Before .ttai v4: tc_player's capsule with sliding contacts, the origin at the board's underside. */
static void ski_move_capsule(tc_ed_ai *e, tc_vec3 v, float dt) {
    tc_player *p = e->player;
    float max_slope = p->max_slope;
    bool was_grounded = p->grounded;
    p->max_slope = 70 * TC_PI / 180; /* contacts up to 70 deg are ground or steep slope (report bits 1|2) */
    p->slide_on_ground = true;
    tc_player_move(p, dt, v);
    if (!p->grounded && was_grounded && p->velocity.z <= 0) { /* MEC_004bf9f0: re-snap within 0.7 */
        tc_hit hit;
        tc_vec3 o = tc_v3_add(p->position, tc_v3(0, 0, p->radius));
        if (tc_collision_raycast(p->world, o, tc_v3(0, 0, -1), p->radius + 0.7f, &hit) &&
            hit.normal.z >= cosf(p->max_slope)) {
            float vn = tc_v3_dot(p->velocity, hit.normal);
            p->position.z = o.z - hit.distance;
            if (vn < 0) p->velocity = tc_v3_sub(p->velocity, tc_v3_scale(hit.normal, vn));
            p->grounded = true;
            p->ground_normal = hit.normal;
            p->ground_grip = hit.tri->floor.grip;
        }
    }
    p->slide_on_ground = false;
    p->max_slope = max_slope;
    e->contacts = player_contacts(p);
}

void tc_ed_ai_step(tc_ed_ai *e, float dt, uint32_t inputs, float stick_x, float stick_y, tc_vec3 camera) {
    tc_player *p = e->player;
    tc_ai_perso *a = &e->ai;
    const tc_ai_motion *m = &a->motion;
    tc_vec2 target;
    const tc_ai_state *st;
    const tc_ai_card *card;
    uint32_t type;

    e->time += dt;
    run_triggers(e);
    run_portals(e);
    a->screen_covered = e->screen_covered;
    if (e->auto_start && a->normal.behaviour == a->model->initial_normal) inputs |= 1u << TC_AI_IN_JUMP;
    else e->auto_start = false;
    a->inputs = inputs;
    a->stick_x = stick_x;
    a->stick_y = stick_y;
    a->position = p->position;
    a->velocity = p->velocity;
    a->heading = p->heading;
    a->contacts = e->contacts;
    if (e->camera >= 0) a->other_positions[e->camera] = camera;
    a->report_2bc = e->fly.attack_lagged;
    a->report_2c0 = e->fly.bank_lagged;
    tc_ai_step(a, dt);
    if (a->cover_requested) { /* GameAction2 */
        a->cover_requested = false;
        e->events.cover = true;
        e->screen_covered = false;
    }
    if (m->heading_set) p->heading = m->heading;
    /* SetPositionFrom: the skier's respawn after a hole (B10), Ed's saved-position arrival. Small moves are pose
     * adjustments made for the engine's own origin and body (B62: 0.85 down into the floor); ours differ, so only moves
     * of more than 2 units are applied. */
    if (m->teleport_set && tc_v3_distance(m->teleport, p->position) > 2) {
        tc_player_teleport(p, m->teleport, p->heading);
        tc_mec_walk_reset(&e->walk);
        e->contacts = 0;
        e->events.teleported = true;
    }

    st = &a->model->states[a->state];
    card = st->card != TC_AI_NONE16 ? &a->model->cards[st->card] : NULL;
    type = card ? card->type : TC_AI_NONE16;
    if (type != e->card_type && e->card_type == 5)
        p->velocity = tc_v3(0, 0, 0); /* leaving flight (GAM_0042ab20): stop, keep the heading */
    if (type == 5) {
        tc_vec3 v = p->velocity;
        uint8_t k;
        if (e->card_type != 5)
            tc_mec_fly_enter(&e->fly, card, local_to_global(p->heading, tc_v3(0, -1, 0)), &v);
        if (m->mode_count == 0) tc_mec_fly_request(&e->fly, card, 0, m->proposal, dt, &v);
        for (k = 0; k < m->mode_count; k++) tc_mec_fly_request(&e->fly, card, m->modes[k], m->proposal, dt, &v);
        v = tc_mec_fly_velocity(&e->fly, card, e->env, v, dt);
        tc_player_move(p, dt, v);
        if (v.x * v.x + v.y * v.y > 1e-4f) p->heading = atan2f(v.x, -v.y); /* face along the flight path */
        e->card_type = type;
        e->contacts = player_contacts(p);
        return;
    }
    if (type == 2) {
        tc_mec_contact c;
        tc_vec3 v = p->velocity;
        const tc_ai_sphere_set *body = st->spheres != TC_AI_NONE16 ? &a->model->sphere_sets[st->spheres] : NULL;
        uint8_t k;
        if (e->card_type != 2) tc_mec_ski_enter(&e->ski, p->heading);
        if (body) { /* the handlers read last frame's report and contact records */
            c.ground = (e->walk.report & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) != 0;
            c.wall = (e->walk.report & (TC_AI_CONTACT_WALL | TC_AI_CONTACT_OVERHANG | TC_AI_CONTACT_CEILING)) != 0;
            c.ground_normal = e->walk.ground_normal;
            c.wall_normal = e->walk.wall_normal;
            c.ground_grip = e->walk.ground_grip;
        } else {
            c.ground = p->grounded;
            c.wall = p->against_wall;
            c.ground_normal = p->ground_normal;
            c.wall_normal = p->wall_normal;
            c.ground_grip = p->ground_grip;
        }
        c.ground_factor = 1; /* floor mat[0]: 1 on the ski slopes (not exported yet) */
        /* A speed vector written this frame (ProposalVecA / B: the jump button's (0, -70000, 10000), a spring star's
         * launch) has the mode 10 layout ([2] along the board, [3] up) and is taken once, as one exe frame of a mode 10
         * request, before the queued ones (whose steering it would otherwise replace). The scripts then hold it for
         * the jump (ChangeStateAndWait / Wait), so it is read on that frame only. How the exe routes it: not decoded. */
        if (m->speed_set && (m->proposal[1] != 0 || m->proposal[2] != 0 || m->proposal[3] != 0) &&
            !memchr(m->modes, 10, m->mode_count)) {
            const float *q = m->proposal;
            tc_vec3 f = tc_v3_add(tc_v3_scale(e->ski.y, q[2]), tc_v3_scale(tc_v3(q[7], q[8], q[9]), q[1]));
            if (q[3] != 0) {
                c.ground = c.wall = false;
                f = tc_v3_add(f, tc_v3_scale(e->ski.z, q[3]));
            }
            e->ski.force = tc_v3_add(e->ski.force, tc_v3_scale(f, dt > 0 ? TC_MEC_FRAME / dt : 1));
        }
        if (m->mode_count == 0) tc_mec_ski_request(&e->ski, card, 0, m->proposal, &c, v);
        for (k = 0; k < m->mode_count; k++) tc_mec_ski_request(&e->ski, card, m->modes[k], m->proposal, &c, v);
        v = tc_mec_ski_velocity(&e->ski, card, e->env, &c, p->world, p->position, v, dt);
        if (body) ski_move(e, body, v, dt);
        else ski_move_capsule(e, v, dt);
        p->heading = tc_mec_ski_heading(&e->ski);
        e->card_type = type;
        return;
    }
    if (st->spheres != TC_AI_NONE16) {
        /* Types 0 / 4; other card types (6, 7, 8: the skier's parked, crash and finish states) are not decoded and
         * run the same way without their card, so the body and its origin stay the same. */
        walk(e, type == 0 || type == 4 ? card : NULL, &a->model->sphere_sets[st->spheres], dt);
        e->card_type = type;
        return;
    }
    e->card_type = type;

    if (m->turning) p->heading += m->turn_speed * dt;
    if (m->velocity_set) p->velocity = m->velocity;
    if (m->speed_set) { /* impulse: the jump (B85) */
        p->velocity = local_to_global(p->heading, tc_v3(m->proposal[1], m->proposal[2], m->proposal[3]));
        if (p->velocity.z > 0) {
            p->grounded = false;
            p->since_jump = 0;
        }
    }
    if (m->stop) {
        target = tc_v2(0, 0);
        p->velocity.x = p->velocity.y = 0;
    } else if (m->direction_set && m->mech_mode == 12) {
        tc_vec3 v = local_to_global(p->heading, tc_v3(m->proposal[7], m->proposal[8], 0));
        target = tc_v2(v.x * m->proposal[10], v.y * m->proposal[10]);
    } else if (p->grounded && !m->speed_set) {
        float speed = ground_per_frame(e, a->state) * a->frame_rate;
        target = tc_v2(sinf(p->heading) * speed, -cosf(p->heading) * speed); /* Ed's front: local -Y */
    } else {
        target = tc_v2(p->velocity.x, p->velocity.y); /* airborne: keep the momentum */
    }
    tc_player_drive(p, dt, target);
    e->contacts = player_contacts(p);
}

uint16_t tc_ed_ai_anim(const tc_ed_ai *e) { return e->ai.model->states[e->ai.state].anim; }
float tc_ed_ai_frame(const tc_ed_ai *e) { return e->ai.state_frame; }
bool tc_ed_ai_loops(const tc_ed_ai *e) {
    const tc_ai_state *s = &e->ai.model->states[e->ai.state];
    return s->next >= 0 && (uint32_t)s->next == e->ai.state;
}

tc_mat4 tc_ed_ai_model_matrix(const tc_ed_ai *e) {
    const tc_mec_ski *s = &e->ski;
    tc_vec3 o;
    if (e->card_type != 2) return tc_player_model_matrix(e->player);
    /* With the collide set's sphere (v4) the origin is where the exe has it, the board resting on the sphere: no lift.
     * Before v4 the origin sat on the snow and the pose is lifted by its drop. */
    o = e->ai.model->states[e->ai.state].spheres != TC_AI_NONE16
            ? e->player->position
            : tc_v3_add(e->player->position, tc_v3_scale(s->z, e->ai.model->states[e->ai.state].drop));
    {
        tc_mat4 m = {{s->x.x, s->x.y, s->x.z, 0, s->y.x, s->y.y, s->y.z, 0, s->z.x, s->z.y, s->z.z, 0, o.x, o.y, o.z, 1}};
        return m;
    }
}
