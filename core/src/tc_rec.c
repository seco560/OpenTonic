/* Recording and replaying play runs: tc_rec.h, docs/RECORDING.md. */
#include "tc_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tc_reader.h"
#include "tc_rec_hooks.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_cast.h"
#include "tonic/tc_ed_ai.h"

/* File: "TTRC", u32 version, then records: u8 type, u32 payload size, payload. An unknown type is skipped. */
#define HEADER_SIZE 8u
#define RECORD_HEAD 5u
/* TC_RT_FRAME's mask: which inputs follow its dt. Held and the sticks stay as they were when absent, the mouse is 0. */
enum { IN_HELD = 1, IN_MOUSE = 2, IN_MOVE = 4, IN_LOOK = 8 };

struct tc_recorder {
    tc_rec_sink sink;
    void *user;
    uint8_t *buf;
    size_t len, cap;
    bool started;                /* the file's header is out */
    tc_viewer *viewer;           /* the open segment's, NULL between them */
    uint32_t frame, draws, marks;
    uint32_t held;
    float move[2], look[2];
    float sample_wait, cast_wait;
    bool has_story;
    int32_t lives;
    int16_t values[31];
};

/* ---- hashes ---- */

static uint32_t fold(uint64_t h) { return (uint32_t)(h ^ (h >> 32)); }

static uint64_t hash_u32(uint64_t h, uint32_t x) { return tc_hash64(h, &x, sizeof x); }

/* Everything a perso's run depends on that a sample doesn't hold; dsgvar memory as its model sizes it. */
static uint64_t hash_perso(uint64_t h, const tc_ai_perso *p) {
    h = tc_hash64(h, &p->position, sizeof p->position);
    h = tc_hash64(h, &p->velocity, sizeof p->velocity);
    h = tc_hash64(h, &p->heading, sizeof p->heading);
    h = hash_u32(h, p->normal.behaviour);
    h = hash_u32(h, p->reflex.behaviour);
    h = hash_u32(h, p->state);
    h = tc_hash64(h, &p->state_frame, sizeof p->state_frame);
    h = hash_u32(h, (uint32_t)p->hit_points);
    h = hash_u32(h, (uint32_t)p->active | (uint32_t)p->dead << 1);
    h = hash_u32(h, p->rng);
    if (p->dsg && p->model) h = tc_hash64(h, p->dsg, p->model->dsg.size);
    return h;
}

static uint32_t hash_globals(const tc_ai_globals *g) {
    uint64_t h = hash_u32(1, g->lives);
    h = tc_hash64(h, g->values, sizeof g->values);
    h = hash_u32(h, g->has_checkpoint);
    h = tc_hash64(h, &g->checkpoint, sizeof g->checkpoint);
    h = hash_u32(h, (uint32_t)g->cam_mode);
    h = hash_u32(h, g->cam_serial);
    return fold(hash_u32(h, g->vignette_serial));
}

/* What the core hands a frontend for a frame: the draw list, and the frame's values at a fixed aspect (a window's shape
 * is no part of the run). */
static void hash_output(const tc_viewer *v, const tc_view_draw *draws, uint32_t count, uint64_t *draw, uint64_t *frame) {
    static const float aspect = 1;
    tc_view_frame f;
    tc_viewer_frame(v, aspect, &f);
    *draw = tc_hash64(hash_u32(1, count), draws, (size_t)count * sizeof(*draws));
    *frame = tc_hash64(1, &f, sizeof f);
}

/* ---- samples ---- */

