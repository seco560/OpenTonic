/* Where a frame's time goes (tc_prof.h), headless, on a few levels from small to big: Ed walking on sud0 (the game's own
 * cast), Ed walking and the camera flying over LibertyCity2001 (an imported city: no sectors, so nothing culled), and the
 * skier on the biggest SSX course. Fixed 60 Hz steps with scripted input; a warm-up is left out: a second, or with Ed
 * until he has landed (LibertyCity2001's start is high in the air: a fall of some 14 s), at most 30 s. Prints per frame: tc_viewer_update and tc_viewer_draws (mean, 95th percentile, worst), each zone's mean,
 * the work counters, the draws and triangles handed to the GPU (at 16:9: tc_viewer_set_aspect culls levels without
 * sectors). GPU time is not here: only the core's.
 *
 * Informational, not part of the gate: timings vary from run to run and machine to machine. Compare runs on the same
 * machine, before and after a change. Usage: perfcheck [out/ttlv] [seconds]. Test-only stdio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tonic/tonic.h"

static double now(void *user) {
    struct timespec ts;
    (void)user;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
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

static int by_value(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

typedef struct series { double *ms; int n; } series;

static void series_report(const char *label, series *s) {
    double sum = 0;
    int i, worst = 0;
    if (s->n == 0) return;
    for (i = 0; i < s->n; i++) {
        sum += s->ms[i];
        if (s->ms[i] > s->ms[worst]) worst = i;
    }
    printf("    %-14s mean %7.3f ms   p95 ", label, sum / s->n);
    qsort(s->ms, (size_t)s->n, sizeof(double), by_value);
    printf("%7.3f   worst %7.3f (frame %d)\n", s->ms[(int)(s->n * 0.95)], s->ms[s->n - 1], worst);
}

typedef enum script { WALK, FLY, RIDE } script;

/* The input of frame i (60 a second): WALK and RIDE push the stick forward and steer in slow waves (so Ed and the
 * camera turn through the level), Jump now and then on WALK; FLY leaves play mode and flies forward at the fast speed
 * in a steady turn (on LibertyCity2001 a circle some 1000 units across, round the city's middle: it stays over it). */
static void drive(tc_viewer *v, script s, int i) {
    float steer = sinf((float)i / 60 * 0.7f) * 0.6f;
    if (s == FLY) {
        tc_viewer_set_action(v, TC_VA_FORWARD, true);
        tc_viewer_set_action(v, TC_VA_FAST, true);
        tc_viewer_mouse_look(v, 3, 0); /* a steady turn: 0.54 rad/s */
        return;
    }
    tc_viewer_set_move_stick(v, steer, 1);
    if (s == WALK) tc_viewer_set_action(v, TC_VA_JUMP, i % 180 < 6);
}

