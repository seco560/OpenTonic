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

uint32_t tc_ai_card_u32(const tc_ai_card *c, uint32_t offset) {
    if (!c || offset < 4 || offset % 4 || offset / 4 - 1 >= c->word_count) return 0;
    return c->words[offset / 4 - 1];
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
    m->portals = (tc_ai_portal *)rd_array(r, m->portal_count, sizeof(tc_ai_portal), 68);
    for (i = 0; m->portals && i < m->portal_count; i++) {
        tc_ai_portal *p = &m->portals[i];
        uint32_t n;
        p->position = rd_vec3(r);
        p->radius = rd_f32(r);
        for (k = 0; k < 9; k++) p->rows[k] = rd_f32(r);
        p->perso = rd_u16(r);
        p->fire = rd_u8(r);
        p->flags = rd_u8(r);
        p->fire_dsg = rd_u16(r);
        (void)rd_u16(r);
        p->fire_value = rd_i32(r);
        p->sector = rd_u16(r);
        (void)rd_u16(r);
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
    m->data_hash = chunks_hash(&cs);

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

    if (chunk_need(&cs, "ZDES", r)) { /* optional: his ZDE zones (older files: none) */
        tc_zde_set_read(&m->zde, r);
        if (r->error == TC_ERR_OUT_OF_MEMORY) { e = r->error; goto fail; }
        if (r->error || m->zde.state_count != m->state_count) tc_zde_set_free(&m->zde);
    }

    if (chunk_need(&cs, "MODZ", r)) { /* optional: a Special Edition level's walk / ski switches (tc_ai_mode_zone) */
        m->mode_zone_count = rd_u16(r);
        (void)rd_u16(r);
        m->mode_zones = (tc_ai_mode_zone *)rd_array(r, m->mode_zone_count, sizeof(tc_ai_mode_zone), 72);
        if (!m->mode_zones) m->mode_zone_count = 0;
        for (i = 0; i < m->mode_zone_count && !r->error; i++) {
            tc_ai_mode_zone *z = &m->mode_zones[i];
            uint16_t prims;
            z->mode = rd_u8(r);
            z->require = rd_u8(r);
            z->once = rd_u8(r);
            z->teleport = rd_u8(r);
            z->vector = rd_vec3(r);
            for (k = 0; k < 12; k++) z->inverse[k] = rd_f32(r);
            prims = rd_u16(r);
            (void)rd_u16(r);
            z->prim_count = 0;
            for (k = 0; k < prims && !r->error; k++) {
                tc_ai_zone_prim q;
                q.kind = rd_u32(r);
                q.a = rd_vec3(r);
                q.b = rd_vec3(r);
                if (z->prim_count < 4) z->prim[z->prim_count++] = q;
            }
        }
        CHUNK_END(r);
    }
    if (chunk_need(&cs, "ALTM", r)) { /* optional: the second model (a whole .ttai), loaded by the host when needed */
        m->alt_size = (uint32_t)r->size;
        m->alt_data = (uint8_t *)tc_alloc(r->size ? r->size : 1);
        if (!m->alt_data) { e = TC_ERR_OUT_OF_MEMORY; goto fail; }
        memcpy(m->alt_data, r->data, r->size);
        r->offset = r->size;
        CHUNK_END(r);
    }

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
                                         !(m->normal_count + m->reflex_count)))  /* reflexes only: the SE's icons */
        e = TC_ERR_TRUNCATED;
    if (e != TC_OK) tc_ai_model_free(m);
    return e;
}

tc_result tc_ai_model_load_tables(tc_ai_model *m, const uint8_t *data, size_t size) {
    reader r;
    uint32_t i;
    r.data = data;
    r.size = size;
    r.offset = 0;
    r.error = TC_OK;
    m->string_count = rd_u16(&r);
    m->strings = (char **)rd_array(&r, m->string_count, sizeof(char *), 2);
    if (!m->strings) m->string_count = 0;
    for (i = 0; i < m->string_count; i++) m->strings[i] = rd_string(&r);
    m->vector_count = rd_u16(&r);
    m->vectors = (tc_vec3 *)rd_array(&r, m->vector_count, sizeof(tc_vec3), 12);
    if (!m->vectors) m->vector_count = 0;
    for (i = 0; i < m->vector_count; i++) m->vectors[i] = rd_vec3(&r);
    return r.error;
}

tc_result tc_ai_model_load_dsg(tc_ai_model *m, const uint8_t *data, size_t size) {
    reader r;
    tc_result e;
    memset(m, 0, sizeof(*m));
    r.data = data;
    r.size = size;
    r.offset = 0;
    r.error = TC_OK;
    rd_dsg_def(&r, &m->dsg);
    if ((e = chunk_end(&r)) != TC_OK) tc_ai_model_free(m);
    return e;
}

void tc_ai_model_link(tc_ai_model *m, const tc_level *level) {
    if (m->link_triangles == level->collision_triangle_count && m->link_crc == level->collision_crc) return;
    tc_free(m->floor_sounds);
    m->floor_sounds = NULL;
    m->floor_sound_count = 0;
}

bool tc_ai_zone_contains(const float inverse[12], const tc_ai_zone_prim *prims, uint32_t count, tc_vec3 p) {
    const float *m = inverse;
    tc_vec3 q = tc_v3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                      m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
    uint32_t k;
    for (k = 0; k < count; k++) {
        const tc_ai_zone_prim *z = &prims[k];
        if (z->kind == 7 && tc_v3_distance(q, z->a) <= z->b.x) return true;
        if (z->kind == 8 && q.x >= z->a.x && q.y >= z->a.y && q.z >= z->a.z && q.x <= z->b.x && q.y <= z->b.y &&
            q.z <= z->b.z) return true;
    }
    return false;
}

void tc_ai_model_free(tc_ai_model *m) {
    uint32_t i;
    tc_zde_set_free(&m->zde);
    tc_free(m->mode_zones);
    tc_free(m->alt_data);
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
    int32_t ctx;             /* OP 24's perso (AI_00491380, DAT_004ffcf4): functions and fields read it; 0 = Me */
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

/* Another perso by id: the live one (the cast), or NULL. */
static tc_ai_perso *live(const tc_ai_perso *p, int32_t id) {
    tc_ai_perso *q;
    if (id == 0 || !p->find) return NULL;
    q = p->find(p->find_user, id);
    return q && q->model ? q : NULL;
}

static uint8_t *dsg_mem(const tc_ai_perso *p, int32_t perso, const tc_ai_dsg_def **def) {
    int o;
    tc_ai_perso *q;
    if (perso == 0) { *def = &p->model->dsg; return p->dsg; }
    if ((q = live(p, perso)) != NULL) { *def = &q->model->dsg; return q->dsg; }
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
        if (t == 10) { /* a perso id (set by the scripts); the snapshot's raw pointers name no perso of the cast */
            v.type = V_PERSO;
            if (v.i == 0) v.i = -1; /* a null pointer: no perso (not Me) */
            else if (p->self_id && v.i == p->self_id) v.i = 0;
        }
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
    default:
        if (v.type == V_PERSO && v.i == 0 && p->self_id) v.i = p->self_id; /* "Me", as the writer's id */
        wr_le(b, (uint32_t)as_int(v), 4);
        break;
    }
}

float tc_ai_dsgvar_float(const tc_ai_perso *p, uint32_t i) { return as_float(dsg_get(p, 0, i)); }
float tc_ai_other_dsgvar_float(const tc_ai_perso *p, uint32_t other, uint32_t i) {
    if (!p->model || other >= p->model->other_count) return 0;
    return as_float(dsg_get(p, p->model->others[other].id, i));
}
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

/* A perso's point at an offset in its axes (FUN_0047b2f0 on its world matrix): a portal's frame from the data, else a live
 * perso's position and heading. Port: a live perso's axes are its turn about up only (tilted platforms are not asked
 * for points). false: nobody by that id. */
static bool perso_point(const tc_ai_perso *p, int32_t perso, tc_vec3 off, tc_vec3 *out) {
    tc_ai_perso *q;
    const float *m;
    tc_vec3 pos;
    if (perso_frame(p, perso, &pos, &m)) { /* first: a ghost's heading says nothing of where a portal faces */
        *out = tc_v3_add(pos, tc_v3(m[0] * off.x + m[1] * off.y + m[2] * off.z, m[3] * off.x + m[4] * off.y + m[5] * off.z,
                                    m[6] * off.x + m[7] * off.y + m[8] * off.z));
        return true;
    }
    q = perso == 0 ? (tc_ai_perso *)p : p->find ? p->find(p->find_user, perso) : NULL;
    if (!q) return false;
    {
        float co = cosf(q->heading), si = sinf(q->heading);
        *out = tc_v3_add(q->position, tc_v3(co * off.x - si * off.y, si * off.x + co * off.y, off.z));
    }
    return true;
}

static tc_vec3 perso_position(const tc_ai_perso *p, int32_t perso) {
    int o = perso == 0 ? -1 : other_index(p, perso);
    tc_ai_perso *q = live(p, perso);
    if (perso == 0) return p->position;
    if (q) return q->position;
    return o >= 0 && p->other_positions ? p->other_positions[o] : p->position;
}

/* ---- waypoints and ways (docs/ENGINE.md "Ways and waypoints") ---- */

