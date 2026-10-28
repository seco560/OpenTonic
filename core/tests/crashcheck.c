/* Totalski's obstacle crash (docs/NOTES.md "Ski obstacles"): the skier starts 12 units uphill of a tree, rides into it
 * and must crash (the tree's proximity trigger forces B9, which plays state 274 on a card capped at 15 u/s), then ride
 * on, blinking (dsg17 = 1) until the reflex clears it 3 s later. Usage: crashcheck [out/ttlv] [trigger index].
 * Test-only stdio. */
#include <stdio.h>
#include <stdlib.h>
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

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    int which = argc > 2 ? atoi(argv[2]) : 5, i, k;
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_level level;
    tc_collision world;
    tc_ai_model model;
    tc_player player;
    tc_ed_ai ed;
    const tc_ai_trigger *t;
    tc_vec3 start, d;
    float best = -1e9f, crash_at = -1, max_crash_speed = 0, clear_at = -1, back_at = -1;

    snprintf(path, sizeof(path), "%s/Totalski.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { fprintf(stderr, "%s?\n", path); return 1; }
    free(data);
    snprintf(path, sizeof(path), "%s/Totalski.ttai", dir);
    if (!(data = read_file(path, &size)) || tc_ai_model_load(&model, data, size) != TC_OK) { fprintf(stderr, "%s?\n", path); return 1; }
    free(data);
    tc_ai_model_link(&model, &level);
    if (which < 0 || which >= model.trigger_count) { fprintf(stderr, "no trigger %d (%u)\n", which, model.trigger_count); return 1; }
    if (tc_collision_init(&world, level.collision, 3 * level.collision_triangle_count, level.collision_floors) != 0) return 1;
    t = &model.triggers[which];
    start = t->position;
    for (k = 0; k < 16; k++) { /* the highest ground 12 away: uphill */
        float a = (float)k * TC_PI / 8;
        tc_vec3 o = tc_v3_add(t->position, tc_v3(12 * cosf(a), 12 * sinf(a), 30));
        tc_hit h;
        if (tc_collision_raycast(&world, o, tc_v3(0, 0, -1), 80, &h) && o.z - h.distance > best) {
            best = o.z - h.distance;
            start = tc_v3(o.x, o.y, best + 1);
        }
    }
    d = tc_v3_sub(t->position, start);
    tc_player_init(&player, &world, start, atan2f(d.x, -d.y));
    if (tc_ed_ai_init(&ed, &model, &player, "Totalski") != TC_OK) return 1;
    printf("crashcheck: trigger %d (r %.1f -> B%u), start 12 uphill\n", which, (double)t->radius, t->behaviour);
    for (i = 0; i < 30 * 8; i++) { /* at the game's tick */
        float time = (float)i * TC_ED_AI_TICK;
        tc_ed_ai_step(&ed, TC_ED_AI_TICK, 0, 0, 0, tc_v3_add(player.position, tc_v3(0, 5, 2)));
        if (ed.ai.state == 274) {
            if (crash_at < 0) crash_at = time;
            if (time > crash_at + 0.2f && tc_v3_length(player.velocity) > max_crash_speed)
                max_crash_speed = tc_v3_length(player.velocity);
        }
        if (crash_at >= 0 && back_at < 0 && ed.ai.state != 274 && ed.ai.normal.behaviour == 3) back_at = time;
        if (back_at >= 0 && clear_at < 0 && tc_ai_dsgvar_float(&ed.ai, 17) == 0) clear_at = time;
    }
    printf("  crash (state 274) at %.2f s, top speed in it %.1f, riding again at %.2f s, blinking until %.2f s\n",
           (double)crash_at, (double)max_crash_speed, (double)back_at, (double)clear_at);
    k = crash_at > 0 && max_crash_speed <= 15.5f && back_at > crash_at && clear_at > back_at;
    printf(k ? "ok\n" : "FAIL\n");
    tc_ed_ai_free(&ed);
    tc_ai_model_free(&model);
    tc_collision_free(&world);
    tc_level_free(&level);
    return !k;
}
