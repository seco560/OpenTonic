/* The cast's mechanics: card types 6 and 7 (docs/ENGINE.md "Moving collision, read"). Read from the exe; the port's
 * choices are marked "port". */
#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_cast.h"
#include "tonic/tc_mec.h"
#include "tonic/tc_obstacle.h"

static tc_vec3 local_to_global(float heading, tc_vec3 v) {
    float c = cosf(heading), s = sinf(heading);
    return tc_v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* The exe's speed / turn clamp (DNM_RequestType7_004b17d1): |x| above max -> +-max, below min -> +-min (the sign kept). */
static float clamp_magnitude(float x, float min, float max) {
    float m = fabsf(x);
    if (m > max) return x < 0 ? -max : max;
    if (m < min) return x < 0 ? -min : min;
    return x;
}

/* ---- Type 6: DNM_RequestType6_004b11ea (the integrator DNM_IntegrateType6_004aca45 only shifts the snapshots) ---- */
static void lift_request(tc_cast_dyn *d, tc_ai_perso *p, int mode, const float *pr, float dt) {
    tc_vec3 a = tc_v3(pr[1], pr[2], pr[3]), b = tc_v3(pr[4], pr[5], pr[6]), dir = tc_v3_sub(b, a), pos = p->position;
    float half = 0.5f * pr[0], len = tc_v3_length(dir), s = d->lift_s, l2 = tc_v3_dot(dir, dir);
    if (l2 != 0 && l2 != 1) dir = tc_v3_scale(dir, 1 / sqrtf(l2));
    if (mode == 14) { /* to the start */
        s = 0;
        d->lift_x = d->lift_rate = 0;
        pos = a;
        d->moving = true;
    } else if (mode == 15) { /* on along the way */
        float lag = half != 0 ? half : 0.001f; /* FUN_004c9afb's guard */
        d->lift_rate = (len - d->lift_x) / lag;
        d->lift_x += d->lift_rate * dt;
        if (fabsf(s) < fabsf(len * 0.6321f)) { /* 1 - 1/e of the way: exponentially, in T / 2 */
            d->lift_elapsed = half;
            s = d->lift_x;
        } else { /* then on at that speed: there at T */
            s = ((d->lift_elapsed - half) / (half * 2.718f) + 0.6321f) * len;
            d->lift_elapsed += dt;
        }
        pos = tc_v3_add(a, tc_v3_scale(dir, s));
        if (fabsf(s) < fabsf(len)) {
            d->moving = true;
        } else {
            d->moving = false;
            pos = b;
        }
    }
    d->lift_s = s;
    d->velocity = tc_v3_scale(tc_v3_sub(pos, p->position), dt); /* as the exe stores it (dt x the step) */
    p->position = pos;
}

/* ---- Type 7: DNM_RequestType7_004b17d1, DNM_IntegrateType7_004acb49, forces DNM_Type7Forces_004c831a ----
 * Card: +4 mass, +0xC min speed, +0x10 min turn, +0x14 max speed, +0x18 max turn, +0x1C gravity factor, +0x20 damping,
 * +0x24 stiffness. */
typedef struct wanted7 { tc_vec3 v; float turn; } wanted7; /* DynamWork +0x6C: cleared every frame (FUN_004b26cb) */

static void float_request(tc_cast_dyn *d, tc_ai_perso *p, const tc_ai_card *card, int mode, const float *pr, float dt,
                          wanted7 *w) {
    float min_speed = tc_ai_card_f32(card, 0x0C), min_turn = tc_ai_card_f32(card, 0x10);
    float max_speed = tc_ai_card_f32(card, 0x14), max_turn = tc_ai_card_f32(card, 0x18);
    switch (mode) {
    case 3: case 4: { /* turn by [0xD] */
        float t = mode == 3 ? pr[0xD] : -pr[0xD];
        w->turn = clamp_magnitude(t, min_turn, max_turn);
        break;
    }
    case 5: w->turn = (pr[0xD] - p->heading) / dt; break; /* to the angle [0xD] (about [0xE..0x10]: up here) */
    case 0x12: w->turn = clamp_magnitude((pr[0xD] - p->heading) / dt, min_turn, max_turn); break;
    case 12: { /* the move direction x speed, in its axes */
        float speed = clamp_magnitude(pr[10], min_speed, max_speed);
        d->unk_70 = -1;
        w->v = local_to_global(p->heading, tc_v3_scale(tc_v3(pr[7], pr[8], pr[9]), speed));
        break;
    }
    case 0x14: { /* the same in world axes, turning towards it (FUN_004ae280: the angle from its front) */
        tc_vec3 dir = tc_v3(pr[7], pr[8], pr[9]);
        float speed;
        d->unk_70 = -1;
        if (dir.x == 0 && dir.y == 0 && dir.z == 0) break;
        speed = clamp_magnitude(pr[10], min_speed, max_speed);
        w->v = tc_v3_scale(dir, speed);
        if (max_turn > 0 && (dir.x != 0 || dir.y != 0)) {
            float want = atan2f(dir.x, -dir.y), da = remainderf(want - p->heading, 2 * TC_PI);
            w->turn = clamp_magnitude(clampf(da / dt, -max_turn, max_turn), min_turn, max_turn);
        }
        break;
    }
    case 0x15: /* a spring towards ProposalVecA (gravity factor 0 only) */
        if (tc_ai_card_f32(card, 0x1C) == 0 && d->unk_70 < 0) {
            tc_vec3 n = tc_v3_sub(tc_v3(pr[1], pr[2], pr[3]), p->position);
            float k = 6.2831855f * pr[0], l2; /* 2 pi x the frequency [0] (DAT_004d2f18) */
            d->unk_70 = 0;
            if (n.x == 0 && n.y == 0 && n.z == 0) { d->unk_70 = -1; break; }
            d->velocity = tc_v3_scale(n, k);
            l2 = tc_v3_dot(n, n);
            if (l2 != 0 && l2 != 1) n = tc_v3_scale(n, 1 / sqrtf(l2));
            d->plane_n = n;
            d->plane_k = k * k * tc_ai_card_f32(card, 0x04);
            d->plane_offset = tc_v3_dot(n, p->position);
        }
        break;
    default: break; /* 0x17 (an orbit about ProposalVecA): not ported */
    }
}

/* DNM_Type7Forces_004c831a for one substep h: the velocity the forces give (linear and angular). load: the masses
 * riding it and the first rider's position (FUN_004c32f6). */
static void float_forces(tc_cast_dyn *d, const tc_ai_perso *p, const tc_ai_card *card, float gravity, float load,
                         tc_vec3 load_at, float h, tc_vec3 *v, tc_vec3 *w) {
    float mass = tc_ai_card_f32(card, 0x04), g = tc_ai_card_f32(card, 0x1C) * gravity;
    *v = tc_v3(0, 0, 0);
    *w = tc_v3(0, 0, 0);
    if (!(mass > 0)) return;
    if (g == 0) {
        if (d->unk_70 >= 0) { /* the plane spring of mode 0x15 */
            float f = (d->plane_offset - tc_v3_dot(p->position, d->plane_n)) * d->plane_k;
            *v = tc_v3_add(d->velocity, tc_v3_scale(d->plane_n, f * h / mass));
        }
        return;
    }
    {
        float k = tc_ai_card_f32(card, 0x24), m = mass + load, c = -(m * g), damp = -(tc_ai_card_f32(card, 0x20) * d->velocity.z);
        if (d->unk_70 < 0) { /* the rest height latches where it is, under the load it has now */
            d->rest_z = p->position.z - c / k;
            d->unk_70 = 0;
            d->substeps = true;
        }
        *v = tc_v3_add(d->velocity, tc_v3(0, 0, (damp + (d->rest_z - p->position.z) * k + c) * h / m));
        if (mass > 125) { /* the tilt (DAT_004e77fc): towards the first rider, more than 0.5 off centre */
            float dx = load_at.x - p->position.x, dy = load_at.y - p->position.y;
            tc_vec3 t = tc_v3(0, 0, 0);
            if (dx * dx + dy * dy > 0.25f) {
                float c2 = -((200 + load) * g);
                t = tc_v3(c2 * dy, -(dx * c2), 0);
            }
            t = tc_v3_add(t, tc_v3_scale(tc_v3(d->rot.x, d->rot.y, 0), -25000)); /* back to level */
            t = tc_v3_add(t, d->omega);                                           /* as read (FUN_004b73db) */
            t = tc_v3_add(t, tc_v3_scale(tc_v3(d->omega.x, d->omega.y, 0), -3250)); /* damped */
            *w = tc_v3_add(d->omega, tc_v3_scale(t, h / mass));
        }
    }
}

/* MEC_CollideMoveNoTurn_004bedd6 (flags 4, no card) at the end of the type 7 integrator: its own sweep from where it
 * started this step to where its motion took it. The type 7 persos read so far have no ZDM of their own but a collide
 * set of a single sphere at their origin (+0x36; MEC_SweepSphere_0043d480: radius +0x30 / 2): the cavedoc crate.
 * It meets the static world of its sector's collide list and the persos its set allows (+0x34, +0x35). */
static void own_sweep(tc_cast *c, tc_cast_actor *a, tc_vec3 start, float dt) {
    tc_collision *w = c->world;
    tc_ai_perso *p = &a->ai;
    tc_ai_sphere_set body;
    tc_vec3 v, end = p->position;
    const uint8_t *solid;
    int32_t self, ignore;
    uint8_t prio;
    bool meets;
    if (!w || !(a->entry->own_radius > 0) || !(dt > 0)) return;
    if (end.x == start.x && end.y == start.y && end.z == start.z) return;
    memset(&body, 0, sizeof(body));
    body.count = 1;
    body.spheres[0].radius = a->entry->own_radius;
    body.spheres[0].flags = 1;
    body.spheres[0].rebound = body.spheres[0].grip = 1;
    solid = w->solid, self = w->self, ignore = w->ignore, prio = w->self_priority, meets = w->self_meets;
    w->solid = c->sectors && a->sector.sector != TC_SECTOR_NONE ? tc_sectors_solid(c->sectors, a->sector.sector) : NULL;
    w->self = a->entry->id;
    w->ignore = -1;
    w->self_priority = a->entry->own_priority;
    w->self_meets = a->entry->own_meets != 0;
    p->position = start;
    v = tc_v3_scale(tc_v3_sub(end, start), 1 / dt);
    tc_mec_collide_move(&a->dyn.walk, NULL, w, &body, 0, dt, &p->position, &v);
    w->solid = solid, w->self = self, w->ignore = ignore, w->self_priority = prio, w->self_meets = meets;
}

static void float_step(tc_cast *c, tc_cast_actor *a, const tc_ai_card *card, const wanted7 *wanted, float dt) {
    tc_cast_dyn *d = &a->dyn;
    tc_ai_perso *p = &a->ai;
    float left = dt, step, load = 0;
    tc_vec3 load_at = p->position, start = p->position;
    if (c->main && c->main_carrier == a->entry->id) { /* FUN_004c32f6: its riders (the main actor) */
        load = c->main_mass;
        load_at = c->main->position;
    }
    step = d->substeps ? 0.005f : dt;
    if (dt < step) step = dt;
    while (left > 0) {
        float h = step <= left ? step : left;
        tc_vec3 v, w, mv, mw;
        /* port: g of the air environment every level's sectors share (the exe looks the perso's own up, MEC_EnvAt) */
        float_forces(d, p, card, 9.81f, load, load_at, h, &v, &w);
        mv = tc_v3_add(wanted->v, v);
        mw = tc_v3_add(tc_v3(0, 0, wanted->turn), w);
        if (mv.x == 0 && mv.y == 0 && mv.z == 0 && mw.x == 0 && mw.y == 0 && mw.z == 0 && !d->substeps) break;
        p->position = tc_v3_add(p->position, tc_v3_scale(mv, h));
        d->rot = tc_v3_add(d->rot, tc_v3_scale(mw, h)); /* FUN_004c36e7: the rotations as vectors */
        d->velocity = v;
        d->omega = w;
        left -= step;
    }
    own_sweep(c, a, start, dt);
    d->tilted = d->rot.x != 0 || d->rot.y != 0;
    p->heading = d->rot.z;
    d->moving = tc_v3_length_sq(d->velocity) > 0 || tc_v3_length_sq(wanted->v) > 0;
}

/* The ground walkers' wanted motion this frame (DNM_RequestWalk_004ae392, types 0 and 12; type 12's card fields sit 4
 * bytes lower): mode 12's move direction x speed in its axes, mode 0x14's in the world's (turning it), else the
 * animation's own motion (FUN_0042a8d0 pushes it as mode 12). */
static tc_vec3 ground_wanted(tc_ai_perso *p, const tc_ai_card *card, float dt) {
    const tc_ai_motion *m = &p->motion;
    const tc_ai_state *st = &p->model->states[p->state];
    if (m->direction_set && m->mech_mode == 12) {
        float speed = clamp_magnitude(m->proposal[10], 0, tc_ai_card_f32(card, card->type == 12 ? 0x10 : 0x14));
        return tc_v3_scale(local_to_global(p->heading, tc_v3(m->proposal[7], m->proposal[8], m->proposal[9])), speed);
    }
    if (m->direction_set && m->mech_mode == 20) { /* the GoTo meta-actions (GoToWP, GoOverWay) */
        float turn;
        tc_vec3 v = tc_mec_walk_request_world(card, m->proposal, p->heading, dt, &turn);
        p->heading += turn * dt;
        return v;
    }
    if (st->root_count) {
        uint32_t fr = p->state_frame > 0 ? (uint32_t)p->state_frame % st->root_count : 0;
        return tc_v3_scale(local_to_global(p->heading, p->model->root[st->root_first + fr]), (float)p->frame_rate);
    }
    return tc_v3(0, 0, 0);
}

/* Type 12 (DNM_IntegrateType12_004a9d20), and the port's stand-in for a type 0 / 4 state without a body: it collides
 * only while its report has no ground (MEC_CollideMoveNoTurn_004bedd6 under (report & 3) == 0), and on a grippy ground
 * its forces (FUN_004c3e50: grip >= 1) cancel gravity and the old velocity: a grounded type 12 perso goes where its
 * requests take it, through walls, at its height. Port: it starts grounded (the snapshot's report is not exported:
 * Suzy and the like stand on their floor in the data), so no collision at all. A perf shortcut of the original (NPCs
 * walking without collision tests); kept as read. */
static void glide_step(tc_cast_actor *a, const tc_ai_card *card, float dt) {
    tc_ai_perso *p = &a->ai;
    tc_cast_dyn *d = &a->dyn;
    tc_vec3 v = tc_v3(0, 0, 0);
    if (p->motion.turning) p->heading += p->motion.turn_speed * dt;
    if (!p->motion.stop) v = ground_wanted(p, card, dt);
    v.z = 0;
    p->position = tc_v3_add(p->position, tc_v3_scale(v, dt));
    d->velocity = v;
    p->velocity = v;
    p->contacts = TC_AI_CONTACT_GROUND;
    d->moving = tc_v3_length_sq(v) > 0;
}

/* ---- Types 0 and 4: the walkers (DNM_IntegrateWalk_004aa6af with DNM_RequestWalk_004ae392 on the ground, FUN_004b0f2a
 * in the air), as Ed's (tc_ed_ai.c walk()): its state's ZDM body (CMEC's walker part), riding (tc_mec_walk_carry), the
 * requests or else the animation's root motion, then the collide-and-move of tc_mec_walk_step. Returns false when the
 * state has no body (then glide_step). */
static bool walk_step(tc_cast *c, tc_cast_actor *a, const tc_ai_card *card, float dt) {
    const tc_cast_family *f = &c->level->cast.families[a->entry->family];
    tc_ai_perso *p = &a->ai;
    const tc_ai_motion *m = &p->motion;
    const float *pr = m->proposal;
    tc_cast_dyn *d = &a->dyn;
    tc_collision *w = c->world;
    const tc_ai_sphere_set *body;
    tc_vec3 wanted = tc_v3(0, 0, 0), jump = tc_v3(0, 0, 0);
    const tc_vec3 *impulse = NULL;
    const uint8_t *solid;
    int32_t self, ignore;
    uint8_t prio, k;
    bool meets, air = card->type == 4;
    tc_mec_env env = tc_mec_env_default();
    if (!w || !f->body || p->state >= f->state_count || f->body[p->state] == 0xFFFF) return false;
    body = &f->bodies[f->body[p->state]];
    solid = w->solid, self = w->self, ignore = w->ignore, prio = w->self_priority, meets = w->self_meets;
    w->solid = c->sectors && a->sector.sector != TC_SECTOR_NONE ? tc_sectors_solid(c->sectors, a->sector.sector) : NULL;
    w->self = a->entry->id;
    w->ignore = -1;
    w->self_priority = a->entry->own_priority;
    w->self_meets = a->entry->own_meets != 0;
    if (c->sectors && a->sector.sector != TC_SECTOR_NONE) tc_mec_env_at(c->sectors, a->sector.sector, p->position, &env);
    tc_mec_walk_carry(&d->walk, w, body, &p->heading, dt, &p->position);
    if (air) tc_mec_walk_air(&d->walk);
    if (m->turning) p->heading += m->turn_speed * dt;
    if (m->velocity_set) d->velocity = m->velocity;
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
        d->velocity = tc_v3(0, 0, 0);
    } else {
        wanted = ground_wanted(p, card, dt);
    }
    if (air && m->stop) d->velocity = tc_v3(0, 0, 0);
    tc_mec_walk_step(&d->walk, card, env, w, body, p->heading, wanted, air, impulse, dt, &p->position, &d->velocity);
    w->solid = solid, w->self = self, w->ignore = ignore, w->self_priority = prio, w->self_meets = meets;
    if (p->position.z < w->bounds_min.z - 20) { /* port: fell out of the level: back where it was placed, at rest */
        p->position = tc_v3(a->entry->world.m[3], a->entry->world.m[7], a->entry->world.m[11]);
        d->velocity = tc_v3(0, 0, 0);
        tc_mec_walk_reset(&d->walk);
    }
    p->velocity = d->velocity;
    p->contacts = tc_mec_walk_contacts(&d->walk) & 0x1F;
    p->report_high = (d->walk.report | d->walk.report_prev) & TC_MEC_ON_PERSO;
    p->standing_on = p->report_high == TC_MEC_ON_PERSO && d->walk.carrier > 0 ? d->walk.carrier : 0;
    p->ground_material = d->walk.ground_own;
    p->wall_material = d->walk.wall_own;
    d->moving = tc_v3_length_sq(d->velocity) > 0;
    return true;
}

