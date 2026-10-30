#include "tc_internal.h"
#include "tc_reader.h"
#include "tonic/tc_ai.h"
#include "tonic/tc_collision.h"
#include <ctype.h>

/* ---- loader ---- */

static void rd_dsg_def(reader *r, tc_ai_dsg_def *d) {
    uint32_t i;
    d->count = rd_u16(r);
    (void)rd_u16(r);
    d->size = rd_u32(r);
    d->vars = (tc_ai_dsgvar *)rd_array(r, d->count, sizeof(tc_ai_dsgvar), 8);
    for (i = 0; d->vars && i < d->count; i++) {
        d->vars[i].offset = rd_u32(r);
        d->vars[i].type = rd_u8(r);
        r->offset += have(r, 3) ? 3 : 0;
    }
    d->initial = (uint8_t *)rd_array(r, d->size, 1, 1);
    if (d->initial && have(r, d->size)) {
        memcpy(d->initial, r->data + r->offset, d->size);
        r->offset += d->size;
    }
    r->offset += have(r, (4 - d->size % 4) % 4) ? (4 - d->size % 4) % 4 : 0;
}

float tc_ai_card_f32(const tc_ai_card *c, uint32_t offset) {
    float f;
    if (!c || offset < 4 || offset % 4 || offset / 4 - 1 >= c->word_count) return 0;
    memcpy(&f, &c->words[offset / 4 - 1], 4);
    return f;
}

static void free_dsg_def(tc_ai_dsg_def *d) { tc_free(d->vars); tc_free(d->initial); }

static tc_vec3 rd_vec3(reader *r) {
    tc_vec3 v;
    v.x = rd_f32(r);
    v.y = rd_f32(r);
    v.z = rd_f32(r);
    return v;
}

/* Chunks (tools/ttre/ai.py): SCPT, STAT, BTNS, STRS, VECS, PRSO, CARD, BODY, ZDMF, TRIG, PORT, PLAC, TRAN, SNDS, WPTS,
 * MECE, ZONE, FSND, LINK; all required (the water is the .ttlv's: SECT). Each is read to its exact end (chunk_end). */
#define CHUNK_END(r) do { if ((e = chunk_end(r)) != TC_OK) goto fail; } while (0)

static void rd_portals(reader *r, tc_ai_model *m) {
    uint32_t i, k;
    m->portal_count = rd_u16(r);
    (void)rd_u16(r);
    m->portals = (tc_ai_portal *)rd_array(r, m->portal_count, sizeof(tc_ai_portal), 64);
    for (i = 0; m->portals && i < m->portal_count; i++) {
        tc_ai_portal *p = &m->portals[i];
        uint32_t n;
        p->position = rd_vec3(r);
        p->radius = rd_f32(r);
        for (k = 0; k < 9; k++) p->rows[k] = rd_f32(r);
        p->perso = rd_u16(r);
        p->fire = rd_u8(r);
        (void)rd_u8(r);
        p->fire_dsg = rd_u16(r);
        (void)rd_u16(r);
        p->fire_value = rd_i32(r);
        p->capture_count = rd_u16(r);
        p->fire_count = rd_u16(r);
        n = (uint32_t)p->capture_count + p->fire_count;
        p->ops = (tc_ai_portal_op *)rd_array(r, n, sizeof(tc_ai_portal_op), 24);
        for (k = 0; p->ops && k < n; k++) {
            tc_ai_portal_op *o = &p->ops[k];
            o->op = rd_u8(r);
            o->kind = rd_u8(r);
            o->a = rd_u16(r);
            o->b = rd_u32(r);
            o->c = rd_u32(r);
            o->v = rd_vec3(r);
        }
    }
    m->name_count = rd_u16(r);
    m->names = (char **)rd_array(r, m->name_count, sizeof(char *), 2);
    for (i = 0; m->names && i < m->name_count; i++) m->names[i] = rd_string(r);
    for (i = 0; !r->error && m->portals && i < m->portal_count; i++) {
        const tc_ai_portal *p = &m->portals[i];
        for (k = 0; k < (uint32_t)p->capture_count + p->fire_count; k++) {
            const tc_ai_portal_op *o = &p->ops[k];
            if (o->op == TC_AI_POP_LEVEL &&
                (o->b >= m->name_count || (o->c != TC_AI_NONE && (o->c >= m->name_count || o->a >= m->name_count))))
                r->error = TC_ERR_TRUNCATED;
        }
    }
}

static void rd_transitions(reader *r, tc_ai_model *m) {
    uint32_t i, k;
    m->transitions.cover_threshold = rd_u16(r);
    m->transitions.cover_range = rd_u16(r);
    m->transitions.reveal_threshold = rd_u16(r);
    m->transitions.reveal_range = rd_u16(r);
    for (i = 0; i < 4; i++) {
        tc_ai_effect *e = &m->transitions.effects[i];
        e->variant_count = rd_u16(r);
        (void)rd_u16(r);
        for (k = 0; k < 9; k++) e->fields[k] = rd_f32(r);
        e->variants = (char **)rd_array(r, e->variant_count, sizeof(char *), 2);
        for (k = 0; e->variants && k < e->variant_count; k++) e->variants[k] = rd_string(r);
    }
    m->has_transitions = !r->error;
}

static void free_strings(char **s, uint32_t n) {
    uint32_t i;
    for (i = 0; s && i < n; i++) tc_free(s[i]);
    tc_free(s);
}

/* The SCPT chunk: nodes, scripts, behaviours, the initial behaviours and the perso's dsgvars. */
static void rd_scpt(reader *r, tc_ai_model *m) {
    uint32_t i, k;
    m->node_count = rd_u32(r);
    m->nodes = (tc_ai_node *)rd_array(r, m->node_count, sizeof(tc_ai_node), 8);
    for (i = 0; m->nodes && i < m->node_count; i++) {
        m->nodes[i].param = rd_u32(r);
        m->nodes[i].type = rd_u8(r);
        m->nodes[i].depth = rd_u8(r);
        (void)rd_u16(r);
    }
    m->script_count = rd_u32(r);
    m->scripts = (uint32_t *)rd_array(r, m->script_count, 4, 4);
    for (i = 0; m->scripts && i < m->script_count; i++) {
        m->scripts[i] = rd_u32(r);
        if (m->scripts[i] >= m->node_count) r->error = TC_ERR_TRUNCATED;
    }
    m->normal_count = rd_u16(r);
    m->reflex_count = rd_u16(r);
    k = (uint32_t)m->normal_count + m->reflex_count;
    m->behaviours = (tc_ai_behaviour *)rd_array(r, k, sizeof(tc_ai_behaviour), 8);
    for (i = 0; m->behaviours && i < k; i++) {
        tc_ai_behaviour *b = &m->behaviours[i];
        b->schedule = rd_i32(r);
        b->first_script = rd_u16(r);
        b->script_count = rd_u16(r);
        if ((uint32_t)b->first_script + b->script_count > m->script_count ||
            (b->schedule >= 0 && (uint32_t)b->schedule >= m->script_count))
            r->error = TC_ERR_TRUNCATED;
        if (b->script_count > m->max_rules) m->max_rules = b->script_count;
    }
    m->initial_normal = rd_u16(r);
    m->initial_reflex = rd_u16(r);
    rd_dsg_def(r, &m->dsg);
}

