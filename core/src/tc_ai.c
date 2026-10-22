#include "tc_internal.h"
#include "tc_reader.h"
#include "tonic/tc_ai.h"
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

static void free_dsg_def(tc_ai_dsg_def *d) { tc_free(d->vars); tc_free(d->initial); }

tc_result tc_ai_model_load(tc_ai_model *m, const uint8_t *data, size_t size) {
    reader r = {data, size, 0, TC_OK};
    uint32_t i, k;
    memset(m, 0, sizeof(*m));
    if (size < 8 || memcmp(data, "TTAI", 4) != 0) return TC_ERR_BAD_MAGIC;
    r.offset = 4;
    if (rd_u32(&r) != 1) return TC_ERR_UNSUPPORTED_VERSION;

    m->node_count = rd_u32(&r);
    m->nodes = (tc_ai_node *)rd_array(&r, m->node_count, sizeof(tc_ai_node), 8);
    for (i = 0; m->nodes && i < m->node_count; i++) {
        m->nodes[i].param = rd_u32(&r);
        m->nodes[i].type = rd_u8(&r);
        m->nodes[i].depth = rd_u8(&r);
        (void)rd_u16(&r);
    }
    m->script_count = rd_u32(&r);
    m->scripts = (uint32_t *)rd_array(&r, m->script_count, 4, 4);
    for (i = 0; m->scripts && i < m->script_count; i++) {
        m->scripts[i] = rd_u32(&r);
        if (m->scripts[i] >= m->node_count) r.error = TC_ERR_TRUNCATED;
    }
    m->normal_count = rd_u16(&r);
    m->reflex_count = rd_u16(&r);
    k = (uint32_t)m->normal_count + m->reflex_count;
    m->behaviours = (tc_ai_behaviour *)rd_array(&r, k, sizeof(tc_ai_behaviour), 8);
    for (i = 0; m->behaviours && i < k; i++) {
        tc_ai_behaviour *b = &m->behaviours[i];
        b->schedule = rd_i32(&r);
        b->first_script = rd_u16(&r);
        b->script_count = rd_u16(&r);
        if ((uint32_t)b->first_script + b->script_count > m->script_count ||
            (b->schedule >= 0 && (uint32_t)b->schedule >= m->script_count))
            r.error = TC_ERR_TRUNCATED;
        if (b->script_count > m->max_rules) m->max_rules = b->script_count;
    }
    m->initial_normal = rd_u16(&r);
    m->initial_reflex = rd_u16(&r);
    rd_dsg_def(&r, &m->dsg);

    m->state_count = rd_u16(&r);
    m->initial_state = rd_u16(&r);
    m->states = (tc_ai_state *)rd_array(&r, m->state_count, sizeof(tc_ai_state), 20);
    for (i = 0; m->states && i < m->state_count; i++) {
        tc_ai_state *s = &m->states[i];
        s->next = rd_i16(&r);
        s->frames = rd_u16(&r);
        s->speed = rd_u8(&r);
        s->flags = rd_u8(&r);
        s->transition_count = rd_u16(&r);
        s->prohibited_count = rd_u16(&r);
        s->repeats = rd_u8(&r);
        (void)rd_u8(&r);
        s->anim = rd_u16(&r);
        (void)rd_u16(&r);
        s->ground = rd_f32(&r);
        s->transitions = (tc_ai_transition *)rd_array(&r, s->transition_count, sizeof(tc_ai_transition), 4);
        for (k = 0; s->transitions && k < s->transition_count; k++) {
            s->transitions[k].target = rd_u16(&r);
            s->transitions[k].via = rd_u16(&r);
        }
        s->prohibited = (uint16_t *)rd_array(&r, s->prohibited_count, 2, 2);
        for (k = 0; s->prohibited && k < s->prohibited_count; k++) s->prohibited[k] = rd_u16(&r);
    }
    m->button_count = rd_u16(&r);
    (void)rd_u16(&r);
    m->buttons = (tc_ai_button *)rd_array(&r, m->button_count, sizeof(tc_ai_button), 8);
    for (i = 0; m->buttons && i < m->button_count; i++) {
        m->buttons[i].inputs = rd_u16(&r);
        m->buttons[i].analog = rd_u8(&r);
        (void)rd_u8(&r);
        m->buttons[i].lo = rd_i16(&r);
        m->buttons[i].hi = rd_i16(&r);
    }
    m->string_count = rd_u16(&r);
    m->strings = (char **)rd_array(&r, m->string_count, sizeof(char *), 2);
    for (i = 0; m->strings && i < m->string_count; i++) m->strings[i] = rd_string(&r);
    m->vector_count = rd_u16(&r);
    m->vectors = (tc_vec3 *)rd_array(&r, m->vector_count, sizeof(tc_vec3), 12);
    for (i = 0; m->vectors && i < m->vector_count; i++) {
        m->vectors[i].x = rd_f32(&r);
        m->vectors[i].y = rd_f32(&r);
        m->vectors[i].z = rd_f32(&r);
    }
    m->other_count = rd_u16(&r);
    (void)rd_u16(&r);
    m->others = (tc_ai_other *)rd_array(&r, m->other_count, sizeof(tc_ai_other), 12);
    for (i = 0; m->others && i < m->other_count; i++) {
        m->others[i].id = rd_u16(&r);
        m->others[i].role = rd_u8(&r);
        (void)rd_u8(&r);
        rd_dsg_def(&r, &m->others[i].dsg);
    }
    if (!r.error && m->node_count && m->nodes[m->node_count - 1].type != TC_AI_END) r.error = TC_ERR_TRUNCATED;
    if (r.error) {
        tc_result e = r.error;
        tc_ai_model_free(m);
        return e;
    }
    return TC_OK;
}