static void read_sample(const tc_viewer *v, uint32_t frame, tc_rec_sample *s) {
    const tc_player *pl = tc_viewer_player(v);
    const tc_ed_ai *d = tc_viewer_driver(v);
    const tc_cast *c = tc_viewer_cast(v);
    uint32_t i;
    memset(s, 0, sizeof *s);
    s->frame = frame;
    s->flags = (v->playing ? TC_RS_PLAYING : 0) | (v->paused ? TC_RS_PAUSED : 0) |
               (tc_viewer_ai_active(v) ? TC_RS_AI : 0) | (v->director ? TC_RS_DIRECTOR : 0) |
               (v->cam.first_person ? TC_RS_FIRST_PERSON : 0) | (v->cam.closeup ? TC_RS_CLOSEUP : 0) |
               (v->picture_left > 0 ? TC_RS_PICTURE : 0);
    if (pl) {
        s->position[0] = pl->position.x, s->position[1] = pl->position.y, s->position[2] = pl->position.z;
        s->velocity[0] = pl->velocity.x, s->velocity[1] = pl->velocity.y, s->velocity[2] = pl->velocity.z;
        s->heading = pl->heading;
        s->placed = pl->placed;
        if (pl->grounded) s->flags |= TC_RS_GROUNDED;
    }
    s->eye[0] = v->camera.position.x, s->eye[1] = v->camera.position.y, s->eye[2] = v->camera.position.z;
    s->yaw = v->camera.yaw;
    s->pitch = v->camera.pitch;
    s->vertical_fov = v->camera.vertical_fov;
    s->orbit_distance = v->orbit_distance;
    s->animation_time = v->animation_time;
    s->ed_phase = v->ed_phase;
    s->ed_clip = v->ed_clip;
    s->behaviour = s->state = s->wanted_state = s->card = -1;
    if (d) {
        s->behaviour = d->ai.normal.behaviour;
        s->state = d->ai.state;
        s->wanted_state = d->ai.wanted_state;
        s->card = (int32_t)d->dyn.card_type;
        s->hit_points = d->ai.hit_points;
        s->ai_time_ms = d->ai.time_ms;
        s->restarts = d->restarts;
        s->ed_hash = fold(hash_perso(1, &d->ai));
        if (d->ai.running_reflex) s->flags |= TC_RS_REFLEX;
    }
    s->sector = tc_viewer_ed_sector(v);
    s->camera_sector = v->light_sector;
    s->viewer_phase = (uint32_t)v->phase;
    s->light_set = v->light_set;
    s->rng = v->rng;
    s->picture_left = v->picture_left;
    s->loading_time = v->loading_time;
    if (c) {
        uint64_t h = 1;
        s->lives = c->globals.lives;
        s->awake = c->awake;
        s->thinking = c->thinking;
        s->spawned = c->spawned;
        s->cast_rng = c->rng;
        for (i = 0; i < c->count; i++) h = hash_perso(h, c->actors[i].ai);
        s->cast_hash = fold(h);
        s->globals_hash = hash_globals(&c->globals);
    }
}

#define FIELD(n, k) {#n, (uint32_t)offsetof(tc_rec_sample, n), k}
#define FIELD_AT(n, i, k) {#n "[" #i "]", (uint32_t)(offsetof(tc_rec_sample, n) + (i) * 4), k}
static const tc_rec_field fields[] = {
    FIELD(frame, 'u'), FIELD(flags, 'x'),
    FIELD_AT(position, 0, 'f'), FIELD_AT(position, 1, 'f'), FIELD_AT(position, 2, 'f'),
    FIELD_AT(velocity, 0, 'f'), FIELD_AT(velocity, 1, 'f'), FIELD_AT(velocity, 2, 'f'), FIELD(heading, 'f'),
    FIELD_AT(eye, 0, 'f'), FIELD_AT(eye, 1, 'f'), FIELD_AT(eye, 2, 'f'), FIELD(yaw, 'f'), FIELD(pitch, 'f'),
    FIELD(vertical_fov, 'f'), FIELD(orbit_distance, 'f'), FIELD(animation_time, 'f'), FIELD(ed_phase, 'f'),
    FIELD(ed_clip, 'i'), FIELD(behaviour, 'i'), FIELD(state, 'i'), FIELD(wanted_state, 'i'), FIELD(card, 'i'),
    FIELD(hit_points, 'i'), FIELD(lives, 'i'), FIELD(sector, 'u'), FIELD(camera_sector, 'u'), FIELD(ai_time_ms, 'u'),
    FIELD(placed, 'u'), FIELD(restarts, 'u'), FIELD(viewer_phase, 'u'), FIELD(light_set, 'u'), FIELD(awake, 'u'),
    FIELD(thinking, 'u'), FIELD(spawned, 'u'), FIELD(rng, 'x'), FIELD(cast_rng, 'x'), FIELD(ed_hash, 'x'),
    FIELD(cast_hash, 'x'), FIELD(globals_hash, 'x'), FIELD(picture_left, 'f'), FIELD(loading_time, 'f'),
};

const tc_rec_field *tc_rec_sample_field(uint32_t i) {
    return i < sizeof(fields) / sizeof(fields[0]) ? &fields[i] : NULL;
}

const char *tc_rec_door_name(uint32_t door) {
    static const char *const names[TC_RD_COUNT] = {
        "?", "step speed", "pause", "recentre camera", "close-up", "first person", "look settings", "step look",
        "invert y", "light set", "follow light", "step gamma", "step fov", "fog", "lighting", "sectors", "wheel",
        "play / free-fly", "AI / simple controller", "demo on", "demo off", "debug menu", "hud"
    };
    return door < TC_RD_COUNT ? names[door] : "?";
}

/* ---- writing ---- */

static uint8_t *reserve(tc_recorder *r, size_t n) {
    if (r->len + n > r->cap) {
        size_t cap = r->cap ? r->cap : 4096;
        uint8_t *b;
        while (cap < r->len + n) cap *= 2;
        if (!(b = (uint8_t *)tc_alloc(cap))) return NULL;
        if (r->len) memcpy(b, r->buf, r->len);
        tc_free(r->buf);
        r->buf = b;
        r->cap = cap;
    }
    r->len += n;
    return r->buf + r->len - n;
}

static void put(tc_recorder *r, const void *p, size_t n) {
    uint8_t *o = reserve(r, n);
    if (o && n) memcpy(o, p, n);
}

static void put_u32(tc_recorder *r, uint32_t x) { put(r, &x, 4); }

