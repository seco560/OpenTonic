/* Portals headless (docs/PORTALS.md): Ed walks into a portal and its effect is checked. The screen transitions are
 * faked: the screen counts as covered 1.6 s after each cover request (effect A's length).
 *   sud0, ZOR_ArcheCocktail (red): walk-in (B66 -> B68 -> state 228), cover, then a level request for "bar";
 *   Land, SDO_Arche2 (blue walk-in): teleport to its waypoint, then the step-out (state 229);
 *   north, KEV_SousMap1a2 (blue zone teleport): cover at once, no walk-in, teleport;
 *   Totalski, the SYM_GenTrou holes (.ttai ZONE, COND_13): inside a hole's zone box the skier falls in (dsg29 := 1),
 *   5 above the box he does not.
 * Usage: portalcheck [out/ttlv]. Test-only stdio. */
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

/* The portal whose fire ops include op `op` (and, for a level change, to `level`); -1 none. With `nth`, the nth one. */
static int find_portal(const tc_ai_model *m, int op, const char *level, int nth) {
    uint16_t i, k;
    for (i = 0; i < m->portal_count; i++) {
        const tc_ai_portal *g = &m->portals[i];
        for (k = 0; k < g->fire_count; k++) {
            const tc_ai_portal_op *o = &g->ops[g->capture_count + k];
            if (o->op == op && (!level || strcmp(m->names[o->b], level) == 0) && nth-- == 0) return i;
        }
    }
    return -1;
}

typedef struct result { int captured, saw_228, saw_229, covers; float level_at, teleport_at; char level[64]; tc_vec3 to; } result;

