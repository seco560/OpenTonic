#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_cast.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_player.h"
#include "tonic/tc_obstacle.h"
#include "tonic/tc_collision.h"

static tc_ai_perso *cast_find(void *user, int32_t id) {
    tc_cast *c = (tc_cast *)user;
    if (id == c->main_id || (c->level->cast.main_alias && id == c->level->cast.main_alias)) return c->main;
    if (id <= 0 || (uint32_t)id >= c->id_count || c->by_id[id] < 0) return NULL;
    if ((uint32_t)c->by_id[id] >= c->count) return &c->ghosts[c->by_id[id] - (int32_t)c->count].ai;
    return &c->actors[c->by_id[id]].ai;
}

/* The live persos one by one (the ZDE conditions): the active actors (not frozen), then the main actor. */
static tc_ai_perso *cast_each(void *user, uint32_t k) {
    tc_cast *c = (tc_cast *)user;
    uint32_t i;
    for (i = 0; i < c->count; i++) {
        tc_ai_perso *p = &c->actors[i].ai;
        if (!p->model || !p->active || p->frozen) continue;
        if (k-- == 0) return p;
    }
    return k == 0 ? c->main : NULL;
}

static tc_vec3 rotate_z(float heading, tc_vec3 v) {
    float co = cosf(heading), si = sinf(heading);
    return tc_v3(co * v.x - si * v.y, si * v.x + co * v.y, v.z);
}

static tc_result start_actor(tc_cast *c, tc_cast_actor *a);

/* Port: where the main actor's shots start, in his axes (front = -Y): at his mouth, below his head sphere (1.95 up). */
#define TC_CAST_SHOT_HEIGHT 1.7f
#define TC_CAST_SHOT_FRONT -0.5f

/* META 11's instancing (GAM_Always_004259a0): the template's actor (TC_CAST_SPAWN). While its last instance is still out,
 * that one again, where it is (the generator's +0x30 object: META_SpawnInState_0049f190 reuses it); else it starts
 * afresh at the generator's place and heading, in its sector. Port: one instance per template, and the launch vector
 * becomes its velocity too (its scripts read Velocity at once: the bee's B2 aims its wall probe along it; how the exe
 * makes it so, GAM_Always, is not read). */
static tc_ai_perso *cast_spawn(void *user, int32_t id, const tc_ai_perso *by, tc_vec3 launch) {
    tc_cast *c = (tc_cast *)user;
    tc_cast_actor *a;
    int32_t k;
    if (id <= 0 || (uint32_t)id >= c->id_count || (k = c->by_id[id]) < 0 || (uint32_t)k >= c->count) return NULL;
    a = &c->actors[k];
    if (!(a->entry->flags & TC_CAST_SPAWN) || !a->ai.model) return NULL;
    if (a->ai.active && !a->ai.frozen) return &a->ai;
    tc_ai_free(&a->ai);
    if (start_actor(c, a) != TC_OK) { memset(&a->ai, 0, sizeof(a->ai)); return NULL; }
    a->ai.active = true;
    a->ai.position = by->position;
    if (by == c->main) /* port: from his mouth (the exe's instance starts at his matrix, his feet; offset not found) */
        a->ai.position = tc_v3_add(by->position, rotate_z(by->heading, tc_v3(0, TC_CAST_SHOT_FRONT, TC_CAST_SHOT_HEIGHT)));
    a->ai.heading = by->heading;
    a->position_prev = a->ai.position;
    a->rot_prev = tc_v3(0, 0, by->heading);
    a->dyn.velocity = rotate_z(by->heading, launch);
    a->ai.velocity = a->dyn.velocity;
    a->sector.sector = c->main_sector;
    a->sector.position = a->ai.position;
    if (by != c->main) { /* another generator: its sector */
        uint32_t i;
        for (i = 0; i < c->count; i++)
            if (&c->actors[i].ai == by) { a->sector.sector = c->actors[i].sector.sector; break; }
    }
    c->spawned++;
    return &a->ai;
}