static void put_text(tc_recorder *r, const char *s) {
    uint32_t n = s ? (uint32_t)strlen(s) : 0;
    put_u32(r, n);
    put(r, s, n);
}

/* A record of `size` payload bytes follows. */
static void head(tc_recorder *r, tc_rec_type type, uint32_t size) {
    uint8_t t = (uint8_t)type;
    put(r, &t, 1);
    put_u32(r, size);
}

/* Nothing leaves before the first segment; afterwards all of it, at once. */
static void flush(tc_recorder *r) {
    if (r->len && r->started) {
        r->sink(r->user, r->buf, r->len);
        r->len = 0;
    }
}

tc_recorder *tc_recorder_create(tc_rec_sink sink, void *user) {
    tc_recorder *r = (tc_recorder *)tc_calloc(1, sizeof *r);
    if (!r) return NULL;
    r->sink = sink;
    r->user = user;
    put(r, "TTRC", 4);
    put_u32(r, TC_REC_VERSION);
    return r;
}

/* Closed with a level still open (back to a menu, quitting): that is an ending too, unlike a crash's. */
void tc_recorder_free(tc_recorder *r) {
    if (!r) return;
    if (r->viewer) {
        r->viewer->rec = NULL;
        tc_rec_end(r, r->viewer);
    }
    flush(r);
    tc_free(r->buf);
    tc_free(r);
}

void tc_recorder_note(tc_recorder *r, const char *key, const char *value) {
    uint32_t k = (uint32_t)strlen(key), n = value ? (uint32_t)strlen(value) : 0;
    head(r, TC_RT_NOTE, 8 + k + n);
    put_text(r, key);
    put_text(r, value);
    flush(r);
}

static void write_sample(tc_recorder *r, const tc_viewer *v) {
    tc_rec_sample s;
    read_sample(v, r->frame, &s);
    head(r, TC_RT_SAMPLE, sizeof s);
    put(r, &s, sizeof s);
}

static void write_cast(tc_recorder *r, const tc_viewer *v) {
    const tc_cast *c = tc_viewer_cast(v);
    uint32_t i;
    if (!c || !c->count) return;
    head(r, TC_RT_CAST, c->count * (uint32_t)sizeof(tc_rec_actor));
    for (i = 0; i < c->count; i++) {
        const tc_cast_actor *a = &c->actors[i];
        tc_rec_actor o;
        memset(&o, 0, sizeof o);
        o.id = a->entry->id;
        o.position[0] = a->ai->position.x, o.position[1] = a->ai->position.y, o.position[2] = a->ai->position.z;
        o.behaviour = a->ai->normal.behaviour;
        o.state = a->ai->state;
        o.hit_points = a->ai->hit_points;
        o.flags = (a->ai->active ? 1u : 0) | (a->ai->dead ? 2u : 0) | (a->awake ? 4u : 0) | (a->thought ? 8u : 0);
        put(r, &o, sizeof o);
    }
}

/* The story values and lives, when they changed (the first time: as they start). */
static void write_story(tc_recorder *r, const tc_viewer *v) {
    const tc_cast *c = tc_viewer_cast(v);
    int32_t lives;
    if (!c) return;
    lives = c->globals.lives;
    if (r->has_story && lives == r->lives && !memcmp(r->values, c->globals.values, sizeof r->values)) return;
    r->has_story = true;
    r->lives = lives;
    memcpy(r->values, c->globals.values, sizeof r->values);
    head(r, TC_RT_STORY, 4 + sizeof r->values);
    put(r, &lives, 4);
    put(r, r->values, sizeof r->values);
}

/* The frontends make the next level's viewer before they free the last one: the new one closes the old one's segment. */
void tc_rec_begin(tc_recorder *r, tc_viewer *v, const tc_viewer_desc *desc) {
    const tc_ed_ai *d = tc_viewer_driver(v);
    const tc_viewer_carry *c = desc->carry;
    const tc_ai_model *m = d ? d->ai.model : NULL;
    uint32_t n = desc->level_name ? (uint32_t)strlen(desc->level_name) : 0;
    uint32_t carry = c ? (uint32_t)(offsetof(tc_viewer_carry, dsg) + c->dsg_size) : 0;
    uint32_t tail = c ? (uint32_t)(sizeof(*c) - offsetof(tc_viewer_carry, vertical_fov)) : 0;
    if (r->viewer) {
        r->viewer->rec = NULL;
        tc_rec_end(r, r->viewer);
    }
    r->started = true;
    r->viewer = v;
    r->frame = r->draws = 0;
    r->held = 0;
    r->move[0] = r->move[1] = r->look[0] = r->look[1] = 0;
    r->sample_wait = r->cast_wait = 0;
    r->has_story = false;
    head(r, TC_RT_SEGMENT, 4 + n + 8 + 32 + 16 + 8 + carry + tail);
    put_text(r, desc->level_name);
    put_u32(r, v->level->edition);
    put_u32(r, desc->seed);
    put(r, v->level->converter, 16);
    {
        char stamp[16] = {0};
        uint64_t none = 0;
        if (m) memcpy(stamp, m->converter, sizeof stamp);
        put(r, stamp, sizeof stamp);
        put(r, &v->level->data_hash, 8);
        put(r, m ? &m->data_hash : &none, 8);
    }
    put_u32(r, carry);
    put_u32(r, tail);
    if (c) {
        put(r, c, carry);
        put(r, &c->vertical_fov, tail);
    }
    write_sample(r, v);
    write_story(r, v);
    write_cast(r, v);
    flush(r);
}