static int run(const char *dir, const char *name, int op, const char *level, int nth, result *res) {
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_level lv;
    tc_collision world;
    tc_ai_model model;
    tc_player player;
    tc_ed_ai ed;
    const tc_ai_portal *g;
    tc_vec3 front, start;
    tc_hit hit;
    float cover_at = -1, t;
    int i, k, last_state = -1, last_beh = -1;
    memset(res, 0, sizeof(*res));
    res->level_at = res->teleport_at = -1;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) { fprintf(stderr, "%s?\n", path); return 0; }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    if (!(data = read_file(path, &size)) || tc_ai_model_load(&model, data, size) != TC_OK) { fprintf(stderr, "%s?\n", path); return 0; }
    free(data);
    if (tc_collision_init(&world, lv.collision, 3 * lv.collision_triangle_count, lv.collision_floors) != 0) return 0;
    k = find_portal(&model, op, level, nth);
    if (k < 0) { fprintf(stderr, "%s: no such portal\n", name); return 0; }
    g = &model.portals[k];
    /* Start 8 away on ground at its height, facing it: in front of it (its local -Y) if there is ground there, else the
     * first of 16 directions that has some. */
    front = tc_v3_normalize(tc_v3(-g->rows[1], -g->rows[4], 0));
    start = g->position;
    for (k = 0; k < 17; k++) {
        float a = (float)(k - 1) * TC_PI / 8;
        tc_vec3 dir = k == 0 ? front : tc_v3(cosf(a), sinf(a), 0), o = tc_v3_add(g->position, tc_v3_scale(dir, 8));
        if (tc_collision_raycast(&world, tc_v3_add(o, tc_v3(0, 0, 3)), tc_v3(0, 0, -1), 6, &hit)) {
            start = tc_v3(o.x, o.y, o.z + 3 - hit.distance + 0.05f);
            front = dir;
            break;
        }
    }
    tc_player_init(&player, &world, start, atan2f(-front.x, front.y));
    tc_player_apply_floors(&player);
    if (tc_ed_ai_init(&ed, &model, &player, name) != TC_OK) return 0;
    printf("%s: portal %d @(%.1f, %.1f, %.1f) r %.1f, Ed starts at (%.1f, %.1f, %.1f)\n", name, k, (double)g->position.x,
           (double)g->position.y, (double)g->position.z, (double)g->radius, (double)start.x, (double)start.y, (double)start.z);
    for (i = 0; i < 30 * 20; i++) { /* at the game's tick */
        tc_vec3 eye;
        uint32_t in = ed.portal < 0 && res->teleport_at < 0 ? 1u << TC_AI_IN_UP : 0;
        t = (float)i * TC_ED_AI_TICK;
        eye = tc_v3_add(player.position, tc_v3(sinf(player.heading) * -5, cosf(player.heading) * 5, 2)); /* behind Ed */
        if (cover_at >= 0 && t >= cover_at + 1.6f) { ed.screen_covered = true; cover_at = -1; }
        tc_ed_ai_step(&ed, TC_ED_AI_TICK, in, 0, 0, eye);
        if (ed.portal >= 0) res->captured = 1;
        if (ed.ai.state == 228) res->saw_228 = 1;
        if (ed.ai.state == 229 && res->teleport_at >= 0) res->saw_229 = 1;
        if ((int)ed.ai.state != last_state || (int)ed.ai.normal.behaviour != last_beh) {
            last_state = ed.ai.state;
            last_beh = ed.ai.normal.behaviour;
            printf("  %5.2f  B%d state %d  (%.1f, %.1f, %.1f)\n", (double)t, last_beh, last_state, (double)player.position.x,
                   (double)player.position.y, (double)player.position.z);
        }
        if (ed.events.cover) {
            ed.events.cover = false;
            res->covers++;
            cover_at = t;
            printf("  %5.2f  cover requested\n", (double)t);
        }
        if (ed.events.teleported) {
            ed.events.teleported = ed.events.camera_set = false;
            res->teleport_at = t;
            res->to = player.position;
            ed.screen_covered = false; /* the reveal */
            printf("  %5.2f  teleported to (%.1f, %.1f, %.1f), camera (%.1f, %.1f, %.1f)\n", (double)t, (double)player.position.x,
                   (double)player.position.y, (double)player.position.z, (double)ed.events.camera.x,
                   (double)ed.events.camera.y, (double)ed.events.camera.z);
        }
        if (ed.events.level) {
            res->level_at = t;
            snprintf(res->level, sizeof(res->level), "%s", ed.events.level);
            printf("  %5.2f  level request %s%s%s\n", (double)t, ed.events.level, ed.events.place ? " at " : "",
                   ed.events.place ? ed.events.place : "");
            break;
        }
        if (res->teleport_at >= 0 && t > res->teleport_at + 4) break;
    }
    tc_ed_ai_free(&ed);
    tc_ai_model_free(&model);
    tc_collision_free(&world);
    tc_level_free(&lv);
    return 1;
}

/* Totalski's holes: for each portal with a zone that sets the skier's fall flag dsg29, the skier at the centre of its
 * first box (taken out of the perso's axes) is captured, 5 above the box he is not (7 of the 11 boxes hold the
 * perso's own origin, so that is no test). Returns holes tested, -1 on
 * a load error; *inside / *outside count the expected outcomes. */
