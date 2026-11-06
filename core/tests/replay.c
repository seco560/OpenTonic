/* The recordings' command line (docs/RECORDING.md). Test-only stdio.
 *   replay info FILE...              the session: notes, levels, data stamps, length, marks, how it ended
 *   replay timeline FILE             what happened, in order: levels, marks, doors, behaviours, deaths, story values
 *   replay csv FILE                  the trail, one row per sample: for plots and for reading a run
 *   replay check [-d DIR] FILE...    runs each recording headless against the levels in DIR (default ../files/out/ttlv;
 *                                    Special Edition levels from DIR/../se/ttlv) and says where it left the recorded
 *                                    run. Exit 1 on any drift, 2 on a level or file it could not run. */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

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

/* The level's file in folder, any case: path gets it. */
static bool find_level(const char *folder, const char *level, const char *ext, char *path, size_t size) {
    DIR *d = opendir(folder);
    struct dirent *e;
    size_t n = strlen(level), m = strlen(ext);
    bool found = false;
    if (!d) return false;
    while (!found && (e = readdir(d))) {
        size_t i;
        if (strlen(e->d_name) != n + m) continue;
        for (i = 0; i < n && tolower((unsigned char)e->d_name[i]) == tolower((unsigned char)level[i]); i++) {}
        if (i == n && !strcmp(e->d_name + n, ext)) {
            snprintf(path, size, "%s/%s", folder, e->d_name);
            found = true;
        }
    }
    closedir(d);
    return found;
}

static int open_file(const char *path, tc_replay *r, uint8_t **data) {
    size_t size;
    tc_result e;
    if (!(*data = read_file(path, &size))) {
        printf("%s: can't read\n", path);
        return 0;
    }
    if ((e = tc_replay_open(r, *data, size)) != TC_OK) {
        printf("%s: %s\n", path, tc_result_string(e));
        free(*data);
        return 0;
    }
    return 1;
}

static const char *ending(const tc_replay *r) {
    if (r->truncated) return "cut off mid-record (a crash or a kill)";
    return r->closed ? "closed" : "ended inside a level (a crash or a kill)";
}

static int info(const char *path) {
    tc_replay r;
    tc_replay_record o;
    uint8_t *data;
    size_t at = 0;
    uint32_t i;
    double seconds = 0;
    if (!open_file(path, &r, &data)) return 2;
    printf("%s: format %u, %zu bytes, %s\n", path, r.version, r.size, ending(&r));
    while (tc_replay_walk(&r, &at, &o))
        if (o.type == TC_RT_NOTE) printf("  %s: %s\n", o.key, o.text);
    for (i = 0; i < r.segment_count; i++) {
        const tc_replay_segment *s = &r.segments[i];
        printf("  [%u] %s%s, seed %08x, %u frames, %.1f s, %u draws, %u marks%s%s%s, data %s %016llx / %s %016llx\n", i,
               s->level, s->edition ? " (SE)" : "", s->seed, s->frames, s->seconds, s->draws, s->marks,
               s->has_carry ? ", through a portal to " : "", s->has_carry ? (s->carry.place[0] ? s->carry.place : "the start") : "",
               s->closed ? "" : ", NOT CLOSED", s->level_stamp, (unsigned long long)s->level_hash, s->ai_stamp,
               (unsigned long long)s->ai_hash);
        seconds += s->seconds;
    }
    printf("  %u levels, %.1f s of play\n", r.segment_count, seconds);
    tc_replay_close(&r);
    free(data);
    return 0;
}

static void stamp(const tc_replay_record *o) { printf("%9.3f f%-7u ", o->time, o->frame); }