/* ---- Type 10: the projectiles (DNM_RequestType10_004b252e, DNM_IntegrateType10_004adfc3, forces FUN_004c709a) ----
 * Card +4 mass, +8 gravity factor. Requests: 0x13 / 0x16 ProposalVecA as the wanted velocity in world axes, 0x15 in its
 * own; none: the forces keep the velocity (+0x38) when the gravity factor is 0 (Ed's bees: they fly straight). Port:
 * with a gravity factor, gravity x it on the velocity (the exe's ballistic forces with thrust are not read), and no
 * collide-and-move of its own (the bee's scripts end it at the wall their probe found, or on a ZDE hit). */
static void projectile_step(tc_cast_actor *a, const tc_ai_card *card, float dt) {
    tc_ai_perso *p = &a->ai;
    const tc_ai_motion *m = &p->motion;
    const float *pr = m->proposal;
    tc_cast_dyn *d = &a->dyn;
    tc_vec3 v = d->velocity, va = tc_v3(pr[1], pr[2], pr[3]);
    float g = tc_ai_card_f32(card, 0x08);
    uint8_t k;
    bool requested = false;
    if (m->velocity_set) v = m->velocity;
    for (k = 0; k < m->mode_count; k++) {
        if (m->modes[k] == 0x13 || m->modes[k] == 0x16) v = va, requested = true;
        else if (m->modes[k] == 0x15) v = local_to_global(p->heading, va), requested = true;
    }
    if (!requested && g != 0) v.z -= 9.81f * g * dt; /* port */
    if (m->turning) p->heading += m->turn_speed * dt;
    p->position = tc_v3_add(p->position, tc_v3_scale(v, dt));
    d->velocity = v;
    p->velocity = v;
    p->contacts = 0;
    d->moving = tc_v3_length_sq(v) > 0;
}

