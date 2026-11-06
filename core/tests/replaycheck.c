/* Recording and replaying headless (docs/RECORDING.md), without anyone's recordings: plays scripted sessions into a
 * recorder held in memory, then checks that
 *   - replaying them comes out exact: every sample, hash, sound and cast record, on retail and Special Edition levels;
 *   - doors, pauses, frames without a draw and uneven dts all replay;
 *   - a real portal: a second segment, and the level change the first run ended with;
 *   - a recording cut off at any point still opens, and replays up to the cut (a crash's file);
 *   - a changed input is caught, at the frame it changed things;
 *   - input arriving between the frames of a replay changes nothing;
 *   - pausing a replay and turning the camera round Ed changes nothing either, and the camera comes back;
 *   - pausing in play: the camera turns round Ed and only that, and comes back.
 * Usage: replaycheck [out/ttlv]. Test-only stdio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

static int failures;

static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    *size = (size_t)n;
    return data;
}

typedef struct memfile { uint8_t *data; size_t size, cap; uint32_t writes; } memfile;

static void sink(void *user, const void *data, size_t size) {
    memfile *m = (memfile *)user;
    if (m->size + size > m->cap) {
        m->cap = (m->size + size) * 2;
        m->data = (uint8_t *)realloc(m->data, m->cap);
    }
    memcpy(m->data + m->size, data, size);
    m->size += size;
    m->writes++;
}

typedef struct world { tc_level level; uint8_t *ai; size_t ai_size; } world;

static bool load(world *w, const char *folder, const char *name) {
    char path[512];
    size_t size;
    uint8_t *data;
    tc_result e;
    memset(w, 0, sizeof *w);
    snprintf(path, sizeof path, "%s/%s.ttlv", folder, name);
    if (!(data = read_file(path, &size))) return false;
    e = tc_level_load(&w->level, data, size);
    free(data);
    if (e != TC_OK) return false;
    snprintf(path, sizeof path, "%s/%s.ttai", folder, name);
    w->ai = read_file(path, &w->ai_size);
    return true;
}

static void unload(world *w) {
    free(w->ai);
    tc_level_free(&w->level);
}

static void describe(tc_viewer_desc *d, world *w, const char *name, uint32_t seed, tc_recorder *r, const tc_viewer_carry *c) {
    memset(d, 0, sizeof *d);
    d->level = &w->level;
    d->ai_data = w->ai;
    d->ai_size = w->ai_size;
    d->level_name = name;
    d->seed = seed;
    d->recorder = r;
    d->carry = c;
}

/* A small deterministic stream of its own, so the script doesn't touch the viewer's. */
static uint32_t script_rng = 1;
static float script_random(void) {
    script_rng = script_rng * 1664525u + 1013904223u;
    return (float)(script_rng >> 8) / (float)(1u << 24);
}

/* A player's session: uneven dts, keys, the stick, the mouse, doors, a pause, a frame now and then not drawn. */
static void play(tc_viewer *v, int frames) {
    int i;
    for (i = 0; i < frames; i++) {
        uint32_t count, opaque;
        float dt = 1.0f / 60 + (script_random() - 0.5f) * 0.008f;
        if (i % 97 == 50) dt = 0.1f; /* a hitch: the frontends' cap */
        if (i % 41 == 0) tc_viewer_set_action(v, TC_VA_FORWARD, script_random() < 0.8f);
        if (i % 67 == 0) tc_viewer_set_action(v, TC_VA_JUMP, true);
        if (i % 67 == 6) tc_viewer_set_action(v, TC_VA_JUMP, false);
        if (i % 113 == 0) tc_viewer_set_action(v, TC_VA_ACTION, true);
        if (i % 113 == 5) tc_viewer_set_action(v, TC_VA_ACTION, false);
        tc_viewer_mouse_look(v, (script_random() - 0.5f) * 8, (script_random() - 0.5f) * 3);
        if (i % 200 < 100) tc_viewer_set_move_stick(v, script_random() - 0.5f, 0.9f);
        else tc_viewer_set_move_stick(v, 0, 0);
        if (i == 120) tc_viewer_wheel(v, 2);
        if (i == 180) tc_viewer_toggle_closeup(v);
        if (i == 240) tc_viewer_toggle_closeup(v);
        if (i == 300) tc_viewer_toggle_pause(v);
        if (i == 320) tc_viewer_toggle_pause(v);
        if (i == 360) tc_viewer_toggle_first_person(v);
        if (i == 400) tc_viewer_toggle_first_person(v);
        if (i == 440) tc_viewer_cycle_sectors(v);
        if (i == 460) tc_viewer_step_look(v, 1);
        if (i == 480) tc_viewer_release_all(v);
        tc_viewer_update(v, dt);
        if (i % 53 != 7) tc_viewer_draws(v, &count, &opaque); /* minimised: no draw */
    }
}

