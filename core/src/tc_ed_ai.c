#include "tc_internal.h"
#include <ctype.h>
#include <string.h>
#include "tonic/tc_ed_ai.h"
#include "tonic/tc_alloc.h"

/* His in-flight aim hover's flag (B45 script 11: 1 while Lateral holds it, 2 for the 500 ms after) */
#define TC_ED_HOVER_DSG 35

static bool same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

/* Is p within the portal's capture: inside its zone (ZONE: COND_13, the origin in the perso's ZDD zone, taken into its
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

/* The takeover's first behaviour (tc_ai_model.start_chain), at the start and at a restart: the exe's start trigger runs
 * again when the map is initialised again. step() goes on to the next ones, then to the initial behaviour. */
static void start_chain(tc_ed_ai *e) {
    const tc_ai_model *m = e->ai.model;
    e->start_step = 0;
    if (!m->start_chain_count) return;
    tc_ai_force_behaviour(&e->ai, m->start_chain[0]);
    e->start_step = 1;
}

static void find_walk_in(tc_ed_ai *e);

tc_result tc_ed_ai_init(tc_ed_ai *e, const tc_ai_model *model, tc_player *player, const char *level_name) {
    tc_result r = tc_ai_init(&e->ai, model);
    uint32_t i;
    if (r == TC_OK && !model->state_count) r = TC_ERR_TRUNCATED; /* Ed needs his states */
    e->player = player;
    e->mechanics = NULL;
    e->mechanics_user = NULL;
    e->cast_portals = false;
    e->camera = -1;
    e->run_scale = 2.0f;  /* run (state 10) 4.5 -> 9.0 u/s: a little under the old controller's 9.9 (user) */
    e->walk_scale = 1;    /* walk (state 1) 3.5 u/s, already close to the old 3.3 */
    e->dyn.card_type = TC_AI_NONE16;
    memset(&e->dyn.fly, 0, sizeof(e->dyn.fly));
    memset(&e->dyn.swim, 0, sizeof(e->dyn.swim));
    e->dyn.env_depth = 0;
    e->dyn.env = tc_mec_env_default();
    e->sectors = NULL;
    e->stepping = false;
    e->solid_union = NULL;
    e->sector.sector = TC_SECTOR_NONE;
    e->sector.position = player->position;
    e->sector.placed = player->placed;
    e->placed_seen = player->placed;
    tc_mec_walk_reset(&e->dyn.walk);
    e->dyn.contacts = 0;
    e->auto_start = false;
    e->wait_start = false;
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
    e->auto_start = (level_name && (same_name(level_name, "totalski") || same_name(level_name, "skymap"))) ||
                    model->auto_start;
    for (i = 0; i < model->other_count; i++)
        if (model->others[i].role == 1) e->camera = (int)i;
    find_walk_in(e);
    start_chain(e);
    return TC_OK;
}

