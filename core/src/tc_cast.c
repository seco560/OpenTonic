#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_cast.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_player.h"

static tc_ai_perso *cast_find(void *user, int32_t id) {
    tc_cast *c = (tc_cast *)user;
    if (id == c->main_id || (c->level->cast.main_alias && id == c->level->cast.main_alias)) return c->main;
    if (id <= 0 || (uint32_t)id >= c->id_count || c->by_id[id] < 0) return NULL;
    if ((uint32_t)c->by_id[id] >= c->count) return &c->ghosts[c->by_id[id] - (int32_t)c->count].ai;
    return &c->actors[c->by_id[id]].ai;
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
    m->states = NULL;
    m->state_count = 0;
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
    p->active = e->active != 0;
    p->dead = e->dead != 0;
    p->reinit = e->reinit != 0;
    p->keep_off_main = !(e->flags & TC_CAST_MAY_FORCE_MAIN);
    p->zones_of = e;
    p->removal_guard = true;
    p->run_mechanics = e->mechanics;
    p->run_animation = e->animation;
    p->radius2 = e->radius2;
    p->hit_points = e->hit_points;
    p->hit_points_max = e->hit_points_max;
    p->position = tc_v3(e->world.m[3], e->world.m[7], e->world.m[11]);
    p->heading = a->heading0;
    c->rng = c->rng * 1103515245u + 12345u;
    p->rng = c->rng;
    p->time_ms = c->main ? c->main->time_ms : p->time_ms;
    a->state_prev = p->state;
    a->frame_prev = p->state_frame;
    return TC_OK;
}

tc_result tc_cast_init(tc_cast *c, const tc_level *l, tc_ai_perso *main) {
    const tc_cast_data *d = &l->cast;
    uint32_t i, n = 0;
    memset(c, 0, sizeof(*c));
    c->level = l;
    c->main = main;
    c->main_id = d->main_actor;
    c->rng = 0xCA57u;
    if (main) {
        main->find = cast_find;
        main->find_user = c;
        main->self_id = c->main_id;
        main->globals = &c->globals;
        if (l->cast.main_zones.zone_count) {
            main->zones_of = &l->cast.main_zones;
            main->zones_follow = true;
        }
    }
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
        a->model.states = c->states[e->family];
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
        a->thought = false;
        if (p->reinit_requested) { /* PROC 257 on a perso that comes back (std game +0x20 = 1): from its start, out */
            tc_ai_free(p);
            if (start_actor(c, a) != TC_OK) { memset(p, 0, sizeof(*p)); return; }
            p->active = false;
        }
        if (!p->model || !p->active) { a->awake = false; return; }
        a->awake = (a->entry->flags & TC_CAST_ALWAYS_AWAKE) || !s || main_sector == TC_SECTOR_NONE ||
                   a->entry->sector == TC_SECTOR_NONE || tc_sectors_active(s, main_sector, a->entry->sector);
        if (!a->awake) return;
        c->awake++;
        d2 = tc_v3_length_sq(tc_v3_sub(p->position, main_pos));
        p->main_distance2 = d2;
        p->time_ms = c->main ? c->main->time_ms : p->time_ms + (uint32_t)(dt * 1000 + 0.5f);
        if (d2 <= TC_CAST_NEAR * TC_CAST_NEAR) p->near_main_ms = p->time_ms ? p->time_ms : 1;
        think = (p->radius2 < 0 || p->radius2 > d2) && !p->frozen; /* GAM_PersoFrame_0042b520; frozen: a prop */
        animate = p->run_mechanics || p->run_animation;
        was_active = p->active && !p->frozen;
        tc_ai_step_parts(p, dt, think, animate);
        /* saved: here (it took itself out, or the removal guard kept it: a save point's radius may exceed TC_CAST_NEAR) */
        if (was_active && (!p->active || p->frozen) && (a->entry->flags & TC_CAST_SAVE_POINT) && c->main) {
            c->globals.has_checkpoint = true;
            c->globals.checkpoint = c->main->position;
            c->globals.checkpoint_heading = c->main->heading;
        }
        a->thought = think;
        c->thinking += think;
        if (p->run_mechanics) { /* port: turning in place stands in for the mechanics */
            if (p->motion.heading_set) p->heading = p->motion.heading;
            else if (p->motion.turning) p->heading += p->motion.turn_speed * dt;
        }
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

/* The exe's order (docs/ENGINE.md "The frame"): the run-first list (0x8c1738, GAM_004287b0: the platforms, lifts,
 * crates) before the dynamic world's walk (GAM_00428550). Port: within each pass in level perso order (the exe walks
 * the list's order, then the tree's); the main actor stepped before both (tc_viewer). */
void tc_cast_step(tc_cast *c, float dt, const tc_sectors *s, uint16_t main_sector) {
    uint32_t i, pass;
    tc_vec3 main_pos = c->main ? c->main->position : tc_v3(0, 0, 0);
    c->sounds_out_count = 0;
    c->awake = c->thinking = 0;
    for (i = 0; i < c->ghost_count; i++) /* the shared ones: where the main actor's host says they are (the camera's eye) */
        if (c->ghosts[i].main_other >= 0 && c->main && c->main->other_positions)
            c->ghosts[i].ai.position = c->main->other_positions[c->ghosts[i].main_other];
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < c->count; i++)
            if (((c->actors[i].entry->flags & TC_CAST_RUN_FIRST) != 0) == (pass == 0))
                step_actor(c, &c->actors[i], dt, s, main_sector, main_pos);
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
    /* The placement turned about Z by the heading's change (local -Y = the front, along (sin h, -cos h)). */
    *world = a->entry->world;
    d = p->heading - a->heading0;
    if (d != 0) {
        cs = cosf(d), sn = sinf(d);
        for (r = 0; r < 3; r++) {
            float x = a->entry->world.m[r], y = a->entry->world.m[4 + r];
            world->m[r] = cs * x - sn * y;
            world->m[4 + r] = sn * x + cs * y;
        }
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