tc_result tc_ai_model_load(tc_ai_model *m, const uint8_t *data, size_t size) {
    chunks cs;
    reader rr, *r = &rr;
    uint32_t i, k;
    tc_result e;
    memset(m, 0, sizeof(*m));
    if ((e = chunks_open(&cs, data, size, "TTAI")) != TC_OK) return e;
    chunk_stamp(&cs, m->converter, sizeof m->converter);

    chunk_need(&cs, "SCPT", r);
    rd_scpt(r, m);
    CHUNK_END(r);

    chunk_need(&cs, "STAT", r);
    m->state_count = rd_u16(r);
    m->initial_state = rd_u16(r);
    m->states = (tc_ai_state *)rd_array(r, m->state_count, sizeof(tc_ai_state), 24);
    for (i = 0; m->states && i < m->state_count; i++) {
        tc_ai_state *s = &m->states[i];
        s->next = rd_i16(r);
        s->frames = rd_u16(r);
        s->speed = rd_u8(r);
        s->flags = rd_u8(r);
        s->transition_count = rd_u16(r);
        s->prohibited_count = rd_u16(r);
        s->repeats = rd_u8(r);
        (void)rd_u8(r);
        s->anim = rd_u16(r);
        s->card = rd_u16(r);
        s->ground = rd_f32(r);
        s->drop = rd_f32(r);
        s->transitions = (tc_ai_transition *)rd_array(r, s->transition_count, sizeof(tc_ai_transition), 4);
        for (k = 0; s->transitions && k < s->transition_count; k++) {
            s->transitions[k].target = rd_u16(r);
            s->transitions[k].via = rd_u16(r);
        }
        s->prohibited = (uint16_t *)rd_array(r, s->prohibited_count, 2, 2);
        for (k = 0; s->prohibited && k < s->prohibited_count; k++) s->prohibited[k] = rd_u16(r);
    }
    CHUNK_END(r);

    chunk_need(&cs, "BTNS", r);
    m->button_count = rd_u16(r);
    (void)rd_u16(r);
    m->buttons = (tc_ai_button *)rd_array(r, m->button_count, sizeof(tc_ai_button), 8);
    for (i = 0; m->buttons && i < m->button_count; i++) {
        m->buttons[i].inputs = rd_u16(r);
        m->buttons[i].analog = rd_u8(r);
        (void)rd_u8(r);
        m->buttons[i].lo = rd_i16(r);
        m->buttons[i].hi = rd_i16(r);
    }
    CHUNK_END(r);

    chunk_need(&cs, "STRS", r);
    m->string_count = rd_u16(r);
    m->strings = (char **)rd_array(r, m->string_count, sizeof(char *), 2);
    for (i = 0; m->strings && i < m->string_count; i++) m->strings[i] = rd_string(r);
    CHUNK_END(r);

    chunk_need(&cs, "VECS", r);
    m->vector_count = rd_u16(r);
    m->vectors = (tc_vec3 *)rd_array(r, m->vector_count, sizeof(tc_vec3), 12);
    for (i = 0; m->vectors && i < m->vector_count; i++) m->vectors[i] = rd_vec3(r);
    CHUNK_END(r);

    chunk_need(&cs, "PRSO", r);
    m->other_count = rd_u16(r);
    (void)rd_u16(r);
    m->others = (tc_ai_other *)rd_array(r, m->other_count, sizeof(tc_ai_other), 12);
    for (i = 0; m->others && i < m->other_count; i++) {
        m->others[i].id = rd_u16(r);
        m->others[i].role = rd_u8(r);
        (void)rd_u8(r);
        rd_dsg_def(r, &m->others[i].dsg);
    }
    CHUNK_END(r);

    chunk_need(&cs, "CARD", r);
    m->card_count = rd_u16(r);
    (void)rd_u16(r);
    m->cards = (tc_ai_card *)rd_array(r, m->card_count, sizeof(tc_ai_card), 8);
    for (i = 0; m->cards && i < m->card_count; i++) {
        tc_ai_card *c = &m->cards[i];
        c->type = rd_u32(r);
        c->word_count = rd_u32(r);
        if (c->word_count > 19) { r->error = TC_ERR_TRUNCATED; break; }
        for (k = 0; k < c->word_count; k++) c->words[k] = rd_u32(r);
    }
    CHUNK_END(r);

    chunk_need(&cs, "BODY", r);
    m->root_count = rd_u32(r);
    m->root = (tc_vec3 *)rd_array(r, m->root_count, sizeof(tc_vec3), 12);
    for (i = 0; m->root && i < m->root_count; i++) m->root[i] = rd_vec3(r);
    for (i = 0; m->states && i < m->state_count; i++) {
        tc_ai_state *s = &m->states[i];
        s->root_first = rd_u32(r);
        s->root_count = rd_u16(r);
        s->spheres = rd_u16(r);
        if ((uint64_t)s->root_first + s->root_count > m->root_count) r->error = TC_ERR_TRUNCATED;
    }
    m->sphere_set_count = rd_u16(r);
    (void)rd_u16(r);
    m->sphere_sets = (tc_ai_sphere_set *)rd_array(r, m->sphere_set_count, sizeof(tc_ai_sphere_set), 4);
    for (i = 0; m->sphere_sets && i < m->sphere_set_count; i++) {
        tc_ai_sphere_set *z = &m->sphere_sets[i];
        z->count = rd_u16(r);
        (void)rd_u16(r);
        if (z->count > TC_AI_MAX_SPHERES) { r->error = TC_ERR_TRUNCATED; break; }
        for (k = 0; k < z->count; k++) {
            z->spheres[k].center = rd_vec3(r);
            z->spheres[k].radius = rd_f32(r);
        }
    }
    CHUNK_END(r);

    chunk_need(&cs, "ZDMF", r);
    if (rd_u16(r) != m->sphere_set_count) r->error = TC_ERR_TRUNCATED;
    for (i = 0; !r->error && m->sphere_sets && i < m->sphere_set_count; i++) {
        tc_ai_sphere_set *z = &m->sphere_sets[i];
        if (rd_u16(r) != z->count) r->error = TC_ERR_TRUNCATED;
        for (k = 0; k < z->count; k++) {
            z->spheres[k].flags = rd_u16(r);
            z->spheres[k].point = rd_u16(r) == 5; /* the collide element type: 7 sphere, 5 point */
            z->spheres[k].rebound = rd_f32(r);
            z->spheres[k].grip = rd_f32(r);
        }
    }
    CHUNK_END(r);

    chunk_need(&cs, "TRIG", r);
    m->trigger_count = rd_u16(r);
    (void)rd_u16(r);
    m->triggers = (tc_ai_trigger *)rd_array(r, m->trigger_count, sizeof(tc_ai_trigger), 24);
    for (i = 0; m->triggers && i < m->trigger_count; i++) {
        tc_ai_trigger *t = &m->triggers[i];
        t->position = rd_vec3(r);
        t->radius = rd_f32(r);
        t->behaviour = rd_u16(r);
        t->guard = rd_u16(r);
        t->sleep_ms = rd_u32(r);
        if (t->behaviour >= m->normal_count) r->error = TC_ERR_TRUNCATED;
    }
    CHUNK_END(r);

    chunk_need(&cs, "PORT", r);
    rd_portals(r, m);
    CHUNK_END(r);

    chunk_need(&cs, "PLAC", r);
    m->place_count = rd_u16(r);
    (void)rd_u16(r);
    m->places = (tc_ai_place *)rd_array(r, m->place_count, sizeof(tc_ai_place), 18);
    for (i = 0; m->places && i < m->place_count; i++) {
        m->places[i].name = rd_string(r);
        m->places[i].position = rd_vec3(r);
        m->places[i].heading = rd_f32(r);
    }
    CHUNK_END(r);

    chunk_need(&cs, "TRAN", r);
    rd_transitions(r, m);
    CHUNK_END(r);

    chunk_need(&cs, "SNDS", r);
    m->sound_count = rd_u32(r);
    m->sounds = (tc_ai_sound *)rd_array(r, m->sound_count, sizeof(tc_ai_sound), 8);
    for (i = 0; m->sounds && i < m->sound_count; i++) {
        m->sounds[i].frame = rd_u16(r);
        m->sounds[i].event = rd_u16(r);
        m->sounds[i].first = rd_u8(r);
        m->sounds[i].period = rd_u8(r);
        (void)rd_u16(r);
    }
    for (i = 0; m->states && i < m->state_count; i++) {
        m->states[i].sound_first = rd_u32(r);
        m->states[i].sound_count = rd_u16(r);
        (void)rd_u16(r);
        if ((uint64_t)m->states[i].sound_first + m->states[i].sound_count > m->sound_count) r->error = TC_ERR_TRUNCATED;
    }
    CHUNK_END(r);

    chunk_need(&cs, "MECE", r);
    m->mech_event_count = rd_u32(r);
    m->mech_events = (tc_ai_mech_event *)rd_array(r, m->mech_event_count, sizeof(tc_ai_mech_event), 8);
    for (i = 0; m->mech_events && i < m->mech_event_count; i++) {
        m->mech_events[i].frame = rd_u16(r);
        m->mech_events[i].mode = rd_u16(r);
        m->mech_events[i].first = rd_u8(r);
        m->mech_events[i].period = rd_u8(r);
        (void)rd_u16(r);
    }
    for (i = 0; m->states && i < m->state_count; i++) {
        m->states[i].mech_first = rd_u32(r);
        m->states[i].mech_count = rd_u16(r);
        (void)rd_u16(r);
        if ((uint64_t)m->states[i].mech_first + m->states[i].mech_count > m->mech_event_count) r->error = TC_ERR_TRUNCATED;
    }
    CHUNK_END(r);

    chunk_need(&cs, "WPTS", r);
    m->water_point_set_count = rd_u16(r);
    (void)rd_u16(r);
    m->water_point_sets = (tc_ai_water_points *)rd_array(r, m->water_point_set_count, sizeof(tc_ai_water_points), 4);
    for (i = 0; m->water_point_sets && i < m->water_point_set_count; i++) {
        tc_ai_water_points *w = &m->water_point_sets[i];
        uint8_t n = (uint8_t)rd_u16(r);
        (void)rd_u16(r);
        for (k = 0; k < n; k++) {
            uint8_t kind = (uint8_t)rd_u32(r);
            tc_vec3 v = rd_vec3(r);
            if (w->count < TC_AI_MAX_WATER_POINTS) w->kind[w->count] = kind, w->point[w->count++] = v;
        }
    }
    for (i = 0; m->states && i < m->state_count; i++) {
        m->states[i].water_points = rd_u16(r);
        if (m->states[i].water_points != TC_AI_NONE16 && m->states[i].water_points >= m->water_point_set_count)
            r->error = TC_ERR_TRUNCATED;
    }
    CHUNK_END(r);

    chunk_need(&cs, "ZONE", r);
    {
        uint16_t n = rd_u16(r);
        (void)rd_u16(r);
        if (n != m->portal_count) r->error = TC_ERR_TRUNCATED;
        for (i = 0; !r->error && m->portals && i < n; i++) {
            tc_ai_portal *p = &m->portals[i];
            uint16_t prims = rd_u16(r);
            (void)rd_u16(r);
            if (!prims) continue;
            for (k = 0; k < 12; k++) p->zone_inverse[k] = rd_f32(r);
            for (k = 0; k < prims; k++) {
                tc_ai_zone_prim z;
                z.kind = rd_u32(r);
                z.a = rd_vec3(r);
                z.b = rd_vec3(r);
                if (p->zone_count < TC_AI_MAX_ZONE_PRIMS) p->zone[p->zone_count++] = z;
            }
        }
    }
    CHUNK_END(r);

    chunk_need(&cs, "FSND", r);
    m->floor_sound_count = rd_u32(r);
    m->floor_sounds = (uint8_t *)rd_array(r, m->floor_sound_count, 1, 1);
    for (i = 0; m->floor_sounds && i < m->floor_sound_count; i++) m->floor_sounds[i] = rd_u8(r);
    CHUNK_END(r);

    chunk_need(&cs, "LINK", r);
    m->link_triangles = rd_u32(r);
    m->link_crc = rd_u32(r);
    CHUNK_END(r);

    for (i = 0; m->states && i < m->state_count; i++) {
        if ((m->states[i].card != TC_AI_NONE16 && m->states[i].card >= m->card_count) ||
            (m->states[i].spheres != TC_AI_NONE16 && m->states[i].spheres >= m->sphere_set_count)) {
            e = TC_ERR_TRUNCATED;
            goto fail;
        }
    }
    if (m->node_count && m->nodes[m->node_count - 1].type != TC_AI_END) { e = TC_ERR_TRUNCATED; goto fail; }
    return TC_OK;

fail:
    tc_ai_model_free(m);
    return e;
}
#undef CHUNK_END

tc_result tc_ai_model_load_scripts(tc_ai_model *m, const uint8_t *data, size_t size) {
    reader r;
    tc_result e;
    memset(m, 0, sizeof(*m));
    r.data = data;
    r.size = size;
    r.offset = 0;
    r.error = TC_OK;
    rd_scpt(&r, m);
    if ((e = chunk_end(&r)) == TC_OK && (!m->node_count || m->nodes[m->node_count - 1].type != TC_AI_END ||
                                         !m->normal_count))
        e = TC_ERR_TRUNCATED;
    if (e != TC_OK) tc_ai_model_free(m);
    return e;
}

