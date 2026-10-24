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

/* Is p within the portal's capture: inside its zone (v9: COND_13, the origin in the perso's ZDD zone, taken into its
 * axes), else within its radius. */
static bool portal_inside(const tc_ai_portal *g, tc_vec3 p) {
    const float *m = g->zone_inverse;
    tc_vec3 q;
    uint8_t k;
    if (!g->zone_count) return tc_v3_distance(p, g->position) < g->radius;
    q = tc_v3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
              m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
    for (k = 0; k < g->zone_count; k++) {
        const tc_ai_zone_prim *z = &g->zone[k];
        if (z->kind == 7 && tc_v3_distance(q, z->a) <= z->b.x) return true;
        if (z->kind == 8 && q.x >= z->a.x && q.y >= z->a.y && q.z >= z->a.z && q.x <= z->b.x && q.y <= z->b.y &&
            q.z <= z->b.z) return true;
    }
    return false;
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
    memset(&e->swim, 0, sizeof(e->swim));
    e->env_depth = 0;
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
            e->portal_armed[i] = !portal_inside(&model->portals[i], player->position);
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
        if (!e->portal_armed[i] && !portal_inside(&m->portals[i], e->player->position))
            e->portal_armed[i] = 1;
    if (e->portal < 0) {
        for (i = 0; e->portal_armed && i < m->portal_count; i++) {
            const tc_ai_portal *g = &m->portals[i];
            bool ok = e->portal_armed[i] && portal_inside(g, e->player->position);
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
/* Stopgap, not from the exe (docs/NOTES.md "Sliding stages"): in the original the skier riding into a wall fast
 * stumbles and keeps going; the port's collision response stops him dead and he sticks. Until the original's reaction
 * is traced: a wall hit from above 8 u/s that leaves under 30 % of the speed keeps half the speed along the wall (for a
 * dead-on hit, along the wall towards the side he drifted to) and puts him 5 cm off it. */
static void ski_unstick(tc_ed_ai *e, tc_vec3 before, tc_vec3 *v) {
    tc_player *p = e->player;
    tc_vec3 n = tc_v3(e->walk.wall_normal.x, e->walk.wall_normal.y, 0), t;
    float in = tc_v3_length(before), along;
    if (!(e->walk.report & TC_AI_CONTACT_WALL) || in < 8 || tc_v3_length(*v) >= 0.3f * in || tc_v3_length(n) < 1e-3f) return;
    n = tc_v3_normalize(n);
    t = tc_v3_sub(before, tc_v3_scale(n, tc_v3_dot(before, n)));
    t.z = fminf(t.z, 0);
    along = tc_v3_length(t);
    if (along < 0.2f * in) { /* head-on: along the wall, towards the side of the drift (or the board's right) */
        tc_vec3 side = tc_v3(-n.y, n.x, 0);
        float s = tc_v3_dot(before, side);
        if (fabsf(s) < 1e-3f) s = tc_v3_dot(e->ski.x, side);
        t = tc_v3_scale(side, (s < 0 ? -1.0f : 1.0f) * 0.2f * in);
        along = 0.2f * in;
    }
    *v = tc_v3_scale(t, 0.5f * fmaxf(along, 0.3f * in) / along);
    p->position = tc_v3_add(p->position, tc_v3_scale(n, 0.05f));
}

/* Second stopgap: no progress near a wall (under 0.75 u in 0.4 s, a wall touched within 0.5 s, on a slope or in the
 * air; the flat start and finish never trigger it). Corners flicker between wall and no contact, and a board still
 * facing into the corner rides straight back in, so the skier is turned to the most open of 16 directions (free
 * distance by rays at three heights, preferring downhill and away from the wall) and sent off at 7 u/s. Pockets (Totalski:
 * a dead end downhill, a groove open only above the knees) bring him back, so a rescue within 2 u of the last one
 * escalates: the second moves him up to 2 u out along the most open way, the third and later lift him 1.2 first. */
static void ski_unpin(tc_ed_ai *e, tc_vec3 *v, float dt) {
    static const float heights[3] = {0.2f, 0.6f, 1.4f};
    tc_player *p = e->player;
    tc_vec3 g = e->walk.ground_normal, down, away, best = tc_v3(0, 0, 0);
    float best_score = -INFINITY, best_free = 0, h, lift = 0;
    int k, level;
    if (e->walk.report & TC_AI_CONTACT_WALL) {
        e->ski_wall_age = 0;
        e->ski_wall_normal = e->walk.wall_normal;
    } else {
        e->ski_wall_age += dt;
    }
    down = (e->walk.report & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) ? tc_v3(g.x, g.y, 0) : tc_v3(0, 0, 0);
    if (tc_v3_length(tc_v3_sub(p->position, e->ski_anchor)) > 0.75f || e->ski_wall_age > 0.5f ||
        (tc_v3_length(down) < 0.1f && (e->walk.report & TC_AI_CONTACT_GROUND))) {
        e->ski_anchor = p->position;
        e->ski_pinned = 0;
        return;
    }
    e->ski_pinned += dt;
    if (e->ski_pinned < 0.4f) return;
    e->ski_rescues = tc_v3_length(tc_v3_sub(p->position, e->ski_rescue_at)) < 2 ? e->ski_rescues + 1 : 1;
    e->ski_rescue_at = p->position;
    level = e->ski_rescues;
    if (level >= 3) lift = 1.2f;
    down = tc_v3_length(down) > 1e-3f ? tc_v3_normalize(down) : down;
    away = tc_v3(e->ski_wall_normal.x, e->ski_wall_normal.y, 0);
    away = tc_v3_length(away) > 1e-3f ? tc_v3_normalize(away) : away;
    for (k = 0; k < 16; k++) {
        tc_vec3 d = tc_v3(cosf(k * TC_PI / 8), sinf(k * TC_PI / 8), 0);
        float free = 6, score;
        int j;
        for (j = 0; j < 3; j++) {
            tc_hit hit;
            if (tc_collision_raycast(p->world, tc_v3_add(p->position, tc_v3(0, 0, lift + heights[j])), d, 6, &hit))
                free = fminf(free, hit.distance);
        }
        score = free + (level >= 2 ? 0.5f : 2) * tc_v3_dot(d, down) + tc_v3_dot(d, away);
        if (score > best_score) best_score = score, best = d, best_free = free;
    }
    h = atan2f(-best.x, best.y);
    tc_mec_ski_enter(&e->ski, h);
    p->heading = h;
    if (level >= 2)
        p->position = tc_v3_add(p->position, tc_v3_add(tc_v3_scale(best, fminf(fmaxf(best_free - 0.8f, 0.3f), 2)),
                                                       tc_v3(0, 0, lift)));
    else if (e->walk.passes >= 6) /* wedged: the collide loop gave up, a velocity alone would not move him */
        p->position = tc_v3_add(p->position, tc_v3_add(tc_v3_scale(best, 0.5f), tc_v3(0, 0, 0.3f)));
    else if (best_free > 1)
        p->position = tc_v3_add(p->position, tc_v3_scale(best, 0.2f));
    *v = tc_v3_scale(best, 7);
    e->ski_anchor = p->position;
    e->ski_pinned = 0;
}

static void ski_move(tc_ed_ai *e, const tc_ai_sphere_set *body, tc_vec3 v, float dt) {
    tc_player *p = e->player;
    tc_vec3 before = v;
    uint32_t had, bits;
    e->walk.slope_cos = 0.34202f; /* faces up to 70 deg are slopes he slides on, not walls (stopgap) */
    had = e->walk.report;
    bits = tc_mec_collide_move(&e->walk, NULL, p->world, body, 0, dt, &p->position, &v);
    if (!bits && (had & TC_AI_CONTACT_GROUND)) tc_mec_resnap(&e->walk, p->world, body, 0, 0.7f, 0.7f, &p->position);
    ski_unstick(e, before, &v);
    ski_unpin(e, &v, dt);
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

/* The footstep switch (docs/NOTES.md "Footsteps"): an animation's step sound (Bnk_0 event 49) plays the step of the
 * floor's sound type v (GameMaterial +8, .ttai v10 per triangle): TC_AI_STEP_SOUND + v is added next to Ed's own
 * step (event 49), which keeps playing (the user hears both in the original). The floor = the triangle under the origin (the exe: the ground contact's material). */
static void footsteps(tc_ed_ai *e) {
    tc_ai_perso *a = &e->ai;
    const tc_ai_model *m = a->model;
    tc_hit hit;
    int v = -1;
    uint8_t k;
    uint8_t queued = a->sound_count;
    for (k = 0; k < queued; k++) {
        if (a->sounds[k] != TC_AI_FOOTSTEP_EVENT || !m->floor_sound_count) continue;
        if (v < 0) {
            v = 0xFF;
            if (tc_collision_raycast(e->player->world, tc_v3_add(e->player->position, tc_v3(0, 0, 0.5f)), tc_v3(0, 0, -1),
                                     1.5f, &hit) && hit.tri->source < m->floor_sound_count)
                v = m->floor_sounds[hit.tri->source];
        }
        /* Ed's own step plays too, the floor's on top (user, from the original) */
        if (v >= 1 && v <= 23 && a->sound_count < TC_AI_MAX_SOUNDS) a->sounds[a->sound_count++] = (uint16_t)(TC_AI_STEP_SOUND + v);
    }
}

/* The water zones the scripts test (GAM_00437900 + MEC_004c0fb5, docs/NOTES.md "Swimming"): kind 0 = the water flag at
 * the origin, kinds 1..6 at the playing state's sample points (placed by the model matrix); combined = the value the
 * present kinds 1..6 share, 3 when they differ, kind 0's without points. Every exported plane is water (MecEnv +0x28 1). */
static void water_zones(tc_ed_ai *e) {
    tc_ai_perso *a = &e->ai;
    const tc_ai_state *st = &a->model->states[a->state];
    tc_mec_env env;
    int8_t combined = -1;
    bool mixed = false;
    int k;
    for (k = 0; k < 7; k++) a->zone[k] = -1;
    a->zone[0] = tc_mec_env_at(a->model, e->player->position, &env) > 0;
    if (st->water_points != TC_AI_NONE16 && a->model->water_count) {
        const tc_ai_water_points *w = &a->model->water_point_sets[st->water_points];
        tc_mat4 m = tc_ed_ai_model_matrix(e);
        for (k = 0; k < w->count; k++) {
            tc_vec3 q = w->point[k], p = tc_v3(m.m[0] * q.x + m.m[4] * q.y + m.m[8] * q.z + m.m[12],
                                             m.m[1] * q.x + m.m[5] * q.y + m.m[9] * q.z + m.m[13],
                                             m.m[2] * q.x + m.m[6] * q.y + m.m[10] * q.z + m.m[14]);
            if (w->kind[k] >= 1 && w->kind[k] <= 6) a->zone[w->kind[k]] = tc_mec_env_at(a->model, p, &env) > 0;
        }
    } else if (st->water_points != TC_AI_NONE16) {
        const tc_ai_water_points *w = &a->model->water_point_sets[st->water_points];
        for (k = 0; k < w->count; k++) if (w->kind[k] >= 1 && w->kind[k] <= 6) a->zone[w->kind[k]] = 0;
    }
    for (k = 1; k <= 6; k++) {
        if (a->zone[k] < 0) continue;
        if (combined < 0) combined = a->zone[k];
        else if (combined != a->zone[k]) mixed = true;
    }
    a->zone_combined = mixed ? 3 : combined >= 0 ? combined : a->zone[0];
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
    /* dynamics +0x2BC / +0x2C0: the lagged attack / bank in flight, the lagged polar angle / heading swimming */
    a->report_2bc = e->card_type == 1 ? e->swim.polar_lagged : e->fly.attack_lagged;
    a->report_2c0 = e->card_type == 1 ? e->swim.heading_lagged : e->fly.bank_lagged;
    a->env_depth = e->env_depth;
    {
        tc_mat4 mm = tc_ed_ai_model_matrix(e);
        a->has_axes = true;
        a->axes[0] = tc_v3(mm.m[0], mm.m[1], mm.m[2]);
        a->axes[1] = tc_v3(mm.m[4], mm.m[5], mm.m[6]);
        a->axes[2] = tc_v3(mm.m[8], mm.m[9], mm.m[10]);
    }
    water_zones(e);
    tc_ai_step(a, dt);
    footsteps(e);
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
    /* The mechanical environment at the feet (the integrators' lookup at the step's start; water planes: .ttai v8) */
    e->env_depth = tc_mec_env_at(a->model, p->position, &e->env);
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
            v = tc_v3_scale(local_to_global(p->heading, root_motion(a)), (float)a->frame_rate);
        }
        if (m->velocity_set) v = tc_v3_add(v, tc_v3(m->velocity.x, m->velocity.y, 0)); /* pending: z := 0 */
        tc_player_move(p, dt, v);
        p->velocity = v;
        e->card_type = type;
        e->contacts = player_contacts(p);
        return;
    }
    if (type == 1) {
        /* Swimming (card type 1): the swim forces look the water up themselves, at their probe below the origin */
        tc_vec3 v = p->velocity;
        uint8_t k;
        if (e->card_type != 1) tc_mec_swim_enter(&e->swim, p->heading);
        if (m->mode_count == 0) tc_mec_swim_request(&e->swim, card, 0, m->proposal, dt);
        for (k = 0; k < m->mode_count; k++) tc_mec_swim_request(&e->swim, card, m->modes[k], m->proposal, dt);
        v = tc_mec_swim_velocity(&e->swim, card, a->model, p->position, v, dt, &e->env_depth);
        tc_player_move(p, dt, v);
        p->heading = e->swim.heading_lagged + TC_PI / 2; /* the swim heading's front, in tc_player's terms */
        e->card_type = type;
        e->contacts = player_contacts(p);
        return;
    }
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
    if (e->card_type == 5 || e->card_type == 1) {
        /* Flight / swimming: the model follows the frame (FUN_004c8dfc / 004c91d2), head first, about the origin */
        const tc_ai_state *st = &e->ai.model->states[e->ai.state];
        tc_vec3 x, y, z, p = e->player->position;
        if (st->card == TC_AI_NONE16) return tc_player_model_matrix(e->player);
        if (e->card_type == 5) tc_mec_fly_model_axes(&e->fly, &e->ai.model->cards[st->card], &x, &y, &z);
        else tc_mec_swim_model_axes(&e->swim, &e->ai.model->cards[st->card], &x, &y, &z);
        {
            tc_mat4 m = {{x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1}};
            return m;
        }
    }
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
