/* Headless checks of the play-mode controller (no window, no GPU); C mirror of the former Swift
 * PlayCheck, printing the same lines. Usage: playcheck [out/ttlv]
 * 1. Synthetic scene: floor, 30-degree ramp, wall. 2. Each converted level: drop Ed at his start,
 * walk into a wall. Test-only platform code: stdio file reading and POSIX <dirent.h>. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tonic/tonic.h"

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

static int failures = 0;

static void check(int ok, const char *what) {
    printf("%s%s\n", ok ? "  ok   " : "  FAIL ", what);
    if (!ok) failures++;
}

/* Swift's Float description: shortest digits that round-trip, always with a decimal point. */
static const char *swift_float(float x, char *buf, size_t size) {
    int d;
    for (d = 0; d < 12; d++) {
        snprintf(buf, size, "%.*f", d, (double)x);
        if (strtof(buf, NULL) == x) break;
    }
    if (d == 0) strncat(buf, ".0", size - strlen(buf) - 1);
    return buf;
}

static const char *b(int v) { return v ? "true" : "false"; }

static tc_player_input input(float mx, float my, int run, int jump) {
    tc_player_input in;
    in.move = tc_v2(mx, my);
    in.run = run != 0;
    in.jump = jump != 0;
    return in;
}

/* Steps the controller; returns how many frames moved a body-sphere centre through a triangle
 * (a centre always stays a radius away from every surface, so any crossing means tunnelling). */
static int run(tc_player *p, float seconds, float fps, tc_player_input in) {
    int crossings = 0, i, n = (int)(seconds * fps), k;
    for (i = 0; i < n; i++) {
        tc_vec3 before = p->position, d;
        float offs[2], len;
        tc_player_step(p, 1 / fps, &in);
        offs[0] = p->radius;
        offs[1] = p->height - p->radius;
        for (k = 0; k < 2; k++) {
            tc_vec3 a = tc_v3_add(before, tc_v3(0, 0, offs[k]));
            d = tc_v3_sub(p->position, before);
            len = tc_v3_length(d);
            if (len > 1e-5f && tc_collision_raycast(p->world, a, tc_v3_div(d, len), len, NULL) &&
                p->position.z > p->world->bounds_min.z - 20)
                crossings++;
        }
    }
    return crossings;
}

static int quad(tc_vec3 *out, tc_vec3 a, tc_vec3 b2, tc_vec3 c, tc_vec3 d) {
    out[0] = a; out[1] = b2; out[2] = c; out[3] = a; out[4] = c; out[5] = d;
    return 6;
}

static int cmp_str(const void *a, const void *b2) { return strcmp(*(char *const *)a, *(char *const *)b2); }
static int cmp_anim(const void *a, const void *b2) {
    return strcmp(tc_anim_state_name(*(const tc_anim_state *)a), tc_anim_state_name(*(const tc_anim_state *)b2));
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = (uint8_t *)malloc((size_t)n + 1);
        if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
        *size = (size_t)n;
    }
    fclose(f);
    return data;
}