/* A family's states as tc_ai states (no mechanics, no root motion, no bodies: the cast does not move). */
static bool family_states(tc_cast *c, uint32_t k) {
    const tc_cast_family *f = &c->level->cast.families[k];
    uint32_t i, j, n = 0;
    tc_ai_state *st;
    for (i = 0; i < f->state_count; i++) n += f->states[i].sound_count;
    st = c->states[k] = (tc_ai_state *)tc_calloc(f->state_count ? f->state_count : 1, sizeof(tc_ai_state));
    c->sounds[k] = (tc_ai_sound *)tc_calloc(n ? n : 1, sizeof(tc_ai_sound));
    if (!st || !c->sounds[k]) return false;
    c->sound_count[k] = n;
    n = 0;
    for (i = 0; i < f->state_count; i++) {
        const tc_cast_state *s = &f->states[i];
        tc_ai_state *d = &st[i];
        d->next = s->next;
        d->frames = s->frames;
        d->speed = s->speed;
        d->flags = s->flags;
        d->repeats = s->repeats;
        d->anim = s->anim;
        d->card = d->spheres = d->water_points = TC_AI_NONE16;
        if (f->body) { /* CMEC's walker part: the family's ZDM bodies and root motion (the actors' models borrow them) */
            d->spheres = f->body[i];
            d->root_first = f->root_first[i];
            d->root_count = f->root_rows[i];
        }
        d->sound_first = n;
        d->sound_count = s->sound_count;
        for (j = 0; j < s->sound_count; j++, n++) {
            c->sounds[k][n].frame = s->sounds[j].frame;
            c->sounds[k][n].event = s->sounds[j].event;
            c->sounds[k][n].first = s->sounds[j].first;
            c->sounds[k][n].period = s->sounds[j].period;
        }
        if (s->transition_count) {
            d->transitions = (tc_ai_transition *)tc_calloc(s->transition_count, sizeof(tc_ai_transition));
            if (!d->transitions) return false;
            for (j = 0; j < s->transition_count; j++) {
                d->transitions[j].target = s->transitions[2 * j];
                d->transitions[j].via = s->transitions[2 * j + 1];
            }
            d->transition_count = s->transition_count;
        }
        d->prohibited = s->prohibited; /* the level's, borrowed */
        d->prohibited_count = s->prohibited_count;
    }
    return true;
}

static void detach(tc_ai_model *m) { /* the borrowed family arrays, before tc_ai_model_free */
    memset(&m->zde, 0, sizeof(m->zde));
    m->states = NULL;
    m->state_count = 0;
    m->root = NULL;
    m->root_count = 0;
    m->sphere_sets = NULL;
    m->sphere_set_count = 0;
    m->sounds = NULL;
    m->sound_count = 0;
}

/* The AI's start (tc_ai_init) with the cast's links and the entry's std game. */
static tc_result start_actor(tc_cast *c, tc_cast_actor *a) {
    const tc_cast_entry *e = a->entry;
    tc_ai_perso *p = &a->ai;
    tc_result r = tc_ai_init(p, &a->model);
    if (r != TC_OK) return r;
    p->find = cast_find;
    p->find_user = c;
    p->main_actor = c->main_id;
    p->self_id = e->id;
    p->globals = &c->globals;
    p->level_name = c->main ? c->main->level_name : NULL; /* LevelIs (COND 62): the level, as for the main actor */
    p->world_id = c->level->cast.world;
    p->ways = c->level->ways.waypoint_count ? &c->level->ways : NULL;
    p->own_ways = tc_ways_of(&c->level->ways, e->id);
    p->each = cast_each;
    p->spawn = cast_spawn;
    p->family_type = e->family_type;
    p->active = e->active != 0 && !(e->flags & TC_CAST_SPAWN); /* a template: out until instanced (cast_spawn) */
    p->dead = e->dead != 0;
    p->reinit = e->reinit != 0;
    p->keep_off_main = !(e->flags & TC_CAST_MAY_FORCE_MAIN);
    p->zones_of = e;
    p->brisk = (e->flags & TC_CAST_TALK) != 0;
    p->removal_guard = !(e->flags & (TC_CAST_SCENE | TC_CAST_SPAWN | TC_CAST_TALK)); /* a scene's actors, the spawned: wherever Ed is */
    p->run_mechanics = e->mechanics;
    p->run_animation = e->animation;
    p->radius2 = e->radius2;
    p->hit_points = e->hit_points;
    p->hit_points_max = p->hit_points_max_init = e->hit_points_max;
    p->position = tc_v3(e->world.m[3], e->world.m[7], e->world.m[11]);
    p->heading = a->heading0;
    c->rng = c->rng * 1103515245u + 12345u;
    p->rng = c->rng;
    p->time_ms = c->main ? c->main->time_ms : p->time_ms;
    a->state_prev = p->state;
    a->frame_prev = p->state_frame;
    a->position_prev = p->position;
    a->rot_prev = tc_v3(0, 0, p->heading);
    memset(&a->dyn, 0, sizeof(a->dyn));
    a->dyn.card_type = 0xFFFF;
    tc_mec_walk_reset(&a->dyn.walk);
    a->sector.sector = e->sector;
    a->sector.position = p->position;
    a->sector.placed = 0;
    return TC_OK;
}

/* The main actor's links the cast gives him (tc_cast_init). */
static void link_main(tc_cast *c, tc_ai_perso *main) {
    const tc_level *l = c->level;
    main->find = cast_find;
    main->find_user = c;
    main->self_id = c->main_id;
    main->globals = &c->globals;
    main->world_id = l->cast.world;
    main->ways = l->ways.waypoint_count ? &l->ways : NULL;
    main->own_ways = tc_ways_of(&l->ways, c->main_id);
    main->each = cast_each;
    main->spawn = cast_spawn;
    if (l->cast.main_zones.zone_count) {
        main->zones_of = &l->cast.main_zones;
        main->zones_follow = true;
    }
}