void tc_ai_model_free(tc_ai_model *m) {
    uint32_t i;
    for (i = 0; m->states && i < m->state_count; i++) {
        tc_free(m->states[i].transitions);
        tc_free(m->states[i].prohibited);
    }
    for (i = 0; m->strings && i < m->string_count; i++) tc_free(m->strings[i]);
    for (i = 0; m->others && i < m->other_count; i++) free_dsg_def(&m->others[i].dsg);
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

/* ---- the perso's frame ---- */

static tc_vec3 local_to_global(const tc_ai_perso *p, tc_vec3 v) {
    float c = cosf(p->heading), s = sinf(p->heading);
    return tc_v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}
static tc_vec3 global_to_local(const tc_ai_perso *p, tc_vec3 v) {
    float c = cosf(p->heading), s = sinf(p->heading);
    return tc_v3(c * v.x + s * v.y, -s * v.x + c * v.y, v.z);
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

static void state_play(tc_ai_perso *p, uint32_t s) {
    p->state = (uint16_t)s;
    p->state_frame = 0;
    p->frame_rate = p->model->states[s].speed ? p->model->states[s].speed : 30;
    p->repeats_left = p->model->states[s].repeats;
    p->state_ended = false;
    p->state_changes++;
}

static bool change_state(tc_ai_perso *p, uint32_t target) {
    uint32_t play;
    if (target >= p->model->state_count) return false;
    if (target == p->wanted_state && !p->state_ended) return true;
    if (!state_allowed(p, target, &play)) return false;
    p->wanted_state = (uint16_t)target;
    state_play(p, play < p->model->state_count ? play : target);
    return true;
}

static bool can_change_state(const tc_ai_perso *p, uint32_t target) {
    uint32_t play;
    return target == p->wanted_state || (target < p->model->state_count && state_allowed(p, target, &play));
}

/* GAM_PlayAnim_00415fb0 / 00415c00: at the end of the animation, repeat it (State +0x3A times), else play
 * the next state (the requested state, InState, stays); with no next state hold the last frame, "ended". */
static void advance_state(tc_ai_perso *p, float dt) {
    int guard;
    if (p->state_ended) return;
    float frames = dt * p->frame_rate;
    for (guard = 0; guard < 8; guard++) {
        const tc_ai_state *s = &p->model->states[p->state];
        float length = (float)(s->frames ? s->frames : 1);
        uint8_t rate = p->frame_rate;
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
    case 73: case 74: return true; /* ZoneTest (true while COND_70 is false), NotInAnyZone: no zones yet */
    case 90: { uint32_t n = (uint32_t)as_int(arg(r, i, 0)); return n < 32 ? (p->global_flags >> n) & 1 : (p->global_flags & n) != 0; }
    case 30: case 37: case 38: case 44: case 45: case 46: case 47: case 59: case 63: case 64:
    case 69: case 70: case 71: case 72:
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
    case 27: { int32_t lo = as_int(arg(r, i, 0)), hi = as_int(arg(r, i, 1));
               return v_int(hi > lo ? lo + (int32_t)(rnd(p) * (float)(hi - lo + 1)) : lo); }
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
    case 50: { value v = v_int(-1); v.type = V_PERSO; return v; }
    case 51: case 52: case 91: case 92: case 104: return v_float(0);
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
        if (n->param == 4) { v.type = V_PERSO; v.i = 0; } /* Me */
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

static void set_mech_mode(tc_ai_perso *p, int mode) { p->motion.mech_mode = mode; }

static uint32_t procedure(run *r, uint32_t i) {
    tc_ai_perso *p = r->p;
    float *pr = p->motion.proposal;
    uint32_t c = N(r, i)->param;
    static const signed char mech[] = { /* PROC 51..65 -> mechanics mode */
        23, 12, 20, 3, 4, 1, 2, 10, 21, 6, 7, 8, 21, 19, 22};
    switch (c) {
    case 9: p->g_flags = (uint8_t)as_int(arg(r, i, 0)); p->g_armed = 1; return 0;
    case 10: { /* GAM_0041ec90: frame rate = state rate * percent / 100, as a byte */
        uint32_t rate = (uint32_t)p->model->states[p->state].speed * (uint32_t)as_int(arg(r, i, 0)) / 100;
        p->frame_rate = (uint8_t)rate;
        return 0;
    }
    case 7: { uint32_t s = arg_param(r, i, 0); p->change_level = s == TC_AI_NONE ? -1 : (int32_t)s; return 0; }
    case 34: p->motion.stop = true; memset(pr, 0, sizeof(p->motion.proposal)); return 0;
    case 38: {
        tc_vec3 a = argv3(r, i, 0), b = argv3(r, i, 2);
        pr[1] = a.x; pr[2] = a.y; pr[3] = a.z; pr[0x11] = argf(r, i, 1); pr[0x12] = b.x; pr[0x13] = b.y; pr[0x14] = b.z;
        return 0;
    }
    case 39: pr[0x18] = argf(r, i, 0); return 0;
    case 40: pr[0xB] = argf(r, i, 0); return 0;
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
    case 244: p->motion.jump = true; p->motion.jump_vector = argv3(r, i, 0); p->motion.jump_height = argf(r, i, 1);
        set_mech_mode(p, 9);
        return 0;
    case 245: case 246: {
        float a = argf(r, i, 0) * TC_PI / 180;
        if (c == 246) a = p->dt > 0 ? a / p->dt : 0;
        p->motion.turn_speed = a;
        p->motion.turning = true;
        set_mech_mode(p, 3);
        return 0;
    }
    case 249: return change_state(p, arg_param(r, i, 0)) ? 0 : F_ABORT;
    case 0: case 2: case 5: case 25: case 26: case 27: case 32: case 33: case 70: case 71: case 79: case 80:
    case 84: case 85: case 86: case 105: case 106: case 107: case 108: case 145: case 146: case 147: case 148:
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
        case 11: case 12: case 19: case 21: case 40: slot->a = 0; break; /* not modelled: finish at once */
        default: unknown(r, i); slot->a = 0; break;
        }
    }
    switch (m) {
    case 4:
        if (slot->a && p->wanted_state == (uint32_t)slot->b && !p->state_ended) return F_STOP;
        break;
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
        p->frame_rate = m->states[p->state].speed ? m->states[p->state].speed : 30;
        p->repeats_left = m->states[p->state].repeats;
    }
    p->motion.mech_mode = -1;
    return m->state_count ? TC_OK : TC_ERR_TRUNCATED;
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
    memcpy(axes, p->motion.axes, sizeof(axes));
    memset(&p->motion, 0, sizeof(p->motion));
    p->motion.mech_mode = mode;          /* the mechanics mode stays until the AI picks another */
    memcpy(p->motion.axes, axes, sizeof(axes));
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
