#include "tc_internal.h"
#include "tonic/tc_ed_ai.h"

tc_result tc_ed_ai_init(tc_ed_ai *e, const tc_ai_model *model, tc_player *player, const char *level_name) {
    tc_result r = tc_ai_init(&e->ai, model);
    uint32_t i;
    e->player = player;
    e->camera = -1;
    e->run_scale = 2.0f;  /* run (state 10) 4.5 -> 9.0 u/s: a little under the old controller's 9.9 (user) */
    e->walk_scale = 1;    /* walk (state 1) 3.5 u/s, already close to the old 3.3 */
    if (r != TC_OK) return r;
    e->ai.level_name = level_name;
    for (i = 0; i < model->other_count; i++)
        if (model->others[i].role == 1) e->camera = (int)i;
    return TC_OK;
}

void tc_ed_ai_free(tc_ed_ai *e) { tc_ai_free(&e->ai); }

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

void tc_ed_ai_step(tc_ed_ai *e, float dt, uint32_t inputs, float stick_x, float stick_y, tc_vec3 camera) {
    tc_player *p = e->player;
    tc_ai_perso *a = &e->ai;
    const tc_ai_motion *m = &a->motion;
    tc_vec2 target;

    a->inputs = inputs;
    a->stick_x = stick_x;
    a->stick_y = stick_y;
    a->position = p->position;
    a->velocity = p->velocity;
    a->heading = p->heading;
    a->contacts = (p->grounded ? TC_AI_CONTACT_GROUND : 0) | (p->against_wall ? TC_AI_CONTACT_WALL : 0);
    if (e->camera >= 0) a->other_positions[e->camera] = camera;
    tc_ai_step(a, dt);

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
}

uint16_t tc_ed_ai_anim(const tc_ed_ai *e) { return e->ai.model->states[e->ai.state].anim; }
float tc_ed_ai_frame(const tc_ed_ai *e) { return e->ai.state_frame; }
bool tc_ed_ai_loops(const tc_ed_ai *e) {
    const tc_ai_state *s = &e->ai.model->states[e->ai.state];
    return s->next >= 0 && (uint32_t)s->next == e->ai.state;
}