void tc_rec_end(tc_recorder *r, tc_viewer *v) {
    (void)v;
    head(r, TC_RT_SEGMENT_END, 0);
    r->viewer = NULL;
    flush(r);
}

void tc_rec_door(tc_recorder *r, tc_rec_door_id door, int32_t a, float x, float y) {
    uint32_t d = (uint32_t)door;
    head(r, TC_RT_DOOR, 16);
    put_u32(r, d);
    put(r, &a, 4);
    put(r, &x, 4);
    put(r, &y, 4);
}

static bool same2(const float *a, const float *b) { return !memcmp(a, b, 2 * sizeof(float)); }

/* The frame goes out before it runs: a crash in it is in the file. */
void tc_rec_frame_in(tc_recorder *r, tc_viewer *v, float dt) {
    float mouse[2], move[2], look[2];
    static const float zero[2] = {0, 0};
    uint8_t mask = 0;
    uint32_t size = 5;
    mouse[0] = v->mouse_dx, mouse[1] = v->mouse_dy;
    move[0] = v->move_x, move[1] = v->move_y;
    look[0] = v->look_x, look[1] = v->look_y;
    if (v->held != r->held) mask |= IN_HELD, size += 4;
    if (!same2(mouse, zero)) mask |= IN_MOUSE, size += 8;
    if (!same2(move, r->move)) mask |= IN_MOVE, size += 8;
    if (!same2(look, r->look)) mask |= IN_LOOK, size += 8;
    head(r, TC_RT_FRAME, size);
    put(r, &dt, 4);
    put(r, &mask, 1);
    if (mask & IN_HELD) put_u32(r, v->held);
    if (mask & IN_MOUSE) put(r, mouse, 8);
    if (mask & IN_MOVE) put(r, move, 8);
    if (mask & IN_LOOK) put(r, look, 8);
    r->held = v->held;
    memcpy(r->move, move, sizeof move);
    memcpy(r->look, look, sizeof look);
    flush(r);
}

void tc_rec_frame_out(tc_recorder *r, tc_viewer *v, float dt) {
    const uint16_t *sounds;
    uint32_t n = tc_viewer_sounds(v, &sounds);
    r->frame++;
    if (n) {
        head(r, TC_RT_SOUNDS, n * 2);
        put(r, sounds, n * 2);
    }
    write_story(r, v);
    r->sample_wait -= dt;
    if (r->sample_wait <= 0) {
        r->sample_wait = fmaxf(r->sample_wait + TC_REC_SAMPLE_SECONDS, 0);
        write_sample(r, v);
    }
    r->cast_wait -= dt;
    if (r->cast_wait <= 0) {
        r->cast_wait = TC_REC_CAST_SECONDS;
        write_cast(r, v);
    }
    flush(r);
}

void tc_rec_draw(tc_recorder *r, tc_viewer *v, const tc_view_draw *draws, uint32_t count) {
    if (++r->draws % TC_REC_HASH_DRAWS) {
        head(r, TC_RT_DRAW, 0);
        return; /* goes out with the next frame */
    }
    {
        uint64_t draw, frame;
        hash_output(v, draws, count, &draw, &frame);
        head(r, TC_RT_DRAW, 20);
        put_u32(r, count);
        put(r, &draw, 8);
        put(r, &frame, 8);
    }
}

uint32_t tc_recorder_mark(tc_recorder *r, const char *text) {
    if (!r->viewer) return 0;
    r->marks++;
    head(r, TC_RT_MARK, 8 + (text ? (uint32_t)strlen(text) : 0));
    put_u32(r, r->marks);
    put_text(r, text);
    write_sample(r, r->viewer);
    write_cast(r, r->viewer);
    flush(r);
    return r->marks;
}

/* ---- reading ---- */

/* Record at *at: its type and payload; false at the end or where the file is cut short. */
static bool next_record(const tc_replay *r, size_t *at, uint8_t *type, const uint8_t **body, uint32_t *size) {
    if (r->size - *at < RECORD_HEAD) return false;
    memcpy(size, r->data + *at + 1, 4);
    if (*size > r->size - *at - RECORD_HEAD) return false;
    *type = r->data[*at];
    *body = r->data + *at + RECORD_HEAD;
    *at += RECORD_HEAD + *size;
    return true;
}

/* Payload reader: reads past the end give zeros. */
typedef struct cursor { const uint8_t *p; uint32_t left; } cursor;

