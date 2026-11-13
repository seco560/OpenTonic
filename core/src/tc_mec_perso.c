/* Every perso's mechanics by its state's ID card type (tc_mec.h, tc_mec_perso): flight 5, swimming 1, skiing 2,
 * hanging 11, the kinematic 8, the surface move 7 (gravity factor 0), the still 6, the walkers 0 / 4 and a capsule for
 * states without a body. Moved out of Ed's driver (tc_ed_ai.c) on 2026-11-13. */
#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_mec.h"
#include "tonic/tc_player.h"

static tc_vec3 local_to_global(float heading, tc_vec3 v) {
    float c = cosf(heading), s = sinf(heading);
    return tc_v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

static uint32_t player_contacts(const tc_player *p) {
    return (p->grounded ? TC_AI_CONTACT_GROUND : 0) | (p->against_wall ? TC_AI_CONTACT_WALL : 0);
}

/* Card type 11 (Ed hanging by his hand point, zone 9: states 46-49, 197, 238-245; FUN_004adb4d, request FUN_004b2793,
 * forces FUN_004c6993): collide-and-move without a card (flags 1). The motion: mode 12's move direction x speed, else
 * the animation's root motion (the port's stand-in for mode 12, as for walking), along the ground's plane
 * (FUN_004c1541). While grounded (the hand point on the ledge) the grip cancels last frame's velocity and
 * FUN_004a7b71 presses the body into the ground by 0.0125 every frame (a force -0.0125 m / dt^2 along its normal): the
 * hand touches the ledge again each frame, which R10 needs (it lets go 200 ms after GroundMaterialIs(8) fails).
 * Not ported: FUN_004c6993's alignment (FUN_0043cf80 -> FUN_00463940 finds the ledge's edge near card +0x14 and turns
 * Ed square to it at card +0xC from it, at rate card +0x2C); gravity, which here loses its part along the contact
 * normal and is negligible in the air (x dt^2) before R10 drops Ed. */
static void hang(const tc_mec_perso *x, const tc_ai_sphere_set *body, float dt) {
    tc_player *p = x->body;
    tc_ai_perso *a = x->ai;
    const tc_ai_motion *m = &a->motion;
    bool grounded = (tc_mec_walk_contacts(&x->dyn->walk) & 3) != 0;
    tc_vec3 v, n = x->dyn->walk.ground_normal;
    if (m->turning) p->heading += m->turn_speed * dt;
    if (m->stop)
        v = tc_v3(0, 0, 0);
    else if (m->direction_set && m->mech_mode == 12)
        v = tc_v3_scale(local_to_global(p->heading, tc_v3(m->proposal[7], m->proposal[8], m->proposal[9])), m->proposal[10]);
    else
        v = tc_v3_scale(local_to_global(p->heading, x->root), (float)a->frame_rate);
    if (grounded) {
        v = tc_v3_sub(v, tc_v3_scale(n, tc_v3_dot(v, n)));
        if (dt > 0) v = tc_v3_add(v, tc_v3_scale(n, -0.0125f / dt)); /* FUN_004a7b71 */
    }
    tc_mec_collide_move(&x->dyn->walk, NULL, p->world, body, p->heading, dt, &p->position, &v);
    p->velocity = v;
    x->dyn->contacts = tc_mec_walk_contacts(&x->dyn->walk) & 0x1F;
    p->grounded = (x->dyn->contacts & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) != 0;
    p->against_wall = (x->dyn->contacts & TC_AI_CONTACT_WALL) != 0;
}

/* Card type 8 (FUN_004ac62b, request FUN_004af04e; Ed's climb onto a ledge, state 153, and the skier's crash): no forces
 * (FUN_004c4aa4 empties the list), so the velocity is exactly the request's: mode 12 the move direction x speed in
 * world axes, mode 13 ProposalVecA, any other mode none (the port takes the root motion as mode 12's, as for walking).
 * Nothing projects it on the ground, no gravity, no re-snap: the climb's root motion lifts Ed onto the ledge. Then
 * collide-and-move without a card (MEC_004bedd6, card 0). */
static void kinematic(const tc_mec_perso *x, const tc_ai_sphere_set *body, float dt) {
    tc_player *p = x->body;
    tc_ai_perso *a = x->ai;
    const tc_ai_motion *m = &a->motion;
    tc_vec3 v;
    if (m->turning) p->heading += m->turn_speed * dt;
    if (m->stop) v = tc_v3(0, 0, 0);
    else if (m->direction_set && m->mech_mode == 12)
        v = tc_v3_scale(local_to_global(p->heading, tc_v3(m->proposal[7], m->proposal[8], m->proposal[9])), m->proposal[10]);
    else if (m->speed_set && m->mech_mode == 13)
        v = tc_v3(m->proposal[1], m->proposal[2], m->proposal[3]);
    else
        v = tc_v3_scale(local_to_global(p->heading, x->root), (float)a->frame_rate);
    tc_mec_collide_move(&x->dyn->walk, NULL, p->world, body, p->heading, dt, &p->position, &v);
    p->velocity = v;
    x->dyn->contacts = tc_mec_walk_contacts(&x->dyn->walk) & 0x1F;
    p->grounded = (x->dyn->contacts & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) != 0;
    p->against_wall = (x->dyn->contacts & TC_AI_CONTACT_WALL) != 0;
}

/* Card types 0 and 4 (tc_mec_walk; card NULL: a card type not decoded yet): what the request handlers make of this
 * frame's requests (the AI's SetMechMode*, the animation's events, the root motion's mode 12), then the integrator.
 * PROC 35 / the Velocity field set the velocity itself (B89's air steering, every frame of a jump).
 * Ground (FUN_004ae392): mode 12 = the move direction x speed (capped by card +0x14), or the animation's root motion
 * (FUN_0042a8d0 pushes it as mode 12); the proposal's vectors are never used. Air (FUN_004b0f2a): modes 6 / 7 / 21 zero
 * the velocity and want ProposalVecA in Ed's axes (the jump: state 43's animation pushes mode 7 at frame 0), 8 wants it
 * without the reset, 9 wants ProposalVecD; mode 12 and the root motion mean nothing in the air. */
static void walk(const tc_mec_perso *x, const tc_ai_card *card, const tc_ai_sphere_set *body, float dt) {
    tc_player *p = x->body;
    tc_ai_perso *a = x->ai;
    const tc_ai_motion *m = &a->motion;
    const float *pr = m->proposal;
    bool air = card && card->type == 4;
    tc_vec3 wanted = tc_v3(0, 0, 0), jump = tc_v3(0, 0, 0);
    const tc_vec3 *impulse = NULL;
    uint32_t k;
    /* riding: first where the carrier took him (DNM_FollowCarrier_004a83d2), then his own move from there */
    tc_mec_walk_carry(&x->dyn->walk, p->world, body, &p->heading, dt, &p->position);
    if (air) tc_mec_walk_air(&x->dyn->walk);
    if (m->turning) p->heading += m->turn_speed * dt;
    if (m->velocity_set) p->velocity = m->velocity;
    if (air) {
        for (k = 0; k < m->mode_count; k++) {
            tc_vec3 va = local_to_global(p->heading, tc_v3(pr[1], pr[2], pr[3]));
            switch (m->modes[k]) {
            case 6: case 7: case 21: jump = va; impulse = &jump; wanted = tc_v3(0, 0, 0); break;
            case 8: wanted = tc_v3_add(wanted, va); break;
            case 9: wanted = tc_v3_add(wanted, local_to_global(p->heading, tc_v3(pr[4], pr[5], pr[6]))); break;
            default: break;
            }
        }
    } else if (m->stop) {
        p->velocity = tc_v3(0, 0, 0);
    } else if (m->direction_set && m->mech_mode == 12) {
        float speed = m->proposal[10], cap = card ? tc_ai_card_f32(card, card->type == 0 ? 0x14 : 0x10) : 0;
        if (cap > 0 && fabsf(speed) > cap) speed = speed < 0 ? -cap : cap;
        wanted = tc_v3_scale(local_to_global(p->heading, tc_v3(m->proposal[7], m->proposal[8], m->proposal[9])), speed);
    } else if (m->direction_set && m->mech_mode == 20) { /* the GoTo meta-actions (GoToWP: his cutscene walks) */
        float turn;
        wanted = tc_mec_walk_request_world(card, m->proposal, p->heading, dt, &turn);
        p->heading += turn * dt;
    } else {
        wanted = tc_v3_scale(local_to_global(p->heading, x->root), (float)a->frame_rate);
    }
    if (air && m->stop) p->velocity = tc_v3(0, 0, 0);
    tc_mec_walk_step(&x->dyn->walk, card, x->dyn->env, p->world, body, p->heading, wanted, air, impulse, dt,
                     &p->position, &p->velocity);
    x->dyn->contacts = tc_mec_walk_contacts(&x->dyn->walk) & 0x1F;
    p->grounded = (x->dyn->contacts & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) != 0;
    p->against_wall = (x->dyn->contacts & TC_AI_CONTACT_WALL) != 0;
    p->ground_normal = x->dyn->walk.ground_normal;
    p->wall_normal = x->dyn->walk.wall_normal;
    p->ground_grip = x->dyn->walk.ground_grip;
    if (p->position.z < p->world->bounds_min.z - 20) { /* fell out of the level */
        tc_player_respawn(p);
        tc_mec_walk_reset(&x->dyn->walk);
    }
}

/* The skier's move (FUN_004ab91d after its forces): the collide set's single sphere around his origin, swept with
 * no card (MEC_004bedd6 gets none: default contact classes, no step-up); when nothing was touched and last frame
 * had ground, the re-snap within 0.7 onto ground within 45.6 deg of up (cos 0.7). */
static void ski_move(const tc_mec_perso *x, const tc_ai_sphere_set *body, tc_vec3 v, float dt) {
    tc_player *p = x->body;
    uint32_t had, bits;
    had = x->dyn->walk.report;
    bits = tc_mec_collide_move(&x->dyn->walk, NULL, p->world, body, 0, dt, &p->position, &v);
    if (!bits && (had & TC_AI_CONTACT_GROUND)) tc_mec_resnap(&x->dyn->walk, p->world, body, 0, 0.7f, 0.7f, &p->position);
    p->velocity = v;
    x->dyn->contacts = tc_mec_walk_contacts(&x->dyn->walk) & 0x1F;
    p->grounded = (x->dyn->walk.report & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) != 0;
    p->against_wall = (x->dyn->walk.report & TC_AI_CONTACT_WALL) != 0;
    p->ground_normal = x->dyn->walk.ground_normal;
    p->wall_normal = x->dyn->walk.wall_normal;
    p->ground_grip = x->dyn->walk.ground_grip;
    if (p->position.z < p->world->bounds_min.z - 20) {
        tc_player_respawn(p);
        tc_mec_walk_reset(&x->dyn->walk);
    }
}

/* States without spheres: tc_player's capsule with sliding contacts, the origin at the board's underside. */
static void ski_move_capsule(const tc_mec_perso *x, tc_vec3 v, float dt) {
    tc_player *p = x->body;
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
    x->dyn->contacts = player_contacts(p);
}

tc_vec3 tc_mec_root_motion(const tc_ai_perso *a) {
    const tc_ai_state *st = &a->model->states[a->state];
    uint32_t f;
    if (!st->root_count) return tc_v3(0, 0, 0);
    f = a->state_frame > 0 ? (uint32_t)a->state_frame % st->root_count : 0;
    return a->model->root[st->root_first + f];
}

/* A perso's mechanics for this step (tc_mec.h): its state's ID card's type picks the integrator. Moved from Ed's driver
 * (tc_ed_ai.c) on 2026-11-13, as it was: every perso's now. */
void tc_mec_perso_step(const tc_mec_perso *x, float dt) {
    tc_player *p = x->body;
    tc_ai_perso *a = x->ai;
    tc_mec_dyn *d = x->dyn;
    const tc_ai_motion *m = &a->motion;
    const tc_ai_card *card = x->card;
    uint32_t type = card ? card->type : TC_AI_NONE16;
    tc_vec2 target;

    if (type != d->card_type && d->card_type == 5)
        p->velocity = tc_v3(0, 0, 0); /* leaving flight (GAM_0042ab20): stop, keep the heading */
    /* Port: riding is the walk / air integrator's (types 0 / 4; the exe's 11 and 12 ride too, not ported): any other card
     * lets go of the carrier, so coming back does not snap him to where the carrier has gone meanwhile */
    if (type != 0 && type != 4) d->walk.carrier = -1;
    if (type == 7) {
        /* Card type 7 (DNM_IntegrateType7_004acb49 / DNM_RequestType7_004b17d1): with the card's gravity factor 0 (all of
         * Ed's: surface swimming 5 / 18 / 19, the portal walk-in 228, the skier's finish 321) the velocity is kept and the
         * request's motion added: the perso moves at the wanted speed, at constant height. The handler is the walk
         * one's (turns; move direction x speed within card +0xC..+0x14); without a move request the root motion.
         * Not ported: the velocity carried in from the last state (kept, undamped, in the exe: dropped here), the spring
         * towards a plane (dynamics +0x74 / +0x8C / +0x90) when not moving, gravity factors other than 0. */
        tc_vec3 v;
        if (m->turning) p->heading += m->turn_speed * dt;
        if (m->direction_set && (m->mech_mode == 12 || m->mech_mode == 20)) {
            float speed = m->proposal[10], cap = tc_ai_card_f32(card, 0x14);
            tc_vec3 d = tc_v3(m->proposal[7], m->proposal[8], m->proposal[9]);
            if (cap > 0 && fabsf(speed) > cap) speed = speed < 0 ? -cap : cap;
            v = tc_v3_scale(m->mech_mode == 12 ? local_to_global(p->heading, d) : d, speed);
        } else {
            v = tc_v3_scale(local_to_global(p->heading, x->root), (float)a->frame_rate);
        }
        if (m->velocity_set) v = tc_v3_add(v, tc_v3(m->velocity.x, m->velocity.y, 0)); /* pending: z := 0 */
        tc_player_move(p, dt, v);
        p->velocity = v;
        d->card_type = type;
        d->contacts = player_contacts(p);
        return;
    }
    if (type == 1) {
        /* Swimming (card type 1): the swim forces look the water up themselves, at their probe below the origin */
        tc_vec3 v = p->velocity;
        uint8_t k;
        if (d->card_type != 1) tc_mec_swim_enter(&d->swim, p->heading);
        if (m->mode_count == 0) tc_mec_swim_request(&d->swim, card, 0, m->proposal, dt);
        for (k = 0; k < m->mode_count; k++) tc_mec_swim_request(&d->swim, card, m->modes[k], m->proposal, dt);
        v = tc_mec_swim_velocity(&d->swim, card, x->sectors, x->sector, p->position, v, dt, &d->env_depth);
        tc_player_move(p, dt, v);
        p->heading = d->swim.heading_lagged + TC_PI / 2; /* the swim heading's front, in tc_player's terms */
        d->card_type = type;
        d->contacts = player_contacts(p);
        return;
    }
    if (type == 5) {
        tc_vec3 v = p->velocity;
        uint8_t k;
        if (d->card_type != 5)
            tc_mec_fly_enter(&d->fly, card, local_to_global(p->heading, tc_v3(0, -1, 0)), &v);
        if (m->mode_count == 0) tc_mec_fly_request(&d->fly, card, 0, m->proposal, dt, &v);
        for (k = 0; k < m->mode_count; k++) tc_mec_fly_request(&d->fly, card, m->modes[k], m->proposal, dt, &v);
        v = tc_mec_fly_velocity(&d->fly, card, d->env, v, dt);
        tc_player_move(p, dt, v);
        if (v.x * v.x + v.y * v.y > 1e-4f) p->heading = atan2f(v.x, -v.y); /* face along the flight path */
        d->card_type = type;
        d->contacts = player_contacts(p);
        return;
    }
    if (type == 2) {
        tc_mec_contact c;
        tc_vec3 v = p->velocity;
        const tc_ai_sphere_set *body = x->spheres;
        uint8_t k;
        if (d->card_type != 2) tc_mec_ski_enter(&d->ski, p->heading);
        if (body) { /* the handlers read last frame's report and contact records */
            c.ground = (d->walk.report & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) != 0;
            c.wall = (d->walk.report & (TC_AI_CONTACT_WALL | TC_AI_CONTACT_OVERHANG | TC_AI_CONTACT_CEILING)) != 0;
            c.ground_normal = d->walk.ground_normal;
            c.wall_normal = d->walk.wall_normal;
            c.ground_grip = d->walk.ground_grip;
            c.wall_grip = d->walk.wall_grip;
        } else {
            c.ground = p->grounded;
            c.wall = p->against_wall;
            c.ground_normal = p->ground_normal;
            c.wall_normal = p->wall_normal;
            c.ground_grip = p->ground_grip;
            c.wall_grip = 1;
        }
        c.jumped = false;
        c.ground_factor = 1; /* floor mat[0]: 1 on the ski slopes (not exported yet) */
        /* A speed vector written this frame (ProposalVecA / B: the jump button's (0, -70000, 10000), a spring star's
         * launch) has the mode 10 layout ([2] along the board, [3] up) and is taken once, as one exe frame of a mode 10
         * request, before the queued ones (whose steering it would otherwise replace). The scripts then hold it for
         * the jump (ChangeStateAndWait / Wait), so it is read on that frame only. How the exe routes it: not decoded. */
        if (m->speed_set && (m->proposal[1] != 0 || m->proposal[2] != 0 || m->proposal[3] != 0) &&
            !memchr(m->modes, 10, m->mode_count)) {
            const float *q = m->proposal;
            tc_vec3 f = tc_v3_add(tc_v3_scale(d->ski.y, q[2]), tc_v3_scale(tc_v3(q[7], q[8], q[9]), q[1]));
            if (q[3] != 0) {
                c.ground = c.wall = false;
                c.jumped = true;
                f = tc_v3_add(f, tc_v3_scale(d->ski.z, q[3]));
            }
            d->ski.force = tc_v3_add(d->ski.force, tc_v3_scale(f, dt > 0 ? TC_MEC_FRAME / dt : 1));
        }
        if (m->mode_count == 0) tc_mec_ski_request(&d->ski, card, 0, m->proposal, &c, v);
        for (k = 0; k < m->mode_count; k++) tc_mec_ski_request(&d->ski, card, m->modes[k], m->proposal, &c, v);
        /* FUN_004b00bf's jump clears dynamics +0xC4 and +0xC0: the forces see no contact, and the integrator's re-snap
         * (last frame's report & 1) cannot pull the take-off back onto the snow */
        if (c.jumped) d->walk.report = d->walk.report_prev = 0;
        v = tc_mec_ski_velocity(&d->ski, card, d->env, &c, p->world, p->position, v, dt);
        if (body) ski_move(x, body, v, dt);
        else ski_move_capsule(x, v, dt);
        p->heading = tc_mec_ski_heading(&d->ski);
        d->card_type = type;
        return;
    }
    if (type == 6) {
        /* Card type 6 (DNM_IntegrateType6_004aca45 only shifts the snapshots; DNM_RequestType6_004b11ea moves on the
         * MoveLift modes 14 / 15 alone, which Ed's scripts never ask for): no gravity, no collision, he stays where he
         * is. The skier sitting at the start gate (states 118 / 119) and Ed's states 142 / 143. The collision report
         * is not touched either: the contacts stay the last state's. Before 2026-11-13 these ran as a walker without
         * a card, and the skier crept down the slope while he waited. */
        p->velocity = tc_v3(0, 0, 0); /* port: the exe keeps the last state's, unused while here */
        d->card_type = type;
        return;
    }
    if (type == 11 && x->spheres) {
        hang(x, x->spheres, dt);
        d->card_type = type;
        return;
    }
    if (type == 8 && x->spheres) {
        kinematic(x, x->spheres, dt);
        d->card_type = type;
        return;
    }
    if (x->spheres) {
        /* Types 0 / 4; other card types are not decoded and run the same way without their card, so the body and its
         * origin stay the same. */
        walk(x, type == 0 || type == 4 ? card : NULL, x->spheres, dt);
        d->card_type = type;
        return;
    }
    d->card_type = type;

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
        float speed = x->ground * a->frame_rate;
        target = tc_v2(sinf(p->heading) * speed, -cosf(p->heading) * speed); /* Ed's front: local -Y */
    } else {
        target = tc_v2(p->velocity.x, p->velocity.y); /* airborne: keep the momentum */
    }
    tc_player_drive(p, dt, target);
    d->contacts = player_contacts(p);
}