void tc_cast_detach_main(tc_cast *c) {
    uint32_t i;
    if (!c || !c->main) return;
    for (i = 0; i < c->ghost_count; i++) {
        tc_cast_ghost_perso *g = &c->ghosts[i];
        uint8_t *own;
        if (g->main_other < 0) continue;
        /* his copy goes with him: the ghost keeps the values in memory of its own (the layout stays borrowed) */
        own = (uint8_t *)tc_alloc(g->model.dsg.size ? g->model.dsg.size : 1);
        if (own && g->ai.dsg) memcpy(own, g->ai.dsg, g->model.dsg.size);
        else if (own) memset(own, 0, g->model.dsg.size ? g->model.dsg.size : 1);
        g->ai.dsg = own;
        g->main_other = TC_CAST_GHOST_OWN_MEMORY;
    }
    if (c->main->find_user == c) {
        c->main->find = NULL;
        c->main->find_user = NULL;
        c->main->globals = NULL;
        c->main->ways = NULL;
        c->main->own_ways = NULL;
        c->main->each = NULL;
        c->main->spawn = NULL;
        c->main->zones_of = NULL;
        c->main->zones_follow = false;
    }
    c->main = NULL;
}

void tc_cast_set_main(tc_cast *c, tc_ai_perso *main) {
    uint32_t i, k;
    if (!c) return;
    if (c->main && c->main != main) tc_cast_detach_main(c);
    c->main = main;
    if (!main) return;
    link_main(c, main);
    /* the ghosts he keeps a copy of (the camera) share his copy again, as tc_cast_init sets them up */
    for (i = 0; i < c->ghost_count; i++) {
        tc_cast_ghost_perso *g = &c->ghosts[i];
        if (g->main_other != TC_CAST_GHOST_OWN_MEMORY || !main->model) continue;
        for (k = 0; k < main->model->other_count; k++) {
            const tc_ai_other *o = &main->model->others[k];
            if (o->id != g->ai.self_id || !main->other_dsg) continue;
            tc_free(g->ai.dsg);
            g->ai.dsg = main->other_dsg[k];
            g->model.dsg = o->dsg;
            g->ai.position = main->other_positions[k];
            g->ai.read_only_for_cast = o->role == 1;
            g->main_other = (int32_t)k;
            break;
        }
    }
}