static void take(cursor *c, void *out, uint32_t n) {
    uint32_t k = n < c->left ? n : c->left;
    memset(out, 0, n);
    memcpy(out, c->p, k);
    c->p += k;
    c->left -= k;
}

static uint32_t take_u32(cursor *c) {
    uint32_t x;
    take(c, &x, 4);
    return x;
}

static float take_f32(cursor *c) {
    float x;
    take(c, &x, 4);
    return x;
}

/* A u32 length and its text, cut to fit out (terminated); the rest skipped. */
static void take_text(cursor *c, char *out, uint32_t size) {
    uint32_t n = take_u32(c), keep = n < size - 1 ? n : size - 1, skip = n - keep;
    take(c, out, keep);
    out[keep] = 0;
    if (skip > c->left) skip = c->left;
    c->p += skip;
    c->left -= skip;
}

static void read_segment(cursor *c, tc_replay_segment *s) {
    uint32_t carry, tail;
    memset(s, 0, sizeof *s);
    take_text(c, s->level, sizeof s->level);
    s->edition = take_u32(c);
    s->seed = take_u32(c);
    take(c, s->level_stamp, 16);
    take(c, s->ai_stamp, 16);
    take(c, &s->level_hash, 8);
    take(c, &s->ai_hash, 8);
    carry = take_u32(c);
    tail = take_u32(c);
    s->has_carry = carry != 0;
    if (carry) {
        if (carry > offsetof(tc_viewer_carry, vertical_fov)) carry = offsetof(tc_viewer_carry, vertical_fov);
        take(c, &s->carry, carry);
        if (tail > sizeof(s->carry) - offsetof(tc_viewer_carry, vertical_fov))
            tail = (uint32_t)(sizeof(s->carry) - offsetof(tc_viewer_carry, vertical_fov));
        take(c, &s->carry.vertical_fov, tail);
    }
}

static void read_frame(cursor *c, float *dt, uint32_t *held, float *mouse, float *move, float *look) {
    uint8_t mask;
    *dt = take_f32(c);
    take(c, &mask, 1);
    if (mask & IN_HELD) *held = take_u32(c);
    mouse[0] = mouse[1] = 0;
    if (mask & IN_MOUSE) take(c, mouse, 8);
    if (mask & IN_MOVE) take(c, move, 8);
    if (mask & IN_LOOK) take(c, look, 8);
}

tc_result tc_replay_open(tc_replay *r, const void *data, size_t size) {
    size_t at = HEADER_SIZE, cap = 0;
    uint8_t type;
    const uint8_t *body;
    uint32_t n;
    memset(r, 0, sizeof *r);
    if (size < HEADER_SIZE || memcmp(data, "TTRC", 4) != 0) return TC_ERR_BAD_MAGIC;
    r->data = (const uint8_t *)data;
    r->size = size;
    memcpy(&r->version, r->data + 4, 4);
    if (r->version != TC_REC_VERSION) return TC_ERR_STALE;
    for (;;) {
        size_t start = at;
        tc_replay_segment *s = r->segment_count ? &r->segments[r->segment_count - 1] : NULL;
        if (!next_record(r, &at, &type, &body, &n)) {
            r->truncated = at != r->size;
            break;
        }
        if (type == TC_RT_SEGMENT) {
            cursor c = {body, n};
            if (r->segment_count == cap) {
                size_t ncap = cap ? cap * 2 : 8;
                tc_replay_segment *b = (tc_replay_segment *)tc_alloc(ncap * sizeof *b);
                if (!b) { tc_replay_close(r); return TC_ERR_OUT_OF_MEMORY; }
                if (r->segment_count) memcpy(b, r->segments, r->segment_count * sizeof *b);
                tc_free(r->segments);
                r->segments = b;
                cap = ncap;
            }
            s = &r->segments[r->segment_count++];
            read_segment(&c, s);
            s->first = at;
            s->end = at;
        } else if (s) {
            if (type == TC_RT_FRAME) {
                float dt;
                memcpy(&dt, body, 4);
                s->frames++;
                s->seconds += dt;
            } else if (type == TC_RT_DRAW) s->draws++;
            else if (type == TC_RT_MARK) s->marks++;
            else if (type == TC_RT_DOOR && n >= 4) {
                uint32_t door;
                memcpy(&door, body, 4);
                if (door == TC_RD_DEMO_ON) r->autopilot = true;
            }
            else if (type == TC_RT_SEGMENT_END) s->closed = true;
            s->end = type == TC_RT_SEGMENT_END ? start : at;
        }
    }
    r->closed = r->segment_count && r->segments[r->segment_count - 1].closed;
    return TC_OK;
}

