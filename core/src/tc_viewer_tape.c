/* The viewer's original demo tapes (tc_tape.h; docs/RECORDING.md "The original demos"). */
#include "tc_viewer_internal.h"

/* The tape object of cast entry `id`, or NULL. */
static const tc_tape_object *tape_object(const tc_viewer_tape *t, uint16_t id) {
    int32_t i = (int32_t)id + t->offset;
    return i >= 0 && (uint32_t)i < t->tape.object_count ? &t->tape.objects[i] : NULL;
}

/* Between the last two frames read: 0 the earlier, 1 the later. */
static float tape_alpha(const tc_viewer_tape *t) {
    double span = t->tape.time - t->tape.time_prev;
    double a = span > 0 ? (t->clock - t->tape.time_prev) / span : 1;
    return (float)(a < 0 ? 0 : a > 1 ? 1 : a);
}

static tc_vec3 tape_position(const tc_tape_object *o, float a) {
    return tc_v3_add(o->position_prev, tc_v3_scale(tc_v3_sub(o->position, o->position_prev), a));
}

static void tape_rotation(const tc_tape_object *o, float a, float r[9]) {
    float q[4], sign = 1, len = 0;
    int k;
    if (o->rotation[0] * o->rotation_prev[0] + o->rotation[1] * o->rotation_prev[1] + o->rotation[2] * o->rotation_prev[2] +
        o->rotation[3] * o->rotation_prev[3] < 0)
        sign = -1;
    for (k = 0; k < 4; k++) {
        q[k] = o->rotation_prev[k] + (sign * o->rotation[k] - o->rotation_prev[k]) * a;
        len += q[k] * q[k];
    }
    len = len > 1e-8f ? 1 / sqrtf(len) : 1;
    for (k = 0; k < 4; k++) q[k] *= len;
    tc_tape_rotation(q, r);
}

static tc_mat34 tape_matrix(const tc_tape_object *o, float a) {
    float r[9];
    tc_vec3 p = tape_position(o, a);
    tc_mat34 m;
    tape_rotation(o, a, r);
    m.m[0] = r[0], m.m[1] = r[1], m.m[2] = r[2], m.m[3] = p.x;
    m.m[4] = r[3], m.m[5] = r[4], m.m[6] = r[5], m.m[7] = p.y;
    m.m[8] = r[6], m.m[9] = r[7], m.m[10] = r[8], m.m[11] = p.z;
    return m;
}

static float tape_frame(const tc_tape_object *o, float a) { return o->frame_prev + (o->frame - o->frame_prev) * a; }

/* An actor's pose as the tape has it; false: not on show. */
bool tcv_tape_actor_pose(const tc_viewer *v, const tc_cast_actor *c, tc_mat34 *world, const tc_cast_anim **anim, tc_sample *s) {
    const tc_viewer_tape *t = v->tape;
    const tc_tape_object *o = tape_object(t, c->entry->id);
    const tc_ai_model *m = c->ai.model;
    const tc_cast_family *f = &v->level->cast.families[c->entry->family];
    const tc_ai_state *st;
    const tc_cast_anim *an;
    float a = tape_alpha(t), frame;
    uint32_t n, s0;
    if (!o || !o->shown || !m || o->state >= m->state_count) return false;
    st = &m->states[o->state];
    if (st->anim >= f->anim_count) return false;
    an = &f->anims[st->anim];
    n = an->frame_count;
    if (!n || !an->channel_count) return false;
    frame = tape_frame(o, a);
    s0 = frame > 0 ? (uint32_t)frame : 0;
    s->blend = frame - (float)s0;
    if (s0 >= n) { s0 = n - 1; s->blend = 0; }
    s->s0 = s0;
    s->s1 = s0 + 1 < n ? s0 + 1 : (st->next == (int16_t)o->state ? 0 : n - 1);
    *anim = an;
    *world = tape_matrix(o, a);
    return true;
}

/* The state animation's sounds this frame passed (the scripts' own, first == 0xFF, aren't the animation's to play). */
static void tape_sounds(tc_viewer *v, const tc_ai_model *m, const tc_tape_object *o) {
    const tc_ai_state *st;
    float from;
    uint32_t k;
    if (!m || o->state >= m->state_count) return;
    st = &m->states[o->state];
    from = o->state != o->state_prev || o->frame < o->frame_before ? -1.0f : o->frame_before;
    for (k = 0; k < st->sound_count && v->sound_count < TC_AI_MAX_SOUNDS; k++) {
        const tc_ai_sound *e = &m->sounds[st->sound_first + k];
        if (e->first != 0xFF && (float)e->frame > from && (float)e->frame <= o->frame) v->sounds[v->sound_count++] = e->event;
    }
}

/* What a frame just read brought: the sounds, the actors' sectors. */
static void tape_frame_read(tc_viewer *v) {
    tc_viewer_tape *t = v->tape;
    tc_viewer_play *p = v->play;
    const tc_tape_object *ed = &t->tape.objects[t->ed];
    uint32_t i;
    if (ed->shown) tape_sounds(v, &p->main.model, ed);
    for (i = 0; i < p->cast.count; i++) {
        tc_cast_actor *c = &p->cast.actors[i];
        const tc_tape_object *o = tape_object(t, c->entry->id);
        tc_vec3 d;
        if (!o || !o->shown) continue;
        if (v->sectors && c->sector.sector != TC_SECTOR_NONE) tc_sector_move(&c->sector, v->sectors, o->position);
        d = tc_v3_sub(o->position, v->camera.position);
        if (tc_v3_dot(d, d) <= TC_CAST_HEARING * TC_CAST_HEARING) tape_sounds(v, c->ai.model, o);
    }
}