/* Not a door: the portal test's move, made at the same frame in the run and in its replay. */
static int place_frame = -1;
static tc_vec3 place_at;
static float place_heading;
static bool stray_inputs;
static bool pause_and_look;
static uint32_t pauses, pauses_restored;

/* A frontend's paused playback: the camera turned, drawn from, then resumed. */
static void pause_replay(tc_replay *r, tc_viewer *v) {
    tc_camera before = v->camera;
    uint32_t c, o;
    int i;
    tc_replay_pause(r, true);
    for (i = 0; i < 40; i++) {
        tc_viewer_set_action(v, TC_VA_FORWARD, true);
        tc_viewer_mouse_look(v, 12, -5);
        if (i == 10) tc_viewer_wheel(v, 3);
        tc_viewer_paused_look(v, 1.0f / 60);
        tc_viewer_draws(v, &c, &o);
    }
    /* it never moved: the test is empty (the level's scripted shots keep theirs: the outro's director) */
    if (!v->director && !memcmp(&before, &v->camera, sizeof before)) pauses_restored = 0xFFFFFFFFu;
    tc_replay_pause(r, false);
    pauses++;
    if (!memcmp(&before, &v->camera, sizeof before) && pauses_restored != 0xFFFFFFFFu) pauses_restored++;
}
static void place_ed(tc_viewer *v) { tc_player_teleport((tc_player *)tc_viewer_player(v), place_at, place_heading); }

static const tc_replay_drift *replay_segment(tc_replay *r, uint32_t i, world *w, const char *name, uint32_t *frames) {
    tc_viewer v;
    tc_viewer_desc d;
    const tc_replay_segment *s = &r->segments[i];
    float dt;
    describe(&d, w, name, s->seed, NULL, s->has_carry ? &s->carry : NULL);
    if (tc_viewer_init(&v, &d) != TC_OK) return NULL;
    tc_replay_begin(r, i, &v, true);
    do {
        if ((int)r->frame == place_frame && i == 0) place_ed(&v);
        if (pause_and_look && r->drawn && r->frame % 37 == 0) pause_replay(r, &v);
        if (stray_inputs) { /* a player leaning on everything while watching: the frame's own inputs replace them */
            tc_viewer_set_action(&v, TC_VA_FORWARD, r->frame % 2 == 0);
            tc_viewer_set_action(&v, TC_VA_JUMP, r->frame % 3 == 0);
            tc_viewer_mouse_look(&v, 40, -25);
            tc_viewer_set_move_stick(&v, -1, 1);
            tc_viewer_set_look_stick(&v, 1, 1);
            if (r->frame % 50 == 0) tc_viewer_release_all(&v);
        }
    } while (tc_replay_step(r, &dt));
    *frames = r->frame;
    tc_viewer_free(&v);
    return &r->drift;
}

static void show(const char *name, const tc_replay_drift *d) {
    if (d && d->drifted) printf("        %s: frame %u: %s; Ed off by %.3f at most\n", name, d->first_frame, d->first_what, (double)d->max_distance);
}