void tc_replay_summarize(const tc_replay *r, tc_replay_summary *o) {
    tc_replay_record rec;
    size_t at = 0, n = 0;
    uint32_t i, shown = 0, whole;
    memset(o, 0, sizeof *o);
    /* "started" is "2026-11-06_003012": the minutes are enough to tell runs apart in a list */
    while (tc_replay_walk(r, &at, &rec) && rec.type != TC_RT_SEGMENT)
        if (rec.type == TC_RT_NOTE && !strcmp(rec.key, "started") && strlen(rec.text) >= 15)
            snprintf(o->when, sizeof o->when, "%.10s %.2s:%.2s", rec.text, rec.text + 11, rec.text + 13);
    for (i = 0; i < r->segment_count; i++) {
        const tc_replay_segment *s = &r->segments[i];
        o->seconds += s->seconds;
        o->marks += s->marks;
        if (i && !strcmp(s->level, r->segments[i - 1].level) && s->edition == r->segments[i - 1].edition) continue;
        if (shown < 4) {
            n += (size_t)snprintf(o->levels + n, sizeof o->levels - n, "%s%s%s", shown ? ", " : "", s->level, s->edition ? " (SE)" : "");
            if (n >= sizeof o->levels) n = sizeof o->levels - 1;
        }
        shown++;
    }
    if (shown > 4) snprintf(o->levels + n, sizeof o->levels - n, " and %u more", shown - 4);
    if (!shown) snprintf(o->levels, sizeof o->levels, "no level");
    o->playable = r->segment_count > 0 && !r->autopilot;
    whole = (uint32_t)(o->seconds + 0.5);
    n = (size_t)snprintf(o->details, sizeof o->details, "%u:%02u", whole / 60, whole % 60);
    if (o->marks) n += (size_t)snprintf(o->details + n, sizeof o->details - n, ", %u mark%s", o->marks, o->marks == 1 ? "" : "s");
    if (r->autopilot) snprintf(o->details + n, sizeof o->details - n, ", the old autopilot: can't be played");
    else if (r->truncated || !r->closed) snprintf(o->details + n, sizeof o->details - n, ", did not end cleanly");
}

void tc_replay_close(tc_replay *r) {
    tc_free(r->segments);
    memset(r, 0, sizeof *r);
}

bool tc_replay_walk(const tc_replay *r, size_t *at, tc_replay_record *o) {
    /* The running counts stay in *o between calls: hand the same record back. */
    uint8_t type;
    const uint8_t *body;
    uint32_t n;
    cursor c;
    if (*at == 0) {
        memset(o, 0, sizeof *o);
        o->segment = TC_REPLAY_NO_SEGMENT;
        *at = HEADER_SIZE;
    }
    if (!next_record(r, at, &type, &body, &n)) return false;
    c.p = body;
    c.left = n;
    o->type = (tc_rec_type)type;
    switch (type) {
    case TC_RT_NOTE:
        take_text(&c, o->key, sizeof o->key);
        take_text(&c, o->text, sizeof o->text);
        break;
    case TC_RT_MARK:
        o->mark = take_u32(&c);
        o->key[0] = 0;
        take_text(&c, o->text, sizeof o->text);
        break;
    case TC_RT_SEGMENT:
        o->segment = o->segment == TC_REPLAY_NO_SEGMENT ? 0 : o->segment + 1;
        o->frame = 0;
        o->time = 0;
        o->held = 0;
        o->move[0] = o->move[1] = o->look[0] = o->look[1] = 0;
        break;
    case TC_RT_DOOR:
        o->door = take_u32(&c);
        take(&c, &o->door_a, 4);
        o->door_x = take_f32(&c);
        o->door_y = take_f32(&c);
        break;
    case TC_RT_FRAME:
        read_frame(&c, &o->dt, &o->held, o->mouse, o->move, o->look);
        o->frame++;
        o->time += o->dt;
        break;
    case TC_RT_SAMPLE:
        take(&c, &o->sample, n < sizeof o->sample ? n : (uint32_t)sizeof o->sample);
        break;
    case TC_RT_DRAW:
        o->has_hash = n != 0;
        if (n) {
            o->draw_count = take_u32(&c);
            take(&c, &o->draw_hash, 8);
            take(&c, &o->frame_hash, 8);
        }
        break;
    case TC_RT_SOUNDS:
        o->sounds = body;
        o->sound_count = n / 2;
        break;
    case TC_RT_CAST:
        o->actors = body;
        o->actor_count = n / (uint32_t)sizeof(tc_rec_actor);
        break;
    case TC_RT_STORY:
        take(&c, &o->lives, 4);
        take(&c, o->values, sizeof o->values);
        break;
    default: break;
    }
    return true;
}

/* ---- replaying ---- */

void tc_replay_begin(tc_replay *r, uint32_t segment, tc_viewer *v, bool verify) {
    r->viewer = v;
    r->verify = verify;
    r->segment = segment;
    r->at = r->segments[segment].first;
    r->frame = 0;
    r->time = 0;
    r->held = 0;
    r->move[0] = r->move[1] = r->look[0] = r->look[1] = 0;
    r->paused = r->drawn = false;
    tc_viewer_hud_waits(v, false); /* the recording says whether its run had the HUD (TC_RD_HUD) */
    memset(&r->drift, 0, sizeof r->drift);
}