tc_result tc_cast_init(tc_cast *c, const tc_level *l, tc_ai_perso *main) {
    const tc_cast_data *d = &l->cast;
    uint32_t i, n = 0;
    memset(c, 0, sizeof(*c));
    c->globals.powers = TC_AI_POWERS_ALL; /* port: every power from the start (tc_ai.h) */
    c->level = l;
    c->main = main;
    c->main_id = d->main_actor;
    c->rng = 0xCA57u;
    c->main_carrier = -1;
    if (main) link_main(c, main);
    if (!d->count) return TC_OK;
    c->states = (tc_ai_state **)tc_calloc(d->family_count ? d->family_count : 1, sizeof(*c->states));
    c->sounds = (tc_ai_sound **)tc_calloc(d->family_count ? d->family_count : 1, sizeof(*c->sounds));
    c->sound_count = (uint32_t *)tc_calloc(d->family_count ? d->family_count : 1, sizeof(uint32_t));
    c->actors = (tc_cast_actor *)tc_calloc(d->count, sizeof(tc_cast_actor));
    c->by_prso = (int32_t *)tc_calloc(l->perso_count ? l->perso_count : 1, sizeof(int32_t));
    for (i = 0; i < d->count; i++) if (d->entries[i].id + 1u > c->id_count) c->id_count = d->entries[i].id + 1u;
    for (i = 0; i < d->ghost_count; i++) if (d->ghosts[i].id + 1u > c->id_count) c->id_count = d->ghosts[i].id + 1u;
    c->ghosts = (tc_cast_ghost_perso *)tc_calloc(d->ghost_count ? d->ghost_count : 1, sizeof(tc_cast_ghost_perso));
    if (!c->ghosts) goto oom;
    c->by_id = (int32_t *)tc_calloc(c->id_count ? c->id_count : 1, sizeof(int32_t));
    if (!c->states || !c->sounds || !c->sound_count || !c->actors || !c->by_prso || !c->by_id) goto oom;
    for (i = 0; i < c->id_count; i++) c->by_id[i] = -1;
    for (i = 0; i < l->perso_count; i++) c->by_prso[i] = -1;
    for (i = 0; i < d->family_count; i++)
        if (!family_states(c, i)) goto oom;
    for (i = 0; i < d->count; i++) {
        const tc_cast_entry *e = &d->entries[i];
        const tc_cast_family *f;
        tc_cast_actor *a = &c->actors[n];
        tc_result r;
        if (!e->size || e->family >= d->family_count) continue;
        f = &d->families[e->family];
        r = tc_ai_model_load_scripts(&a->model, e->scripts, e->size);
        if (r == TC_ERR_OUT_OF_MEMORY) goto oom;
        if (r != TC_OK) continue; /* unreadable scripts: left out */
        if (e->tables_size && (r = tc_ai_model_load_tables(&a->model, e->tables, e->tables_size)) != TC_OK) {
            tc_ai_model_free(&a->model);
            if (r == TC_ERR_OUT_OF_MEMORY) goto oom;
            continue;
        }
        a->model.states = c->states[e->family];
        a->model.root = c->level->cast.families[e->family].root;
        a->model.root_count = c->level->cast.families[e->family].root_count;
        a->model.sphere_sets = c->level->cast.families[e->family].bodies;
        a->model.zde = c->level->cast.families[e->family].zde;
        a->model.sphere_set_count = c->level->cast.families[e->family].body_count;
        a->model.state_count = f->state_count;
        a->model.initial_state = e->initial_state < f->state_count ? e->initial_state : 0;
        a->model.sounds = c->sounds[e->family];
        a->model.sound_count = c->sound_count[e->family];
        a->entry = e;
        a->heading0 = tc_heading_of(&e->world);
        if ((r = start_actor(c, a)) != TC_OK) {
            detach(&a->model);
            tc_ai_model_free(&a->model);
            if (r == TC_ERR_OUT_OF_MEMORY) goto oom;
            continue;
        }
        c->by_id[e->id] = (int32_t)n;
        if (e->prso < l->perso_count) c->by_prso[e->prso] = (int32_t)n;
        n++;
    }
    c->count = n;
    for (i = 0; i < d->ghost_count; i++) { /* after the actors: their ids map past count */
        const struct tc_cast_ghost *g = &d->ghosts[i];
        tc_cast_ghost_perso *gp = &c->ghosts[c->ghost_count];
        tc_result r;
        uint32_t k;
        gp->main_other = -1;
        /* one the main actor keeps a copy of (the camera: his driver feeds its live eye there; the skier's fix game
         * perso): share his copy, so the cast and he see the same */
        for (k = 0; main && main->model && k < main->model->other_count; k++)
            if (main->model->others[k].id == g->id) gp->main_other = (int32_t)k;
        r = tc_ai_model_load_dsg(&gp->model, g->dsg, g->size);
        if (r == TC_ERR_OUT_OF_MEMORY) goto oom;
        if (r != TC_OK || (r = tc_ai_init(&gp->ai, &gp->model)) != TC_OK) {
            tc_ai_model_free(&gp->model);
            if (r == TC_ERR_OUT_OF_MEMORY) goto oom;
            continue;
        }
        gp->ai.position = g->position;
        if (gp->main_other >= 0) { /* his memory and layout instead of its own */
            const tc_ai_other *o = &main->model->others[gp->main_other];
            tc_free(gp->ai.dsg);
            gp->ai.dsg = main->other_dsg[gp->main_other];
            tc_ai_model_free(&gp->model);
            gp->model.dsg = o->dsg;  /* borrowed: detached before the free */
            gp->ai.position = main->other_positions[gp->main_other];
            gp->ai.read_only_for_cast = o->role == 1; /* the camera */
        }
        gp->ai.self_id = g->id;
        gp->ai.main_actor = c->main_id;
        gp->ai.world_id = d->world;
        gp->ai.find = cast_find;
        gp->ai.find_user = c;
        if (g->id < c->id_count && c->by_id[g->id] < 0) c->by_id[g->id] = (int32_t)(c->count + c->ghost_count);
        c->ghost_count++;
    }
    return TC_OK;
oom:
    c->count = n;
    tc_cast_free(c);
    return TC_ERR_OUT_OF_MEMORY;
}

void tc_cast_free(tc_cast *c) {
    uint32_t i, k;
    if (!c) return;
    for (i = 0; c->actors && i < c->count; i++) {
        tc_ai_free(&c->actors[i].ai);
        detach(&c->actors[i].model);
        tc_ai_model_free(&c->actors[i].model);
    }
    for (i = 0; c->ghosts && i < c->ghost_count; i++) {
        if (c->ghosts[i].main_other >= 0) { /* borrowed from the main actor */
            c->ghosts[i].ai.dsg = NULL;
            memset(&c->ghosts[i].model.dsg, 0, sizeof(c->ghosts[i].model.dsg));
        } else if (c->ghosts[i].main_other == TC_CAST_GHOST_OWN_MEMORY) { /* its memory, his model's layout */
            memset(&c->ghosts[i].model.dsg, 0, sizeof(c->ghosts[i].model.dsg));
        }
        tc_ai_free(&c->ghosts[i].ai);
        tc_ai_model_free(&c->ghosts[i].model);
    }
    tc_free(c->ghosts);
    for (i = 0; c->states && i < c->level->cast.family_count; i++) {
        if (!c->states[i]) continue;
        for (k = 0; k < c->level->cast.families[i].state_count; k++) tc_free(c->states[i][k].transitions);
        tc_free(c->states[i]);
    }
    for (i = 0; c->sounds && i < c->level->cast.family_count; i++) tc_free(c->sounds[i]);
    if (c->main && c->main->find_user == c) {
        c->main->find = NULL;
        c->main->find_user = NULL;
        c->main->globals = NULL;
        c->main->ways = NULL;
        c->main->own_ways = NULL;
        c->main->each = NULL;
        c->main->spawn = NULL;
        c->main->zones_of = NULL;
        c->main->zones_follow = false;
    }
    tc_free(c->states);
    tc_free(c->sounds);
    tc_free(c->sound_count);
    tc_free(c->actors);
    tc_free(c->by_id);
    tc_free(c->by_prso);
    memset(c, 0, sizeof(*c));
}