static void session(const char *folder, const char *name, int frames) {
    world w;
    memfile m = {0};
    tc_recorder *rec;
    tc_viewer v;
    tc_viewer_desc d;
    tc_replay r;
    const tc_replay_drift *drift;
    uint32_t got = 0;
    char what[160];
    if (!load(&w, folder, name)) {
        printf("  (no %s/%s: skipped)\n", folder, name);
        return;
    }
    rec = tc_recorder_create(sink, &m);
    tc_recorder_note(rec, "frontend", "replaycheck");
    check(m.size == 0, "nothing written before a level opens");
    describe(&d, &w, name, 0x1234567u, rec, NULL);
    if (tc_viewer_init(&v, &d) != TC_OK) { check(0, "viewer"); unload(&w); return; }
    tc_viewer_set_look(&v, 1.25f, false, true);
    play(&v, frames / 2);
    tc_recorder_mark(rec, "halfway");
    play(&v, frames - frames / 2);
    tc_viewer_free(&v);
    tc_recorder_free(rec);
    check(tc_replay_open(&r, m.data, m.size) == TC_OK && r.segment_count == 1 && r.closed && !r.truncated, "the file reads back");
    drift = replay_segment(&r, 0, &w, name, &got);
    show(name, drift);
    snprintf(what, sizeof what, "%s: %u frames in %zu KB replay exact (%u samples, %u hashes, %u sounds, %u casts)", name, got, m.size / 1024, drift ? drift->samples : 0, drift ? drift->hashes : 0,
             drift ? drift->sounds : 0, drift ? drift->casts : 0);
    check(drift && !drift->drifted && got == (uint32_t)frames && drift->samples > 0 && drift->hashes > 0, what);

    stray_inputs = true;
    drift = replay_segment(&r, 0, &w, name, &got);
    stray_inputs = false;
    show(name, drift);
    check(drift && !drift->drifted && got == (uint32_t)frames, "keys, mouse and sticks while it plays change nothing");
    pause_and_look = true;
    pauses = pauses_restored = 0;
    drift = replay_segment(&r, 0, &w, name, &got);
    pause_and_look = false;
    show(name, drift);
    snprintf(what, sizeof what, "paused %u times with the camera turned: exact, and the camera back each time", pauses);
    check(drift && !drift->drifted && got == (uint32_t)frames && pauses > 3 && pauses_restored == pauses, what);
    {
        /* a crash's file: cut anywhere, it opens and runs up to the cut */
        tc_replay cut;
        size_t at = m.size / 2 + 3;
        check(tc_replay_open(&cut, m.data, at) == TC_OK && cut.segment_count == 1 && !cut.closed, "cut in half: opens, not closed");
        drift = replay_segment(&cut, 0, &w, name, &got);
        show(name, drift);
        check(drift && !drift->drifted && got > 0 && got < (uint32_t)frames, "cut in half: replays exact up to the cut");
        tc_replay_close(&cut);

        /* a changed input: flip a held key in the file, and it is found */
        {
            uint8_t *copy = (uint8_t *)malloc(m.size);
            size_t o = 8;
            uint32_t frame = 0, want = (uint32_t)frames / 3;
            tc_replay bad;
            memcpy(copy, m.data, m.size);
            while (o + 5 <= m.size) {
                uint32_t n;
                memcpy(&n, copy + o + 1, 4);
                if (copy[o] == TC_RT_FRAME && frame++ == want) {
                    float dt;
                    memcpy(&dt, copy + o + 5, 4);
                    dt += 0.05f; /* an extra game tick or two from here on */
                    memcpy(copy + o + 5, &dt, 4);
                    break;
                }
                o += 5 + n;
            }
            check(tc_replay_open(&bad, copy, m.size) == TC_OK, "changed: opens");
            drift = replay_segment(&bad, 0, &w, name, &got);
            show(name, drift);
            check(drift && drift->drifted && drift->first_frame >= want && drift->first_frame < want + 8,
                  "a frame's dt changed: caught within a few frames of it");
            tc_replay_close(&bad);
            free(copy);
        }
    }
    tc_replay_close(&r);
    free(m.data);
    unload(&w);
}