void tc_ai_model_link(tc_ai_model *m, const tc_level *level) {
    if (m->link_triangles == level->collision_triangle_count && m->link_crc == level->collision_crc) return;
    tc_free(m->floor_sounds);
    m->floor_sounds = NULL;
    m->floor_sound_count = 0;
}

void tc_ai_model_free(tc_ai_model *m) {
    uint32_t i;
    for (i = 0; m->states && i < m->state_count; i++) {
        tc_free(m->states[i].transitions);
        tc_free(m->states[i].prohibited);
    }
    for (i = 0; m->strings && i < m->string_count; i++) tc_free(m->strings[i]);
    for (i = 0; m->others && i < m->other_count; i++) free_dsg_def(&m->others[i].dsg);
    tc_free(m->cards);
    tc_free(m->root);
    tc_free(m->sphere_sets);
    tc_free(m->triggers);
    tc_free(m->sounds);
    tc_free(m->mech_events);
    tc_free(m->water_point_sets);
    tc_free(m->floor_sounds);
    for (i = 0; m->portals && i < m->portal_count; i++) tc_free(m->portals[i].ops);
    tc_free(m->portals);
    free_strings(m->names, m->name_count);
    for (i = 0; m->places && i < m->place_count; i++) tc_free(m->places[i].name);
    tc_free(m->places);
    for (i = 0; i < 4; i++) free_strings(m->transitions.effects[i].variants, m->transitions.effects[i].variant_count);
    tc_free(m->nodes);
    tc_free(m->scripts);
    tc_free(m->behaviours);
    free_dsg_def(&m->dsg);
    tc_free(m->states);
    tc_free(m->buttons);
    tc_free(m->strings);
    tc_free(m->vectors);
    tc_free(m->others);
    memset(m, 0, sizeof(*m));
}

/* ---- values ---- */

enum { V_NONE, V_INT, V_FLOAT, V_VEC, V_PERSO, V_REF };
typedef struct value { int type; int32_t i; float f; tc_vec3 v; } value;

/* Statement result flags, as the exe's (AI_0048e970 / AI_0048eab0). */
#define F_STOP 0x1u      /* stop this intelligence for the frame (meta-action running, Proc9) */
#define F_ABORT 0x2u     /* stop this script (a refused ChangeState) */
#define F_META_DONE 0x4u
#define F_RESTART 0x8u   /* this intelligence changed behaviour: start the new one now */
#define F_END 0x10u

static value v_int(int32_t i) { value v; memset(&v, 0, sizeof(v)); v.type = V_INT; v.i = i; return v; }
static value v_float(float f) { value v; memset(&v, 0, sizeof(v)); v.type = V_FLOAT; v.f = f; return v; }
static value v_vec(tc_vec3 x) { value v; memset(&v, 0, sizeof(v)); v.type = V_VEC; v.v = x; return v; }
static float as_float(value v) { return v.type == V_FLOAT ? v.f : v.type == V_VEC ? v.v.x : (float)v.i; }
static int32_t as_int(value v) { return v.type == V_FLOAT ? (int32_t)v.f : v.i; }
static bool truthy(value v) {
    return v.type == V_FLOAT ? v.f != 0 : v.type == V_VEC ? (v.v.x != 0 || v.v.y != 0 || v.v.z != 0) : v.i != 0;
}

typedef struct run {
    tc_ai_perso *p;
    tc_ai_intel *intel;
    uint32_t slot;           /* 0 schedule, 1 + k rule k */
    bool meta_ran;           /* exe DAT_0051b01b */
} run;

static const tc_ai_node *N(const run *r, uint32_t i) { return &r->p->model->nodes[i]; }

/* First node after i's subtree. */
static uint32_t skip(const run *r, uint32_t i) {
    const tc_ai_node *n = r->p->model->nodes;
    uint8_t d = n[i].depth;
    for (i++; n[i].type != TC_AI_END && n[i].depth > d; i++) {}
    return i;
}

/* k-th child of node i, or TC_AI_NONE. */
static uint32_t child(const run *r, uint32_t i, int k) {
    const tc_ai_node *n = r->p->model->nodes;
    uint8_t d = n[i].depth;
    uint32_t c = i + 1;
    if (n[c].type == TC_AI_END || n[c].depth <= d) return TC_AI_NONE;
    while (k-- > 0) {
        c = skip(r, c);
        if (n[c].type == TC_AI_END || n[c].depth <= d) return TC_AI_NONE;
    }
    return c;
}

static void unknown(const run *r, uint32_t i) {
    const tc_ai_node *n = N(r, i);
    if (n->type < 10 && n->param < TC_AI_UNKNOWN_STRIDE) r->p->unknown[n->type * TC_AI_UNKNOWN_STRIDE + n->param]++;
}

/* ---- dsgvars ---- */

static int other_index(const tc_ai_perso *p, int32_t id) {
    int i;
    for (i = 0; i < p->model->other_count; i++)
        if (p->model->others[i].id == id) return i;
    return -1;
}

static uint8_t *dsg_mem(const tc_ai_perso *p, int32_t perso, const tc_ai_dsg_def **def) {
    int o;
    if (perso == 0) { *def = &p->model->dsg; return p->dsg; }
    o = other_index(p, perso);
    if (o < 0) return NULL;
    *def = &p->model->others[o].dsg;
    return p->other_dsg[o];
}

static uint32_t rd_le(const uint8_t *b, int n) {
    uint32_t v = 0;
    while (n-- > 0) v = (v << 8) | b[n];
    return v;
}
static void wr_le(uint8_t *b, uint32_t v, int n) {
    int i;
    for (i = 0; i < n; i++) b[i] = (uint8_t)(v >> (8 * i));
}
static int dsg_width(uint8_t type) { return type <= 2 ? 1 : type <= 4 ? 2 : type == 12 ? 12 : 4; }

static value dsg_get(const tc_ai_perso *p, int32_t perso, uint32_t i) {
    const tc_ai_dsg_def *def;
    uint8_t *mem = dsg_mem(p, perso, &def);
    const uint8_t *b;
    uint8_t t;
    if (!mem || i >= def->count || def->vars[i].offset + dsg_width(def->vars[i].type) > def->size) return v_int(0);
    b = mem + def->vars[i].offset;
    t = def->vars[i].type;
    switch (t) {
    case 0: case 1: return v_int((int8_t)b[0]);
    case 2: return v_int(b[0]);
    case 11: return v_int(b[0]); /* perso list (CollectNearby): count in byte 0, capacity in byte 1 */
    case 3: return v_int((int16_t)rd_le(b, 2));
    case 4: return v_int((int32_t)rd_le(b, 2));
    case 7: return v_float(bits_to_float(rd_le(b, 4)));
    case 12: return v_vec(tc_v3(bits_to_float(rd_le(b, 4)), bits_to_float(rd_le(b + 4, 4)), bits_to_float(rd_le(b + 8, 4))));
    default: {
        value v = v_int((int32_t)rd_le(b, 4));
        if (t == 10) v.type = V_PERSO; /* raw pointer from the snapshot: only null-ness is meaningful */
        return v;
    }
    }
}

static uint32_t float_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static void dsg_set(tc_ai_perso *p, int32_t perso, uint32_t i, value v) {
    const tc_ai_dsg_def *def;
    uint8_t *mem = dsg_mem(p, perso, &def), *b;
    if (!mem || i >= def->count || def->vars[i].offset + dsg_width(def->vars[i].type) > def->size) return;
    b = mem + def->vars[i].offset;
    switch (def->vars[i].type) {
    case 0: case 1: case 2: wr_le(b, (uint32_t)as_int(v), 1); break;
    case 3: case 4: wr_le(b, (uint32_t)as_int(v), 2); break;
    case 7: wr_le(b, float_bits(as_float(v)), 4); break;
    case 12:
        if (v.type != V_VEC) v.v = tc_v3(as_float(v), as_float(v), as_float(v));
        wr_le(b, float_bits(v.v.x), 4);
        wr_le(b + 4, float_bits(v.v.y), 4);
        wr_le(b + 8, float_bits(v.v.z), 4);
        break;
    default: wr_le(b, (uint32_t)as_int(v), 4); break;
    }
}

float tc_ai_dsgvar_float(const tc_ai_perso *p, uint32_t i) { return as_float(dsg_get(p, 0, i)); }
void tc_ai_dsgvar_set_int(tc_ai_perso *p, uint32_t i, int32_t v) { dsg_set(p, 0, i, v_int(v)); }
void tc_ai_dsgvar_set_float(tc_ai_perso *p, uint32_t i, float v) { dsg_set(p, 0, i, v_float(v)); }
void tc_ai_dsgvar_set_vec(tc_ai_perso *p, uint32_t i, tc_vec3 v) { dsg_set(p, 0, i, v_vec(v)); }

/* ---- the perso's frame ---- */

static tc_vec3 local_to_global(const tc_ai_perso *p, tc_vec3 v) {
    float c = cosf(p->heading), s = sinf(p->heading);
    return tc_v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}
static tc_vec3 global_to_local(const tc_ai_perso *p, tc_vec3 v) {
    float c = cosf(p->heading), s = sinf(p->heading);
    return tc_v3(c * v.x + s * v.y, -s * v.x + c * v.y, v.z);
}
/* A level perso's placement, where the model knows it: the portals (.ttai PORT), whose perso Ed's dsg99 holds while he
 * walks in. rows: its 3x3, row-major. */
static bool perso_frame(const tc_ai_perso *p, int32_t perso, tc_vec3 *position, const float **rows) {
    uint16_t k;
    for (k = 0; k < p->model->portal_count; k++)
        if ((int32_t)p->model->portals[k].perso == perso) {
            *position = p->model->portals[k].position;
            *rows = p->model->portals[k].rows;
            return true;
        }
    return false;
}

/* The heading that faces a point (Ed's front, local -Y, along (sin h, -cos h)). */
static float heading_to(const tc_ai_perso *p, tc_vec3 target) {
    float dx = target.x - p->position.x, dy = target.y - p->position.y;
    return dx * dx + dy * dy > 1e-8f ? atan2f(dx, -dy) : p->heading;
}

static tc_vec3 perso_position(const tc_ai_perso *p, int32_t perso) {
    int o = perso == 0 ? -1 : other_index(p, perso);
    if (perso == 0) return p->position;
    return o >= 0 && p->other_positions ? p->other_positions[o] : p->position;
}