void tcv_tape_update(tc_viewer *v, float dt, bool fresh) {
    tc_viewer_tape *t = v->tape;
    tc_viewer_play *p = v->play;
    const tc_tape_object *ed = &t->tape.objects[t->ed];
    float a, r[9];
    tc_vec3 at;
    if (!t->done) {
        t->clock += (double)dt * 1000;
        while (t->tape.time <= t->clock) {
            if (!tc_tape_next(&t->tape)) { t->done = true; break; }
            tape_frame_read(v);
        }
    }
    a = tape_alpha(t);

    /* Ed: where the rest of the viewer looks for him (his sector, the shadow, the lights) */
    at = tape_position(ed, a);
    {
        tc_vec3 d = tc_v3_sub(at, p->main.player.position);
        if (t->clip_state < 0 || tc_v3_dot(d, d) > 100) p->main.player.placed++; /* the start, or a teleport: his sector anew */
    }
    tape_rotation(ed, a, r);
    p->main.player.position = at;
    p->main.player.velocity = tc_v3(0, 0, 0);
    p->main.player.heading = atan2f(-r[1], r[4]); /* local -Y = the front, along (sin h, -cos h) */
    {
        tc_mat34 m = tape_matrix(ed, a);
        p->pose_draw = p->main.pose_prev = p->main.pose_cur = tc_mat4_from34(&m);
    }
    if (!ed->shown || ed->state >= p->main.model.state_count) {
        v->ed_clip = -1;
    } else {
        const tc_ai_state *st = &p->main.model.states[ed->state];
        if (t->clip_state != (int32_t)ed->state) {
            char name[16];
            int i;
            snprintf(name, sizeof(name), "s%u", (unsigned)st->anim);
            i = tcv_ed_anim(v->level, name);
            t->clip = i >= 0 ? i : tcv_ed_anim(v->level, "idle");
            t->clip_state = (int32_t)ed->state;
        }
        v->ed_clip = t->clip;
        v->ed_state = -1;
        v->ed_loop = st->next == (int16_t)ed->state;
        v->ed_phase = tape_frame(ed, a);
    }

    /* The camera: the perso's own, looking along its -Y as any perso faces (measured: Ed in the middle of its view) */
    if (t->camera >= 0) {
        const tc_tape_object *c = &t->tape.objects[t->camera];
        float q[9];
        tape_rotation(c, a, q);
        v->camera.position = tape_position(c, a);
        v->camera.yaw = atan2f(-q[1], -q[4]);
        v->camera.pitch = asinf(fmaxf(-1, fminf(1, -q[7])));
    } else {
        tcv_update_camera(v, dt, fresh);
    }
}

bool tc_viewer_play_tape(tc_viewer *v, const void *data, size_t size) {
    tc_viewer_play *p = v->play;
    const tc_level *l = v->level;
    tc_viewer_tape *t;
    tc_tape scan;
    uint8_t *first;
    int32_t *votes, best = 0, offset = 0, id;
    uint32_t i, e;
    if (v->tape || !p || !v->playing || !p->use_ai || !p->main.has_driver || !p->has_cast || l->edition != TC_EDITION_RETAIL) return false;
    t = tc_alloc(sizeof *t);
    if (!t) return false;
    memset(t, 0, sizeof *t);
    if (!tc_tape_open(&t->tape, data, size)) { tc_free(t); return false; }
    /* The exe's list has other persos before the level's, a different number in each level: the shift most of what first
     * shows up at its placement agrees on. A pass through a copy of the tape, to see who first shows up where. */
    votes = tc_alloc(2 * 4096 * sizeof *votes);
    first = t->tape.object_count ? tc_alloc(t->tape.object_count * sizeof *first) : NULL;
    if (votes && first && tc_tape_open(&scan, data, size)) {
        memset(votes, 0, 2 * 4096 * sizeof *votes);
        memset(first, 0, t->tape.object_count * sizeof *first);
        do {
            for (i = 0; i < scan.object_count; i++) {
                const tc_tape_object *o = &scan.objects[i];
                if (!o->known || first[i]) continue;
                first[i] = 1;
                for (e = 0; e < l->cast.count; e++) {
                    const float *w = l->cast.entries[e].world.m;
                    if (fabsf(w[3] - o->position.x) < 0.02f && fabsf(w[7] - o->position.y) < 0.02f && fabsf(w[11] - o->position.z) < 0.02f) {
                        int32_t s = (int32_t)i - (int32_t)l->cast.entries[e].id + 4096;
                        if (s >= 0 && s < 2 * 4096 && ++votes[s] > best) best = votes[s], offset = s - 4096;
                    }
                }
            }
        } while (tc_tape_next(&scan));
        tc_tape_close(&scan);
    }
    tc_free(votes);
    id = (int32_t)l->cast.main_actor + offset;
    if (best < 2 || id < 0 || (uint32_t)id >= t->tape.object_count || !first || !first[id]) {
        tc_free(first);
        tc_tape_close(&t->tape);
        tc_free(t);
        return false;
    }
    t->offset = offset;
    t->ed = id;
    t->camera = -1;
    t->clip_state = -1;
    t->clock = t->tape.time;
    if (p->main.driver.camera >= 0) {
        id = (int32_t)p->main.model.others[p->main.driver.camera].id + offset;
        if (id >= 0 && (uint32_t)id < t->tape.object_count && first[id]) t->camera = id;
    }
    tc_free(first);
    v->tape = t;
    return true;
}

bool tc_viewer_tape_done(const tc_viewer *v) { return v->tape && v->tape->done; }

void tcv_tape_free(tc_viewer *v) {
    if (!v->tape) return;
    tc_tape_close(&v->tape->tape);
    tc_free(v->tape);
    v->tape = NULL;
}