static void synthetic(void) {
    char msg[512];
    float s = (float)tan(30.0 * 3.14159265358979323846 / 180);
    tc_vec3 tris[18];
    int n = 0;
    tc_collision synth;
    tc_player p, r, j, f;
    tc_player_input none = input(0, 0, 0, 0);

    printf("synthetic: floor z=0 (x,y in -20..20), wall at y=10 facing -y, 30\xC2\xB0 ramp rising along +x from x=5\n");
    n += quad(tris + n, tc_v3(-20, -20, 0), tc_v3(20, -20, 0), tc_v3(20, 20, 0), tc_v3(-20, 20, 0));
    n += quad(tris + n, tc_v3(-20, 10, 0), tc_v3(-20, 10, 5), tc_v3(20, 10, 5), tc_v3(20, 10, 0)); /* wall plane y = 10 */
    n += quad(tris + n, tc_v3(5, -20, 0.001f), tc_v3(15, -20, 10 * s), tc_v3(15, -5, 10 * s), tc_v3(5, -5, 0.001f)); /* ramp */
    if (tc_collision_init(&synth, tris, (uint32_t)n, NULL) != 0) { puts("out of memory"); exit(1); }

    tc_player_init(&p, &synth, tc_v3(0, 0, 3), 0);
    run(&p, 2, 120, none);
    snprintf(msg, sizeof msg, "drop lands on the floor (z = %.3f, grounded %s)", p.position.z, b(p.grounded));
    check(p.grounded && fabsf(p.position.z) < 0.05f, msg);
    run(&p, 6, 120, input(0, 1, 0, 0));
    snprintf(msg, sizeof msg, "walking into the wall stops at y = %.3f (wall 10, radius %.2f)", p.position.y, p.radius);
    check(p.position.y < 10 - p.radius + 0.02f && p.position.y > 9, msg);
    run(&p, 2, 120, input(1, 1, 0, 0));
    snprintf(msg, sizeof msg, "diagonal input slides along the wall (x = %.2f, y = %.3f)", p.position.x, p.position.y);
    check(p.position.x > 3 && p.position.y < 10 - p.radius + 0.02f, msg);

    tc_player_init(&r, &synth, tc_v3(10, -12, 4), 0);
    run(&r, 2, 120, none);
    {
        float z0 = r.position.z, x0 = r.position.x;
        run(&r, 3, 120, none);
        snprintf(msg, sizeof msg, "stands still on the 30\xC2\xB0 ramp (z %.3f, expected %.3f; drift %.4f)", r.position.z, 5 * s,
                 fabsf(r.position.x - x0));
        check(r.grounded && fabsf(r.position.x - x0) < 0.01f && fabsf(r.position.z - z0) < 0.01f, msg);
    }
    run(&r, 1.5f, 120, input(1, 0, 0, 0));
    snprintf(msg, sizeof msg, "walks up the ramp (sphere rests on the slope, feet z = slope + r(1/cos - 1)) (x %.2f, z %.3f)",
             r.position.x, r.position.z);
    check(r.grounded && r.position.x > 12.5f &&
              fabsf(r.position.z - ((r.position.x - 5) * s + r.radius * (1 / cosf(TC_PI / 6) - 1))) < 0.02f, msg);
    run(&r, 4.5f, 120, input(-1, 0, 0, 0));
    snprintf(msg, sizeof msg, "walks down the ramp onto the floor, staying grounded (x %.2f, z %.3f)", r.position.x, r.position.z);
    check(r.grounded && r.position.x < 5 && fabsf(r.position.z) < 0.05f, msg);

    tc_player_init(&j, &synth, tc_v3(0, 0, 0), 0);
    run(&j, 0.5f, 120, none);
    {
        float top = 0;
        int seen[TC_ANIM_COUNT] = {0}, i, k = 0;
        tc_anim_state list[TC_ANIM_COUNT];
        char names[128] = "";
        for (i = 0; i < 240; i++) {
            tc_player_input in = input(0, 0, 0, i < 5);
            tc_player_step(&j, 1.0f / 120, &in);
            top = fmaxf(top, j.position.z);
            seen[j.animation] = 1;
        }
        for (i = 0; i < TC_ANIM_COUNT; i++) if (seen[i]) list[k++] = (tc_anim_state)i;
        qsort(list, (size_t)k, sizeof list[0], cmp_anim);
        for (i = 0; i < k; i++) {
            if (i) strcat(names, ",");
            strcat(names, tc_anim_state_name(list[i]));
        }
        snprintf(msg, sizeof msg, "jump peaks at %.2f and lands; animations %s", top, names);
        check(top > 0.9f && j.grounded && fabsf(j.position.z) < 0.05f, msg);
    }

    tc_player_init(&f, &synth, tc_v3(-10, 0, 200), 0);
    {
        int crossed = run(&f, 8, 30, none);
        snprintf(msg, sizeof msg, "200-unit drop at 30 fps (terminal 50 u/s) does not tunnel (z = %.3f)", f.position.z);
        check(crossed == 0 && f.grounded && fabsf(f.position.z) < 0.05f, msg);
    }
    tc_collision_free(&synth);
}