/* ---- states (GAM_00437260 and friends) ---- */

static bool state_allowed(const tc_ai_perso *p, uint32_t target, uint32_t *play) {
    const tc_ai_state *cur = &p->model->states[p->state];
    uint32_t k;
    *play = target;
    for (k = 0; k < cur->transition_count; k++)
        if (cur->transitions[k].target == target) { *play = cur->transitions[k].via; return true; }
    for (k = 0; k < cur->prohibited_count; k++)
        if (cur->prohibited[k] == target) return false;
    return cur->flags != 1;
}

static void play_sound(tc_ai_perso *p, uint32_t event);
static void queue_mech_request(tc_ai_perso *p, int mode);

/* An animation event reached: fire on a 0 counter and reload it, else count down (0xFF stays). FUN_004366c0. */
static bool event_due(uint8_t *counter, uint8_t period) {
    if (*counter == 0) { *counter = period; return true; }
    if (*counter != 0xFF) (*counter)--;
    return false;
}

/* A new state's animation (FUN_00436600 then FUN_00436560): when a script changed the state (by_script), the old
 * state's events with first 0xFF fire (FUN_00436600 with 1; a natural next state passes 0); the new state's counters
 * start at their first. */
static void state_play_from(tc_ai_perso *p, uint32_t s, bool by_script) {
    const tc_ai_state *old = &p->model->states[p->state], *st = &p->model->states[s];
    uint32_t k;
    if (by_script) {
        for (k = 0; k < old->sound_count; k++)
            if (p->model->sounds[old->sound_first + k].first == 0xFF) play_sound(p, p->model->sounds[old->sound_first + k].event);
        for (k = 0; k < old->mech_count; k++)
            if (p->model->mech_events[old->mech_first + k].first == 0xFF)
                queue_mech_request(p, p->model->mech_events[old->mech_first + k].mode);
    }
    for (k = 0; k < st->sound_count && k < TC_AI_MAX_STATE_EVENTS; k++)
        p->sound_counter[k] = p->model->sounds[st->sound_first + k].first;
    for (k = 0; k < st->mech_count && k < TC_AI_MAX_STATE_EVENTS; k++)
        p->mech_counter[k] = p->model->mech_events[st->mech_first + k].first;
    p->state = (uint16_t)s;
    p->state_frame = 0;
    p->frame_rate = p->model->states[s].speed ? p->model->states[s].speed : 30;
    p->repeats_left = p->model->states[s].repeats;
    p->state_ended = false;
    p->state_changes++;
}

static void state_play(tc_ai_perso *p, uint32_t s) { state_play_from(p, s, false); }

static bool change_state(tc_ai_perso *p, uint32_t target) {
    uint32_t play;
    if (target >= p->model->state_count) return false;
    if (target == p->wanted_state && !p->state_ended) return true;
    if (!state_allowed(p, target, &play)) return false;
    p->wanted_state = (uint16_t)target;
    state_play_from(p, play < p->model->state_count ? play : target, true);
    return true;
}

static bool can_change_state(const tc_ai_perso *p, uint32_t target) {
    uint32_t play;
    return target == p->wanted_state || (target < p->model->state_count && state_allowed(p, target, &play));
}

static void play_sound(tc_ai_perso *p, uint32_t event) {
    if (event <= 0xFFFF && p->sound_count < TC_AI_MAX_SOUNDS) p->sounds[p->sound_count++] = (uint16_t)event;
}

/* A mechanics request for this frame (REQ_Push_0047a370: up to 8, a full queue drops it), without the AI's
 * persistent mode: an animation event (FUN_00436910) only queues. */
static void queue_mech_request(tc_ai_perso *p, int mode) {
    if (p->motion.mode_count < 8) p->motion.modes[p->motion.mode_count++] = (int8_t)mode;
}

/* The animation's events at frames in [from, to) of the playing state: sounds, mechanics requests. */
static void state_sounds(tc_ai_perso *p, const tc_ai_state *s, float from, float to) {
    uint32_t k;
    for (k = 0; k < s->sound_count; k++) {
        const tc_ai_sound *e = &p->model->sounds[s->sound_first + k];
        if ((float)e->frame >= from && (float)e->frame < to &&
            (k >= TC_AI_MAX_STATE_EVENTS || event_due(&p->sound_counter[k], e->period)))
            play_sound(p, e->event);
    }
    for (k = 0; k < s->mech_count; k++) {
        const tc_ai_mech_event *e = &p->model->mech_events[s->mech_first + k];
        if ((float)e->frame >= from && (float)e->frame < to &&
            (k >= TC_AI_MAX_STATE_EVENTS || event_due(&p->mech_counter[k], e->period)))
            queue_mech_request(p, e->mode);
    }
}

/* GAM_PlayAnim_00415fb0 / 00415c00: at the end of the animation, repeat it (State +0x3A times), else play
 * the next state (the requested state, InState, stays); with no next state hold the last frame, "ended". */
static void advance_state(tc_ai_perso *p, float dt) {
    int guard;
    if (p->state_ended || !p->model->state_count) return;
    float frames = dt * p->frame_rate;
    for (guard = 0; guard < 8; guard++) {
        const tc_ai_state *s = &p->model->states[p->state];
        float length = (float)(s->frames ? s->frames : 1);
        uint8_t rate = p->frame_rate;
        state_sounds(p, s, p->state_frame, fminf(length, p->state_frame + frames));
        if (p->state_frame + frames < length) { p->state_frame += frames; return; }
        frames -= length - p->state_frame;
        if (p->repeats_left) { p->repeats_left--; p->state_frame = 0; continue; }
        if (s->next < 0) { p->state_frame = length; p->state_ended = true; return; }
        if ((uint32_t)s->next == p->state) { p->state_frame = 0; p->repeats_left = s->repeats; continue; }
        state_play(p, (uint32_t)s->next);
        frames = frames / (rate ? rate : 1) * p->frame_rate; /* the rest of the step at the new state's rate */
    }
}

/* ---- behaviours ---- */

static void set_behaviour(tc_ai_perso *p, tc_ai_intel *in, uint32_t b) {
    uint32_t k;
    in->behaviour = (uint16_t)b;
    in->schedule_pos = TC_AI_NONE;
    for (k = 0; k <= p->model->max_rules; k++) in->slots[k].node = TC_AI_NONE;
}

void tc_ai_force_behaviour(tc_ai_perso *p, uint16_t b) {
    if (b < p->model->normal_count && p->normal.behaviour != b) set_behaviour(p, &p->normal, b);
}

void tc_ai_force_reflex(tc_ai_perso *p, uint16_t reflex) {
    uint32_t b = (uint32_t)p->model->normal_count + reflex;
    if (reflex < p->model->reflex_count && p->reflex.behaviour != b) set_behaviour(p, &p->reflex, b);
}

/* ChangeBehaviour / ChangeReflex (FUN_0048b7d0): restarting is only for the intelligence that is running. */
static uint32_t change_behaviour(run *r, bool reflex, uint32_t param) {
    tc_ai_perso *p = r->p;
    tc_ai_intel *in = reflex ? &p->reflex : &p->normal;
    uint32_t b;
    if (param == TC_AI_NONE || ((param & TC_AI_REFLEX_FLAG) != 0) != reflex) return 0;
    b = (param & 0xFFFF) + (reflex ? p->model->normal_count : 0);
    if (b >= (uint32_t)p->model->normal_count + p->model->reflex_count || in->behaviour == b) return 0;
    set_behaviour(p, in, b);
    return reflex == p->running_reflex ? F_RESTART : 0;
}

/* ---- evaluation ---- */

static value eval(run *r, uint32_t i);

static value arg(run *r, uint32_t i, int k) {
    uint32_t c = child(r, i, k);
    return c == TC_AI_NONE ? v_int(0) : eval(r, c);
}
static float argf(run *r, uint32_t i, int k) { return as_float(arg(r, i, k)); }
static tc_vec3 argv3(run *r, uint32_t i, int k) {
    value v = arg(r, i, k);
    return v.type == V_VEC ? v.v : tc_v3(as_float(v), as_float(v), as_float(v));
}
static uint32_t arg_param(run *r, uint32_t i, int k) {
    uint32_t c = child(r, i, k);
    return c == TC_AI_NONE ? TC_AI_NONE : N(r, c)->param;
}

static float rnd(tc_ai_perso *p) {
    p->rng = p->rng * 1103515245u + 12345u;
    return (float)((p->rng >> 8) & 0xFFFFFF) / 16777216.0f;
}

/* Assignable places: a dsgvar or field of a perso, optionally one vector component. */
typedef struct lval { int kind; int32_t perso; uint32_t index; int component; } lval; /* kind 0 none, 1 dsg, 2 field */

static lval lvalue(run *r, uint32_t i) {
    const tc_ai_node *n = N(r, i);
    lval l = {0, 0, 0, -1};
    if (n->type == TC_AI_DSGVAR || n->type == TC_AI_DSGVAR_REF) { l.kind = 1; l.index = n->param; }
    else if (n->type == TC_AI_FIELD) { l.kind = 2; l.index = n->param; }
    else if (n->type == TC_AI_OP && n->param == 12) { /* Dot(perso, member) */
        uint32_t m = child(r, i, 1);
        value who = arg(r, i, 0);
        if (m == TC_AI_NONE) return l;
        l = lvalue(r, m);
        l.perso = who.type == V_PERSO ? who.i : -1;
    } else if (n->type == TC_AI_OP && n->param >= 13 && n->param <= 15) {
        uint32_t c = child(r, i, 0);
        if (c == TC_AI_NONE) return l;
        l = lvalue(r, c);
        l.component = (int)n->param - 13;
    }
    return l;
}

static value field_get(run *r, int32_t perso, uint32_t f) {
    tc_ai_perso *p = r->p;
    if (perso != 0) return f == 0 ? v_vec(perso_position(p, perso)) : v_int(0);
    switch (f) {
    case 0: return v_vec(p->position);
    case 1: return v_vec(tc_v3(sinf(p->heading), -cosf(p->heading), 0));
    case 2: return v_vec(p->velocity);
    case 4: case 5: case 6: return v_vec(p->motion.axes[f - 4]);
    case 13: return v_float(p->float13);
    case 14: return v_float(p->float14);
    default: return v_int(0);
    }
}