/* GAM_0042a8d0 from GAM_PlayAnim_Update_00415c00, before the AI: a playing animation with root motion this frame writes its
 * direction and length into the proposal ([7..9], [10]). The go-to meta-actions (Pursuit, RunAway, GoToWP ...) drop the
 * direction and keep that speed, so a perso chases at its run cycle's pace: the scripts set no speed of their own. A zero
 * row leaves the proposal as it was. */
static void root_to_proposal(tc_ai_perso *p) {
    const tc_ai_state *st = &p->model->states[p->state];
    float *pr = p->motion.proposal, len;
    tc_vec3 row;
    if (!st->root_count) return;
    row = p->model->root[st->root_first + (p->state_frame > 0 ? (uint32_t)p->state_frame % st->root_count : 0)];
    row = tc_v3_scale(row, (float)p->frame_rate); /* units per animation frame -> per second, as ground_wanted */
    if ((len = tc_v3_length(row)) == 0) return;
    pr[7] = row.x / len, pr[8] = row.y / len, pr[9] = row.z / len;
    pr[10] = len;
}

/* One perso's frame (GAM_00428070 for a marked perso): awake when the main character's sector or its activity list
 * (+0x64) holds it (GAM_00428490), or it is on the engine's always-awake list (0x8c172c, marked every frame by the
 * main loop at 0x428a8f); then its AI within the activation radius, its animation (docs/ENGINE.md). */
static void step_actor(tc_cast *c, tc_cast_actor *a, float dt, const tc_sectors *s, uint16_t main_sector,
                       tc_vec3 main_pos) {
    uint32_t k;
    {
        tc_ai_perso *p = &a->ai;
        bool think, animate, was_active;
        float d2;
        a->state_prev = p->state;
        a->frame_prev = p->state_frame;
        a->position_prev = p->position;
        a->rot_prev = a->dyn.tilted ? a->dyn.rot : tc_v3(0, 0, p->heading);
        a->thought = false;
        if (p->reinit_requested) { /* PROC 257 on a perso that comes back (std game +0x20 = 1): from its start, out */
            tc_ai_free(p);
            if (start_actor(c, a) != TC_OK) { memset(p, 0, sizeof(*p)); return; }
            p->active = false;
        }
        if (!p->model || !p->active) { a->awake = false; return; }
        a->awake = (a->entry->flags & TC_CAST_ALWAYS_AWAKE) || !s || main_sector == TC_SECTOR_NONE ||
                   a->sector.sector == TC_SECTOR_NONE || tc_sectors_active(s, main_sector, a->sector.sector);
        if (!a->awake) return;
        c->awake++;
        d2 = tc_v3_length_sq(tc_v3_sub(p->position, main_pos));
        p->main_distance2 = d2;
        p->time_ms = c->main ? c->main->time_ms : p->time_ms + (uint32_t)(dt * 1000 + 0.5f);
        if (d2 <= TC_CAST_NEAR * TC_CAST_NEAR) p->near_main_ms = p->time_ms ? p->time_ms : 1;
        p->world = c->world; /* FUNC 162's probe: the static world */
        think = (p->radius2 < 0 || p->radius2 > d2) && !p->frozen; /* GAM_PersoFrame_0042b520; frozen: a prop */
        if ((a->entry->flags & TC_CAST_TALK) && !a->talking) { /* port: its scene waits for him */
            float dx = p->position.x - main_pos.x, dy = p->position.y - main_pos.y;
            a->talking = c->talk_press && dx * dx + dy * dy <= TC_CAST_TALK_REACH * TC_CAST_TALK_REACH;
            think = think && a->talking;
        }
        animate = p->run_mechanics || p->run_animation;
        was_active = p->active && !p->frozen;
        if (p->run_mechanics && p->model->state_count) root_to_proposal(p);
        tc_ai_step_parts(p, dt, think, animate);
        /* saved: here (it took itself out, or the removal guard kept it: a save point's radius may exceed TC_CAST_NEAR) */
        if (was_active && (!p->active || p->frozen) && (a->entry->flags & TC_CAST_SAVE_POINT) && c->main) {
            c->globals.has_checkpoint = true;
            c->globals.checkpoint = c->main->position;
            c->globals.checkpoint_heading = c->main->heading;
        }
        if (a->entry->flags & TC_CAST_TALK) { /* its scene's way out is the main actor's: the cover, then the level */
            if (p->cover_requested) p->cover_requested = false, c->talk_cover = true;
            if (c->main) p->screen_covered = c->main->screen_covered;
            if (p->change_level >= 0) {
                if ((uint32_t)p->change_level < p->model->string_count) c->talk_level = p->model->strings[p->change_level];
                p->change_level = -1;
            }
        }
        a->thought = think;
        c->thinking += think;
        if (p->run_mechanics && !tc_cast_mechanics(c, a, dt)) { /* port: turning in place stands in for the others */
            if (p->motion.heading_set) p->heading = p->motion.heading;
            else if (p->motion.turning) p->heading += p->motion.turn_speed * dt;
        }
        if (s && a->sector.sector != TC_SECTOR_NONE && (p->position.x != a->sector.position.x ||
                                                       p->position.y != a->sector.position.y ||
                                                       p->position.z != a->sector.position.z))
            tc_sector_move(&a->sector, s, p->position); /* GAM_0042c810: through the portals it crossed */
        if (d2 <= TC_CAST_HEARING * TC_CAST_HEARING)
            for (k = 0; k < p->sound_count; k++) {
                uint16_t e = p->sounds[k];
                if (c->sounds_out_count >= TC_CAST_SOUNDS_PER_TICK ||
                    (e == a->last_sound && p->time_ms - a->last_sound_ms < TC_CAST_SOUND_GAP_MS)) {
                    c->sounds_dropped++;
                    continue;
                }
                a->last_sound = e;
                a->last_sound_ms = p->time_ms;
                c->sounds_out[c->sounds_out_count++] = e;
            }
    }
}

