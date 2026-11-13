#include "tc_internal.h"
#include "tc_prof_hooks.h"
#include <string.h>

bool tc_prof_on;
static tc_prof_clock clock_fn;
static void *clock_user;
static tc_prof_stats kept;

void tc_prof_set_clock(tc_prof_clock clock, void *user) {
    clock_fn = clock;
    clock_user = user;
    tc_prof_on = clock != NULL;
    memset(&kept, 0, sizeof kept);
}

bool tc_prof_enabled(void) { return tc_prof_on; }

double tc_prof_now(void) { return clock_fn ? clock_fn(clock_user) : 0.0; }

void tc_prof_end(tc_prof_zone z, double t0) {
    if ((unsigned)z >= TC_PZ_COUNT) return;
    kept.seconds[z] += tc_prof_now() - t0;
    kept.calls[z] += 1;
}

void tc_prof_count(tc_prof_counter c, uint64_t n) {
    if ((unsigned)c < TC_PC_COUNT) kept.counts[c] += n;
}

void tc_prof_read(tc_prof_stats *out, bool reset) {
    if (out) *out = kept;
    if (reset) memset(&kept, 0, sizeof kept);
}

const char *tc_prof_zone_name(tc_prof_zone z) {
    static const char *const names[TC_PZ_COUNT] = {
        "update", "ticks", "cast first", "ed", "cast rest", "custom", "simple", "camera", "sight", "lighting",
        "portals", "draws", "draw statics", "draw persos", "draw sort", "draw cull",
    };
    return (unsigned)z < TC_PZ_COUNT ? names[z] : "?";
}

const char *tc_prof_counter_name(tc_prof_counter c) {
    static const char *const names[TC_PC_COUNT] = {
        "collision queries", "collision cells", "collision tris", "collision loose", "sight tris",
    };
    return (unsigned)c < TC_PC_COUNT ? names[c] : "?";
}