void tc_replay_pause(tc_replay *r, bool on) {
    tc_viewer *v = r->viewer;
    if (on == r->paused || !v) return;
    r->paused = on;
    if (on) {
        r->was_inspecting = v->inspecting;
        r->saved_camera = v->camera;
        r->saved_cam = v->cam;
        r->saved_orbit = v->orbit_distance;
        v->inspecting = true;
        return;
    }
    v->inspecting = r->was_inspecting;
    v->camera = r->saved_camera;
    v->cam = r->saved_cam;
    v->orbit_distance = r->saved_orbit;
    if (r->drawn) { /* the camera's sight reads the last draw list: this camera's isn't the run's */
        uint32_t count, opaque;
        tc_viewer_draws(v, &count, &opaque);
    }
}

static void apply_door(tc_viewer *v, uint32_t door, int32_t a, float x) {
    switch ((tc_rec_door_id)door) {
    case TC_RD_STEP_SPEED: tc_viewer_step_speed(v, a); break;
    case TC_RD_TOGGLE_PAUSE: tc_viewer_toggle_pause(v); break;
    case TC_RD_RECENTER: tc_viewer_recenter_camera(v); break;
    case TC_RD_CLOSEUP: tc_viewer_toggle_closeup(v); break;
    case TC_RD_FIRST_PERSON: tc_viewer_toggle_first_person(v); break;
    case TC_RD_SET_LOOK: tc_viewer_set_look(v, x, (a & 1) != 0, (a & 2) != 0); break;
    case TC_RD_STEP_LOOK: tc_viewer_step_look(v, a); break;
    case TC_RD_INVERT_Y: tc_viewer_toggle_invert_y(v); break;
    case TC_RD_LIGHT_SET: tc_viewer_set_light_set(v, (uint32_t)a); break;
    case TC_RD_FOLLOW_LIGHT: tc_viewer_follow_light(v); break;
    case TC_RD_STEP_GAMMA: tc_viewer_step_gamma(v, a != 0); break;
    case TC_RD_STEP_FOV: tc_viewer_step_fov(v, a); break;
    case TC_RD_FOG: tc_viewer_toggle_fog(v); break;
    case TC_RD_LIGHTING: tc_viewer_cycle_lighting(v); break;
    case TC_RD_SECTORS: tc_viewer_cycle_sectors(v); break;
    case TC_RD_WHEEL: tc_viewer_wheel(v, x); break;
    case TC_RD_TOGGLE_PLAY: tc_viewer_toggle_play(v); break;
    case TC_RD_TOGGLE_AI: tc_viewer_toggle_ai(v); break;
    case TC_RD_DEBUG: tc_viewer_toggle_debug(v); break;
    case TC_RD_HUD: tc_viewer_hud_waits(v, true); break;
    case TC_RD_DEMO_ON: case TC_RD_DEMO_OFF: case TC_RD_COUNT: break;
    }
}

static void note_first(tc_replay *r, const char *what) {
    tc_replay_drift *d = &r->drift;
    if (d->drifted) return;
    d->drifted = true;
    d->first_frame = r->frame;
    d->first_time = r->time;
    snprintf(d->first_what, sizeof d->first_what, "%s", what);
}

static void compare_sample(tc_replay *r, const tc_rec_sample *want) {
    tc_replay_drift *d = &r->drift;
    tc_rec_sample got;
    float dx, dy, dz, dist;
    uint32_t i;
    read_sample(r->viewer, r->frame, &got);
    d->samples++;
    dx = got.position[0] - want->position[0];
    dy = got.position[1] - want->position[1];
    dz = got.position[2] - want->position[2];
    dist = sqrtf(dx * dx + dy * dy + dz * dz);
    d->end_distance = dist;
    if (dist > d->max_distance) {
        d->max_distance = dist;
        d->max_frame = r->frame;
    }
    if (!memcmp(&got, want, sizeof got)) return;
    d->bad_samples++;
    for (i = 0; !d->drifted && i < sizeof(fields) / sizeof(fields[0]); i++) {
        const tc_rec_field *f = &fields[i];
        const uint8_t *a = (const uint8_t *)want + f->offset, *b = (const uint8_t *)&got + f->offset;
        char what[96];
        float fa, fb;
        uint32_t ua, ub;
        if (!memcmp(a, b, 4)) continue;
        memcpy(&fa, a, 4), memcpy(&fb, b, 4), memcpy(&ua, a, 4), memcpy(&ub, b, 4);
        if (f->kind == 'f') snprintf(what, sizeof what, "%s: recorded %.9g, replayed %.9g", f->name, (double)fa, (double)fb);
        else if (f->kind == 'x') snprintf(what, sizeof what, "%s: recorded %08x, replayed %08x", f->name, ua, ub);
        else snprintf(what, sizeof what, "%s: recorded %d, replayed %d", f->name, (int)ua, (int)ub);
        note_first(r, what);
    }
}