/* Its obstacle (tc_obstacle.h) where it is now: its state animation's current frame at its matrix; not met while it is
 * out or dead (FUN_0043c640) or not drawn. */
static void pose_obstacle(tc_cast *c, tc_cast_actor *a) {
    tc_obstacle *ob = a->obstacle;
    tc_mat34 w;
    const tc_cast_anim *an;
    tc_sample sm;
    if (!ob) return;
    if (!tc_cast_pose(c, a, 1, &w, &an, &sm)) {
        ob->live = false;
        return;
    }
    ob->live = a->ai.active && !a->ai.dead;
    ob->sector = a->sector.sector;
    tc_obstacle_pose(ob, &w, &an->transforms[(size_t)sm.s0 * an->channel_count], &an->shown[(size_t)sm.s0 * an->channel_count],
                     an->channel_count);
}

static void begin_step(tc_cast *c) {
    uint32_t i;
    c->sounds_out_count = 0;
    c->awake = c->thinking = 0;
    for (i = 0; i < c->ghost_count; i++) /* the shared ones: where the main actor's host says they are (the camera's eye) */
        if (c->ghosts[i].main_other >= 0 && c->main && c->main->other_positions)
            c->ghosts[i].ai.position = c->main->other_positions[c->ghosts[i].main_other];
}

static void step_pass(tc_cast *c, bool first, float dt, const tc_sectors *s, uint16_t main_sector) {
    uint32_t i;
    c->sectors = s;
    tc_vec3 main_pos = c->main ? c->main->position : tc_v3(0, 0, 0);
    for (i = 0; i < c->count; i++)
        if (((c->actors[i].entry->flags & TC_CAST_RUN_FIRST) != 0) == first) {
            step_actor(c, &c->actors[i], dt, s, main_sector, main_pos);
            pose_obstacle(c, &c->actors[i]);
        }
}

/* The exe's order (docs/ENGINE.md "The frame"): the run-first list (0x8c1738, GAM_004287b0: the platforms, lifts,
 * crates) before the dynamic world's walk (GAM_00428550). Port: within each pass in level perso order (the exe walks
 * the list's order, then the tree's). tc_viewer steps the main actor between the two (tc_cast_step_first, his step,
 * tc_cast_step_rest), so what he stands on has moved first; tc_cast_step runs both passes. */
void tc_cast_step_first(tc_cast *c, float dt, const tc_sectors *s, uint16_t main_sector) {
    c->main_sector = main_sector;
    begin_step(c);
    step_pass(c, true, dt, s, main_sector);
}

const tc_mat34 *tc_cast_sector_matrix(const tc_cast *c, uint16_t sector) {
    uint32_t i;
    for (i = 0; i < c->moving_sector_count; i++)
        if (c->moving_sectors[i].sector == sector) return &c->moving_sectors[i].world;
    return NULL;
}

/* The exe's table of moving sectors: this tick's orders (tc_ai_globals.sector_orders), then each record's step, as the
 * frame does after the persos (GAM_00429d00, from 0x428a69's step 7). Port: the sector's static triangles follow
 * (tc_collision_move_sector); the persos standing in it do not yet (GAM_004299e0 moves those of its character list that
 * are flagged or stand on it). */