static void field_set(run *r, int32_t perso, uint32_t f, value v) {
    tc_ai_perso *p = r->p;
    if (perso != 0) return;
    switch (f) {
    case 2: p->motion.velocity = v.v; p->motion.velocity_set = true; break;
    case 4: case 5: case 6: p->motion.axes[f - 4] = v.v; p->motion.axes_set = true; break;
    case 13: p->float13 = as_float(v); break;
    case 14: p->float14 = as_float(v); break;
    default: break;
    }
}

static value lval_get(run *r, lval l) {
    value v = l.kind == 1 ? (l.perso < 0 ? v_int(0) : dsg_get(r->p, l.perso, l.index))
            : l.kind == 2 ? field_get(r, l.perso, l.index) : v_int(0);
    if (l.component >= 0) return v_float(v.type == V_VEC ? (&v.v.x)[l.component] : 0);
    return v;
}

static void lval_set(run *r, lval l, value v) {
    if (l.component >= 0) {
        value whole = lval_get(r, (lval){l.kind, l.perso, l.index, -1});
        if (whole.type != V_VEC) whole = v_vec(tc_v3(0, 0, 0));
        (&whole.v.x)[l.component] = as_float(v);
        v = whole;
    }
    if (l.kind == 1 && l.perso >= 0) dsg_set(r->p, l.perso, l.index, v);
    else if (l.kind == 2) field_set(r, l.perso, l.index, v);
}

static value arith(int op, value a, value b) {
    if (a.type == V_VEC || b.type == V_VEC) {
        tc_vec3 x = a.type == V_VEC ? a.v : tc_v3(as_float(a), as_float(a), as_float(a));
        tc_vec3 y = b.type == V_VEC ? b.v : tc_v3(as_float(b), as_float(b), as_float(b));
        switch (op) {
        case 0: return v_vec(tc_v3_add(x, y));
        case 1: return v_vec(tc_v3_sub(x, y));
        case 2: return v_vec(tc_v3(x.x * y.x, x.y * y.y, x.z * y.z));
        default: return v_vec(tc_v3(y.x ? x.x / y.x : 0, y.y ? x.y / y.y : 0, y.z ? x.z / y.z : 0));
        }
    }
    if (a.type == V_FLOAT || b.type == V_FLOAT) {
        float x = as_float(a), y = as_float(b);
        return v_float(op == 0 ? x + y : op == 1 ? x - y : op == 2 ? x * y : (y != 0 ? x / y : 0));
    }
    switch (op) {
    case 0: return v_int(a.i + b.i);
    case 1: return v_int(a.i - b.i);
    case 2: return v_int(a.i * b.i);
    default: return v_int(b.i ? a.i / b.i : 0);
    }
}

static int32_t button_counter(run *r, uint32_t param) {
    return param < r->p->model->button_count ? r->p->button_counter[param] : -2;
}

static float button_analog(const tc_ai_perso *p, uint32_t b) {
    const tc_ai_button *bt;
    float x = 100 * p->stick_x, y = -100 * p->stick_y; /* DirectInput axes: y negative = up */
    if (b >= p->model->button_count) return 0;
    bt = &p->model->buttons[b];
    switch (bt->analog) {
    case TC_AI_ANALOG_ANGLE: return (x != 0 || y != 0) ? atan2f(y, x) * 180 / TC_PI : 0;
    case TC_AI_ANALOG_MAGNITUDE: return fminf(sqrtf(x * x + y * y), 100);
    case TC_AI_ANALOG_X: return x;
    case TC_AI_ANALOG_Y: return y;
    default: return 0;
    }
}

static bool compare(int op, value a, value b) {
    if (a.type == V_FLOAT || b.type == V_FLOAT) {
        float x = as_float(a), y = as_float(b);
        switch (op) {
        case 3: return x == y;
        case 4: return x != y;
        case 5: return x < y;
        case 6: return x > y;
        case 7: return x <= y;
        default: return x >= y;
        }
    }
    if (a.type == V_VEC && b.type == V_VEC) {
        bool eq = a.v.x == b.v.x && a.v.y == b.v.y && a.v.z == b.v.z;
        return op == 4 ? !eq : eq;
    }
    switch (op) {
    case 3: return a.i == b.i;
    case 4: return a.i != b.i;
    case 5: return a.i < b.i;
    case 6: return a.i > b.i;
    case 7: return a.i <= b.i;
    default: return a.i >= b.i;
    }
}

static bool same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

/* FUN_004976f0 (COND 70): in water */
static bool in_water(const tc_ai_perso *p) {
    if (p->zone_combined == 1) return true;
    return p->zone_combined == 3 && (p->zone[3] == 1 || p->zone[4] == 1);
}

static bool condition(run *r, uint32_t i) {
    tc_ai_perso *p = r->p;
    uint32_t c = N(r, i)->param;
    switch (c) {
    case 0: return truthy(arg(r, i, 0)) && truthy(arg(r, i, 1));
    case 1: return truthy(arg(r, i, 0)) || truthy(arg(r, i, 1));
    case 2: return !truthy(arg(r, i, 0));
    case 3: case 4: case 5: case 6: case 7: case 8: return compare((int)c, arg(r, i, 0), arg(r, i, 1));
    case 9: return button_counter(r, arg_param(r, i, 0)) > 0;
    case 10: return button_counter(r, arg_param(r, i, 0)) == 1;
    case 11: return button_counter(r, arg_param(r, i, 0)) < 0;
    case 12: return button_counter(r, arg_param(r, i, 0)) == -1;
    case 31: return (p->contacts & TC_AI_CONTACT_GROUND) != 0;
    case 32: return (p->contacts & TC_AI_CONTACT_WALL) != 0;
    case 33: return p->contacts == 0;
    case 34: return (p->contacts & TC_AI_CONTACT_SLOPE) != 0;
    case 35: return (p->contacts & TC_AI_CONTACT_OVERHANG) != 0;
    case 36: return (p->contacts & TC_AI_CONTACT_CEILING) != 0;
    case 54: return !truthy(arg(r, i, 0));
    case 55: {
        uint32_t stored = (uint32_t)as_int(arg(r, i, 0)), duration = (uint32_t)as_int(arg(r, i, 1));
        return (stored <= p->time_ms ? p->time_ms - stored : stored - p->time_ms) >= duration;
    }
    case 56: {
        value who = arg(r, i, 0);
        uint32_t b = arg_param(r, i, 1);
        if (who.type != V_PERSO || who.i != 0 || b == TC_AI_NONE) return false;
        return (b & TC_AI_REFLEX_FLAG) ? p->reflex.behaviour == (b & 0xFFFF) + p->model->normal_count
                                       : p->normal.behaviour == b;
    }
    case 57: return can_change_state(p, arg_param(r, i, 0));
    case 58: return p->wanted_state == arg_param(r, i, 0);
    case 62: {
        uint32_t s = arg_param(r, i, 0);
        return p->level_name && s < p->model->string_count && p->model->strings[s] &&
               same_name(p->model->strings[s], p->level_name);
    }
    case 65: { value v = arg(r, i, 0); return v.type == V_PERSO ? v.i >= 0 && v.i != (int32_t)TC_AI_NONE : truthy(v); }
    /* The water zones (docs/NOTES.md "Swimming"): FUN_004976f0 (70) in water: all sample points in it, or mixed with
     * point 3 or 4 in it; 71 the head (point 1) out; 72 point 1 in (or none); 73 FUN_004977f0; 74 no point in water. */
    case 70: return in_water(p);
    case 71: return in_water(p) && p->zone[1] == 0;
    case 72: return in_water(p) && p->zone[1] != 0;
    case 73: return !in_water(p) || (p->zone[2] >= 0 ? p->zone[2] != 1 : !(p->zone[1] != 0));
    case 74: { int k; for (k = 0; k < 7; k++) if (p->zone[k] == 1) return false; return true; }
    case 69: { /* FUN_00497690: falling: a type 4 or 10 card, no contacts, not rising */
        const tc_ai_state *st = p->model->state_count ? &p->model->states[p->state] : NULL;
        uint32_t type = st && st->card != TC_AI_NONE16 ? p->model->cards[st->card].type : TC_AI_NONE16;
        return (type == 4 || type == 10) && p->contacts == 0 && p->velocity.z <= 0;
    }
    case 90: { uint32_t n = (uint32_t)as_int(arg(r, i, 0)); return n < 32 ? (p->global_flags >> n) & 1 : (p->global_flags & n) != 0; }
    case 63: return p->screen_covered; /* GameState63 = GAM_004290a0: the cover transition has finished */
    /* FUN_004985e0: the contact's collide identifier & mask (FUN_00497890), when the report has that contact */
    case 44: return (p->contacts & TC_AI_CONTACT_GROUND) && (p->ground_material & as_int(arg(r, i, 0)));
    case 45: return (p->contacts & TC_AI_CONTACT_WALL) && (p->wall_material & as_int(arg(r, i, 0)));
    case 47: return (p->contacts & TC_AI_CONTACT_SLOPE) && (p->ground_material & as_int(arg(r, i, 0)));
    case 46: { /* no contact with that material: the ground's if any, else the wall's */
        int32_t mask = as_int(arg(r, i, 0));
        if (p->contacts & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE)) return !(p->ground_material & mask);
        if (p->contacts & (TC_AI_CONTACT_WALL | TC_AI_CONTACT_OVERHANG | TC_AI_CONTACT_CEILING))
            return !(p->wall_material & mask);
        return true;
    }
    case 30: case 37: case 38: case 59: case 64:
        return false;    /* collision probes, zones, materials, touched objects, game state: not modelled yet */
    default: unknown(r, i); return false;
    }
}