static void scenario(const char *dir, const char *name, script s, float seconds) {
    static const char *const kinds[] = {"Ed walks", "free-fly", "the skier rides"};
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    tc_prof_stats st;
    series upd = {0}, drw = {0};
    double t0, load_ms, init_ms, draws_sum = 0, tris_sum = 0, static_tris = 0, hidden_sum = 0, cells_sum = 0;
    int i, frames = (int)(seconds * 60), warm, z;
    tc_vec3 start, settle;

    printf("\n%s: %s, %.0f s\n", name, kinds[s], (double)seconds);
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    t0 = now(NULL);
    if (!(data = read_file(path, &size))) { printf("  skipped: no %s\n", path); return; }
    if (tc_level_load(&level, data, size) != TC_OK) { printf("  skipped: %s does not load\n", path); free(data); return; }
    load_ms = (now(NULL) - t0) * 1000;
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof desc);
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai ? ai_size : 0;
    desc.level_name = name;
    desc.seed = 3;
    t0 = now(NULL);
    if (tc_viewer_init(&v, &desc) != TC_OK) { printf("  skipped: the viewer does not start\n"); free(ai); tc_level_free(&level); return; }
    init_ms = (now(NULL) - t0) * 1000;
    printf("  %.1f MB, load %.0f ms, viewer init %.0f ms; %u statics, %u persos, %u triangles; %s, %s\n",
           (double)size / 1e6, load_ms, init_ms, v.stats.meshes, v.stats.persos, v.stats.triangles,
           tc_viewer_can_play(&v) ? (tc_viewer_ai_active(&v) ? "Ed's AI" : "simple Ed") : "no Ed",
           v.sectors ? "sectors" : "no sectors");
    if (s == FLY && v.playing) tc_viewer_toggle_play(&v);
    if (s == FLY) v.camera.pitch = 0; /* level: free-fly follows the pitch, and the start's -0.2 sinks it under the city */

    upd.ms = (double *)calloc((size_t)frames, sizeof(double));
    drw.ms = (double *)calloc((size_t)frames, sizeof(double));
    /* the warm-up: a second; with Ed played, on until he has landed (dropped less than a unit in the last second) */
    {
        tc_vec3 top = v.playing ? tc_viewer_player(&v)->position : v.camera.position;
        uint32_t count, opaque;
        settle = top;
        for (warm = 0; warm < 30 * 60; warm++) {
            drive(&v, s, warm);
            tc_viewer_update(&v, 1.0f / 60);
            tc_viewer_draws(&v, &count, &opaque);
            if ((warm + 1) % 60 == 0) {
                tc_vec3 p = v.playing ? tc_viewer_player(&v)->position : v.camera.position;
                bool landed = settle.z - p.z < 1;
                settle = p;
                if (s == FLY || !v.playing || landed) break;
            }
        }
        if (warm >= 60)
            printf("  (warm-up %.0f s: Ed fell %.0f units before landing)\n", (double)(warm + 1) / 60, (double)(top.z - settle.z));
    }
    start = v.playing && s != FLY ? tc_viewer_player(&v)->position : v.camera.position;
    tc_prof_set_clock(now, NULL);
    for (i = 0; i < frames; i++) {
        uint32_t count, opaque, k;
        const tc_view_draw *d;
        tc_view_frame f;
        double a, b, c;
        drive(&v, s, warm + 1 + i);
        a = now(NULL);
        tc_viewer_update(&v, 1.0f / 60);
        b = now(NULL);
        tc_viewer_set_aspect(&v, 16.0f / 9); /* as a 16:9 window: levels without sectors are culled */
        d = tc_viewer_draws(&v, &count, &opaque);
        tc_viewer_frame(&v, 16.0f / 9, &f);
        c = now(NULL);
        upd.ms[upd.n++] = (b - a) * 1000;
        drw.ms[drw.n++] = (c - b) * 1000;
        draws_sum += count;
        hidden_sum += v.occluded_count;
        cells_sum += v.occluded_cells;
        for (k = 0; k < count; k++) {
            tris_sum += d[k].index_count / 3;
            if (d[k].mesh < v.perso_first || (d[k].mesh >= v.piece_first && d[k].mesh < v.piece_first + v.piece_count))
                static_tris += d[k].index_count / 3;
        }
    }
    tc_prof_read(&st, true);
    tc_prof_set_clock(NULL, NULL);

    series_report("update", &upd);
    series_report("draws+frame", &drw);
    printf("    zones, per frame:");
    for (z = 0; z < TC_PZ_COUNT; z++) {
        if (!st.calls[z]) continue;
        printf("%s %s %.3f ms", z % 5 == 0 ? "\n     " : "", tc_prof_zone_name((tc_prof_zone)z),
               st.seconds[z] * 1000 / frames);
        if (z != TC_PZ_COUNT - 1) printf(" ·");
    }
    printf("\n    work, per frame:");
    for (z = 0; z < TC_PC_COUNT; z++) printf(" %s %.0f%s", tc_prof_counter_name((tc_prof_counter)z),
                                             (double)st.counts[z] / frames, z + 1 < TC_PC_COUNT ? " ·" : "");
    printf("\n    drawn, per frame: %.0f draws, %.0f triangles (statics %.0f, persos and Ed %.0f)\n", draws_sum / frames,
           tris_sum / frames, static_tris / frames, (tris_sum - static_tris) / frames);
    if (hidden_sum + cells_sum > 0)
        printf("    hidden, per frame (occlusion culling): %.0f draws, %.0f cells of wide statics\n", hidden_sum / frames, cells_sum / frames);
    {
        tc_vec3 end = tc_viewer_can_play(&v) && s != FLY ? tc_viewer_player(&v)->position : v.camera.position;
        printf("    moved %.1f units (the scenario ran)\n", (double)tc_v3_length(tc_v3_sub(end, start)));
    }
    free(upd.ms);
    free(drw.ms);
    tc_viewer_free(&v);
    tc_level_free(&level);
    free(ai);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    float seconds = argc > 2 ? (float)atof(argv[2]) : 20;
    if (!(seconds > 0)) seconds = 20;
    printf("perfcheck: the core's time per frame (no GPU), %s\n", dir);
    scenario(dir, "sud0", WALK, seconds);
    scenario(dir, "LibertyCity2001", WALK, seconds);
    scenario(dir, "LibertyCity2001", FLY, seconds);
    scenario(dir, "SSX_ElysiumAlps", RIDE, seconds);
    return 0;
}
