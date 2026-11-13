/* The skier against the slopes' walls (docs/NOTES.md "Sliding stages"): random drops on snow floors (grip 0, normal z
 * > 0.75, open above), random headings, 5 s of random steering (none / left / right, changed every 0.4 s). A ride that
 * was moving (5 u/s or more) and then covers under 2 units in its last 1.5 s has stopped; one that never reached
 * 5 u/s started on flat snow (the skier cannot push off) and does not count. Prints the stops with where they are.
 * Usage: skicheck [out/ttlv] [level] [rides] [seed]  (default Totalski, 600, 1); fails above 2 % stopped.
 * Test-only stdio. */
#include <math.h>
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

static uint32_t seed;
static float rnd(void) {
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 8) / 16777216.0f;
}

#define FRAMES 150 /* 5 s at the game's 30 ticks */
#define TAIL 45    /* the last 1.5 s */

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv", *name = argc > 2 ? argv[2] : "Totalski";
    int rides = argc > 3 ? atoi(argv[3]) : 600, r, f, done = 0, idle = 0, stopped = 0;
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_level level;
    tc_collision world;
    tc_ai_model model;
    double speed = 0, frames = 0, wall_frames = 0;
    seed = argc > 4 ? (uint32_t)atoi(argv[4]) : 1;

    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { fprintf(stderr, "%s?\n", path); return 1; }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    if (!(data = read_file(path, &size)) || tc_ai_model_load(&model, data, size) != TC_OK) { fprintf(stderr, "%s?\n", path); return 1; }
    free(data);
    if (model.alt_data) { /* a Special Edition level that switches walking / skiing: the skier is the second model (ALTM) */
        tc_ai_model ski;
        if (tc_ai_model_load(&ski, model.alt_data, model.alt_size) != TC_OK) { fprintf(stderr, "%s: ALTM?\n", path); return 1; }
        tc_ai_model_free(&model);
        model = ski;
    }
    tc_ai_model_link(&model, &level);
    if (tc_collision_init(&world, level.collision, 3 * level.collision_triangle_count, level.collision_floors) != 0) return 1;
    tc_collision_set_sectors(&world, level.sectors.collision_sector, level.sectors.collision_count, level.sectors.count);

    for (r = 0; r < rides; r++) {
        const tc_tri *t;
        tc_vec3 start, at[FRAMES];
        tc_player player;
        tc_ed_ai ed;
        tc_hit hit;
        float u, w, heading, vmax = 0, tail = 0;
        uint32_t inputs = 0;
        int tries = 0, teleported = 0, walls = 0;
        do { /* a snow floor of some size */
            t = &world.tris[(uint32_t)(rnd() * (float)world.tri_count) % world.tri_count];
        } while (++tries < 100000 && !(t->normal.z > 0.75f && t->floor.grip <= 0 &&
                                        tc_v3_length(tc_v3_cross(tc_v3_sub(t->b, t->a), tc_v3_sub(t->c, t->a))) > 2));
        if (tries >= 100000) break;
        u = rnd();
        w = rnd();
        if (u + w > 1) { u = 1 - u; w = 1 - w; }
        start = tc_v3_add(t->a, tc_v3_add(tc_v3_scale(tc_v3_sub(t->b, t->a), u), tc_v3_scale(tc_v3_sub(t->c, t->a), w)));
        if (tc_collision_raycast(&world, tc_v3_add(start, tc_v3(0, 0, 0.02f)), tc_v3(0, 0, 1), 3, &hit)) continue;
        start.z += 0.67f; /* the skier's sphere (r 0.65) just above the snow */
        heading = rnd() * 2 * TC_PI;
        tc_player_init(&player, &world, start, heading);
        tc_player_apply_floors(&player);
        if (tc_ed_ai_init(&ed, &model, &player, name) != TC_OK) { fprintf(stderr, "init failed\n"); return 1; }
        tc_ed_ai_set_sectors(&ed, &level.sectors);
        for (f = 0; f < FRAMES; f++) {
            tc_vec3 before = player.position;
            tc_vec3 cam = tc_v3_add(player.position, tc_v3(-4.5f * sinf(player.heading), 4.5f * cosf(player.heading), 1.5f));
            if (f % 12 == 0) {
                float q = rnd();
                inputs = q < 0.4f ? 0 : q < 0.7f ? 1u << TC_AI_IN_LEFT : 1u << TC_AI_IN_RIGHT;
            }
            tc_ed_ai_step(&ed, TC_ED_AI_TICK, inputs, 0, 0, cam);
            if (tc_v3_distance(before, player.position) > 5) { teleported = 1; break; } /* a hole's respawn */
            at[f] = player.position;
            if (f < FRAMES - TAIL && tc_v3_length(player.velocity) > vmax) vmax = tc_v3_length(player.velocity);
            if (f >= FRAMES - TAIL) tail += tc_v3_distance(before, player.position);
            if (f > 30) {
                frames++;
                speed += tc_v3_length(player.velocity);
                if (ed.dyn.contacts & TC_AI_CONTACT_WALL) { walls++; wall_frames++; }
            }
        }
        if (!teleported) {
            done++;
            if (tail < 2 && vmax < 5) idle++;
            if (tail < 2 && vmax >= 5) {
                stopped++;
                printf("  stopped: ride %d from (%.1f, %.1f, %.1f) heading %.0f, at (%.1f, %.1f, %.1f) state %u contacts %#x, "
                       "%d frames at a wall (n %.2f %.2f %.2f, grip %.2f)\n", r, (double)start.x, (double)start.y,
                       (double)start.z, (double)(heading * 180 / TC_PI), (double)at[FRAMES - 1].x, (double)at[FRAMES - 1].y,
                       (double)at[FRAMES - 1].z, ed.ai.state, ed.dyn.contacts, walls, (double)ed.dyn.walk.wall_normal.x,
                       (double)ed.dyn.walk.wall_normal.y, (double)ed.dyn.walk.wall_normal.z, (double)ed.dyn.walk.wall_grip);
            }
        }
        tc_ed_ai_free(&ed);
    }
    printf("skicheck %s: %d rides, %d started on the flat, %d stopped after riding (%.1f %%); mean speed %.1f, at a wall "
           "%.1f %% of frames\n", name, done, idle, stopped, 100.0 * stopped / (done ? done : 1),
           frames > 0 ? speed / frames : 0, frames > 0 ? 100 * wall_frames / frames : 0);
    tc_collision_free(&world);
    tc_ai_model_free(&model);
    tc_level_free(&level);
    if (done == 0 || stopped * 50 > done) { printf("FAIL\n"); return 1; }
    printf("ok\n");
    return 0;
}