static value function(run *r, uint32_t i) {
    tc_ai_perso *p = r->p;
    switch (N(r, i)->param) {
    case 0: case 3: { value who = arg(r, i, 0); return v_vec(perso_position(p, who.type == V_PERSO ? who.i : 0)); }
    case 2: { /* horizontal angle (degrees, CCW, 0 = facing) from this perso to another, in this perso's frame */
        value who = arg(r, i, 0);
        tc_vec3 d = global_to_local(p, tc_v3_sub(perso_position(p, who.type == V_PERSO ? who.i : 0), p->position));
        return v_float(atan2f(d.x, -d.y) * 180 / TC_PI);
    }
    case 21: return v_float(argf(r, i, 0));
    case 22: return v_int(as_int(arg(r, i, 0)));
    case 23: return v_float(sinf(argf(r, i, 0) * TC_PI / 180));
    case 24: return v_float(cosf(argf(r, i, 0) * TC_PI / 180));
    case 25: { float x = argf(r, i, 0); return v_float(x * x); }
    case 26: return v_float(sqrtf(fmaxf(argf(r, i, 0), 0)));
    case 27: { /* FUN_00450070: min + t (max - min) / (n - 1), t from a shuffled table of 0..n-1: max only for the
                * table's last value, so in effect [min, max) */
               int32_t lo = as_int(arg(r, i, 0)), hi = as_int(arg(r, i, 1));
               return v_int(hi > lo ? lo + (int32_t)(rnd(p) * (float)(hi - lo)) : lo); }
    case 28: { float lo = argf(r, i, 0), hi = argf(r, i, 1); return v_float(lo + rnd(p) * (hi - lo)); }
    case 29: return v_float(fminf(argf(r, i, 0), argf(r, i, 1)));
    case 30: return v_float(fmaxf(argf(r, i, 0), argf(r, i, 1)));
    case 31: return v_float(argf(r, i, 0) * TC_PI / 180);
    case 32: return v_float(argf(r, i, 0) * 180 / TC_PI);
    case 33: return v_float(fabsf(argf(r, i, 0)));
    case 34: return v_int(p->hit_points);
    case 37: return v_int(0);
    case 43: return v_int((int32_t)p->time_ms);
    case 49: return v_float(button_analog(p, arg_param(r, i, 0)));
    case 88: return v_float(tc_v3_length(p->velocity));
    case 98: return v_float(tc_v3_length(argv3(r, i, 0)));
    case 99: { tc_vec3 v = argv3(r, i, 0); float l = tc_v3_length(v); return v_vec(l > 0 ? tc_v3_div(v, l) : v); }
    case 100: return v_vec(local_to_global(p, argv3(r, i, 0)));
    case 101: return v_vec(global_to_local(p, argv3(r, i, 0)));
    /* FUNC_VecLengthEtc_0049ab10 cases 0x66 / 0x67: a point through the perso's world matrix (FUN_0047b2f0) or its
     * inverse. Ed's underwater reflex R8 leaves the water when FUNC_102((0, 1, -1)) rises 0.55 above the surface. */
    case 13: return v_float(tc_v3_length(argv3(r, i, 0))); /* used as a length (B82's climb test, B29's aim) */
    case 162: { /* FUN_0049c690, a probe (not decoded): from a point along a direction, both in the perso's axes, to
                 * the first surface; the hit point in those axes. Its uses (B82: (0, -1, 0) against 2.25) say the
                 * ray is not bounded by the direction's length: a miss returns a point far along it. */
        tc_vec3 o = argv3(r, i, 0), d = argv3(r, i, 1);
        float l = tc_v3_length(d), reach = 1000;
        tc_hit hit;
        if (!(l > 0)) return v_vec(o);
        d = tc_v3_div(d, l);
        if (p->world && tc_collision_raycast(p->world, tc_v3_add(p->position, local_to_global(p, o)),
                                             local_to_global(p, d), reach, &hit))
            reach = hit.distance;
        return v_vec(tc_v3_add(o, tc_v3_scale(d, reach)));
    }
    case 102: {
        tc_vec3 v = argv3(r, i, 0);
        if (!p->has_axes) return v_vec(tc_v3_add(p->position, local_to_global(p, v)));
        return v_vec(tc_v3_add(p->position, tc_v3_add(tc_v3_add(tc_v3_scale(p->axes[0], v.x), tc_v3_scale(p->axes[1], v.y)),
                                                        tc_v3_scale(p->axes[2], v.z))));
    }
    case 103: {
        tc_vec3 d = tc_v3_sub(argv3(r, i, 0), p->position);
        if (!p->has_axes) return v_vec(global_to_local(p, d));
        return v_vec(tc_v3(tc_v3_dot(d, p->axes[0]), tc_v3_dot(d, p->axes[1]), tc_v3_dot(d, p->axes[2])));
    }
    case 50: { value v = v_int(-1); v.type = V_PERSO; return v; }
    case 91: return v_float(p->report_2bc);
    case 92: return v_float(p->report_2c0);
    case 51: return v_float(p->env_depth);
    case 52: return v_float(p->position.z + p->env_depth);
    case 104: return v_float(0);
    default: unknown(r, i); return v_int(0);
    }
}

static value operation(run *r, uint32_t i) {
    uint32_t op = N(r, i)->param;
    switch (op) {
    case 0: case 1: case 2: case 3: return arith((int)op, arg(r, i, 0), arg(r, i, 1));
    case 4: { value v = arg(r, i, 0);
              if (v.type == V_VEC) return v_vec(tc_v3_scale(v.v, -1));
              return v.type == V_FLOAT ? v_float(-v.f) : v_int(-v.i); }
    case 5: case 6: case 7: case 8: case 9: case 10: case 11: {
        uint32_t c = child(r, i, 0);
        lval l;
        value v;
        if (c == TC_AI_NONE) return v_int(0);
        l = lvalue(r, c);
        if (op == 11) v = arg(r, i, 1);
        else if (op == 9 || op == 10) v = arith(op == 9 ? 0 : 1, lval_get(r, l), v_int(1));
        else v = arith((int)op - 5, lval_get(r, l), arg(r, i, 1));
        lval_set(r, l, v);
        return v;
    }
    case 12: case 13: case 14: case 15: return lval_get(r, lvalue(r, i));
    case 16: return v_vec(tc_v3_add(argv3(r, i, 0), argv3(r, i, 1)));
    case 17: return v_vec(tc_v3_sub(argv3(r, i, 0), argv3(r, i, 1)));
    case 18: return v_vec(tc_v3_scale(argv3(r, i, 0), argf(r, i, 1)));
    case 19: { float d = argf(r, i, 1); return v_vec(d != 0 ? tc_v3_div(argv3(r, i, 0), d) : tc_v3(0, 0, 0)); }
    case 20: return v_vec(tc_v3_scale(argv3(r, i, 0), -1));
    default: unknown(r, i); return v_int(0);
    }
}

static value eval(run *r, uint32_t i) {
    const tc_ai_node *n = N(r, i);
    value v = v_int(0);
    switch (n->type) {
    case TC_AI_KEYWORD:
        if (n->param == 4 || n->param == 5) { v.type = V_PERSO; v.i = 0; } /* Me; KW_5 = the main actor (the player) */
        else unknown(r, i);
        return v;
    case TC_AI_COND: return v_int(condition(r, i));
    case TC_AI_OP: return operation(r, i);
    case TC_AI_FUNC: return function(r, i);
    case TC_AI_FIELD: return field_get(r, 0, n->param);
    case TC_AI_DSGVAR: case TC_AI_DSGVAR_REF: return dsg_get(r->p, 0, n->param);
    case TC_AI_INT: return v_int((int32_t)n->param);
    case TC_AI_REAL: return v_float(bits_to_float(n->param));
    case TC_AI_CONST_VECTOR:
        return v_vec(n->param < r->p->model->vector_count ? r->p->model->vectors[n->param] : tc_v3(0, 0, 0));
    case TC_AI_VECTOR: return v_vec(tc_v3(argf(r, i, 0), argf(r, i, 1), argf(r, i, 2)));
    case TC_AI_MASK: return v_int((int32_t)(n->param & 0xFFFF));
    case TC_AI_PERSO: v.type = V_PERSO; v.i = (int32_t)n->param; return v;
    default: v.type = V_REF; v.i = (int32_t)n->param; return v; /* button, state, behaviour, string, channel ... */
    }
}

/* ---- statements ---- */

static void set_mech_mode(tc_ai_perso *p, int mode) {
    p->motion.mech_mode = mode;
    if (p->motion.mode_count < 8) p->motion.modes[p->motion.mode_count++] = (int8_t)mode; /* full: dropped, as FUN_0047a370 */
}

