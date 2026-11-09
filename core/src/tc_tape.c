/* See tc_tape.h. */
#include <math.h>
#include <string.h>
#include "tonic/tc_tape.h"
#include "tonic/tc_alloc.h"

#define HEADER 12u
#define MAX_OBJECTS 4096u
#define CUT_DISTANCE 10.0f /* units between two frames: a teleport or a camera cut, not drawn as a slide */

static uint16_t u16_at(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t u32_at(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float f32_at(const uint8_t *p) {
    uint32_t u = u32_at(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

/* The bytes after an event record's mask (the exe's reader, GAM_00433d80 and GAM_00434260 / 434380), or -1 if unknown or
 * past `left`. 0xA000 isn't in the exe's reader (it would read nothing): its 6 bytes are the file's, TotalSki only. */
static int event_size(uint16_t mask, const uint8_t *p, size_t left) {
    switch (mask) {
    case 0x8040: return 3;
    case 0x8041: return left < 3 ? -1 : p[2] == 0 ? 15 : 3;
    case 0x8020: case 0x8002: return 1;
    case 0x8004: return 2;
    case 0x8008: case 0x8010: return 0;
    case 0xA000: return 6;
    case 0x8003: return left < 4 ? -1 : p[3] == 4 ? 17 : 13;
    case 0x9000: {
        int8_t kind;
        if (left < 1) return -1;
        kind = (int8_t)p[0];
        return kind == 0 || kind == 1 ? 19 : kind == 3 ? 35 : 3;
    }
    case 0x1000: return 2;
    case 0x1400: return 18;
    case 0x0800: case 0x0C00: return 3;
    default: return -1;
    }
}

static size_t object_fields_size(uint16_t mask) {
    return ((mask & 0x10) ? 12u : 0u) + ((mask & 0x40) ? 8u : 0u) + ((mask & 2) ? 2u : 0u) + ((mask & 4) ? 4u : 0u) +
           ((mask & 8) ? 1u : 0u) + ((mask & 0x2000) ? 2u : 0u) + ((mask & 0x100) ? 4u : 0u);
}

static bool is_event(uint16_t mask) { return (mask & 0x8000) || mask == 0x1000 || mask == 0x1400 || mask == 0x0800 || mask == 0x0C00; }

static void apply(tc_tape_object *o, uint16_t mask, const uint8_t *p) {
    if (mask & 0x10) {
        o->position.x = f32_at(p), o->position.y = f32_at(p + 4), o->position.z = f32_at(p + 8);
        p += 12;
    }
    if (mask & 0x40) {
        int k;
        float q[4], len = 0;
        for (k = 0; k < 4; k++) { /* as filed: w, x, y, z of its inverse */
            q[k] = (float)(int16_t)u16_at(p + 2 * (size_t)k) / 32767.0f;
            len += q[k] * q[k];
        }
        len = len > 1e-8f ? 1 / sqrtf(len) : 1;
        /* filed as the inverse (world -> perso): measured against Ed's start and which way he travels */
        o->rotation[0] = -q[1] * len, o->rotation[1] = -q[2] * len, o->rotation[2] = -q[3] * len, o->rotation[3] = q[0] * len;
        p += 8;
    }
    if (mask & 2) {
        o->state = u16_at(p);
        p += 2;
    }
    if (mask & 4) o->frame = f32_at(p);
}

/* Walks a frame's records; with apply_them they land on t->objects. false: a record it can't read. */
static bool walk_frame(tc_tape *t, const uint8_t *p, const uint8_t *end, bool apply_them, uint32_t *max_object) {
    while (p < end) {
        uint16_t mask;
        if ((size_t)(end - p) < 2) return false;
        mask = u16_at(p);
        p += 2;
        if (is_event(mask)) {
            int n = event_size(mask, p, (size_t)(end - p));
            if (n < 0 || (size_t)n > (size_t)(end - p)) return false;
            p += n;
            continue;
        }
        {
            size_t fields = object_fields_size(mask);
            uint16_t index;
            if ((size_t)(end - p) < 2 + fields) return false;
            index = u16_at(p);
            p += 2;
            if (index >= MAX_OBJECTS) return false;
            if (max_object && index >= *max_object) *max_object = (uint32_t)index + 1;
            if (apply_them && index < t->object_count) {
                tc_tape_object *o = &t->objects[index];
                apply(o, mask, p);
                o->shown = o->known = true;
            }
            p += fields;
        }
    }
    return true;
}

bool tc_tape_next(tc_tape *t) {
    uint32_t i;
    uint16_t frame_size, ms;
    if (t->frames_read >= t->frame_count || t->cursor + 4 > t->size) return false;
    frame_size = u16_at(t->data + t->cursor);
    ms = u16_at(t->data + t->cursor + 2);
    if (frame_size < 4 || t->cursor + frame_size > t->size) return false;
    for (i = 0; i < t->object_count; i++) {
        tc_tape_object *o = &t->objects[i];
        o->position_prev = o->position;
        memcpy(o->rotation_prev, o->rotation, sizeof o->rotation);
        o->frame_prev = o->frame_before = o->frame;
        o->state_prev = o->state;
        o->shown = false;
    }
    if (!walk_frame(t, t->data + t->cursor + 4, t->data + t->cursor + frame_size, true, NULL)) t->bad_frames++;
    for (i = 0; i < t->object_count; i++) {
        tc_tape_object *o = &t->objects[i];
        tc_vec3 d = tc_v3_sub(o->position, o->position_prev);
        if (tc_v3_dot(d, d) > CUT_DISTANCE * CUT_DISTANCE) { /* drawn in one go */
            o->position_prev = o->position;
            memcpy(o->rotation_prev, o->rotation, sizeof o->rotation);
        }
        if (o->state != o->state_prev || o->frame < o->frame_prev) o->frame_prev = o->frame; /* a new state, or a loop */
    }
    t->cursor += frame_size;
    t->frames_read++;
    t->time_prev = t->time;
    t->time += ms;
    return true;
}

bool tc_tape_open(tc_tape *t, const void *data, size_t size) {
    const uint8_t *b = data;
    uint32_t frames, body, i, max_object = 0;
    size_t at = HEADER;
    double ms = 0;
    memset(t, 0, sizeof *t);
    if (size < HEADER) return false;
    frames = u32_at(b);
    body = u32_at(b + 8);
    if (frames == 0 || body > size - HEADER) return false;
    for (i = 0; i < frames; i++) { /* every frame must fit, before anything is kept */
        uint16_t frame_size;
        if (at + 4 > HEADER + (size_t)body) return false;
        frame_size = u16_at(b + at);
        if (frame_size < 4 || at + frame_size > HEADER + (size_t)body) return false;
        ms += u16_at(b + at + 2);
        walk_frame(t, b + at + 4, b + at + frame_size, false, &max_object); /* a frame it can't read to its end still counts */
        at += frame_size;
    }
    t->data = tc_alloc(size);
    t->objects = max_object ? tc_alloc(max_object * sizeof *t->objects) : NULL;
    if (!t->data || (max_object && !t->objects)) {
        tc_tape_close(t);
        return false;
    }
    memcpy(t->data, b, size);
    memset(t->objects, 0, max_object * sizeof *t->objects);
    for (i = 0; i < max_object; i++) t->objects[i].rotation[3] = 1;
    t->size = HEADER + (size_t)body;
    t->frame_count = frames;
    t->object_count = max_object;
    t->seconds = ms / 1000;
    t->cursor = HEADER;
    if (!tc_tape_next(t)) {
        tc_tape_close(t);
        return false;
    }
    t->time_prev = t->time; /* nothing to draw between yet */
    for (i = 0; i < t->object_count; i++) { /* the first frame is the start, not a move from nowhere */
        tc_tape_object *o = &t->objects[i];
        o->position_prev = o->position;
        memcpy(o->rotation_prev, o->rotation, sizeof o->rotation);
        o->frame_prev = o->frame_before = o->frame;
    }
    return true;
}

void tc_tape_close(tc_tape *t) {
    tc_free(t->data);
    tc_free(t->objects);
    memset(t, 0, sizeof *t);
}

void tc_tape_rotation(const float q[4], float r[9]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    r[0] = 1 - 2 * (y * y + z * z), r[1] = 2 * (x * y - z * w), r[2] = 2 * (x * z + y * w);
    r[3] = 2 * (x * y + z * w), r[4] = 1 - 2 * (x * x + z * z), r[5] = 2 * (y * z - x * w);
    r[6] = 2 * (x * z - y * w), r[7] = 2 * (y * z + x * w), r[8] = 1 - 2 * (x * x + y * y);
}