/* Waypoint number k (1-based, as the scripts hold them), or NULL. */
static const tc_waypoint *waypoint(const tc_ai_perso *p, int32_t k) {
    return p->ways && k > 0 && k <= (int32_t)p->ways->waypoint_count ? &p->ways->waypoints[k - 1] : NULL;
}

/* AI_0048dcf0: a waypoint's world position, through its father's matrix when it has one (port: the live father's
 * position and heading; a father outside the cast leaves it in world axes). */
static tc_vec3 waypoint_position(const tc_ai_perso *p, int32_t k) {
    const tc_waypoint *w = waypoint(p, k);
    const tc_ai_perso *f;
    if (!w) return p->position;
    if (w->father && (f = w->father == p->self_id ? p : live(p, w->father)) != NULL) {
        float c = cosf(f->heading), s = sinf(f->heading);
        tc_vec3 v = w->position;
        return tc_v3_add(f->position, tc_v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z));
    }
    return w->position;
}

/* GAM_004257f0: the perso's way n (the n-th node of its MS_Way list) and its cursor, or NULL. */
static const tc_way *own_way(tc_ai_perso *p, int32_t n, uint8_t **cursor) {
    if (!p->ways || !p->own_ways || n < 0 || n >= (int32_t)p->own_ways->count || n >= TC_AI_MAX_WAYS) return NULL;
    *cursor = p->way_cursor[n];
    return &p->ways->ways[p->own_ways->first + (uint32_t)n];
}
static int32_t way_point(const tc_ai_perso *p, const tc_way *w, uint32_t k) {
    return k < w->count ? (int32_t)p->ways->points[w->first + k] : 0;
}

/* The cursor bytes (the MS_Way node's +0x10 .. +0x13). */
enum { WAY_AT, WAY_STEP, WAY_FROM, WAY_SAVED };

static void way_reinit(uint8_t *c) { c[WAY_AT] = 0, c[WAY_FROM] = 0xFF, c[WAY_STEP] = 0, c[WAY_SAVED] = 0xFF; } /* AI_0048d540 */
static void way_reinit_back(const tc_way *w, uint8_t *c) {                                       /* AI_0048d5a0 */
    c[WAY_AT] = (uint8_t)(w->count ? w->count - 1 : 0), c[WAY_FROM] = 0xFF, c[WAY_STEP] = 0, c[WAY_SAVED] = 0xFF;
}

/* AI_0048d390, on along the way (straight links only: every link in the retail levels is type 0, whose step test
 * AI_004923d0 always passes). Returns 0 at the end (the cursor back at the start), else `go_on`. */
static int way_next(const tc_way *w, uint8_t *c, int go_on) {
    c[WAY_SAVED] = c[WAY_STEP];
    if (c[WAY_AT] != 0 && c[WAY_FROM] != 0xFF) {
        c[WAY_STEP]++;
        if ((uint32_t)w->count == (uint32_t)c[WAY_AT] + 1) { way_reinit(c); return 0; }
    }
    /* AI_0048cd90 */
    if (c[WAY_FROM] == 0xFF && (uint32_t)c[WAY_AT] + 1 == w->count) c[WAY_AT] = 0;
    else c[WAY_FROM] = c[WAY_AT], c[WAY_AT]++;
    if (c[WAY_FROM] != 0xFF && c[WAY_AT] != 0) c[WAY_STEP] = 0;
    return go_on;
}

/* AI_0048d460, back along the way. Returns 0 at its start (the cursor then at the end), else `go_on`. */
static int way_back(const tc_way *w, uint8_t *c, int go_on) {
    if (c[WAY_AT] == 0 && c[WAY_SAVED] == 0) { way_reinit_back(w, c); return 0; }
    c[WAY_SAVED] = c[WAY_STEP];
    /* AI_0048cec0 */
    if (c[WAY_FROM] == 1 && c[WAY_AT] == 0) c[WAY_FROM] = 0xFF, c[WAY_AT] = (uint8_t)(w->count - 1);
    else c[WAY_FROM] = c[WAY_AT], c[WAY_AT]--;
    if (c[WAY_FROM] != 0xFF) c[WAY_STEP] = 0;
    return go_on;
}

/* ---- ZDE zones: the hits (COND 18-29, CollectNearby; docs/ENGINE.md "Shooting") ---- */

/* Zone k of q, when active in its playing state (GAM_PersoActiveZones_0041f280). */
static const tc_zde_zone *zde_zone(const tc_ai_perso *q, int32_t k) {
    const tc_zde_set *z = q && q->model ? &q->model->zde : NULL;
    if (!z || k < 0 || k >= (int32_t)z->zone_count || k >= 32 || q->state >= z->state_count) return NULL;
    return (z->active[q->state] >> k) & 1u ? &z->zones[k] : NULL;
}

/* Whether the zone holds an element of that type (GAM_0041f060: flags & mask == mask; mask 0: any). */
static bool zde_has(const tc_ai_perso *q, const tc_zde_zone *zn, uint16_t mask) {
    uint32_t k;
    for (k = 0; k < zn->count; k++)
        if ((q->model->zde.prims[zn->first + k].type & mask) == mask) return true;
    return false;
}

static tc_vec3 zde_to_world(const tc_ai_perso *q, tc_vec3 v) {
    float c = cosf(q->heading), s = sinf(q->heading);
    return tc_v3_add(q->position, tc_v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z));
}
static tc_vec3 zde_to_local(const tc_ai_perso *q, tc_vec3 w) {
    float c = cosf(q->heading), s = sinf(q->heading);
    tc_vec3 d = tc_v3_sub(w, q->position);
    return tc_v3(c * d.x + s * d.y, -s * d.x + c * d.y, d.z);
}

/* Box b (in qb's axes) against the box a in qa's axes: a's corners taken into qb's axes, their bounds against b. */
static bool zde_box_in(const tc_ai_perso *qa, const tc_zde_prim *a, const tc_ai_perso *qb, const tc_zde_prim *b) {
    tc_vec3 lo = tc_v3(INFINITY, INFINITY, INFINITY), hi = tc_v3(-INFINITY, -INFINITY, -INFINITY);
    int k;
    for (k = 0; k < 8; k++) {
        tc_vec3 c = zde_to_local(qb, zde_to_world(qa, tc_v3(k & 1 ? a->b.x : a->a.x, k & 2 ? a->b.y : a->a.y,
                                                              k & 4 ? a->b.z : a->a.z)));
        lo = tc_v3_min(lo, c), hi = tc_v3_max(hi, c);
    }
    return lo.x <= b->b.x && hi.x >= b->a.x && lo.y <= b->b.y && hi.y >= b->a.y && lo.z <= b->b.z && hi.z >= b->a.z;
}

/* FUN_004631c0 for two elements (port: spheres exact; a box against a box by their bounds in each other's axes; the
 * exe's own tests are not read). */
static bool zde_prims_meet(const tc_ai_perso *qa, const tc_zde_prim *a, const tc_ai_perso *qb, const tc_zde_prim *b) {
    if (a->kind == 8 && b->kind == 7) { const tc_ai_perso *t = qa; const tc_zde_prim *u = a; qa = qb, a = b, qb = t, b = u; }
    if (a->kind == 7 && b->kind == 7) {
        float r = a->b.x + b->b.x;
        return tc_v3_length_sq(tc_v3_sub(zde_to_world(qa, a->a), zde_to_world(qb, b->a))) <= r * r;
    }
    if (a->kind == 7 && b->kind == 8) { /* the sphere's centre into the box's axes, the nearest point of the box */
        tc_vec3 c = zde_to_local(qb, zde_to_world(qa, a->a)), n = tc_v3_max(b->a, tc_v3_min(b->b, c));
        return tc_v3_length_sq(tc_v3_sub(c, n)) <= a->b.x * a->b.x;
    }
    if (a->kind == 8 && b->kind == 8) return zde_box_in(qa, a, qb, b) && zde_box_in(qb, b, qa, a);
    return false;
}

/* Whether zone za of qa meets zone zb of qb (any of their elements; mask_b: only zb's elements of that type). */
static bool zde_zones_meet(const tc_ai_perso *qa, const tc_zde_zone *za, const tc_ai_perso *qb, const tc_zde_zone *zb) {
    uint32_t i, k;
    if (!qa || !qb || qa == qb || !za || !zb) return false;
    for (i = 0; i < za->count; i++)
        for (k = 0; k < zb->count; k++)
            if (zde_prims_meet(qa, &qa->model->zde.prims[za->first + i], qb, &qb->model->zde.prims[zb->first + k]))
                return true;
    return false;
}

/* Whether zone za of qa (NULL: any of its active zones of type mask_a) meets any active zone of qb of type mask_b. */
static bool zde_meets(const tc_ai_perso *qa, const tc_zde_zone *za, uint16_t mask_a, const tc_ai_perso *qb, uint16_t mask_b) {
    uint32_t a, b;
    if (!qa || !qb || qa == qb || !qa->model || !qb->model) return false;
    for (a = 0; a < qa->model->zde.zone_count; a++) {
        const tc_zde_zone *x = za ? za : zde_zone(qa, (int32_t)a);
        if (!x || (!za && !zde_has(qa, x, mask_a))) { if (za) break; continue; }
        for (b = 0; b < qb->model->zde.zone_count; b++) {
            const tc_zde_zone *y = zde_zone(qb, (int32_t)b);
            if (y && zde_has(qb, y, mask_b) && zde_zones_meet(qa, x, qb, y)) return true;
        }
        if (za) break;
    }
    return false;
}