static void move_sectors(tc_cast *c, float dt) {
    tc_ai_globals *g = &c->globals;
    float ms = dt * 1000;
    uint32_t i, k;
    for (i = 0; i < g->sector_order_count; i++) {
        const tc_ai_sector_order *o = &g->sector_orders[i];
        tc_cast_moving_sector *m = NULL;
        for (k = 0; k < c->moving_sector_count; k++)
            if (c->moving_sectors[k].sector == o->sector) m = &c->moving_sectors[k];
        if (!m) { /* GAM_0042a000 / GAM_00429ef0: a new record, while there is room */
            static const tc_mat34 identity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
            if (c->moving_sector_count >= TC_CAST_MAX_MOVING_SECTORS) continue;
            m = &c->moving_sectors[c->moving_sector_count++];
            memset(m, 0, sizeof *m);
            m->sector = o->sector;
            m->world = identity;
        }
        if (o->kind == TC_AI_SECTOR_TURN) { /* GAM_0042a100: the axis and pivot, then GAM_00429e80 */
            m->axis = o->a;
            m->pivot = o->b;
            m->angle_target = m->angle + o->angle;
            m->rate = o->ms ? o->angle / (float)o->ms : o->angle;
            m->turning = true;
        } else { /* GAM_0042a350 (to a place) / GAM_0042a270 (by a vector) -> GAM_0042a1c0 */
            tc_vec3 at = tc_v3(m->world.m[3], m->world.m[7], m->world.m[11]);
            tc_vec3 by = o->kind == TC_AI_SECTOR_TO ? tc_v3_sub(o->a, at) : o->a;
            m->target = tc_v3_add(at, by);
            m->velocity = o->ms ? tc_v3_scale(by, 1.0f / (float)o->ms) : by; /* DAT_004d2248 = 1; no time: the whole of it */
            m->moving = true;
        }
    }
    g->sector_order_count = 0;
    for (i = 0; i < c->moving_sector_count; i++) {
        tc_cast_moving_sector *m = &c->moving_sectors[i];
        bool changed = false;
        if (m->turning) { /* this frame's share of the angle, the last one what is left */
            float left = m->angle_target - m->angle, step = ms * m->rate;
            tc_vec3 a = m->axis;
            float len = tc_v3_length(a);
            if (left < 0 ? step <= left : left <= step) { step = left; m->turning = false; }
            m->angle += step;
            if (len > 1e-6f && step != 0) { /* x -> R (x - pivot) + pivot, before the matrix it has (GAM_00429b40) */
                float co = cosf(step), si = sinf(step), t = 1 - co;
                tc_mat34 r, w = m->world;
                a = tc_v3_scale(a, 1 / len);
                r.m[0] = t * a.x * a.x + co;       r.m[1] = t * a.x * a.y - si * a.z; r.m[2] = t * a.x * a.z + si * a.y;
                r.m[4] = t * a.x * a.y + si * a.z; r.m[5] = t * a.y * a.y + co;       r.m[6] = t * a.y * a.z - si * a.x;
                r.m[8] = t * a.x * a.z - si * a.y; r.m[9] = t * a.y * a.z + si * a.x; r.m[10] = t * a.z * a.z + co;
                r.m[3] = r.m[7] = r.m[11] = 0;
                {
                    tc_vec3 back = tc_m34_vector(&r, m->pivot);
                    r.m[3] = m->pivot.x - back.x; r.m[7] = m->pivot.y - back.y; r.m[11] = m->pivot.z - back.z;
                }
                m->world = tc_m34_mul(&r, &w);
                changed = true;
            }
        }
        if (m->moving) { /* towards the target while it is still ahead, then on it */
            tc_vec3 at = tc_v3(m->world.m[3], m->world.m[7], m->world.m[11]), left = tc_v3_sub(m->target, at), step = left;
            if (tc_v3_dot(m->velocity, left) > 0) step = tc_v3_scale(m->velocity, ms);
            else m->moving = false;
            m->world.m[3] += step.x; m->world.m[7] += step.y; m->world.m[11] += step.z;
            changed = true;
        }
        if (changed && c->world) tc_collision_move_sector(c->world, m->sector, &m->world);
    }
}

void tc_cast_step_rest(tc_cast *c, float dt, const tc_sectors *s, uint16_t main_sector) {
    step_pass(c, false, dt, s, main_sector);
    move_sectors(c, dt);
}

void tc_cast_step(tc_cast *c, float dt, const tc_sectors *s, uint16_t main_sector) {
    tc_cast_step_first(c, dt, s, main_sector);
    tc_cast_step_rest(c, dt, s, main_sector);
}

void tc_cast_set_obstacles(tc_cast *c, struct tc_obstacles *o) {
    uint32_t i;
    c->obstacles = o;
    for (i = 0; i < c->count; i++) {
        tc_cast_actor *a = &c->actors[i];
        a->obstacle = o ? tc_obstacles_find(o, a->entry->id) : NULL;
        if (a->obstacle && !(a->obstacle->flags & TC_PCOL_CAST)) a->obstacle = NULL; /* posed once, by the export */
        pose_obstacle(c, a);
    }
}

bool tc_cast_position(const tc_cast *c, int32_t id, tc_vec3 *out) {
    tc_ai_perso *q = c && c->by_id ? cast_find((void *)c, id) : (c && id == c->main_id ? c->main : NULL);
    if (!q) return false;
    *out = q->position;
    return true;
}