static void compare_cast(tc_replay *r, const uint8_t *body, uint32_t n) {
    const tc_cast *c = tc_viewer_cast(r->viewer);
    uint32_t i, count = n / (uint32_t)sizeof(tc_rec_actor);
    tc_replay_drift *d = &r->drift;
    bool bad = false;
    d->casts++;
    if (!c || c->count != count) {
        d->bad_casts++;
        note_first(r, "the cast's size");
        return;
    }
    for (i = 0; i < count; i++) {
        const tc_cast_actor *a = &c->actors[i];
        tc_rec_actor w;
        char what[96];
        memcpy(&w, body + (size_t)i * sizeof w, sizeof w);
        if (w.position[0] == a->ai->position.x && w.position[1] == a->ai->position.y && w.position[2] == a->ai->position.z &&
            w.behaviour == a->ai->normal.behaviour && w.state == a->ai->state && w.hit_points == a->ai->hit_points)
            continue;
        bad = true;
        snprintf(what, sizeof what, "actor id %u: recorded B%d state %d hp %d, replayed B%d state %d hp %d", w.id,
                 (int)w.behaviour, (int)w.state, (int)w.hit_points, (int)a->ai->normal.behaviour, (int)a->ai->state,
                 (int)a->ai->hit_points);
        note_first(r, what);
    }
    if (bad) d->bad_casts++;
}

/* Where the recorded run left this level for the next: did the replay get there the same way. */
static void compare_carry(tc_replay *r) {
    const tc_viewer_carry *got = tc_viewer_pending_level(r->viewer);
    const tc_replay_segment *next = r->segment + 1 < r->segment_count ? &r->segments[r->segment + 1] : NULL;
    tc_replay_drift *d = &r->drift;
    if (!next || !next->has_carry) return;
    d->carry_checked = true;
    d->carry_differs = !got || strcmp(got->level, next->carry.level) || strcmp(got->place, next->carry.place) ||
                       got->dsg_size != next->carry.dsg_size || memcmp(got->dsg, next->carry.dsg, got->dsg_size);
    if (d->carry_differs) note_first(r, "the level change at the end");
}

bool tc_replay_step(tc_replay *r, float *dt) {
    const tc_replay_segment *s = &r->segments[r->segment];
    bool stepped = false;
    while (r->at < s->end) {
        size_t at = r->at;
        uint8_t type;
        const uint8_t *body;
        uint32_t n;
        cursor c;
        if (!next_record(r, &at, &type, &body, &n)) break;
        if (type == TC_RT_FRAME && stepped) break;
        r->at = at;
        c.p = body;
        c.left = n;
        switch (type) {
        case TC_RT_DOOR: {
            uint32_t door = take_u32(&c);
            int32_t a;
            float x;
            take(&c, &a, 4);
            x = take_f32(&c);
            apply_door(r->viewer, door, a, x);
            break;
        }
        case TC_RT_FRAME: {
            tc_viewer *v = r->viewer;
            float step, mouse[2];
            read_frame(&c, &step, &r->held, mouse, r->move, r->look);
            /* the viewer's input fields as they were: the doors' dead zones are in them already */
            v->held = r->held;
            v->mouse_dx = mouse[0], v->mouse_dy = mouse[1];
            v->move_x = r->move[0], v->move_y = r->move[1];
            v->look_x = r->look[0], v->look_y = r->look[1];
            tc_viewer_update(v, step);
            r->drawn = false;
            r->frame++;
            r->time += step;
            if (dt) *dt = step;
            stepped = true;
            break;
        }
        case TC_RT_SAMPLE:
            if (r->verify) {
                tc_rec_sample want;
                take(&c, &want, n < sizeof want ? n : (uint32_t)sizeof want);
                compare_sample(r, &want);
            }
            break;
        case TC_RT_DRAW: {
            uint32_t count, opaque;
            const tc_view_draw *d = tc_viewer_draws(r->viewer, &count, &opaque);
            r->drawn = true;
            if (r->verify && n) {
                uint32_t want = take_u32(&c);
                uint64_t wd, wf, gd, gf;
                take(&c, &wd, 8);
                take(&c, &wf, 8);
                hash_output(r->viewer, d, count, &gd, &gf);
                r->drift.hashes++;
                if (want != count || wd != gd || wf != gf) {
                    r->drift.bad_hashes++;
                    note_first(r, wd != gd ? "the draw list's hash" : "the frame values' hash (lights, fog, camera)");
                }
            }
            break;
        }
        case TC_RT_SOUNDS:
            if (r->verify) {
                const uint16_t *sounds;
                uint32_t got = tc_viewer_sounds(r->viewer, &sounds);
                r->drift.sounds++;
                if (got * 2 != n || memcmp(sounds, body, n)) {
                    r->drift.bad_sounds++;
                    note_first(r, "the sounds started");
                }
            }
            break;
        case TC_RT_CAST:
            if (r->verify) compare_cast(r, body, n);
            break;
        default: break;
        }
    }
    if (!stepped && r->verify) compare_carry(r);
    return stepped;
}