void tc_ed_ai_free(tc_ed_ai *e) {
    tc_free(e->solid_union);
    e->solid_union = NULL;
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

/* His walk into an arch (tc_ed_ai.walk_in): the normal behaviour the portals' capture forces (B66), and the first other
 * normal behaviour its scripts change to (B68). */
static void find_walk_in(tc_ed_ai *e) {
    const tc_ai_model *m = e->ai.model;
    uint16_t i, k;
    int32_t s;
    e->walk_in[0] = e->walk_in[1] = TC_AI_NONE16;
    for (i = 0; i < m->portal_count && e->walk_in[0] == TC_AI_NONE16; i++) {
        const tc_ai_portal *g = &m->portals[i];
        uint16_t force = TC_AI_NONE16;
        bool cover = false;
        for (k = 0; k < g->capture_count; k++) { /* an arch: it forces the walk-in, which ends in the cover (a zone
                                                  * teleport covers at once and holds him in a wait, B15) */
            if (g->ops[k].op == TC_AI_POP_COVER) cover = true;
            if (g->ops[k].op == TC_AI_POP_FORCE && g->ops[k].a < m->normal_count && force == TC_AI_NONE16) force = g->ops[k].a;
        }
        if (!cover) e->walk_in[0] = force;
    }
    if (e->walk_in[0] == TC_AI_NONE16) return;
    {
        const tc_ai_behaviour *b = &m->behaviours[e->walk_in[0]];
        for (s = b->schedule >= 0 ? -1 : 0; s < (int32_t)b->script_count && e->walk_in[1] == TC_AI_NONE16; s++) {
            uint32_t n = s < 0 ? m->scripts[b->schedule] : m->scripts[b->first_script + (uint32_t)s];
            for (; n < m->node_count && m->nodes[n].type != TC_AI_END; n++)
                if (m->nodes[n].type == TC_AI_BEHAVIOUR && m->nodes[n].param < m->normal_count &&
                    m->nodes[n].param != e->walk_in[0]) {
                    e->walk_in[1] = (uint16_t)m->nodes[n].param;
                    break;
                }
        }
    }
}

/* In his walk into an arch, the cover not yet done (the cast's arches: tc_ed_ai.walk_in). */
static bool walking_in(const tc_ed_ai *e) {
    uint16_t b = e->ai.normal.behaviour;
    return e->cast_portals && !e->screen_covered && b != TC_AI_NONE16 && (b == e->walk_in[0] || b == e->walk_in[1]);
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
        tc_mec_walk_reset(&e->dyn.walk);
        e->dyn.contacts = 0;
        e->events.teleported = true;
        { /* as at the start: a portal the player arrives in captures only once he has left it (a passage's far end) */
            uint16_t k;
            for (k = 0; e->portal_armed && k < m->portal_count; k++)
                if (portal_inside(&m->portals[k], o->v)) e->portal_armed[k] = 0;
        }
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

/* The portals (tc_ai_portal): capture, then the fire ops in order once the fire condition holds. A portal's perso only
 * captures while awake: in Ed's sector or one on its activity list (tc_sectors_active), as the exe runs only those
 * persos' AI. skymap's lava hole (JET_Trou5LaveBouillonnante, sctlavabox) has a zone box reaching ~5 units into
 * sctTunnel20 / 22 beside it; sctTunnel22's list leaves sctlavabox out, so there the hole sleeps. */
static void run_portals(tc_ed_ai *e, bool all) {
    const tc_ai_model *m = e->ai.model;
    uint16_t i, k;
    for (i = 0; e->portal_armed && i < m->portal_count; i++)
        if (!e->portal_armed[i] && !portal_inside(&m->portals[i], e->player->position))
            e->portal_armed[i] = 1;
    if (e->portal < 0) {
        for (i = 0; e->portal_armed && i < m->portal_count; i++) {
            const tc_ai_portal *g = &m->portals[i];
            bool ok = e->portal_armed[i] && (all || !(g->flags & TC_AI_PORTAL_CAST)) &&
                      tc_sectors_active(e->sectors, e->sector.sector, g->sector) && portal_inside(g, e->player->position);
            for (k = 0; ok && k < g->capture_count; k++)
                if (g->ops[k].op == TC_AI_POP_GUARD && (int32_t)tc_ai_dsgvar_float(&e->ai, g->ops[k].a) != (int32_t)g->ops[k].b)
                    ok = false;
            if (!ok) continue;
            if (g->flags & TC_AI_PORTAL_TWO_SIDED) {
                /* Ed on the portal's +Y side: turn its axes half round about Z, so the walk-in stays on his side. The
                 * model is the driver's own (the viewer loads one per level), so the turn is kept in it. */
                float *R = ((tc_ai_portal *)g)->rows;
                tc_vec3 d = tc_v3_sub(e->player->position, g->position);
                if (R[1] * d.x + R[4] * d.y + R[7] * d.z > 0)
                    for (k = 0; k < 9; k++)
                        if (k % 3 != 2) R[k] = -R[k];
            }
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
    run_portals(e, true);
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

/* The footstep switch (docs/NOTES.md "Footsteps"): an animation's step sound (Bnk_0 event 49) plays the step of the
 * floor's sound type v (GameMaterial +8, .ttai FSND per triangle): TC_AI_STEP_SOUND + v is added next to Ed's own
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
            if ((tc_mec_walk_contacts(&e->dyn.walk) & 3) && e->dyn.walk.ground_tri == UINT32_MAX && e->dyn.walk.ground_sound != 0xFF)
                v = e->dyn.walk.ground_sound; /* on a perso (its own collision is not in the static world's raycast) */
            else if (tc_collision_raycast(e->player->world, tc_v3_add(e->player->position, tc_v3(0, 0, 0.5f)), tc_v3(0, 0, -1),
                                     1.5f, &hit) && hit.tri->source < m->floor_sound_count)
                v = m->floor_sounds[hit.tri->source];
        }
        /* Ed's own step plays too, the floor's on top (user, from the original) */
        if (v >= 1 && v <= 23 && a->sound_count < TC_AI_MAX_SOUNDS) {
            a->sound_kind[a->sound_count] = a->sound_kind[k]; /* from where the step event was */
            a->sound_emitter[a->sound_count] = a->sound_emitter[k];
            a->sounds[a->sound_count++] = (uint16_t)(TC_AI_STEP_SOUND + v);
        }
    }
}

/* The water zones the scripts test (GAM_00437900 + MEC_004c0fb5, docs/NOTES.md "Swimming"): kind 0 = the water flag
 * (MecEnv +0x28) at the origin, kinds 1..6 at the playing state's sample points (placed by the model matrix), all looked
 * up in Ed's sector; combined = the value the present kinds 1..6 share, 3 when they differ, kind 0's without points. */
static void water_zones(tc_ed_ai *e) {
    tc_ai_perso *a = &e->ai;
    const tc_ai_state *st = &a->model->states[a->state];
    tc_mec_env env;
    int8_t combined = -1;
    bool mixed = false;
    int k;
    for (k = 0; k < 7; k++) a->zone[k] = -1;
    tc_mec_env_at(e->sectors, e->sector.sector, e->player->position, &env);
    a->zone[0] = env.water != 0;
    if (st->water_points != TC_AI_NONE16) {
        const tc_ai_water_points *w = &a->model->water_point_sets[st->water_points];
        tc_mat4 m = tc_ed_ai_model_matrix(e);
        for (k = 0; k < w->count; k++) {
            tc_vec3 q = w->point[k], p = tc_v3(m.m[0] * q.x + m.m[4] * q.y + m.m[8] * q.z + m.m[12],
                                             m.m[1] * q.x + m.m[5] * q.y + m.m[9] * q.z + m.m[13],
                                             m.m[2] * q.x + m.m[6] * q.y + m.m[10] * q.z + m.m[14]);
            if (w->kind[k] < 1 || w->kind[k] > 6) continue;
            tc_mec_env_at(e->sectors, e->sector.sector, p, &env);
            a->zone[w->kind[k]] = env.water != 0;
        }
    }
    for (k = 1; k <= 6; k++) {
        if (a->zone[k] < 0) continue;
        if (combined < 0) combined = a->zone[k];
        else if (combined != a->zone[k]) mixed = true;
    }
    a->zone_combined = mixed ? 3 : combined >= 0 ? combined : a->zone[0];
}

/* Ed's sector (SectorInfo): the exe moves it on after each frame's mechanics (GAM_0042c810), from where he was to where
 * he is; here in the next step before anything reads it, which is the same for what the step sees. Looked up again
 * (tc_sector_place) after a teleport or respawn: the exe does so where it sets a perso's position (FUN_00451240 in
 * GAM_00429150 etc.). */
/* What Ed collides with (tc_ed_ai_step): his sector's solid row. Port safety net, not the exe's: where the sector the
 * floor below him belongs to (tc_sectors_find) is another, its row counts too, so a sector change the portal walk
 * misses (walking off a ledge under a portal's lower edge: Land's sctsectordebutA into sctsectorsecretroom01 below)
 * does not drop him through the floor he lands on. */
static void set_solid(tc_ed_ai *e) {
    tc_collision *w = e->player->world;
    const uint8_t *row = tc_sectors_solid(e->sectors, e->sector.sector);
    if (!w) return;
    if (row && e->solid_union) {
        uint16_t found = tc_sectors_find(e->sectors, e->player->position), k;
        const uint8_t *other = found != e->sector.sector ? tc_sectors_solid(e->sectors, found) : NULL;
        if (other) {
            for (k = 0; k < e->sectors->count; k++) e->solid_union[k] = row[k] | other[k];
            row = e->solid_union;
        }
    }
    w->solid = row;
}

static void track_sector(tc_ed_ai *e) {
    tc_sector_follow(&e->sector, e->sectors, e->player->position, e->player->placed);
    if (e->stepping) set_solid(e);
}

void tc_ed_ai_set_sectors(tc_ed_ai *e, const tc_sectors *s) {
    tc_free(e->solid_union);
    e->solid_union = s && s->count ? (uint8_t *)tc_calloc(s->count, 1) : NULL; /* NULL (out of memory): no safety net */
    e->sectors = s;
    e->sector.sector = TC_SECTOR_NONE;
    track_sector(e);
}

void tc_ed_ai_track_sector(tc_ed_ai *e) { track_sector(e); }

static void step(tc_ed_ai *e, float dt, uint32_t inputs, float stick_x, float stick_y, tc_vec3 camera) {
    tc_player *p = e->player;
    tc_ai_perso *a = &e->ai;
    const tc_ai_motion *m = &a->motion;
    const tc_ai_state *st;
    const tc_ai_card *card;

    e->time += dt;
    if (p->placed != e->placed_seen) {
        /* Port: placed from outside since the last step. A grounded Ed standing still does not move, so his contacts
         * stay (tc_mec_walk_step, dynamics +0x38C) and he stood on air where he was put. */
        tc_mec_walk_reset(&e->dyn.walk);
        e->dyn.contacts = 0;
    }
    if (a->place_requested) { /* another perso's SetPositionFrom (an arch's PROC 8, a passage's PROC 27): exactly there,
                               * off what he rode (AI_00488010); the host reveals after it */
        a->place_requested = false;
        tc_player_teleport(p, a->place_at, p->heading);
        tc_mec_walk_reset(&e->dyn.walk);
        e->dyn.contacts = 0;
        e->events.teleported = true;
    }
    run_triggers(e);
    run_portals(e, !e->cast_portals);
    a->screen_covered = e->screen_covered;
    if (e->auto_start && (e->start_step || a->normal.behaviour == a->model->initial_normal)) {
        if (!e->wait_start) inputs |= 1u << TC_AI_IN_JUMP;
    } else e->auto_start = false;
    a->inputs = inputs;
    a->stick_x = stick_x;
    a->stick_y = stick_y;
    a->position = p->position;
    a->velocity = p->velocity;
    a->heading = p->heading;
    a->contacts = e->dyn.contacts;
    /* report_seen's perso bits (COND 37) and the perso he rides (FUNC 54: with both bits, his carrier) */
    a->report_high = (e->dyn.walk.report | e->dyn.walk.report_prev) & TC_MEC_ON_PERSO;
    a->standing_on = a->report_high == TC_MEC_ON_PERSO && e->dyn.walk.carrier > 0 ? e->dyn.walk.carrier : 0;
    a->world = p->world;
    /* The material conditions read the collide flags of Ed's own part that made the contact (ContactRecord +0x70):
     * the walk's, which flight and swimming do not update */
    a->ground_material = e->dyn.card_type == 1 || e->dyn.card_type == 5 ? 0 : e->dyn.walk.ground_own;
    a->wall_material = e->dyn.card_type == 1 || e->dyn.card_type == 5 ? 0 : e->dyn.walk.wall_own;
    if (e->camera >= 0) a->other_positions[e->camera] = camera;
    /* dynamics +0x2BC / +0x2C0: the lagged attack / bank in flight, the lagged polar angle / heading swimming */
    a->report_2bc = e->dyn.card_type == 1 ? e->dyn.swim.polar_lagged : e->dyn.fly.attack_lagged;
    a->report_2c0 = e->dyn.card_type == 1 ? e->dyn.swim.heading_lagged : e->dyn.fly.bank_lagged;
    a->env_depth = e->dyn.env_depth;
    {
        tc_mat4 mm = tc_ed_ai_model_matrix(e);
        a->has_axes = true;
        a->axes[0] = tc_v3(mm.m[0], mm.m[1], mm.m[2]);
        a->axes[1] = tc_v3(mm.m[4], mm.m[5], mm.m[6]);
        a->axes[2] = tc_v3(mm.m[8], mm.m[9], mm.m[10]);
    }
    track_sector(e); /* after the portals: one may have moved him */
    water_zones(e);
    tc_ai_step(a, dt);
    if (e->start_step) { /* the takeover's next ForceBehaviour, a frame after the last (Proc9(1) ended its frame) */
        if (e->start_step < a->model->start_chain_count) tc_ai_force_behaviour(a, a->model->start_chain[e->start_step++]);
        else {
            tc_ai_force_behaviour(a, a->model->initial_normal);
            e->start_step = 0;
        }
    }
    footsteps(e);
    /* Port: dying (B73 -> B99: cover, HP refilled, then GameAction0, or ChangeLevel('death') without lives) restarts Ed
     * where the last save point was (tc_ai_globals.checkpoint) or at his start: his start behaviour and reflex (B63
     * reads his dsg134 = 1, the death's, and skips the arrival), then the reveal. The original reloads the level from
     * the save; the port keeps the level as it is, and has no game over screen yet. Ed's other ChangeLevel calls stay
     * unhandled, as before. */
    if (a->change_level >= 0) {
        const char *to = (uint32_t)a->change_level < a->model->string_count ? a->model->strings[a->change_level] : NULL;
        if (to && same_name(to, "death")) a->restart_requested = true;
        a->change_level = -1;
    }
    if (a->restart_requested) {
        const tc_ai_globals *g = a->globals ? a->globals : &a->own_globals;
        a->restart_requested = false;
        e->events.restarted = true;
        if (g->has_checkpoint) tc_player_teleport(p, g->checkpoint, g->checkpoint_heading);
        else tc_player_respawn(p);
        tc_mec_walk_reset(&e->dyn.walk);
        e->dyn.contacts = 0;
        if (a->hit_points <= 0) a->hit_points = a->hit_points_max;
        a->hidden_channels = 0;
        a->transparency = 255;
        tc_ai_force_behaviour(a, a->model->initial_normal);
        if (a->model->initial_reflex < a->model->reflex_count) tc_ai_force_reflex(a, a->model->initial_reflex);
        start_chain(e);
        e->restarts++;
        e->events.teleported = true; /* the host reveals */
    }
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
        tc_mec_walk_reset(&e->dyn.walk);
        e->dyn.contacts = 0;
        e->events.teleported = true;
    }

    st = &a->model->states[a->state];
    card = st->card != TC_AI_NONE16 ? &a->model->cards[st->card] : NULL;
    /* The mechanical environment at the feet (the integrators' lookup at the step's start, in Ed's sector: placed again
     * if a portal or SetPositionFrom moved him this step, as the exe sets the sector with the position) */
    track_sector(e);
    e->dyn.env_depth = tc_mec_env_at(e->sectors, e->sector.sector, p->position, &e->dyn.env);
    /* Port: the in-flight aim hover floats. Lateral held in flight (B45 script 11: his dsg35 = 1) holds him in state
     * 268 / 269 on card 31 (top speed 3, wing area 80) and turns his upper body upright (RotateChannelToward on channel
     * 3, not drawn by the port). At 3 u/s its lift is 92 % of his weight: by the exe's maths he sinks about 1 u/s and
     * leans 23 deg nose down (drawn head along the velocity). The user wants him floating: no climb or sink while it
     * holds, so he also stays level. */
    e->dyn.fly.hold_height = e->dyn.card_type == 5 && card && card->type == 5 &&
                             (int32_t)tc_ai_dsgvar_float(a, TC_ED_HOVER_DSG) == 1;
    { /* his mechanics, as every perso's (tc_mec_perso.c) */
        tc_mec_perso x;
        x.ai = a;
        x.body = p;
        x.dyn = &e->dyn;
        x.card = card;
        x.spheres = st->spheres != TC_AI_NONE16 ? &a->model->sphere_sets[st->spheres] : NULL;
        x.root = tc_mec_root_motion(a);
        x.ground = ground_per_frame(e, a->state);
        x.sectors = e->sectors;
        x.sector = e->sector.sector;
        if (e->mechanics) e->mechanics(e->mechanics_user, &x, dt);
        else tc_mec_perso_step(&x, dt);
    }
}

/* Ed collides with his sector's triangles and those of its collide list only (MEC_SweepPass_0043c830: the perso's
 * sector, then each sector of its +0x58 list), so skies, foliage and waterfalls (sud0's chute02, before the Canyon
 * cave) are not solid. Only while he moves: the camera and the other users of the world see everything. */
void tc_ed_ai_step(tc_ed_ai *e, float dt, uint32_t inputs, float stick_x, float stick_y, tc_vec3 camera) {
    tc_collision *w = e->player->world;
    /* Skipping a portal's walk-in: Ed's own B68 (the walk up to the arch and state 228, stepping in) skips to the cover
     * while Shoot is held or just released. Port choice: Action and Jump do it too, presented to his scripts as Shoot
     * while a portal has captured him and not yet fired. */
    if (((e->portal >= 0 && !e->portal_firing) || walking_in(e)) &&
        (inputs & ((1u << TC_AI_IN_ACTION) | (1u << TC_AI_IN_JUMP))))
        inputs |= 1u << TC_AI_IN_SHOOT;
    e->stepping = true;
    set_solid(e);
    step(e, dt, inputs, stick_x, stick_y, camera);
    /* report_seen as the exe has it right after the integrator (GAM_MecIntegrate_0042b0e0): what the persos that run
     * before him next tick (the run-first platforms) read of him through FUNC 54 / COND 37 */
    e->ai.report_high = (e->dyn.walk.report | e->dyn.walk.report_prev) & TC_MEC_ON_PERSO;
    e->ai.standing_on = e->ai.report_high == TC_MEC_ON_PERSO && e->dyn.walk.carrier > 0 ? e->dyn.walk.carrier : 0;
    e->placed_seen = e->player->placed; /* its own teleports reset as they go */
    if (w) w->solid = NULL;
    e->stepping = false;
}

uint16_t tc_ed_ai_anim(const tc_ed_ai *e) { return e->ai.model->states[e->ai.state].anim; }
float tc_ed_ai_frame(const tc_ed_ai *e) { return e->ai.state_frame; }
bool tc_ed_ai_loops(const tc_ed_ai *e) {
    const tc_ai_state *s = &e->ai.model->states[e->ai.state];
    return s->next >= 0 && (uint32_t)s->next == e->ai.state;
}

tc_mat4 tc_ed_ai_model_matrix(const tc_ed_ai *e) {
    const tc_mec_ski *s = &e->dyn.ski;
    tc_vec3 o;
    if (e->dyn.card_type == 5 || e->dyn.card_type == 1) {
        /* Flight / swimming: the model follows the frame (FUN_004c8dfc / 004c91d2), head first, about the origin */
        const tc_ai_state *st = &e->ai.model->states[e->ai.state];
        tc_vec3 x, y, z, p = e->player->position;
        if (st->card == TC_AI_NONE16) return tc_player_model_matrix(e->player);
        if (e->dyn.card_type == 5) tc_mec_fly_model_axes(&e->dyn.fly, &e->ai.model->cards[st->card], &x, &y, &z);
        else tc_mec_swim_model_axes(&e->dyn.swim, &e->ai.model->cards[st->card], &x, &y, &z);
        {
            tc_mat4 m = {{x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1}};
            return m;
        }
    }
    if (e->dyn.card_type != 2) return tc_player_model_matrix(e->player);
    /* With the collide set's sphere (BODY) the origin is where the exe has it, the board resting on the sphere: no lift.
     * Without it the origin sits on the snow and the pose is lifted by its drop. */
    o = e->ai.model->states[e->ai.state].spheres != TC_AI_NONE16
            ? e->player->position
            : tc_v3_add(e->player->position, tc_v3_scale(s->z, e->ai.model->states[e->ai.state].drop));
    {
        tc_mat4 m = {{s->x.x, s->x.y, s->x.z, 0, s->y.x, s->y.y, s->y.z, 0, s->z.x, s->z.y, s->z.z, 0, o.x, o.y, o.z, 1}};
        return m;
    }
}