static int timeline(const char *path) {
    tc_replay r;
    tc_replay_record o;
    tc_rec_sample last;
    int16_t values[31];
    int32_t lives = 0;
    uint8_t *data;
    size_t at = 0;
    bool has_last = false, has_story = false;
    if (!open_file(path, &r, &data)) return 2;
    memset(&last, 0, sizeof last);
    while (tc_replay_walk(&r, &at, &o)) {
        const tc_rec_sample *s = &o.sample;
        switch (o.type) {
        case TC_RT_NOTE: printf("note: %s: %s\n", o.key, o.text); break;
        case TC_RT_SEGMENT: {
            const tc_replay_segment *g = &r.segments[o.segment];
            printf("---- level %s%s, %.1f s%s\n", g->level, g->edition ? " (SE)" : "", g->seconds, g->closed ? "" : " (not closed)");
            has_last = has_story = false;
            break;
        }
        case TC_RT_DOOR:
            stamp(&o);
            printf("door: %s", tc_rec_door_name(o.door));
            if (o.door_a) printf(" %d", (int)o.door_a);
            if (o.door_x != 0) printf(" %g", (double)o.door_x);
            printf("\n");
            break;
        case TC_RT_MARK:
            stamp(&o);
            printf("MARK %u%s%s\n", o.mark, o.text[0] ? ": " : "", o.text);
            break;
        case TC_RT_STORY: {
            int i;
            if (has_story && o.lives != lives) { stamp(&o); printf("lives %d -> %d\n", (int)lives, (int)o.lives); }
            for (i = 0; has_story && i < 31; i++)
                if (o.values[i] != values[i]) { stamp(&o); printf("story value %d: %d -> %d\n", i, values[i], o.values[i]); }
            memcpy(values, o.values, sizeof values);
            lives = o.lives;
            has_story = true;
            break;
        }
        case TC_RT_SAMPLE:
            if (has_last) {
                if (s->behaviour != last.behaviour) { stamp(&o); printf("behaviour %d -> %d at (%.2f %.2f %.2f)\n", (int)last.behaviour, (int)s->behaviour, (double)s->position[0], (double)s->position[1], (double)s->position[2]); }
                if (s->hit_points != last.hit_points) { stamp(&o); printf("hit points %d -> %d\n", (int)last.hit_points, (int)s->hit_points); }
                if (s->restarts != last.restarts) { stamp(&o); printf("respawn %u\n", s->restarts); }
                if (s->placed != last.placed) { stamp(&o); printf("placed at (%.2f %.2f %.2f)\n", (double)s->position[0], (double)s->position[1], (double)s->position[2]); }
                if (s->sector != last.sector) { stamp(&o); printf("sector %u -> %u\n", last.sector, s->sector); }
                if (s->viewer_phase != last.viewer_phase) { stamp(&o); printf("viewer phase %u -> %u\n", last.viewer_phase, s->viewer_phase); }
                if ((s->flags ^ last.flags) & ~(uint32_t)(TC_RS_GROUNDED | TC_RS_REFLEX)) { stamp(&o); printf("flags %04x -> %04x\n", last.flags, s->flags); }
            }
            last = *s;
            has_last = true;
            break;
        default: break;
        }
    }
    tc_replay_close(&r);
    free(data);
    return 0;
}

static int csv(const char *path) {
    tc_replay r;
    tc_replay_record o;
    uint8_t *data;
    size_t at = 0;
    uint32_t i;
    const tc_rec_field *f;
    if (!open_file(path, &r, &data)) return 2;
    printf("segment,level,time");
    for (i = 0; (f = tc_rec_sample_field(i)); i++) printf(",%s", f->name);
    printf("\n");
    while (tc_replay_walk(&r, &at, &o)) {
        if (o.type != TC_RT_SAMPLE) continue;
        printf("%u,%s,%.4f", o.segment, r.segments[o.segment].level, o.time);
        for (i = 0; (f = tc_rec_sample_field(i)); i++) {
            const uint8_t *p = (const uint8_t *)&o.sample + f->offset;
            float x;
            uint32_t u;
            memcpy(&x, p, 4);
            memcpy(&u, p, 4);
            if (f->kind == 'f') printf(",%.6g", (double)x);
            else if (f->kind == 'x') printf(",%08x", u);
            else if (f->kind == 'i') printf(",%d", (int)u);
            else printf(",%u", u);
        }
        printf("\n");
    }
    tc_replay_close(&r);
    free(data);
    return 0;
}