static void level(const char *folder, const char *name) {
    char path[4096], msg[512], fx[32], fy[32], fz[32];
    size_t size = 0;
    uint8_t *data;
    tc_level lv;
    tc_collision world;
    tc_player p;
    tc_vec3 start, from;
    clock_t t0;
    int fell, k, found = 0;
    float best_dist = INFINITY;
    tc_vec2 best_dir = {0, 0};
    tc_vec3 best_n = {0, 0, 0};

    snprintf(path, sizeof path, "%s/%s", folder, name);
    data = read_file(path, &size);
    if (!data || tc_level_load(&lv, data, size) != TC_OK || lv.version < 5 || !lv.has_ed) {
        if (data) tc_level_free(&lv);
        free(data);
        printf("%s: not v5 / no Ed\n", name);
        failures++;
        return;
    }
    free(data);
    if (tc_collision_init(&world, lv.collision, 3 * lv.collision_triangle_count, lv.collision_floors) != 0) { puts("out of memory"); exit(1); }
    start = tc_v3(lv.ed.start.m[3], lv.ed.start.m[7], lv.ed.start.m[11]);
    tc_player_init(&p, &world, start, tc_heading_of(&lv.ed.start));
    printf("%s: %u triangles, Ed at (%s, %s, %s)\n", name, world.tri_count, swift_float(start.x, fx, sizeof fx),
           swift_float(start.y, fy, sizeof fy), swift_float(start.z, fz, sizeof fz));
    t0 = clock();
    fell = run(&p, 3, 120, input(0, 0, 0, 0));
    if (p.grounded && fell == 0 && tc_v3_distance(p.position, start) >= 0.6f) {
        printf("  note lands %.2f below the start (no floor right under the GPT placement)\n", start.z - p.position.z);
    } else {
        snprintf(msg, sizeof msg, "lands near the start (z %.3f vs %.3f, %.2f away)", p.position.z, start.z,
                 tc_v3_distance(p.position, start));
        check(p.grounded && fell == 0 && tc_v3_distance(p.position, start) < 0.6f, msg);
    }
    /* Nearest wall in 32 directions at chest height; walk straight at it. */
    for (k = 0; k < 32; k++) {
        float a = (float)k / 32 * 2 * TC_PI;
        tc_vec2 d = tc_v2(cosf(a), sinf(a));
        tc_vec3 eye = tc_v3_add(p.position, tc_v3(0, 0, 0.8f));
        tc_hit h;
        if (tc_collision_raycast(&world, eye, tc_v3(d.x, d.y, 0), 20, &h) && fabsf(h.normal.z) < 0.3f && h.distance > 1) {
            float nl = tc_v2_length(tc_v2(h.normal.x, h.normal.y));
            float dd = (h.normal.x / nl) * d.x + (h.normal.y / nl) * d.y;
            int wide = 1, m;
            const float das[2] = {-0.06f, 0.06f};
            if (!(fabsf(dd) > 0.99f)) continue; /* head-on, so sliding can't carry Ed past it */
            for (m = 0; m < 2 && wide; m++) { /* and wide: neighbouring rays hit at about the same distance */
                tc_vec2 e = tc_v2(cosf(a + das[m]), sinf(a + das[m]));
                tc_hit h2;
                wide = tc_collision_raycast(&world, eye, tc_v3(e.x, e.y, 0), 20, &h2) && fabsf(h2.distance - h.distance) < 0.5f;
            }
            if (wide && h.distance < best_dist) { best_dist = h.distance; best_dir = d; best_n = h.normal; found = 1; }
        }
    }
    if (!found) { printf("  (no wall within 20 units)\n"); goto out; }
    from = p.position;
    {
        int crossed = run(&p, best_dist / p.walk_speed + 3, 120, input(best_dir.x, best_dir.y, 0, 0));
        float dx, dy, travelled, lateral;
        crossed += run(&p, 3, 120, input(best_dir.x, best_dir.y, 1, 0));
        dx = p.position.x - from.x;
        dy = p.position.y - from.y;
        travelled = dx * best_dir.x + dy * best_dir.y;
        lateral = fabsf(dx * -best_dir.y + dy * best_dir.x);
        if (from.z - p.position.z > 2) { /* walked off a ledge before reaching the wall: only tunnelling is checked */
            printf("  note fell %.1f into a gap before the wall (lateral %.2f)\n", from.z - p.position.z, lateral);
            check(crossed == 0, "no tunnelling while walking/falling");
            goto out;
        }
        snprintf(msg, sizeof msg,
                 "walk then run 3 s at the nearest head-on wall (%.2f away, normal z %.2f): no tunnelling; moved %.2f along, z %.2f -> %.2f, grounded %s",
                 best_dist, best_n.z, travelled, from.z, p.position.z, b(p.grounded));
        check(crossed == 0 && travelled < best_dist, msg);
        printf("  sim time 3 s + %.1f s took %.0f ms\n", best_dist / p.walk_speed + 3,
               (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    }
out:
    tc_collision_free(&world);
    tc_level_free(&lv);
}

int main(int argc, char **argv) {
    const char *folder = argc > 1 ? argv[1] : "out/ttlv";
    char **names = NULL;
    size_t count = 0, cap = 0, i;
    DIR *dir;
    struct dirent *e;

    synthetic();
    if ((dir = opendir(folder)) != NULL) {
        while ((e = readdir(dir)) != NULL) {
            size_t len = strlen(e->d_name);
            if (len > 5 && strcmp(e->d_name + len - 5, ".ttlv") == 0) {
                if (count == cap) {
                    char **grown = (char **)realloc(names, (cap = cap ? cap * 2 : 32) * sizeof *names);
                    if (!grown) break;
                    names = grown;
                }
                names[count] = (char *)malloc(len + 1);
                if (names[count]) memcpy(names[count++], e->d_name, len + 1);
            }
        }
        closedir(dir);
    }
    if (count == 0) printf("no .ttlv files in %s\n", folder);
    if (count) qsort(names, count, sizeof *names, cmp_str);
    for (i = 0; i < count; i++) { level(folder, names[i]); free(names[i]); }
    free(names);
    if (failures == 0) printf("all checks passed\n");
    else printf("%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