/* Two levels in one file: the second's carry comes back whole, and one the first level never asked for is called out. */
static void chain(const char *folder) {
    world a, b;
    memfile m = {0};
    tc_recorder *rec;
    tc_viewer v, next;
    tc_viewer_desc d;
    tc_viewer_carry carry;
    tc_replay r;
    uint32_t got = 0;
    const tc_replay_drift *drift;
    if (!load(&a, folder, "sud0") || !load(&b, folder, "bar")) {
        printf("  (no sud0 / bar: chain skipped)\n");
        return;
    }
    rec = tc_recorder_create(sink, &m);
    describe(&d, &a, "sud0", 11, rec, NULL);
    tc_viewer_init(&v, &d);
    play(&v, 200);
    memset(&carry, 0, sizeof carry);
    strcpy(carry.level, "bar");
    carry.dsg_size = 16;
    carry.dsg[3] = 7;
    carry.orbit_distance = 9;
    describe(&d, &b, "bar", 22, rec, &carry);
    tc_viewer_init(&next, &d); /* as both frontends: the next level's viewer first, the last one freed after */
    tc_viewer_free(&v);
    play(&next, 200);
    /* quitting with a level open (no tc_viewer_free before the recorder goes): an ending, not a crash */
    tc_recorder_free(rec);
    tc_viewer_free(&next);
    check(tc_replay_open(&r, m.data, m.size) == TC_OK && r.segment_count == 2, "two levels: two segments");
    check(r.segment_count == 2 && r.segments[0].closed && r.segments[0].frames == 200 && r.segments[1].frames == 200 && r.closed,
          "the next viewer made before the last is freed: both closed, each with its own frames");
    check(r.segment_count == 2 && r.segments[1].has_carry && r.segments[1].carry.dsg[3] == 7 && r.segments[1].carry.dsg_size == 16 &&
          r.segments[1].carry.orbit_distance == 9 && !strcmp(r.segments[1].level, "bar"), "the carry comes back");
    drift = replay_segment(&r, 0, &a, "sud0", &got);
    check(drift && got == 200 && !drift->bad_samples && !drift->bad_hashes && drift->carry_checked && drift->carry_differs,
          "a carry the level never asked for: the run exact, the level change called out");
    drift = replay_segment(&r, 1, &b, "bar", &got);
    show("bar", drift);
    check(drift && !drift->drifted && got == 200, "second segment exact");
    tc_replay_close(&r);
    free(m.data);
    unload(&a);
    unload(&b);
}

/* A real level change, the frontends' way (tc_viewer_pending_level, a new viewer with its carry), through a red portal of
 * sud0 (the first with a level in its fire ops). Ed is put a little in front of it at a fixed frame; the capture, the
 * screen and the level asked for are the game's own, and the next level is the one it asked for. */