static int holes(const char *dir, int *inside, int *outside) {
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_level lv;
    tc_collision world;
    tc_ai_model model;
    tc_player player;
    tc_ed_ai ed;
    int n = 0;
    uint16_t i, k;
    *inside = *outside = 0;
    snprintf(path, sizeof(path), "%s/Totalski.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) return -1;
    free(data);
    snprintf(path, sizeof(path), "%s/Totalski.ttai", dir);
    if (!(data = read_file(path, &size)) || tc_ai_model_load(&model, data, size) != TC_OK) return -1;
    free(data);
    if (tc_collision_init(&world, lv.collision, 3 * lv.collision_triangle_count, lv.collision_floors) != 0) return -1;
    tc_player_init(&player, &world, tc_v3(lv.ed.start.m[3], lv.ed.start.m[7], lv.ed.start.m[11]), 0);
    if (tc_ed_ai_init(&ed, &model, &player, "Totalski") != TC_OK) return -1;
    for (i = 0; i < model.portal_count; i++) {
        const tc_ai_portal *g = &model.portals[i];
        const float *m = g->zone_inverse;
        bool hole = false;
        float det, r[9];
        tc_vec3 c, w, t;
        for (k = 0; k < g->capture_count; k++)
            if (g->ops[k].op == TC_AI_POP_SET && g->ops[k].a == 29) hole = true;
        if (!hole || !g->zone_count || g->zone[0].kind != 8) continue;
        n++;
        /* the inverse of the inverse: forward = R^-1 (q - t) */
        det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
        r[0] = (m[5] * m[10] - m[6] * m[9]) / det, r[1] = (m[2] * m[9] - m[1] * m[10]) / det, r[2] = (m[1] * m[6] - m[2] * m[5]) / det;
        r[3] = (m[6] * m[8] - m[4] * m[10]) / det, r[4] = (m[0] * m[10] - m[2] * m[8]) / det, r[5] = (m[2] * m[4] - m[0] * m[6]) / det;
        r[6] = (m[4] * m[9] - m[5] * m[8]) / det, r[7] = (m[1] * m[8] - m[0] * m[9]) / det, r[8] = (m[0] * m[5] - m[1] * m[4]) / det;
        for (k = 0; k < 2; k++) {
            c = tc_v3_scale(tc_v3_add(g->zone[0].a, g->zone[0].b), 0.5f);
            if (k == 1) c.z = g->zone[0].b.z + 5;
            t = tc_v3(c.x - m[3], c.y - m[7], c.z - m[11]);
            w = tc_v3(r[0] * t.x + r[1] * t.y + r[2] * t.z, r[3] * t.x + r[4] * t.y + r[5] * t.z,
                      r[6] * t.x + r[7] * t.y + r[8] * t.z);
            tc_ai_dsgvar_set_int(&ed.ai, 29, 0);
            player.position = w;
            ed.portal = -1;
            ed.portal_armed[i] = 1;
            tc_ed_ai_run_portals(&ed, 1.0f / 60);
            if (k == 0 && (int)tc_ai_dsgvar_float(&ed.ai, 29) == 1) (*inside)++;
            if (k == 1 && (int)tc_ai_dsgvar_float(&ed.ai, 29) == 0) (*outside)++;
        }
    }
    tc_ed_ai_free(&ed);
    tc_ai_model_free(&model);
    tc_collision_free(&world);
    tc_level_free(&lv);
    return n;
}

static int failures;
static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    result r;
    if (run(dir, "sud0", TC_AI_POP_LEVEL, "bar", 0, &r)) {
        check(r.captured && r.saw_228, "sud0 red arch: captured, steps in (state 228)");
        check(r.covers >= 1 && strcmp(r.level, "bar") == 0, "sud0 red arch: cover, then level request \"bar\"");
    } else failures++;
    if (run(dir, "Land", TC_AI_POP_TELEPORT, NULL, 0, &r)) {
        check(r.captured && r.saw_228 && r.covers >= 1, "Land blue arch: walk-in and cover");
        check(r.teleport_at >= 0, "Land blue arch: teleported");
        check(r.saw_229, "Land blue arch: steps out (state 229)");
    } else failures++;
    if (run(dir, "north", TC_AI_POP_TELEPORT, NULL, 2, &r)) {
        check(r.captured && !r.saw_228 && r.covers >= 1 && r.teleport_at >= 0, "north zone portal: cover, no walk-in, teleport");
    } else failures++;
    {
        int inside, outside, n = holes(dir, &inside, &outside);
        char what[160];
        snprintf(what, sizeof(what), "Totalski holes (zones): %d of %d capture inside their box, %d of %d not 5 above "
                 "it", inside, n, outside, n);
        check(n > 0 && inside == n && outside == n, what);
    }
    printf(failures ? "FAIL\n" : "ok\n");
    return failures != 0;
}