bool tc_cast_placed_position(const tc_cast *c, int32_t id, tc_vec3 *out) {
    uint32_t i;
    for (i = 0; c && i < c->level->cast.ghost_count; i++)
        if (c->level->cast.ghosts[i].id == id) { *out = c->level->cast.ghosts[i].position; return true; }
    for (i = 0; c && i < c->level->cast.count; i++)
        if (c->level->cast.entries[i].id == id) {
            const tc_mat34 *w = &c->level->cast.entries[i].world;
            *out = tc_v3(w->m[3], w->m[7], w->m[11]);
            return true;
        }
    return false;
}

const tc_cast_actor *tc_cast_actor_of(const tc_cast *c, uint32_t prso) {
    if (!c || !c->by_prso || prso >= c->level->perso_count || c->by_prso[prso] < 0) return NULL;
    return &c->actors[c->by_prso[prso]];
}

bool tc_cast_pose(const tc_cast *c, const tc_cast_actor *a, float t, tc_mat34 *world, const tc_cast_anim **anim,
                  tc_sample *s) {
    const tc_ai_perso *p = &a->ai;
    const tc_cast_family *f;
    const tc_ai_state *st;
    const tc_cast_anim *an;
    float frame, d, cs, sn;
    uint32_t n, s0;
    int r;
    if (!p->model || !p->active || !p->model->state_count) return false;
    f = &c->level->cast.families[a->entry->family];
    st = &p->model->states[p->state];
    if (st->anim >= f->anim_count) return false;
    an = &f->anims[st->anim];
    n = an->frame_count;
    if (!n || !an->channel_count) return false;
    frame = p->state_frame;
    if (a->state_prev == p->state && p->state_frame >= a->frame_prev) frame = a->frame_prev + (p->state_frame - a->frame_prev) * t;
    s0 = frame > 0 ? (uint32_t)frame : 0;
    s->blend = frame - (float)s0;
    if (s0 >= n) { s0 = n - 1; s->blend = 0; }
    s->s0 = s0;
    s->s1 = s0 + 1 < n ? s0 + 1 : (st->next == (int16_t)p->state ? 0 : n - 1);
    *anim = an;
    /* The placement turned about Z by the heading's change (local -Y = the front, along (sin h, -cos h)), where its
     * mechanics put it (between the last two ticks); tilted ones (card type 7 under a rider) by their whole rotation
     * relative to the placement's heading. */
    *world = a->entry->world;
    {
        tc_vec3 rot = a->dyn.tilted ? a->dyn.rot : tc_v3(0, 0, p->heading), r0 = a->rot_prev, pos;
        tc_vec3 axis;
        float angle, m3[9];
        int k;
        rot = tc_v3_add(r0, tc_v3_scale(tc_v3_sub(rot, r0), t));
        pos = tc_v3_add(a->position_prev, tc_v3_scale(tc_v3_sub(p->position, a->position_prev), t));
        rot.z -= a->heading0;
        angle = tc_v3_length(rot);
        if (angle > 1e-7f) {
            axis = tc_v3_scale(rot, 1 / angle);
            cs = cosf(angle), sn = sinf(angle), d = 1 - cs;
            m3[0] = cs + axis.x * axis.x * d;          m3[1] = axis.x * axis.y * d - axis.z * sn; m3[2] = axis.x * axis.z * d + axis.y * sn;
            m3[3] = axis.y * axis.x * d + axis.z * sn; m3[4] = cs + axis.y * axis.y * d;          m3[5] = axis.y * axis.z * d - axis.x * sn;
            m3[6] = axis.z * axis.x * d - axis.y * sn; m3[7] = axis.z * axis.y * d + axis.x * sn; m3[8] = cs + axis.z * axis.z * d;
            for (r = 0; r < 3; r++)
                for (k = 0; k < 3; k++)
                    world->m[4 * r + k] = m3[3 * r] * a->entry->world.m[k] + m3[3 * r + 1] * a->entry->world.m[4 + k] +
                                          m3[3 * r + 2] * a->entry->world.m[8 + k];
        }
        world->m[3] = pos.x;
        world->m[7] = pos.y;
        world->m[11] = pos.z;
    }
    return true;
}

void tc_cast_respawn(tc_cast *c) {
    uint32_t i;
    for (i = 0; i < c->count; i++) {
        tc_cast_actor *a = &c->actors[i];
        /* out, or kept frozen by the removal guard (a removal with the main actor further than TC_CAST_NEAR) */
        if (a->entry->prso != 0xFFFF || !a->entry->active || (a->ai.active && !a->ai.frozen) || !a->ai.model) continue;
        tc_ai_free(&a->ai);
        if (start_actor(c, a) != TC_OK) memset(&a->ai, 0, sizeof(a->ai));
    }
}

void tc_cast_unknown(const tc_cast *c, uint32_t *out) {
    uint32_t i, k;
    memset(out, 0, 10 * TC_AI_UNKNOWN_STRIDE * sizeof(uint32_t));
    for (i = 0; i < c->count; i++)
        if (c->actors[i].ai.unknown)
            for (k = 0; k < 10 * TC_AI_UNKNOWN_STRIDE; k++) out[k] += c->actors[i].ai.unknown[k];
}