#define PORTAL_PLACE_FRAME 30
#define PORTAL_FRAMES (60 * 30)
static void portal(const char *folder) {
    world a, b;
    memfile m = {0};
    tc_recorder *rec;
    tc_viewer v, after;
    tc_viewer_desc d;
    tc_viewer_carry carry;
    const tc_viewer_carry *pending = NULL;
    const tc_ai_model *model;
    const tc_ai_portal *g = NULL;
    tc_replay r;
    const tc_replay_drift *drift;
    uint32_t got = 0, c, o, k;
    int i;
    char what[160];
    if (!load(&a, folder, "sud0")) {
        printf("  (no sud0: portal skipped)\n");
        return;
    }
    rec = tc_recorder_create(sink, &m);
    describe(&d, &a, "sud0", 31, rec, NULL);
    tc_viewer_init(&v, &d);
    model = tc_viewer_driver(&v)->ai.model;
    for (k = 0; k < model->portal_count && !g; k++) {
        const tc_ai_portal *p = &model->portals[k];
        uint16_t f;
        for (f = 0; f < p->fire_count; f++) {
            const tc_ai_portal_op *op = &p->ops[p->capture_count + f];
            if (op->b < model->name_count && !strcmp(model->names[op->b], "bar")) g = p;
        }
    }
    check(g != NULL, "sud0 has a portal to another level");
    if (!g) { tc_viewer_free(&v); tc_recorder_free(rec); free(m.data); unload(&a); return; }
    place_at = tc_v3_add(g->position, tc_v3(-g->rows[1] * g->radius * 0.5f, -g->rows[4] * g->radius * 0.5f, 0.3f));
    place_heading = atan2f(g->rows[1], -g->rows[4]);
    place_frame = PORTAL_PLACE_FRAME;
    for (i = 0; i < PORTAL_FRAMES && !pending; i++) {
        if (i == place_frame) place_ed(&v);
        tc_viewer_update(&v, 1.0f / 60);
        tc_viewer_draws(&v, &c, &o);
        pending = tc_viewer_pending_level(&v);
    }
    check(pending != NULL, "Ed at the portal: the game asks for the next level");
    if (!pending) { place_frame = -1; tc_viewer_free(&v); tc_recorder_free(rec); free(m.data); unload(&a); return; }
    carry = *pending;
    if (!load(&b, folder, carry.level)) {
        check(0, "the level the portal asks for loads");
        place_frame = -1;
        tc_viewer_free(&v);
        tc_recorder_free(rec);
        free(m.data);
        unload(&a);
        return;
    }
    describe(&d, &b, carry.level, 32, rec, &carry);
    tc_viewer_init(&after, &d); /* as the frontends: made first, the last one freed after */
    tc_viewer_free(&v);
    play(&after, 300);
    tc_viewer_free(&after);
    tc_recorder_free(rec);
    check(tc_replay_open(&r, m.data, m.size) == TC_OK && r.segment_count == 2, "through a portal: two segments");
    drift = replay_segment(&r, 0, &a, "sud0", &got);
    show("sud0", drift);
    snprintf(what, sizeof what, "sud0 -> %s (%u frames): the same run and the same level change", carry.level, got);
    check(drift && !drift->drifted && drift->carry_checked && !drift->carry_differs, what);
    place_frame = -1;
    drift = replay_segment(&r, 1, &b, carry.level, &got);
    show(carry.level, drift);
    check(drift && !drift->drifted && got == 300, "the level after the portal exact, from the recorded carry");
    tc_replay_close(&r);
    free(m.data);
    unload(&b);
    unload(&a);
}