/* GAM_CollectNearby_0041fa70 (mode 0): the live persos, but q itself, with an active zone of type mask meeting q's
 * zone k; up to cap of them into ids (as the scripts name them; q's own id for Me never comes up). */
static uint32_t zde_collect(tc_ai_perso *q, int32_t k, uint16_t mask, int32_t *ids, uint32_t cap) {
    const tc_zde_zone *z = zde_zone(q, k);
    tc_ai_perso *o;
    uint32_t n = 0, j;
    if (!z || !q->each) return 0;
    for (j = 0; n < cap && (o = q->each(q->find_user, j)) != NULL; j++)
        if (o != q && zde_meets(q, z, 0, o, mask)) ids[n++] = o->self_id;
    return n;
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
    p->anim_end_reached = false; /* 3dData +0x162, cleared each advance (GAM_PlayAnim_Advance_00415fb0) */
    if (p->state_ended || !p->model->state_count) return;
    float frames = dt * p->frame_rate;
    for (guard = 0; guard < 8; guard++) {
        const tc_ai_state *s = &p->model->states[p->state];
        float length = (float)(s->frames ? s->frames : 1);
        uint8_t rate = p->frame_rate;
        state_sounds(p, s, p->state_frame, fminf(length, p->state_frame + frames));
        if (p->state_frame + frames < length) { p->state_frame += frames; return; }
        frames -= length - p->state_frame;
        p->anim_end_reached = true;
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

void tc_ai_speech_end(tc_ai_globals *g) {
    if (g->speaker) g->speaker->speaking = 0;
    g->speaker = NULL;
    g->speech = 0;
}

bool tc_ai_set_state(tc_ai_perso *p, uint32_t state) { return state < p->model->state_count && change_state(p, state); }

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
    if (perso != 0) {
        tc_ai_perso *q = live(p, perso);
        if (q && f == 1) return v_vec(tc_v3(sinf(q->heading), -cosf(q->heading), 0));
        if (q && f == 2) return v_vec(q->velocity);
        if (q && f == 3) return v_float(tc_v3_length(q->velocity)); /* FIELD_3Get_004a6870: NormSpeed = |Velocity| */
        return f == 0 ? v_vec(perso_position(p, perso)) : v_int(0);
    }
    switch (f) {
    case 0: return v_vec(p->position);
    case 1: return v_vec(tc_v3(sinf(p->heading), -cosf(p->heading), 0));
    case 2: return v_vec(p->velocity);
    case 3: return v_float(tc_v3_length(p->velocity));
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
    if (l.kind == 1 && l.perso > 0 && r->p->keep_off_main) { /* port: nor his dsgvars (they start his own reflexes) */
        const tc_ai_perso *q = live(r->p, l.perso);
        /* the camera's shot (its dsg39: the sliding stages' start / finish triggers pick the ski camera with it) is
         * written, never read, by the main actor's scripts: the cast may set it (docs/CAMERA.md "Seventh pass") */
        bool shot = q && q->read_only_for_cast && l.index == TC_AI_CAMERA_SHOT_DSG && q->main_actor != 0;
        if (q && !shot && (q->main_actor == 0 || q->read_only_for_cast)) { r->p->forces_blocked++; return; }
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

/* A perso without buttons of its own (the cast) reads the main actor's: its button nodes index his (cast.py). */
static int32_t button_counter(run *r, uint32_t param) {
    const tc_ai_perso *src = r->p;
    if (!src->model->button_count && src->main_actor) {
        const tc_ai_perso *q = live(src, src->main_actor);
        if (q) src = q;
    }
    return param < src->model->button_count ? src->button_counter[param] : -2;
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

/* A perso argument as the live perso (Me, another of the cast, the main actor), or NULL. */
static tc_ai_perso *who_arg(run *r, uint32_t i, int k) {
    value v = arg(r, i, k);
    if (v.type != V_PERSO || v.i < 0) return NULL;
    return v.i == 0 ? r->p : live(r->p, v.i);
}

static bool condition(run *r, uint32_t i) {
    tc_ai_perso *p = r->p;
    uint32_t c = N(r, i)->param;
    switch (c) {
    /* COND_18Etc_00497ef0: the ZDE zones (zone numbers 1-based; the "module" variants, zones on a perso's channels, as its
     * own: port). 18-21 (A, zone, B, zone); 22 / 23 (A, zone, B, type); 24 (A, type, B, type), 25 (A, B: any), 26 (A, type,
     * B), 27 (A, B, type); 28 / 29 (A, zone, type): any other perso's zone of that type (AI_00490290) */
    case 18: case 19: case 20: case 21: {
        tc_ai_perso *a = who_arg(r, i, 0), *b = who_arg(r, i, 2);
        return a && b && zde_zones_meet(a, zde_zone(a, as_int(arg(r, i, 1)) - 1), b, zde_zone(b, as_int(arg(r, i, 3)) - 1));
    }
    case 22: case 23: {
        tc_ai_perso *a = who_arg(r, i, 0), *b = who_arg(r, i, 2);
        const tc_zde_zone *z = a ? zde_zone(a, as_int(arg(r, i, 1)) - 1) : NULL;
        return z && zde_meets(a, z, 0, b, (uint16_t)as_int(arg(r, i, 3)));
    }
    case 24: case 25: case 26: case 27: {
        tc_ai_perso *a = who_arg(r, i, 0), *b;
        uint16_t ma = 0, mb = 0;
        if (c == 24 || c == 26) ma = (uint16_t)as_int(arg(r, i, 1)), b = who_arg(r, i, 2);
        else b = who_arg(r, i, 1);
        if (c == 24) mb = (uint16_t)as_int(arg(r, i, 3));
        if (c == 27) mb = (uint16_t)as_int(arg(r, i, 2));
        return zde_meets(a, NULL, ma, b, mb);
    }
    case 28: case 29: {
        tc_ai_perso *a = who_arg(r, i, 0);
        int32_t one;
        return a && zde_collect(a, as_int(arg(r, i, 1)) - 1, (uint16_t)as_int(arg(r, i, 2)), &one, 1) > 0;
    }
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
    case 56: { /* InBehaviour(perso, behaviour): the behaviour indexes that perso's model */
        value who = arg(r, i, 0);
        uint32_t b = arg_param(r, i, 1);
        tc_ai_perso *q = who.type == V_PERSO ? (who.i == 0 ? p : live(p, who.i)) : NULL;
        if (!q || b == TC_AI_NONE) return false;
        return (b & TC_AI_REFLEX_FLAG) ? q->reflex.behaviour == (b & 0xFFFF) + q->model->normal_count
                                       : q->normal.behaviour == b;
    }
    case 61: { float d = argf(r, i, 0); return d * d > p->main_distance2; }
    case 13: case 15: { /* COND_13Etc_00497d70: b's origin inside a's ZDD zone k, 1-based (13: GAM_ZoneHolds_00420280), or any
                         * of them (15, Cond_CollidePersoAllZDDWithPersoAllZDD: GAM_AnyZoneHolds_004202c0). Port: all the
                         * exported zones count, active in the state or not, and zones on channel objects are not exported */
        bool c13 = c == 13;
        value a = arg(r, i, 0), b = arg(r, i, c13 ? 2 : 1);
        int32_t k = c13 ? as_int(arg(r, i, 1)) : -1;
        const tc_ai_perso *za = a.type == V_PERSO ? (a.i == 0 ? p : live(p, a.i)) : NULL;
        const tc_cast_entry *e = za ? za->zones_of : NULL;
        const float *m;
        tc_vec3 x, q;
        uint32_t z, j;
        if (!e) { if (c13) unknown(r, i); return false; }
        if (b.type != V_PERSO || b.i < 0) return false;
        x = b.i == 0 ? p->position : perso_position(p, b.i);
        m = e->zone_inverse;
        if (za->zones_follow) q = global_to_local(za, tc_v3_sub(x, za->position));
        else q = tc_v3(m[0] * x.x + m[1] * x.y + m[2] * x.z + m[3], m[4] * x.x + m[5] * x.y + m[6] * x.z + m[7],
                       m[8] * x.x + m[9] * x.y + m[10] * x.z + m[11]);
        for (z = 0; z < e->zone_count; z++) {
            const tc_cast_zone *zn = &e->zones[z];
            if (c13 && (int32_t)zn->index != k) continue;
            for (j = 0; j < zn->count; j++) {
                const tc_cast_zone_prim *c = &zn->prims[j];
                if (c->kind == 7 && tc_v3_distance(q, c->a) <= c->b.x) return true;
                if (c->kind == 8 && q.x >= c->a.x && q.y >= c->a.y && q.z >= c->a.z && q.x <= c->b.x && q.y <= c->b.y &&
                    q.z <= c->b.z) return true;
            }
        }
        return false;
    } /* FUN_00498bb0: the main actor within d */
    case 57: return can_change_state(p, arg_param(r, i, 0));
    case 58: return p->wanted_state == arg_param(r, i, 0);
    case 62: {
        uint32_t s = arg_param(r, i, 0);
        return p->level_name && s < p->model->string_count && p->model->strings[s] &&
               same_name(p->model->strings[s], p->level_name);
    }
    case 65: { value v = arg(r, i, 0); return v.type == V_PERSO ? v.i >= 0 && v.i != (int32_t)TC_AI_NONE : truthy(v); }
    case 66: return waypoint(r->p, as_int(arg(r, i, 0))) != NULL; /* Cond_IsValidWayPoint (COND_IsValidEtc_00498af0) */
    /* The water zones (docs/NOTES.md "Swimming"): FUN_004976f0 (70) in water: all sample points in it, or mixed with
     * point 3 or 4 in it; 71 the head (point 1) out; 72 point 1 in (or none); 73 FUN_004977f0; 74 no point in water. */
    case 51: return p->env_water; /* Cond_InEnvironmentWater (COND_50Etc_00498820) */
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
    case 90: { /* Cond_TestPower (0x4991d0 -> 0x419680): every bit of the mask */
        const tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        uint16_t m = (uint16_t)as_int(arg(r, i, 0));
        return (g->powers & m) == m;
    }
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
    case 37: return (p->report_high & 0x10000) != 0; /* COND_OnGroundEtc_00498370: report_seen & 0x10000, a perso contact */
    case 30: case 38: case 59: case 64:
        return false;    /* collision probes, zones, materials, touched objects, game state: not modelled yet */
    default: unknown(r, i); return false;
    }
}

static value function(run *r, uint32_t i) {
    tc_ai_perso *p = r->p;
    switch (N(r, i)->param) {
    case 0: { /* GetPosition: of OP 24's perso */
        value who = arg(r, i, 0);
        return v_vec(perso_position(p, who.type == V_PERSO ? who.i : r->ctx));
    }
    case 3: { /* Func_GetWPAbsolutePosition (FUNC_GetPositionEtc_0049a140 case 3 -> AI_0048dcf0); a perso: its position */
        value w = arg(r, i, 0);
        return v_vec(w.type == V_PERSO ? perso_position(p, w.i) : waypoint_position(p, as_int(w)));
    }
    case 11: { /* Func_DistanceToWP (AI_00491990): from OP 24's perso's origin */
        const tc_ai_perso *me = r->ctx ? live(p, r->ctx) : p;
        int32_t k = as_int(arg(r, i, 0));
        if (!waypoint(p, k)) return v_float(NAN);
        return v_float(tc_v3_length(tc_v3_sub((me ? me : p)->position, waypoint_position(p, k))));
    }
    case 12: { /* Func_CloserWP(way) (FUNC_12_0049a5f0): the way's waypoint nearest this perso, its last one left out */
        uint8_t *c;
        const tc_way *w = own_way(p, as_int(arg(r, i, 0)), &c);
        int32_t best = 0;
        float best_d = -1;
        uint32_t k;
        for (k = 0; w && k + 1 < w->count; k++) {
            int32_t q = way_point(p, w, k);
            float dd = tc_v3_length(tc_v3_sub(waypoint_position(p, q), p->position));
            if (best_d == -1 || dd < best_d) best_d = dd, best = q;
        }
        return v_int(best);
    }
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
    case 35: case 36: { /* FUNC_Counter34Etc_0049ad30 0x23 / 0x24: hit points + n up to the maximum (FUN_004cec30), or - n
                         * down to 0 (FUN_004cecb0), then read; the main actor's are the global table's (here his own) */
        tc_ai_perso *q = who_arg(r, i, 0);
        int32_t n = as_int(arg(r, i, 1));
        if (!q) q = p;
        q->hit_points += N(r, i)->param == 35 ? n : -n;
        if (q->hit_points < 0) q->hit_points = 0;
        if (q->hit_points_max > 0 && q->hit_points > q->hit_points_max) q->hit_points = q->hit_points_max;
        return v_int(q->hit_points);
    }
    case 34: case 37: { /* FUNC_Counter34Etc_0049ad30: the hit points (std game +0x14; the main actor's elsewhere) and
                          * their maximum (+0x15) of a perso */
        value who = arg(r, i, 0);
        const tc_ai_perso *q = who.type == V_PERSO && who.i != 0 ? live(p, who.i) : NULL;
        if (!q) q = p; /* the main character's are a global table (GAM_00419490): the skier's Counter34(fix Ed) */
        return v_int(N(r, i)->param == 34 ? q->hit_points : q->hit_points_max);
    }
    case 4: case 5: case 6: case 7: case 8: case 9: case 10:
    case 14: case 15: case 16: case 17: case 18: case 19: case 20: {
        /* FUNC_GetPositionEtc_0049a140 -> AI_00491ba0: from that perso to this one (OP 24's), in that perso's axes
         * (FUNC 4 / 14: the world's), then AI_00491ab0's part: 4 the length, 5 |x| ... (x, y, xy, z, xz, yz). 14-20 use
         * other points of both (AI_00491920, not read): the origins here. */
        static const uint8_t part[] = {7, 1, 2, 4, 3, 5, 6};
        uint32_t f = N(r, i)->param, k = f >= 14 ? f - 14 : f - 4;
        value who = arg(r, i, 0);
        const tc_ai_perso *me = r->ctx != 0 ? live(p, r->ctx) : p;
        const tc_ai_perso *q = who.type == V_PERSO ? (who.i == 0 ? p : live(p, who.i)) : NULL;
        tc_vec3 d;
        if (!me) me = p;
        if (!q && who.type == V_PERSO && who.i > 0 && other_index(p, who.i) >= 0) { /* a model copy: position only */
            d = tc_v3_sub(me->position, perso_position(p, who.i));
        } else if (!q) {
            return v_float(NAN); /* no such perso: every comparison fails */
        } else {
            d = tc_v3_sub(me->position, q->position);
            if (part[k] != 7) d = global_to_local(q, d);
        }
        switch (part[k]) {
        case 1: return v_float(d.x);
        case 2: return v_float(d.y);
        case 3: return v_float(sqrtf(d.x * d.x + d.y * d.y));
        case 4: return v_float(d.z);
        case 5: return v_float(sqrtf(d.x * d.x + d.z * d.z));
        case 6: return v_float(sqrtf(d.y * d.y + d.z * d.z));
        default: return v_float(tc_v3_length(d));
        }
    }
    case 54: { /* FUNC_54_0049b240 (Func_GetFather): with report_seen & 0x30000, the first perso up the parent chain: the
        * perso it rides (tc_mec_walk.carrier; docs/ENGINE.md "Moving collision, read"); asked as another perso
        * (InPersoContext), the asker itself reads as Me */
        const tc_ai_perso *q = r->ctx == 0 ? p : live(p, r->ctx);
        value v = v_int(-1);
        v.type = V_PERSO;
        if (q && q->standing_on > 0) v.i = q->standing_on == p->self_id ? 0 : q->standing_on;
        return v;
    }
    case 93: { /* FUNC_93Etc_0049b740 case 0x5d: a save value (GAM_004195f0: -1 out of range) */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        int32_t n = as_int(arg(r, i, 0));
        return v_int(n >= 0 && n < 31 ? g->values[n] : -1);
    }
    /* The inventory (GAM_00419440 / GAM_004193d0: the save values by the object model's slot, perso +0x34 -> u16 +2):
     * port stub, the user's choice (2026-11-05): the main actor always holds one of everything (Ed's bees: he shoots
     * as often as his scripts let him), the others nothing, as before. 96 / 97 take one: the object stays. */
    case 95: return v_int(p->main_actor == 0 ? 1 : 0);
    case 96: case 97: {
        value v = arg(r, i, 0);
        return p->main_actor == 0 ? v : v_int(0);
    }
    case 164: return v_int(0); /* the control type (0x8C175C): the port runs type 0 (docs/DECOMP.md) */
    case 40: case 41: case 42: { /* FUNC_40Etc_0049aff0: the lives byte; 41 adds one, 42 takes one first */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        if (N(r, i)->param == 41) g->lives++;
        if (N(r, i)->param == 42) g->lives--;
        return v_int(g->lives);
    }
    case 1: { /* FUNC_GetPositionEtc_0049a140: the turn from its front to the perso, degrees, the short way */
        value who = arg(r, i, 0);
        tc_vec3 at;
        if (who.type != V_PERSO || !perso_point(p, who.i, tc_v3(0, 0, 0), &at)) return v_float(0);
        return v_float(remainderf(heading_to(p, at) - p->heading, 2 * TC_PI) * 57.29578f);
    }
    case 43: return v_int((int32_t)p->time_ms);
    case 44: { /* FUNC_GetTimeEtc_0049b040: from a time got by FUNC 43, either side of now */
        uint32_t t = (uint32_t)as_int(arg(r, i, 0)), now = p->time_ms;
        return v_int((int32_t)(t > now ? t - now : now - t));
    }
    case 49: return v_float(button_analog(p, arg_param(r, i, 0)));
    /* 88, 100, 101: as OP 24's perso when there is one (the exe runs the child as that perso: the cavedoc crate reads
     * InPersoContext(Ed, LocalToGlobalVec((0, -1, 0))), Ed's front) */
    case 88: { const tc_ai_perso *q = r->ctx ? live(p, r->ctx) : p; return v_float(q ? tc_v3_length(q->velocity) : 0); }
    case 98: return v_float(tc_v3_length(argv3(r, i, 0)));
    case 99: { tc_vec3 v = argv3(r, i, 0); float l = tc_v3_length(v); return v_vec(l > 0 ? tc_v3_div(v, l) : v); }
    case 100: { const tc_ai_perso *q = r->ctx ? live(p, r->ctx) : p; return v_vec(local_to_global(q ? q : p, argv3(r, i, 0))); }
    case 101: { const tc_ai_perso *q = r->ctx ? live(p, r->ctx) : p; return v_vec(global_to_local(q ? q : p, argv3(r, i, 0))); }
    /* FUNC_VecLengthEtc_0049ab10 cases 0x66 / 0x67: a point through the perso's world matrix (FUN_0047b2f0) or its
     * inverse. Ed's underwater reflex R8 leaves the water when FUNC_102((0, 1, -1)) rises 0.55 above the surface. */
    case 13: { /* FUNC_GetPositionEtc_0049a140 case 0xd: the squared distance from OP 24's perso's origin to the point (Ed's
                * B29 fires when FUNC 162's wall is further than 10 of it, B82 climbs within 2.25: 3.2 and 1.5 units) */
        const tc_ai_perso *me = r->ctx ? live(p, r->ctx) : p;
        return v_float(tc_v3_length_sq(tc_v3_sub((me ? me : p)->position, argv3(r, i, 0))));
    }
    case 162: { /* FUNC_162_0049c690: from OP 24's perso's origin + the offset (world axes: added as it is) along the
                 * direction (in its axes, FUN_0047b3d0) to the nearest surface of its sector's lists (FUN_00484130):
                 * the hit point, in world axes; none: (FLT_MAX, ..). Port: the static world's raycast, up to 1000 (the
                 * exe's reach is not read: its uses compare FUNC 13 of it, a squared distance, with 2.25 and 10). */
        const tc_ai_perso *me = r->ctx ? live(p, r->ctx) : p;
        tc_vec3 o = argv3(r, i, 0), d = argv3(r, i, 1), from;
        float l = tc_v3_length(d), reach = 1000;
        tc_hit hit;
        if (!me) me = p;
        from = tc_v3_add(me->position, o);
        if (!(l > 0)) return v_vec(from);
        d = local_to_global(me, tc_v3_div(d, l));
        if (me->world && tc_collision_raycast(me->world, from, d, reach, &hit)) reach = hit.distance;
        return v_vec(tc_v3_add(from, tc_v3_scale(d, reach)));
    }
    case 102: case 103: if (r->ctx != 0) { /* OP 24's perso (Ed's dsg57, the thing he carries): its own frame */
        const tc_ai_perso *q = live(p, r->ctx);
        tc_vec3 v = argv3(r, i, 0);
        if (!q) return v_vec(v);
        if (N(r, i)->param == 102) return v_vec(tc_v3_add(q->position, local_to_global(q, v)));
        return v_vec(global_to_local(q, tc_v3_sub(v, q->position)));
    }
    if (N(r, i)->param == 103) {
        tc_vec3 d = tc_v3_sub(argv3(r, i, 0), p->position);
        if (!p->has_axes) return v_vec(global_to_local(p, d));
        return v_vec(tc_v3(tc_v3_dot(d, p->axes[0]), tc_v3_dot(d, p->axes[1]), tc_v3_dot(d, p->axes[2])));
    } else {
        tc_vec3 v = argv3(r, i, 0);
        if (!p->has_axes) return v_vec(tc_v3_add(p->position, local_to_global(p, v)));
        return v_vec(tc_v3_add(p->position, tc_v3_add(tc_v3_add(tc_v3_scale(p->axes[0], v.x), tc_v3_scale(p->axes[1], v.y)),
                                                        tc_v3_scale(p->axes[2], v.z))));
    }
    case 50: { value v = v_int(-1); v.type = V_PERSO; return v; }
    case 91: return v_float(p->report_2bc);
    case 92: return v_float(p->report_2c0);
    case 51: return v_float(p->env_depth);
    case 52: return v_float(p->position.z + p->env_depth);
    case 104: return v_float(0);
    default: unknown(r, i); return v_float(NAN); /* not modelled: compares false (not as 0, which is also Me) */
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
    case 24: { /* OP_24_004913d0: evaluate the second child as that perso's (AI_00491380: none when it is Me) */
        value who = arg(r, i, 0), v;
        uint32_t c = child(r, i, 1);
        int32_t saved = r->ctx;
        if (c == TC_AI_NONE) return v_int(0);
        r->ctx = who.type == V_PERSO ? who.i : 0;
        v = eval(r, c);
        r->ctx = saved;
        return v;
    }
    default: unknown(r, i); return v_int(0);
    }
}

static value eval(run *r, uint32_t i) {
    const tc_ai_node *n = N(r, i);
    value v = v_int(0);
    switch (n->type) {
    case TC_AI_KEYWORD:
        if (n->param == 4) { v.type = V_PERSO; v.i = 0; } /* Me */
        else if (n->param == 5) { v.type = V_PERSO; v.i = r->p->main_actor; } /* the main actor (the player) */
        else if (n->param == 6 && r->p->world_id) { /* World: the world perso (the engine's 0x8c1744) */
            v.type = V_PERSO;
            v.i = r->p->world_id == r->p->self_id ? 0 : r->p->world_id;
        }
        else unknown(r, i);
        return v;
    case TC_AI_COND: return v_int(condition(r, i));
    case TC_AI_OP: return operation(r, i);
    case TC_AI_FUNC: return function(r, i);
    case TC_AI_FIELD: return field_get(r, r->ctx, n->param);
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

/* The removal guard (tc_ai_perso.removal_guard): q stays, frozen, unless the main actor was near it lately. */
static bool kept_as_prop(tc_ai_perso *p, tc_ai_perso *q) {
    if (!p->removal_guard || q->main_actor == 0) return false;
    if (q->near_main_ms && q->time_ms - q->near_main_ms <= TC_AI_REMOVAL_MS) return false;
    q->frozen = true;
    p->removals_kept++;
    return true;
}

/* The perso a procedure acts on: Me, another (live) perso, or NULL. */
static tc_ai_perso *target(run *r, value who) {
    if (who.type != V_PERSO) return NULL;
    return who.i == 0 ? r->p : live(r->p, who.i);
}

static void set_mech_mode(tc_ai_perso *p, int mode) {
    p->motion.mech_mode = mode;
    if (p->motion.mode_count < 8) p->motion.modes[p->motion.mode_count++] = (int8_t)mode; /* full: dropped, as FUN_0047a370 */
}

/* Mode 5 (the turn meta-actions, PROC 247 / 248): this angle now, whatever the card's turn limits (DNM_RequestWalk_004ae392
 * case 5: (angle - position's angle) / dt). The exe's angle is in [0, 2 pi); the port's heading runs on, so it is taken the
 * short way from where the perso faces, which also keeps the drawing between ticks from spinning. */
static void request_heading(tc_ai_perso *p, float angle) {
    float h = p->heading + remainderf(angle - p->heading, 2 * TC_PI);
    p->motion.heading_set = true;
    p->motion.heading = h;
    p->motion.proposal[0xD] = h;
    queue_mech_request(p, 5);
}

/* AI_00492140: mode 3 at this rate (rad/s; the card's max turn clamps it in the request), for this frame. */
static void request_turn(tc_ai_perso *p, float rate) {
    p->motion.turn_speed = rate;
    p->motion.turning = true;
    p->motion.proposal[0xD] = rate;
    queue_mech_request(p, 3);
}

/* The perso list in dsgvar k (its count also into byte 0, which ListEmpty reads), made on first use. */
static tc_ai_list *perso_list(tc_ai_perso *p, uint32_t k) {
    uint32_t j;
    for (j = 0; j < TC_AI_MAX_LISTS; j++)
        if (p->lists[j].dsg == k + 1) return &p->lists[j];
    for (j = 0; j < TC_AI_MAX_LISTS; j++)
        if (p->lists[j].dsg == 0) { p->lists[j].dsg = k + 1; p->lists[j].count = 0; return &p->lists[j]; }
    return NULL;
}
static void list_store(tc_ai_perso *p, uint32_t k, tc_ai_list *l) {
    const tc_ai_dsg_def *def = &p->model->dsg;
    if (k < def->count && def->vars[k].offset < def->size) p->dsg[def->vars[k].offset] = l->count;
}
/* The dsgvar a list argument names (a DSGVAR node of this perso), or -1. */
static int32_t list_arg(run *r, uint32_t i, int k) {
    uint32_t c = child(r, i, k);
    if (c == TC_AI_NONE || (N(r, c)->type != TC_AI_DSGVAR && N(r, c)->type != TC_AI_DSGVAR_REF)) return -1;
    return (int32_t)N(r, c)->param;
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
    case 84: case 85: { /* Proc_ListAffectWithPersoTypeZDE(list, zone, type) (GAM_CollectNearby_0041fa70): the persos whose
                         * zone of that type meets this one's (85, a module's zone: as the perso's own, port) */
        int32_t k = list_arg(r, i, 0);
        tc_ai_list *l = k >= 0 ? perso_list(p, (uint32_t)k) : NULL;
        if (!l) return 0;
        l->count = (uint8_t)zde_collect(p, as_int(arg(r, i, 1)) - 1, (uint16_t)as_int(arg(r, i, 2)), l->ids, TC_AI_LIST_CAP);
        list_store(p, (uint32_t)k, l);
        return 0;
    }
    case 86: return 0; /* Proc_ListAffectTypeZDEWithTypeZDE: not ported */
    case 94: { /* Proc_ListSortByFamily(list, family) (AI_0048fe40): only that family's persos stay */
        int32_t k = list_arg(r, i, 0), fam = as_int(arg(r, i, 1));
        tc_ai_list *l = k >= 0 ? perso_list(p, (uint32_t)k) : NULL;
        uint8_t n = 0, j;
        if (!l) return 0;
        for (j = 0; j < l->count; j++) {
            const tc_ai_perso *q = live(p, l->ids[j]);
            if (q && q->family_type == (uint16_t)fam) l->ids[n++] = l->ids[j];
        }
        l->count = n;
        list_store(p, (uint32_t)k, l);
        return 0;
    }
    case 251: case 252: case 253: { /* Proc_ReInitWay / ReInitWayBack / InitWayWithWp(way, waypoint) (PROC_251Etc_0048c0b0) */
        uint8_t *cur;
        const tc_way *w = own_way(p, as_int(arg(r, i, 0)), &cur);
        uint32_t k;
        if (!w) return 0;
        if (c == 251) way_reinit(cur);
        else if (c == 252) way_reinit_back(w, cur);
        else { /* AI_0048d620 at the first match among all but the last waypoint */
            int32_t q = as_int(arg(r, i, 1));
            for (k = 0; k + 1 < w->count; k++)
                if (way_point(p, w, k) == q) {
                    cur[WAY_FROM] = cur[WAY_SAVED] = 0xFF, cur[WAY_AT] = (uint8_t)k, cur[WAY_STEP] = 0;
                    break;
                }
        }
        return 0;
    }
    case 66: case 68: case 69: { /* PROC_SetCounter66Etc_00489590: 66 hit points := n (FUN_004cebf0), 68 := the maximum
                                  * (FUN_004cec10), 69 += n up to the maximum (FUN_004cec30) */
        tc_ai_perso *q = target(r, arg(r, i, 0));
        int32_t n = c == 68 ? 0 : as_int(arg(r, i, 1));
        if (!q) return 0;
        if (c == 66) q->hit_points = n;
        else if (c == 68) q->hit_points = q->hit_points_max;
        else q->hit_points = q->hit_points + n <= q->hit_points_max ? q->hit_points + n : q->hit_points_max;
        return 0;
    }
    case 72: case 73: case 74: case 75: case 76: { /* PROC_72Etc_004897f0: the max hit points (std game +0x15): 72 := n
        * (FUN_004ced30), 73 := its start value (GAM_00423c20), 74 := the ceiling +0x16 (FUN_004ced50), 75 += n up to
        * the ceiling then the hit points := the max (AI_00489910), 76 -= n not below 0, the hit points clamped to it
        * (FUN_004cedf0). The SE's collect-N rewards (World B3: AddHitPointsMax(Ed, 1)) and retail's Worlds use them. */
        tc_ai_perso *q = target(r, arg(r, i, 0));
        int32_t n = (c == 73 || c == 74) ? 0 : as_int(arg(r, i, 1)) & 0xFF; /* a byte, as the exe's */
        if (!q) return 0;
        if (c == 72) q->hit_points_max = n;
        else if (c == 73) q->hit_points_max = q->hit_points_max_init;
        else if (c == 74) q->hit_points_max = q->hit_points_ceiling;
        else if (c == 75) {
            q->hit_points_max = q->hit_points_max + n <= q->hit_points_ceiling ? q->hit_points_max + n : q->hit_points_ceiling;
            q->hit_points = q->hit_points_max;
        } else {
            q->hit_points_max = q->hit_points_max > n ? q->hit_points_max - n : 0;
            if (q->hit_points > q->hit_points_max) q->hit_points = q->hit_points_max;
        }
        return 0;
    }
    case 143: case 144: { /* PROC_142Etc_0048ac30: a save value + 1 / := v (GAM_00419620) */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        int32_t n = as_int(arg(r, i, 0));
        if (n >= 0 && n < 31) g->values[n] = (int16_t)(c == 143 ? g->values[n] + 1 : as_int(arg(r, i, 1)));
        return 0;
    }
    case 77: case 78: case 79: { /* PROC_77Etc_004899e0: the lives byte := n, + 1, - 1 (GAM_00437a00 / a10 / a30) */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        if (c == 77) g->lives = (uint8_t)as_int(arg(r, i, 0));
        else if (c == 78) g->lives++;
        else g->lives--;
        return 0;
    }
    case 5: case 6: { /* ActivatePerso (GAM_Objinit_004199f0: not when dead) / DeactivatePerso (GAM_Objinit_00419a10) */
        tc_ai_perso *q = target(r, arg(r, i, 0));
        if (q && q != p && q->main_actor == 0) return 0; /* port: the player stays (his host does not read it anyway) */
        if (q && c == 6 && kept_as_prop(p, q)) return 0;
        if (q && c == 5 && !q->dead) q->active = true;
        if (q && c == 6) q->active = false;
        return 0;
    }
    case 17: case 18: case 19: case 20: case 21: case 22: { /* PROC_4Etc_00488580 -> GAM_00424280(perso, mechanics,
        * animation, radius) / GAM_004242d0 (22: the radius only): 17 (0, 0, always), 18 (0, 0, r), 19 (0, 1, always),
        * 20 (0, 1, r), 21 (1, 1, always); the radius is stored squared, -1 as it is */
        tc_ai_perso *q = target(r, arg(r, i, 0));
        float radius = (c == 18 || c == 20 || c == 22) ? argf(r, i, 1) : -1;
        if (!q) return 0;
        if (c != 22) {
            q->run_mechanics = c == 21;
            q->run_animation = c >= 19;
        }
        q->radius2 = radius == -1 ? -1 : radius * radius;
        return 0;
    }
    case TC_AI_PROC_FREEZE: { /* port: a story-checked RemovePerso (cast.py): it stays, a prop */
        tc_ai_perso *q = target(r, arg(r, i, 0));
        if (!q || q->main_actor == 0) return 0;
        q->frozen = true;
        p->removals_kept++;
        return q == p ? F_STOP : 0;
    }
    case 257: { /* PROC_4Etc_00488580 case 0x101 -> GAM_Objinit_00419b60: out of the world (inactive); re-initialised
                 * when std game +0x20 is 1 (GAM_Objinit_00419a80), else dead; its own AI stops at once */
        tc_ai_perso *q = target(r, arg(r, i, 0));
        if (!q || (q != p && q->main_actor == 0)) return 0;
        if (kept_as_prop(p, q)) return q == p ? F_STOP : 0;
        q->active = false;
        if (q->reinit) q->reinit_requested = true;
        else q->dead = true;
        return q == p ? F_STOP : 0;
    }
    case 237: case 238: { /* ForceBehaviour / ForceReflex (0x48b730 / 0x48b800): another perso's (its model's index) */
        tc_ai_perso *q = target(r, arg(r, i, 0));
        uint32_t b = arg_param(r, i, 1);
        if (!q || b == TC_AI_NONE || ((b & TC_AI_REFLEX_FLAG) != 0) != (c == 238)) return 0;
        if (q == p) return change_behaviour(r, c == 238, b);
        if (p->keep_off_main && q->main_actor == 0) { p->forces_blocked++; return 0; }
        if (c == 237) tc_ai_force_behaviour(q, (uint16_t)(b & 0xFFFF));
        else tc_ai_force_reflex(q, (uint16_t)(b & 0xFFFF));
        q->forced_by = p->self_id;
        return 0;
    }
    case 120: case 121: {
        uint32_t ch = arg_param(r, i, 0);
        if (ch < 64) {
            if (c == 120) p->hidden_channels |= (uint64_t)1 << ch;
            else p->hidden_channels &= ~((uint64_t)1 << ch);
        }
        return 0;
    }
    case 109: case 116: case 117: { /* PROC_109Etc_0048a260, PROC_116Etc_0048a410: a sector moved over time (the host's) */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        value sec = arg(r, i, 0);
        tc_ai_sector_order *o;
        if (sec.i < 0 || sec.i >= 0xFFFF || g->sector_order_count >= TC_AI_MAX_SECTOR_ORDERS) return 0;
        o = &g->sector_orders[g->sector_order_count++];
        memset(o, 0, sizeof *o);
        o->sector = (uint16_t)sec.i;
        o->a = argv3(r, i, 1);
        if (c == 109) {
            o->kind = TC_AI_SECTOR_TURN;
            o->b = argv3(r, i, 2);
            o->angle = argf(r, i, 3) * 0.01745329238474369f; /* DAT_004d2cdc */
            o->ms = as_int(arg(r, i, 4));
        } else {
            o->kind = c == 116 ? TC_AI_SECTOR_TO : TC_AI_SECTOR_BY;
            o->ms = as_int(arg(r, i, 2));
        }
        return 0;
    }
    case 200: case 201: case 202: { /* PROC_200Etc_0048b080: the powers := m, &= ~m (Proc_SubPower), |= m (Proc_AddPower) */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        uint16_t m = (uint16_t)as_int(arg(r, i, 0));
        g->powers = c == 200 ? m : c == 201 ? (uint16_t)(g->powers & ~m) : (uint16_t)(g->powers | m);
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
    case 247: case 248: { /* TurnTowardPoint(perso, x, y, z), Proc_TurnPerso(perso) (PROC_TurnTowardPointEtc_0048be20): mode 5
                           * to the angle that faces that point in the perso's axes, or the perso itself (Ed's B75 / B98
                           * walking through the sud0 passages, HEG_GenTransitionFade; the guards facing Ed) */
        value who = arg(r, i, 0);
        tc_vec3 off = c == 247 ? tc_v3(argf(r, i, 1), argf(r, i, 2), argf(r, i, 3)) : tc_v3(0, 0, 0), at;
        if (who.type == V_PERSO && perso_point(p, who.i, off, &at)) request_heading(p, heading_to(p, at));
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
    case 177: case 178: case 180: { /* the camera's mode (record +0x98), its focal (+0x8C; (cam, 0, angle): the second
                                     * argument not read) and its target (cine +0x10, GAM_004311f0): tc_ai_globals */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        int16_t mode = g->cam_mode;
        int32_t cam_perso = g->cam_perso, target = g->cam_target;
        float focal = g->cam_focal;
        if (c == 177) {
            value cam = arg(r, i, 0);
            mode = (int16_t)as_int(arg(r, i, 1));
            cam_perso = cam.type != V_PERSO ? 0 : cam.i == 0 ? p->self_id : cam.i;
        }
        else if (c == 178) focal = argf(r, i, 2);
        else {
            value who = arg(r, i, 1);
            target = who.type != V_PERSO ? 0 : who.i == 0 ? p->self_id : who.i;
        }
        /* a change counts (the outro's POC_GenVisee repeats its focal every frame: the same direction, not a new one) */
        if (mode != g->cam_mode || cam_perso != g->cam_perso || target != g->cam_target || focal != g->cam_focal) {
            g->cam_mode = mode, g->cam_perso = cam_perso, g->cam_target = target, g->cam_focal = focal;
            g->cam_serial++;
        }
        return 0;
    }
    case 210: { /* DisplayVignetteDuring(picture, ms): queued for the host (tc_ai_globals.vignette) */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        uint32_t s = arg_param(r, i, 0);
        const char *name = s < p->model->string_count ? p->model->strings[s] : NULL;
        int32_t ms = as_int(arg(r, i, 1));
        size_t k;
        if (!name) return 0;
        for (k = 0; name[k] && k + 1 < sizeof(g->vignette); k++) g->vignette[k] = name[k];
        g->vignette[k] = 0;
        g->vignette_ms = ms > 0 ? (uint32_t)ms : 0;
        g->vignette_serial++;
        return 0;
    }
    case 0: p->restart_requested = true; return 0; /* GameAction0: the host restarts (tc_ed_ai) */
    case 104: { tc_ai_globals *g = p->globals ? p->globals : &p->own_globals; g->speech_stop = true; return 0; }
    case 16: p->motion.detach = true; return 0; /* PROC_12Etc_00488340 -> GAM_00428ea0: off its carrier */
    case 25: case 26: case 32: case 33: case 70: case 71: case 80:
    case 105: case 106: case 107: case 108:
    case 154: case 155: case 236: case 278: case 279: case 291: case 292:
        return 0; /* world, sound, text, camera and channel effects: no-ops for now */
    default: unknown(r, i); return 0;
    }
}

/* FUN_0049f580 (by FUN_0049f4e0): towards a point, there within `radius` (FUN_0049f530). This frame's mode 12 requests
 * go (FUN_0047a4a0); unless there, the unit direction goes into the proposal [7..9] and mode 0x14 is asked for (the
 * speed [10] stays the script's: SetSpeed, SetMoveDirection). Returns F_META_DONE there, else F_STOP. */
static uint32_t go_along(tc_ai_perso *p, tc_vec3 d, bool there) {
    float l = tc_v3_length(d);
    uint8_t k, n = 0;
    for (k = 0; k < p->motion.mode_count; k++)
        if (p->motion.modes[k] != 12) p->motion.modes[n++] = p->motion.modes[k];
    p->motion.mode_count = n;
    if (there) return F_META_DONE;
    if (d.x != 0 || d.y != 0 || d.z != 0) d = tc_v3_scale(d, 1 / l);
    p->motion.proposal[7] = d.x, p->motion.proposal[8] = d.y, p->motion.proposal[9] = d.z;
    p->motion.direction_set = true;
    set_mech_mode(p, 20);
    return F_STOP;
}

static uint32_t go_to(tc_ai_perso *p, tc_vec3 target, float radius) {
    tc_vec3 d = tc_v3_sub(target, p->position);
    return go_along(p, d, tc_v3_length(d) <= radius); /* FUN_0049f530 */
}

/* The same away from a point (0x49fac0 swaps the two points), done once at least that far (0x49fa10). */
static uint32_t run_away(tc_ai_perso *p, tc_vec3 from, float radius) {
    tc_vec3 d = tc_v3_sub(p->position, from);
    return go_along(p, d, tc_v3_length(d) >= radius);
}

/* META 15-19's start (META_15_004a0130, 16, 17, 18, TurnTowardOverFrames): the short way from its heading to the point,
 * spread over `n` (frames, META 18 / 19: at least 1; else ms, the slot's per-ms rate over |n|). */
static void turn_start(tc_ai_perso *p, tc_ai_slot *slot, tc_vec3 at, int32_t n, bool frames) {
    float span = (float)n;
    if (frames) {
        if (n < 0) n = 1, span = 1;
    } else {
        if (span <= 0) span = -span;
        if (span == 0) span = 1e-6f;
    }
    slot->a = n;
    slot->v[0] = p->heading;
    slot->v[1] = remainderf(heading_to(p, at) - p->heading, 2 * TC_PI) / span;
}

/* META 22 .. 26 (FUN_0049fd40(perso, go_on, back)): towards the way's current waypoint; there, on to the next one
 * (back: the one before). GoOverWay (22) and GoOverWayBack (24) go on until the way's end, GoToNextWPOfWay (25) and
 * its back twin (26) stop at the next waypoint. The target is the waypoint itself (AI_0048d060: a straight link's point
 * is its end waypoint; port: in its father's live axes too, where the exe's link holds a copy). */
static uint32_t go_over_way(tc_ai_perso *p, int32_t n, int go_on, int back) {
    uint8_t *c;
    const tc_way *w = own_way(p, n, &c);
    const tc_waypoint *wp;
    int32_t k;
    if (!w || c[WAY_AT] >= w->count || !(wp = waypoint(p, k = way_point(p, w, c[WAY_AT])))) return F_META_DONE;
    if (go_to(p, waypoint_position(p, k), wp->radius) == F_STOP) return F_STOP;
    return (back ? way_back(w, c, go_on) : way_next(w, c, go_on)) ? F_STOP : F_META_DONE;
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
        case 0: { /* IncrementalTurn2(degrees, ms) (META_0_0049ee00): a = ms, b = elapsed, the rate in rad/s */
            float deg = argf(r, i, 0), ms;
            slot->a = as_int(arg(r, i, 1));
            slot->b = 0;
            ms = (float)slot->a;
            if (ms <= 0) ms = -ms;
            if (ms == 0) ms = 1e-6f;
            slot->v[0] = deg * 0.017453292f / (ms * 0.001f);
            break;
        }
        case 1: { /* IncrementalTurn(degrees, frames) (META_1_0049ed30): a = frames left, the rate as read: degrees per frame
                   * as rad/s */
            int32_t n = as_int(arg(r, i, 1));
            float deg = argf(r, i, 0);
            if (n < 0) n = 1;
            slot->a = n;
            slot->v[0] = n ? deg / (float)n * 0.017453292f : 0;
            break;
        }
        case 2: case 14: { /* Pursuit, RunAway(perso, radius): b = the perso */
            value who = arg(r, i, 0);
            slot->b = who.type == V_PERSO ? who.i : -1;
            slot->v[0] = argf(r, i, 1);
            break;
        }
        case 3: { /* DeltaPursuit(perso, radius, x, y, z): the point in the perso's axes */
            value who = arg(r, i, 0);
            slot->b = who.type == V_PERSO ? who.i : -1;
            slot->v[0] = argf(r, i, 1);
            slot->v[1] = argf(r, i, 2), slot->v[2] = argf(r, i, 3), slot->v[3] = argf(r, i, 4);
            break;
        }
        case 20: slot->a = as_int(arg(r, i, 0)); slot->v[0] = argf(r, i, 1); break; /* RunAwayWP(waypoint, radius) */
        case 27: { /* GoToPosition(point, radius) */
            tc_vec3 v = argv3(r, i, 0);
            slot->v[1] = v.x, slot->v[2] = v.y, slot->v[3] = v.z;
            slot->v[0] = argf(r, i, 1);
            break;
        }
        case 15: turn_start(p, slot, argv3(r, i, 0), as_int(arg(r, i, 1)), false); break; /* (point, ms) */
        case 16: case 17: case 18: case 19: { /* (perso, ms or frames [, x, y, z]): 16 / 17 in ms, 18 / 19 in frames */
            value who = arg(r, i, 0);
            tc_vec3 at, off = m == 16 || m == 18 ? tc_v3(argf(r, i, 2), argf(r, i, 3), argf(r, i, 4)) : tc_v3(0, 0, 0);
            if (who.type != V_PERSO || !perso_point(p, who.i, off, &at)) { slot->a = 0; slot->v[1] = 0; break; }
            turn_start(p, slot, at, as_int(arg(r, i, 1)), m >= 18);
            break;
        }
        case 5: case 6: break; /* WaitEndOfAction, WaitEndOfAnim: no arguments */
        case 7: slot->a = (int32_t)arg_param(r, i, 0); break; /* SpeakAndWaitEnd(dialogue): 1-based, the level's */
        case 29: { /* Wait(ms): FUN_004a0b00 */
            int32_t ms = as_int(arg(r, i, 0));
            if (p->brisk && ms > 250) ms = 250;
            slot->a = (int32_t)(p->time_ms + (uint32_t)ms);
            break;
        }
        case 28: { /* Action_MoveLift(from, to, time): its arguments, once (META_28_004a0990); the first frame resets */
            tc_vec3 a = argv3(r, i, 0), b = argv3(r, i, 1);
            slot->v[0] = a.x, slot->v[1] = a.y, slot->v[2] = a.z;
            slot->v[3] = b.x, slot->v[4] = b.y, slot->v[5] = b.z;
            slot->v[6] = argf(r, i, 2);
            slot->a = 1;
            break;
        }
        case 21: slot->a = as_int(arg(r, i, 0)); break; /* Action_GoToWP(waypoint): META_2Etc_0048f4c0 keeps it */
        case 22: case 23: case 24: case 25: case 26: /* the way (FUN_0049fca0: a float is made an int) */
            slot->a = as_int(arg(r, i, 0));
            break;
        case 11: { /* Action_ExecuteActionAndGenerateObject(perso, state, vector) (META_SpawnInState_0049f190): when this
                    * perso may go to the state (GAM_00437600), an instance of the perso's model at this one's place
                    * (GAM_Always_004259a0; the host's spawn), this perso into the state, the instance's ProposalVecA :=
                    * the vector; then as ChangeStateAndWait (continuation 0x49ef70) */
            value who = arg(r, i, 0);
            uint32_t s = arg_param(r, i, 1);
            tc_vec3 v = argv3(r, i, 2);
            tc_ai_perso *o = NULL;
            slot->a = 0;
            slot->b = (int32_t)s;
            if (s < p->model->state_count && can_change_state(p, s) && who.type == V_PERSO && who.i > 0 && p->spawn)
                o = p->spawn(p->find_user, who.i, p, v);
            if (o && change_state(p, s)) {
                float *op = o->motion.proposal;
                op[1] = v.x, op[2] = v.y, op[3] = v.z, op[0] = tc_v3_length(v); /* AI_00488070 */
                slot->a = 1;
            }
            break;
        }
        case 40: slot->a = 0; break; /* not modelled: finish at once */
        default: unknown(r, i); slot->a = 0; break;
        }
    }
    switch (m) {
    case 4: case 11:
        if (slot->a && p->wanted_state == (uint32_t)slot->b && !p->state_ended) return F_STOP;
        break;
    case 29: /* FUN_004a0b30: running until the clock passes start + ms (the ski props' sleep, the spring's jump) */
        if ((int32_t)(p->time_ms - (uint32_t)slot->a) < 0) return F_STOP;
        break;
    case 28: { /* META_28Cont_004a09c0: ProposalVecA = from, ProposalVecD = to, [0] = time, then the card type 6 request
        * mode 14 (to the start) on the first frame, mode 15 (on along the way) after, until its mechanics stop
        * (velocity_snap 0); docs/ENGINE.md "Moving collision, read" */
        float *pr = p->motion.proposal;
        uint32_t k;
        for (k = 0; k < 6; k++) pr[1 + k] = slot->v[k];
        pr[0] = slot->v[6];
        if (slot->a == 1) {
            slot->a = 0;
            queue_mech_request(p, 14);
            return F_STOP;
        }
        if (!p->mech_moving) break;
        queue_mech_request(p, 15);
        return F_STOP;
    }
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
    case 21: { /* FUN_0049fc50: towards the waypoint, there within its radius */
        const tc_waypoint *wp = waypoint(p, slot->a);
        if (wp && go_to(p, waypoint_position(p, slot->a), wp->radius) == F_STOP) return F_STOP;
        break;
    }
    case 22: case 23: /* 23 (GoOverWay3D, FUN_0049fe90: not read, unused in the retail levels) as 22 */
        if (go_over_way(p, slot->a, 1, 0) == F_STOP) return F_STOP;
        break;
    case 24: if (go_over_way(p, slot->a, 1, 1) == F_STOP) return F_STOP; break;
    case 25: if (go_over_way(p, slot->a, 0, 0) == F_STOP) return F_STOP; break;
    case 26: if (go_over_way(p, slot->a, 0, 1) == F_STOP) return F_STOP; break;
    case 0: /* META_0Cont_0049eeb0: elapsed against the ms */
        if (slot->a <= slot->b) break;
        slot->b += (int32_t)(p->dt * 1000 + 0.5f);
        request_turn(p, slot->v[0]);
        return F_STOP;
    case 1: /* META_1Cont_0049eda0 */
        if (slot->a < 1) break;
        slot->a--;
        request_turn(p, slot->v[0]);
        return F_STOP;
    case 2: if (go_to(p, perso_position(p, slot->b), slot->v[0]) == F_STOP) return F_STOP; break;
    case 3: { /* META_3Cont_0049fbc0 */
        tc_vec3 at;
        if (perso_point(p, slot->b, tc_v3(slot->v[1], slot->v[2], slot->v[3]), &at) && go_to(p, at, slot->v[0]) == F_STOP)
            return F_STOP;
        break;
    }
    case 7: { /* META_7_0049efc0, META_7Cont_0049f000: not speaking, it starts (GAM_004180a0), and waits for 3dData +0x1A0 to
               * clear. Port: a line waits for the one on show (the exe's would leave that speaker waiting for ever) */
        tc_ai_globals *g = p->globals ? p->globals : &p->own_globals;
        if (!g->speech_host) break;
        if (slot->a > 0 && !p->speaking && !g->speech) {
            g->speech = p->speaking = (uint32_t)slot->a;
            g->speaker = p;
            slot->a = 0;
            return F_STOP;
        }
        if (p->speaking || slot->a > 0) return F_STOP;
        break;
    }
    case 5: if (!p->state_ended) return F_STOP; break;                         /* 3dData +0x161 */
    case 6: if (!p->anim_end_reached && !p->state_ended) return F_STOP; break; /* +0x162; held at the end, it stays reached */
    case 14: if (run_away(p, perso_position(p, slot->b), slot->v[0]) == F_STOP) return F_STOP; break;
    case 20: {
        const tc_waypoint *wp = waypoint(p, slot->a);
        if (wp && run_away(p, waypoint_position(p, slot->a), slot->v[0]) == F_STOP) return F_STOP;
        break;
    }
    case 27: if (go_to(p, tc_v3(slot->v[1], slot->v[2], slot->v[3]), slot->v[0]) == F_STOP) return F_STOP; break;
    case 15: case 16: case 17: /* META_15EtcCont_004a0850: ms left, the angle moves by rate x ms */
    case 18: case 19: {        /* META_18EtcCont_004a05d0: frames left, the angle moves by the rate */
        int32_t ms = (int32_t)(p->dt * 1000 + 0.5f);
        if (slot->a < 1) break;
        slot->a -= m >= 18 ? 1 : ms;
        slot->v[0] += slot->v[1] * (m >= 18 ? 1 : (float)ms);
        request_heading(p, slot->v[0]);
        return F_STOP;
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
    r.ctx = 0;
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
    if (m->normal_count) set_behaviour(p, &p->normal, m->initial_normal < m->normal_count ? m->initial_normal : 0);
    else p->normal.behaviour = 0xFFFF; /* none (a model of reflexes only: the SE's silver icons) */
    if (m->initial_reflex < m->reflex_count) set_behaviour(p, &p->reflex, (uint32_t)m->normal_count + m->initial_reflex);
    else p->reflex.behaviour = 0xFFFF;
    p->time_ms = 60000;
    p->rng = 0x1234567u;
    p->own_globals.powers = TC_AI_POWERS_ALL;
    p->hit_points = 4;
    p->hit_points_max = p->hit_points_max_init = 4;
    p->hit_points_ceiling = 255;
    p->active = true;
    p->run_mechanics = p->run_animation = 1;
    p->radius2 = -1;
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
    for (i = 0; i < TC_AI_MAX_WAYS; i++) way_reinit(p->way_cursor[i]);
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

static void step_begin(tc_ai_perso *p, float dt) {
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
}

static void think(tc_ai_perso *p) {
    update_buttons(p);
    p->running_reflex = true;
    if (p->reflex.behaviour == 0xFFFF || !run_intel(p, &p->reflex)) {
        p->running_reflex = false;
        if (p->normal.behaviour != 0xFFFF) run_intel(p, &p->normal);
    }
    p->running_reflex = false;
}

void tc_ai_step(tc_ai_perso *p, float dt) {
    step_begin(p, dt);
    p->time_ms += (uint32_t)(dt * 1000 + 0.5f);
    think(p);
    advance_state(p, dt);
}

void tc_ai_step_parts(tc_ai_perso *p, float dt, bool think_now, bool animate) {
    step_begin(p, dt);
    if (think_now) think(p);
    if (animate) advance_state(p, dt);
}