static uint32_t procedure(run *r, uint32_t i) {
    tc_ai_perso *p = r->p;
    float *pr = p->motion.proposal;
    uint32_t c = N(r, i)->param;
    static const signed char mech[] = { /* PROC 51..65 -> mechanics mode */
        23, 12, 20, 3, 4, 1, 2, 10, 21, 6, 7, 8, 21, 19, 22};
    switch (c) {
    case 9: p->g_flags = (uint8_t)as_int(arg(r, i, 0)); p->g_armed = 1; return 0;
    case 10: { /* GAM_0041ec90: frame rate = state rate * percent / 100, as a byte */
        uint32_t rate = p->model->state_count ? (uint32_t)p->model->states[p->state].speed * (uint32_t)as_int(arg(r, i, 0)) / 100 : 0;
        p->frame_rate = (uint8_t)rate;
        return 0;
    }
    case 7: { uint32_t s = arg_param(r, i, 0); p->change_level = s == TC_AI_NONE ? -1 : (int32_t)s; return 0; }
    case 34: p->motion.stop = true; return 0; /* the speeds (+0x38.., +0x25C..), not the proposal */
    case 35: { /* PROC_DynamicsOnEtc_00488cd0 case 0x23: the velocity (+0x38), world axes; spin cleared */
        tc_vec3 v = argv3(r, i, 0);
        /* Port deviation (docs/DECOMP.md, user's choice): Ed's jump reflex R26 zeroes the velocity at the apex and after
         * 750 ms (rules 9 and 5), and the fall (B90) only sets it again while a direction is held, so letting go of the
         * keys on the way up dropped him straight down from the apex (the exe does this too). A reflex's zero in the air
         * keeps the horizontal speed. The only other literal zeros are B19's (the hang grab), a normal behaviour. */
        if (r->intel == &p->reflex && v.x == 0 && v.y == 0 && v.z == 0 && p->contacts == 0)
            v = tc_v3(p->velocity.x, p->velocity.y, 0);
        p->motion.velocity = v;
        p->motion.velocity_set = true;
        return 0;
    }
    case 47: { /* ProposalVecD = AI_004881d0: [4..6] turns half way to v, keeping its length; unset (zero) stays so */
        tc_vec3 d = tc_v3(pr[4], pr[5], pr[6]), v = argv3(r, i, 0);
        float l = tc_v3_length(d);
        if (l != 0 && (v.x != 0 || v.y != 0 || v.z != 0)) {
            d = tc_v3_scale(tc_v3_add(tc_v3_scale(d, 1 / l), tc_v3_scale(v, 1 / tc_v3_length(v))), l);
            pr[4] = d.x, pr[5] = d.y, pr[6] = d.z;
        }
        return 0;
    }
    case 38: {
        tc_vec3 a = argv3(r, i, 0), b = argv3(r, i, 2);
        pr[1] = a.x; pr[2] = a.y; pr[3] = a.z; pr[0x11] = argf(r, i, 1); pr[0x12] = b.x; pr[0x13] = b.y; pr[0x14] = b.z;
        return 0;
    }
    case 39: pr[0x18] = argf(r, i, 0); return 0;
    case 40: pr[0xB] = argf(r, i, 0); return 0;
    case 42: { /* ProposalVecB(v, dir) = AI_004891a0: direction [7..9] (dynamics proposal +0x1C), then ProposalVecA(v);
                * a zero direction zeroes v.x. The ski slopes' speed arrows (skier B12) */
        tc_vec3 v = argv3(r, i, 0), dir = argv3(r, i, 1);
        if (dir.x == 0 && dir.y == 0 && dir.z == 0) v.x = 0;
        pr[7] = dir.x; pr[8] = dir.y; pr[9] = dir.z;
        pr[1] = v.x; pr[2] = v.y; pr[3] = v.z; pr[0] = tc_v3_length(v);
        p->motion.speed_set = true;
        return 0;
    }
    case 41: { /* AI_00488070: wanted speed vector [1..3], its length [0] */
        tc_vec3 v = argv3(r, i, 0);
        pr[1] = v.x; pr[2] = v.y; pr[3] = v.z; pr[0] = tc_v3_length(v);
        p->motion.speed_set = true;
        return 0;
    }
    case 43: pr[10] = argf(r, i, 0); return 0;
    case 44: pr[10] += argf(r, i, 0); return 0;
    case 45: pr[10] *= argf(r, i, 0); return 0;
    case 48: {
        tc_vec3 v = argv3(r, i, 0);
        float l = tc_v3_length(v);
        if (l > 0) { pr[7] = v.x / l; pr[8] = v.y / l; pr[9] = v.z / l; }
        pr[10] = l;
        p->motion.direction_set = true;
        return 0;
    }
    case 49: pr[0] = argf(r, i, 0); return 0;
    case 50: { tc_vec3 v = argv3(r, i, 0); pr[1] = v.x; pr[2] = v.y; pr[3] = v.z; pr[0] = argf(r, i, 1); return 0; }
    case 51: case 52: case 53: case 54: case 55: case 56: case 57: case 59: case 60: case 61: case 62:
    case 63: case 64: case 65:
        if (c == 59) { tc_vec3 v = argv3(r, i, 0); pr[1] = v.x; pr[2] = v.y; pr[3] = v.z; }
        set_mech_mode(p, mech[c - 51]);
        return 0;
    case 58: case 254: pr[0] = argf(r, i, 0); set_mech_mode(p, 10); return 0;
    case 66: { value who = arg(r, i, 0); if (who.type == V_PERSO && who.i == 0) p->hit_points = as_int(arg(r, i, 1)); return 0; }
    case 120: case 121: {
        uint32_t ch = arg_param(r, i, 0);
        if (ch < 64) {
            if (c == 120) p->hidden_channels |= (uint64_t)1 << ch;
            else p->hidden_channels &= ~((uint64_t)1 << ch);
        }
        return 0;
    }
    case 203: p->display_flag8 = false; return 0;
    case 204: p->display_flag8 = true; return 0;
    case 205: p->transparency = (uint8_t)as_int(arg(r, i, 0)); return 0;
    case 239: case 240: return change_behaviour(r, c == 240, arg_param(r, i, 0));
    case 283: /* SetLightSet(set, duration ms, absolute) = PROC_283_0048ca60 -> 0x449ad0: the host applies it */
        if (p->light_call_count < TC_AI_MAX_LIGHT_CALLS) {
            tc_ai_light_call *l = &p->light_calls[p->light_call_count++];
            l->set = as_int(arg(r, i, 0));
            l->duration_ms = as_int(arg(r, i, 1));
            l->absolute = as_int(arg(r, i, 2)) != 0;
        }
        return 0;
    case 244: p->motion.jump = true; p->motion.jump_vector = argv3(r, i, 0); p->motion.jump_height = argf(r, i, 1);
        set_mech_mode(p, 9);
        return 0;
    case 245: case 246: {
        float a = argf(r, i, 0) * TC_PI / 180;
        if (c == 246) a = p->dt > 0 ? a / p->dt : 0;
        p->motion.turn_speed = a;
        p->motion.turning = true;
        pr[0xD] = a; /* AI_00492140: proposal +0x34, then mode 3 */
        set_mech_mode(p, 3);
        return 0;
    }
    case 247: { /* TurnTowardPoint(perso, x, y, z) (0x48be20): face the point at that offset in the perso's axes (at once,
                 * as TurnTowardOverFrames; Ed's B75 / B98 walking through the sud0 passages, HEG_GenTransitionFade) */
        value who = arg(r, i, 0);
        tc_vec3 pos, off = tc_v3(argf(r, i, 1), argf(r, i, 2), argf(r, i, 3));
        const float *m3;
        if (who.type == V_PERSO && perso_frame(p, who.i, &pos, &m3)) {
            p->motion.heading_set = true;
            p->motion.heading = heading_to(p, tc_v3_add(pos, tc_v3(m3[0] * off.x + m3[1] * off.y + m3[2] * off.z,
                                                                   m3[3] * off.x + m3[4] * off.y + m3[5] * off.z, 0)));
        }
        return 0;
    }
    case 249: return change_state(p, arg_param(r, i, 0)) ? 0 : F_ABORT;
    case 2: p->cover_requested = true; p->screen_covered = false; return 0; /* GameAction2: GAM_00429070 */
    case 242: { /* FUN_0048ba00: the perso's rotation about Z = deg * pi / 180 - pi / 2, for 0 <= deg <= 360 */
        float deg = argf(r, i, 0), h;
        if (!(deg >= 0 && deg <= 360)) return 0;
        h = deg * TC_PI / 180 - TC_PI / 2;
        p->motion.heading_set = true;
        p->motion.heading = h;
        return 0;
    }
    case 145: case 146: case 147: case 148: play_sound(p, arg_param(r, i, 0)); return 0; /* PlaySound3D ... (event) */
    case 27: { /* SetPositionFrom(perso, position) = AI_00488010: this perso only (the others' AI does not run) */
        value who = arg(r, i, 0);
        if (who.type == V_PERSO && who.i == 0) {
            p->motion.teleport_set = true;
            p->motion.teleport = argv3(r, i, 1);
        }
        return 0;
    }
    case 0: case 5: case 25: case 26: case 32: case 33: case 70: case 71: case 79: case 80:
    case 84: case 85: case 86: case 105: case 106: case 107: case 108:
    case 154: case 155: case 236: case 278: case 279: case 291: case 292:
        return 0; /* world, sound, text, camera and channel effects: no-ops for now */
    default: unknown(r, i); return 0;
    }
}

/* Meta-actions: start once, then a continuation every frame until done (AI_0048e7d0). */
static uint32_t meta_action(run *r, uint32_t i) {
    tc_ai_perso *p = r->p;
    tc_ai_slot *slot = &r->intel->slots[r->slot];
    uint32_t m = N(r, i)->param;
    r->meta_ran = true;
    if (slot->node != i) { /* start */
        slot->node = i;
        switch (m) {
        case 4: { /* ChangeStateAndWait */
            uint32_t s = arg_param(r, i, 0);
            slot->b = (int32_t)s;
            slot->a = change_state(p, s);
            break;
        }
        case 12: slot->a = 0; slot->b = 0; break; /* ChangeStateByDirection: a = walking, b = frames */
        case 19: break;
        case 29: slot->a = (int32_t)(p->time_ms + (uint32_t)as_int(arg(r, i, 0))); break; /* Wait(ms): FUN_004a0b00 */
        case 11: case 21: case 40: slot->a = 0; break; /* not modelled: finish at once */
        default: unknown(r, i); slot->a = 0; break;
        }
    }
    switch (m) {
    case 4:
        if (slot->a && p->wanted_state == (uint32_t)slot->b && !p->state_ended) return F_STOP;
        break;
    case 29: /* FUN_004a0b30: running until the clock passes start + ms (the ski props' sleep, the spring's jump) */
        if ((int32_t)(p->time_ms - (uint32_t)slot->a) < 0) return F_STOP;
        break;
    case 12: { /* ChangeStateByDirection(perso, offset, forward state, backward state): walk to the point at offset in
                * the perso's axes (B68: 4.5 in front of an arch) in the forward state, facing it. Not decoded: always
                * forwards, done within 0.4 or after 10 s. */
        value who = arg(r, i, 0);
        tc_vec3 pos, off = argv3(r, i, 1), target, d;
        const float *m3;
        if (who.type != V_PERSO || !perso_frame(p, who.i, &pos, &m3)) break;
        target = tc_v3_add(pos, tc_v3(m3[0] * off.x + m3[1] * off.y + m3[2] * off.z,
                                      m3[3] * off.x + m3[4] * off.y + m3[5] * off.z, 0));
        d = tc_v3_sub(target, p->position);
        if (d.x * d.x + d.y * d.y < 0.4f * 0.4f || ++slot->b > 600) {
            p->motion.stop = true;
            break;
        }
        p->motion.heading_set = true;
        p->motion.heading = heading_to(p, target);
        if (!slot->a) slot->a = change_state(p, arg_param(r, i, 2));
        return F_STOP;
    }
    case 19: { /* TurnTowardOverFrames(perso, frames): face it (at once; the frame count is not modelled) */
        value who = arg(r, i, 0);
        tc_vec3 pos;
        const float *m3;
        if (who.type == V_PERSO && perso_frame(p, who.i, &pos, &m3)) {
            p->motion.heading_set = true;
            p->motion.heading = heading_to(p, pos);
        }
        break;
    }
    default: break;
    }
    slot->node = TC_AI_NONE;
    return F_META_DONE;
}