/* What the Replays menu shows, and a file from the days of the autopilot: listed, deletable, not playable. */
static void summaries(const char *folder) {
    world w;
    memfile m = {0};
    tc_recorder *rec;
    tc_viewer v;
    tc_viewer_desc d;
    tc_replay r;
    tc_replay_summary s;
    char what[256];
    uint8_t *old;
    uint32_t door = 19, zero = 0;
    uint8_t head[5] = {TC_RT_DOOR, 16, 0, 0, 0};
    size_t at;
    if (!load(&w, folder, "bar")) { printf("  (no bar: summaries skipped)\n"); return; }
    rec = tc_recorder_create(sink, &m);
    tc_recorder_note(rec, "started", "2026-11-06_003012");
    describe(&d, &w, "bar", 5, rec, NULL);
    tc_viewer_init(&v, &d);
    play(&v, 120);
    tc_recorder_mark(rec, NULL);
    tc_viewer_free(&v);
    describe(&d, &w, "bar", 6, rec, NULL);
    tc_viewer_init(&v, &d);
    play(&v, 60);
    tc_viewer_free(&v);
    tc_recorder_free(rec);
    tc_replay_open(&r, m.data, m.size);
    tc_replay_summarize(&r, &s);
    snprintf(what, sizeof what, "summary: \"%s\", \"%s\", \"%s\"", s.when, s.levels, s.details);
    check(!strcmp(s.when, "2026-11-06 00:30") && !strcmp(s.levels, "bar") && s.marks == 1 && s.playable &&
          strstr(s.details, "1 mark") && !strstr(s.details, "cleanly"), what);
    tc_replay_close(&r);
    tc_replay_open(&r, m.data, m.size - 3);
    tc_replay_summarize(&r, &s);
    check(s.playable && strstr(s.details, "did not end cleanly"), "cut short: still playable, and it says so");
    tc_replay_close(&r);
    /* the door the autopilot left, put after the first segment's opening records */
    at = r.size;
    old = (uint8_t *)malloc(m.size + 21);
    tc_replay_open(&r, m.data, m.size);
    at = r.segments[0].first;
    tc_replay_close(&r);
    memcpy(old, m.data, at);
    memcpy(old + at, head, 5);
    memcpy(old + at + 5, &door, 4);
    memcpy(old + at + 9, &zero, 4);
    memcpy(old + at + 13, &zero, 4);
    memcpy(old + at + 17, &zero, 4);
    memcpy(old + at + 21, m.data + at, m.size - at);
    tc_replay_open(&r, old, m.size + 21);
    tc_replay_summarize(&r, &s);
    check(r.autopilot && !s.playable && strstr(s.details, "autopilot"), "an autopilot run: listed, not playable");
    tc_replay_close(&r);
    free(old);
    free(m.data);
    unload(&w);
}

/* P in play. */
static void live_pause(const char *folder) {
    world w;
    tc_viewer v;
    tc_viewer_desc d;
    tc_camera before;
    tc_vec3 ed;
    float speed;
    uint32_t c, o;
    int i;
    bool moved;
    if (!load(&w, folder, "sud0")) { printf("  (no sud0: pause skipped)\n"); return; }
    describe(&d, &w, "sud0", 9, NULL, NULL);
    tc_viewer_init(&v, &d);
    for (i = 0; i < 120; i++) { tc_viewer_update(&v, 1.0f / 60); tc_viewer_draws(&v, &c, &o); }
    before = v.camera;
    ed = tc_viewer_player(&v)->position;
    speed = v.orbit_distance;
    tc_viewer_toggle_pause(&v);
    for (i = 0; i < 60; i++) {
        tc_viewer_set_action(&v, TC_VA_FORWARD, true);
        tc_viewer_mouse_look(&v, 10, 4);
        if (i == 5) tc_viewer_wheel(&v, 2);
        tc_viewer_update(&v, 1.0f / 60);
        tc_viewer_draws(&v, &c, &o);
    }
    moved = tc_v3_distance(v.camera.position, before.position) > 1;
    check(moved && tc_v3_distance(v.camera.position, ed) < 25, "paused in play: the camera turns, staying round Ed (Forward held does nothing)");
    check(tc_v3_distance(tc_viewer_player(&v)->position, ed) == 0, "... and Ed stands");
    tc_viewer_release_all(&v);
    tc_viewer_toggle_pause(&v);
    check(!memcmp(&before, &v.camera, sizeof before) && v.orbit_distance == speed && !v.inspecting, "resumed: the camera exactly where it was");
    tc_viewer_free(&v);
    unload(&w);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    char se[512];
    snprintf(se, sizeof se, "%s/../se/ttlv", dir);
    printf("retail\n");
    session(dir, "sud0", 900);
    session(dir, "Land", 600);
    session(dir, "north", 1200);
    printf("Special Edition\n");
    session(se, "sud0", 600);
    session(se, "outro", 600);
    printf("level change\n");
    chain(dir);
    portal(dir);
    printf("pausing\n");
    live_pause(dir);
    printf("the Replays menu\n");
    summaries(dir);
    printf(failures ? "%d FAILED\n" : "ok\n", failures);
    return failures != 0;
}