/* One segment on a fresh viewer; 0 same, 1 drifted, 2 could not run. */
static int check_segment(tc_replay *r, uint32_t i, const char *dir) {
    const tc_replay_segment *s = &r->segments[i];
    char folder[512], path[640], ai_path[640];
    uint8_t *data, *ai = NULL;
    size_t size, ai_size = 0;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    const tc_replay_drift *d = &r->drift;
    int result;
    float dt;
    if (s->edition) snprintf(folder, sizeof folder, "%s/../se/ttlv", dir);
    else snprintf(folder, sizeof folder, "%s", dir);
    if (!find_level(folder, s->level, ".ttlv", path, sizeof path) || !(data = read_file(path, &size))) {
        printf("  [%u] %s: no such level in %s: skipped\n", i, s->level, folder);
        return 2;
    }
    if (tc_level_load(&level, data, size) != TC_OK) {
        printf("  [%u] %s: doesn't load\n", i, s->level);
        free(data);
        return 2;
    }
    free(data);
    if (find_level(folder, s->level, ".ttai", ai_path, sizeof ai_path)) ai = read_file(ai_path, &ai_size);
    memset(&desc, 0, sizeof desc);
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = s->level;
    desc.carry = s->has_carry ? &s->carry : NULL;
    desc.seed = s->seed;
    if (tc_viewer_init(&v, &desc) != TC_OK) {
        printf("  [%u] %s: no viewer\n", i, s->level);
        free(ai);
        tc_level_free(&level);
        return 2;
    }
    if (level.data_hash != s->level_hash) printf("  [%u] %s: the level's data changed since the recording\n", i, s->level);
    {
        const tc_ed_ai *driver = tc_viewer_driver(&v);
        if (driver && driver->ai.model->data_hash != s->ai_hash) printf("  [%u] %s: Ed's AI data changed since the recording\n", i, s->level);
    }
    tc_replay_begin(r, i, &v, true);
    while (tc_replay_step(r, &dt)) {}
    result = d->drifted ? 1 : 0;
    printf("  [%u] %s%s: %u frames, %u samples, %u hashes, %u sounds, %u casts: ", i, s->level, s->edition ? " (SE)" : "",
           r->frame, d->samples, d->hashes, d->sounds, d->casts);
    if (!d->drifted) printf("exact%s\n", d->carry_checked ? ", same level change" : "");
    else {
        printf("DRIFTED at frame %u (%.3f s): %s\n", d->first_frame, d->first_time, d->first_what);
        printf("        different: %u samples, %u hashes, %u sounds, %u casts%s; Ed at most %.3f units off (frame %u), %.3f at the end\n",
               d->bad_samples, d->bad_hashes, d->bad_sounds, d->bad_casts, d->carry_differs ? ", the level change" : "",
               (double)d->max_distance, d->max_frame, (double)d->end_distance);
    }
    tc_viewer_free(&v);
    free(ai);
    tc_level_free(&level);
    return result;
}

static int check(const char *path, const char *dir) {
    tc_replay r;
    uint8_t *data;
    uint32_t i;
    int worst = 0;
    if (!open_file(path, &r, &data)) return 2;
    printf("%s: %u levels, %s\n", path, r.segment_count, ending(&r));
    if (r.autopilot) {
        printf("  made with Ed's autopilot, since removed: skipped\n");
        tc_replay_close(&r);
        free(data);
        return 2;
    }
    for (i = 0; i < r.segment_count; i++) {
        int e = check_segment(&r, i, dir);
        if (e > worst) worst = e;
    }
    tc_replay_close(&r);
    free(data);
    return worst;
}

int main(int argc, char **argv) {
    const char *dir = "../files/out/ttlv";
    int i = 2, worst = 0;
    if (argc < 3) {
        fprintf(stderr, "usage: replay info|timeline|csv FILE, replay check [-d DIR] FILE...\n");
        return 2;
    }
    if (!strcmp(argv[1], "timeline")) return timeline(argv[2]);
    if (!strcmp(argv[1], "csv")) return csv(argv[2]);
    if (!strcmp(argv[1], "check") && !strcmp(argv[2], "-d") && argc > 4) {
        dir = argv[3];
        i = 4;
    }
    for (; i < argc; i++) {
        int e = !strcmp(argv[1], "info") ? info(argv[i]) : check(argv[i], dir);
        if (e > worst) worst = e;
    }
    return worst;
}