bool tc_cast_mechanics(tc_cast *c, tc_cast_actor *a, float dt) {
    const tc_cast_family *f = &c->level->cast.families[a->entry->family];
    tc_ai_perso *p = &a->ai;
    const tc_ai_motion *m = &p->motion;
    const tc_ai_card *card;
    tc_cast_dyn *d = &a->dyn;
    uint16_t ci;
    uint8_t k;
    if (!f->cards || p->state >= f->state_count || (ci = f->cards[p->state]) == 0xFFFF) return false;
    card = &c->level->cast.cards[ci];
    if (card->type == 10) {
        d->card_type = 10;
        projectile_step(a, card, dt);
        p->mech_moving = d->moving;
        return true;
    }
    if (card->type == 0 || card->type == 4 || card->type == 12) { /* the walkers */
        if (card->type != d->card_type) {
            if (d->card_type != 0 && d->card_type != 4 && d->card_type != 12)
                tc_mec_walk_reset(&d->walk), d->velocity = tc_v3(0, 0, 0);
            d->card_type = card->type;
        }
        if (card->type == 12 || !walk_step(c, a, card, dt)) glide_step(a, card, dt);
        p->mech_moving = d->moving;
        return true;
    }
    if (card->type != 6 && card->type != 7) return false;
    if (card->type != d->card_type) { /* DNM_CardTypeInit_004a7459 (port: what the two types need) */
        d->velocity = d->omega = tc_v3(0, 0, 0);
        d->rot = tc_v3(0, 0, p->heading);
        d->unk_70 = -1;
        d->card_type = card->type;
    }
    if (card->type == 6) {
        if (m->mode_count == 0) lift_request(d, p, 0, m->proposal, dt); /* the default request */
        for (k = 0; k < m->mode_count; k++) lift_request(d, p, m->modes[k], m->proposal, dt);
        d->rot = tc_v3(0, 0, p->heading);
    } else {
        wanted7 w = {{0, 0, 0}, 0};
        d->rot.z = p->heading;
        if (m->velocity_set) { /* PROC 35's pending velocity (z := 0), for the next forces */
            d->velocity = tc_v3(m->velocity.x, m->velocity.y, 0);
        }
        if (m->mode_count == 0) float_request(d, p, card, 0, m->proposal, dt, &w);
        for (k = 0; k < m->mode_count; k++) float_request(d, p, card, m->modes[k], m->proposal, dt, &w);
        float_step(c, a, card, &w, dt);
    }
    p->mech_moving = d->moving;
    return true;
}