/* Executes the statement at i; returns the next statement and ORs result flags into *flags. */
static uint32_t statement(run *r, uint32_t i, uint32_t *flags) {
    const tc_ai_node *n = N(r, i);
    switch (n->type) {
    case TC_AI_KEYWORD:
        if (n->param == 0) { /* If: the condition is the child, Then (and Else) follow as siblings */
            uint32_t c = child(r, i, 0), next = skip(r, i);
            bool ok = c != TC_AI_NONE && truthy(eval(r, c));
            if (ok) return N(r, next)->type == TC_AI_KEYWORD && N(r, next)->param == 1 ? next + 1 : next;
            if (N(r, next)->type == TC_AI_KEYWORD && N(r, next)->param == 1) next = skip(r, next);
            if (N(r, next)->type == TC_AI_KEYWORD && N(r, next)->param == 2 && N(r, next)->depth == n->depth)
                return next + 1;
            return next;
        }
        if (n->param == 1) return i + 1;          /* Then: run its children */
        if (n->param == 2) return skip(r, i);     /* Else reached after a Then branch: skip it */
        unknown(r, i);
        return skip(r, i);
    case TC_AI_PROC:
        *flags |= procedure(r, i);
        return skip(r, i);
    case TC_AI_META: {
        uint32_t f = meta_action(r, i);
        *flags |= f;
        return f & F_STOP ? i : skip(r, i);
    }
    default:
        (void)eval(r, i);
        return skip(r, i);
    }
}

/* AI_0048eab0: Proc9's value replaces the result of the statement after the next one. */
static uint32_t proc9_filter(tc_ai_perso *p, uint32_t flags) {
    if (p->g_flags & 0x20) return flags;
    if (!p->g_armed) {
        flags = (flags & 0x1C) | p->g_flags;
        p->g_flags = 0x20;
        p->g_armed = 1;
        return flags;
    }
    p->g_armed = 0;
    return flags;
}

/* Runs one script from node pos; returns F_END, F_STOP, F_ABORT or F_RESTART. */
static uint32_t run_script(run *r, uint32_t pos, bool schedule) {
    tc_ai_perso *p = r->p;
    uint32_t guard;
    for (guard = 0; guard < 100000; guard++) {
        uint32_t flags = 0, next;
        if (N(r, pos)->type == TC_AI_END) return F_END;
        if (schedule) r->intel->schedule_pos = pos;
        next = statement(r, pos, &flags);
        flags = proc9_filter(p, flags);
        if (p->trace) p->trace(p->trace_user, pos, flags);
        if (flags & F_STOP) { /* wins over F_RESTART: a new behaviour then starts next frame (AI_0048eba0) */
            if (schedule && !(flags & F_META_DONE) && N(r, pos)->type != TC_AI_META) r->intel->schedule_pos = next;
            if (flags & F_RESTART) r->intel->schedule_pos = TC_AI_NONE;
            return F_STOP;
        }
        if (flags & F_RESTART) return F_RESTART;
        if (flags & F_ABORT) return F_ABORT;
        pos = next;
    }
    return F_END;
}

/* AI_0048eb20: the behaviour's rules (every script, each from its start or its running meta-action), then
 * its schedule (resumes where it stopped). Returns whether a meta-action ran (then the normal intelligence
 * is skipped after the reflex). */
static bool run_intel(tc_ai_perso *p, tc_ai_intel *in) {
    const tc_ai_model *m = p->model;
    run r;
    int loops;
    r.p = p;
    r.intel = in;
    for (loops = 0; loops < 8; loops++) {
        const tc_ai_behaviour *b = &m->behaviours[in->behaviour];
        uint32_t k, f = F_END;
        r.meta_ran = false;
        for (k = 0; k < b->script_count; k++) {
            r.slot = 1 + k;
            f = run_script(&r, in->slots[1 + k].node != TC_AI_NONE ? in->slots[1 + k].node
                                                                   : m->scripts[b->first_script + k], false);
            if (f != F_END) break;
        }
        if (f == F_RESTART) continue;
        if (f == F_STOP || r.meta_ran) return r.meta_ran;
        if (b->schedule < 0) return false;
        r.slot = 0;
        f = run_script(&r, in->slots[0].node != TC_AI_NONE ? in->slots[0].node
                           : in->schedule_pos != TC_AI_NONE ? in->schedule_pos : m->scripts[b->schedule], true);
        if (f == F_RESTART) continue;
        if (f == F_END) in->schedule_pos = TC_AI_NONE;
        return r.meta_ran;
    }
    return false;
}

/* ---- perso ---- */

tc_result tc_ai_init(tc_ai_perso *p, const tc_ai_model *m) {
    uint32_t i;
    memset(p, 0, sizeof(*p));
    p->model = m;
    p->dsg = (uint8_t *)tc_calloc(m->dsg.size ? m->dsg.size : 1, 1);
    p->other_dsg = (uint8_t **)tc_calloc(m->other_count ? m->other_count : 1, sizeof(uint8_t *));
    p->other_positions = (tc_vec3 *)tc_calloc(m->other_count ? m->other_count : 1, sizeof(tc_vec3));
    p->button_counter = (int32_t *)tc_calloc(m->button_count ? m->button_count : 1, sizeof(int32_t));
    p->unknown = (uint32_t *)tc_calloc(10 * TC_AI_UNKNOWN_STRIDE, sizeof(uint32_t));
    p->normal.slots = (tc_ai_slot *)tc_calloc((size_t)m->max_rules + 1, sizeof(tc_ai_slot));
    p->reflex.slots = (tc_ai_slot *)tc_calloc((size_t)m->max_rules + 1, sizeof(tc_ai_slot));
    if (!p->dsg || !p->other_dsg || !p->other_positions || !p->button_counter || !p->unknown || !p->normal.slots || !p->reflex.slots) {
        tc_ai_free(p);
        return TC_ERR_OUT_OF_MEMORY;
    }
    if (m->dsg.initial) memcpy(p->dsg, m->dsg.initial, m->dsg.size);
    for (i = 0; i < m->other_count; i++) {
        const tc_ai_dsg_def *d = &m->others[i].dsg;
        p->other_dsg[i] = (uint8_t *)tc_calloc(d->size ? d->size : 1, 1);
        if (!p->other_dsg[i]) { tc_ai_free(p); return TC_ERR_OUT_OF_MEMORY; }
        if (d->initial) memcpy(p->other_dsg[i], d->initial, d->size);
    }
    for (i = 0; i < m->button_count; i++) p->button_counter[i] = -2;
    set_behaviour(p, &p->normal, m->initial_normal < m->normal_count ? m->initial_normal : 0);
    if (m->initial_reflex < m->reflex_count) set_behaviour(p, &p->reflex, (uint32_t)m->normal_count + m->initial_reflex);
    else p->reflex.behaviour = 0xFFFF;
    p->time_ms = 60000;
    p->rng = 0x1234567u;
    p->global_flags = 0xFFFFFFFFu;
    p->hit_points = 4;
    p->transparency = 255;
    p->change_level = -1;
    p->g_flags = 0x20;
    p->g_armed = 1;
    p->state = p->wanted_state = m->initial_state < m->state_count ? m->initial_state : 0;
    if (m->state_count) {
        state_play(p, p->state); /* its animation's event counters */
        p->state_changes = 0;
    }
    p->motion.mech_mode = -1;
    return TC_OK;
}

void tc_ai_free(tc_ai_perso *p) {
    uint32_t i;
    for (i = 0; p->other_dsg && p->model && i < p->model->other_count; i++) tc_free(p->other_dsg[i]);
    tc_free(p->dsg);
    tc_free(p->other_dsg);
    tc_free(p->other_positions);
    tc_free(p->button_counter);
    tc_free(p->unknown);
    tc_free(p->normal.slots);
    tc_free(p->reflex.slots);
    p->dsg = NULL;
    p->other_dsg = NULL;
    p->other_positions = NULL;
    p->button_counter = NULL;
    p->unknown = NULL;
    p->normal.slots = p->reflex.slots = NULL;
}

static void update_buttons(tc_ai_perso *p) {
    float x = 100 * p->stick_x, y = -100 * p->stick_y;
    uint32_t b, in = p->inputs;
    /* The game's direction entries OR in the stick axes (left/right beyond 20, up/down beyond 30). */
    if (x < -20) in |= 1u << TC_AI_IN_LEFT;
    if (x > 20) in |= 1u << TC_AI_IN_RIGHT;
    if (y < -30) in |= 1u << TC_AI_IN_UP;
    if (y > 30) in |= 1u << TC_AI_IN_DOWN;
    for (b = 0; b < p->model->button_count; b++) {
        const tc_ai_button *bt = &p->model->buttons[b];
        float a = bt->analog == TC_AI_ANALOG_X ? x : bt->analog == TC_AI_ANALOG_Y ? y : 0;
        bool held = (in & bt->inputs) != 0 ||
                    ((bt->analog == TC_AI_ANALOG_X || bt->analog == TC_AI_ANALOG_Y) && a != 0 && a >= bt->lo && a <= bt->hi);
        int32_t *c = &p->button_counter[b];
        if (held) *c = *c > 0 ? (*c < 0x7FFFFFFF ? *c + 1 : *c) : 1;
        else *c = *c < 0 ? (*c > -0x7FFFFFFF ? *c - 1 : *c) : -1;
    }
}

void tc_ai_step(tc_ai_perso *p, float dt) {
    tc_vec3 axes[3];
    int32_t mode = p->motion.mech_mode;
    float proposal[32];
    memcpy(axes, p->motion.axes, sizeof(axes));
    memcpy(proposal, p->motion.proposal, sizeof(proposal));
    memset(&p->motion, 0, sizeof(p->motion));
    p->sound_count = 0;
    p->light_call_count = 0;
    p->motion.mech_mode = mode;          /* the mechanics mode stays until the AI picks another */
    memcpy(p->motion.axes, axes, sizeof(axes));
    /* The proposal lives in the dynamics (+0x314) and keeps its values: ProposalVecA in one frame, the mode 7 request of
     * the jump animation's event (or SetMechMode7) applies it; the *_set flags say what changed this frame. */
    memcpy(p->motion.proposal, proposal, sizeof(proposal));
    p->dt = dt;
    p->time_ms += (uint32_t)(dt * 1000 + 0.5f);
    update_buttons(p);
    p->running_reflex = true;
    if (p->reflex.behaviour == 0xFFFF || !run_intel(p, &p->reflex)) {
        p->running_reflex = false;
        run_intel(p, &p->normal);
    }
    p->running_reflex = false;
    advance_state(p, dt);
}
