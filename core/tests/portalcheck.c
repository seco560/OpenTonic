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

/* sud0's walk-in passage into Doc's house (HEG_TransMaison01 -> 02, docs/PORTALS.md): Ed walks into its zone from
 * outside (its local +Y), the cover starts, and once covered he is at the far end and walks out. Returns 1 passed,
 * 0 failed, -1 when the level has no such passage (data from before it was exported). */
static int passage(const char *dir) {
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_level lv;
    tc_collision world;
    tc_ai_model model;
    tc_player player;
    tc_ed_ai ed;
    int i, k = -1, covered = 0, ok = 0;
    float cover_at = -1;
    snprintf(path, sizeof(path), "%s/sud0.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) return 0;
    free(data);
    snprintf(path, sizeof(path), "%s/sud0.ttai", dir);
    if (!(data = read_file(path, &size)) || tc_ai_model_load(&model, data, size) != TC_OK) return 0;
    free(data);
    if (tc_collision_init(&world, lv.collision, 3 * lv.collision_triangle_count, lv.collision_floors) != 0) return 0;
    for (i = 0; i < model.portal_count; i++)
        if (fabsf(model.portals[i].position.x - 32.3f) < 0.5f && fabsf(model.portals[i].position.y + 29.0f) < 0.5f) k = i;
    if (k >= 0) {
        const tc_ai_portal *g = &model.portals[k];
        tc_vec3 y = tc_v3_normalize(tc_v3(g->rows[1], g->rows[4], 0)), start = tc_v3_add(g->position, tc_v3_scale(y, 6));
        start.z += 1;
        tc_player_init(&player, &world, start, atan2f(-y.x, y.y));
        if (tc_ed_ai_init(&ed, &model, &player, "sud0") != TC_OK) return 0;
        for (i = 0; i < 30 * 8; i++) {
            float t = (float)i * TC_ED_AI_TICK;
            tc_vec3 eye = tc_v3_add(player.position, tc_v3(sinf(player.heading) * -5, cosf(player.heading) * 5, 2));
            if (cover_at >= 0 && t >= cover_at + 1.6f) { ed.screen_covered = true; cover_at = -1; covered = 1; }
            tc_ed_ai_step(&ed, TC_ED_AI_TICK, t < 2 ? 1u << TC_AI_IN_UP : 0, 0, 0, eye);
            if (ed.events.cover) { ed.events.cover = false; cover_at = t; }
            if (ed.events.teleported) { ed.events.teleported = false; ed.screen_covered = false; }
        }
        /* the far end is HEG_TransMaison02 at (11.6, -28.5); walking out takes him a few units west of it */
        ok = covered && player.position.x < 12 && player.position.x > 0 && fabsf(player.position.y + 28) < 3;
        printf("  sud0 passage: Ed ends at (%.1f, %.1f, %.1f)\n", (double)player.position.x, (double)player.position.y,
               (double)player.position.z);
        tc_ed_ai_free(&ed);
    }
    tc_ai_model_free(&model);
    tc_collision_free(&world);
    tc_level_free(&lv);
    return k < 0 ? -1 : ok;
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

static int look_away; /* in_game: turn the camera 0.5 s into the fixed shot */

/* The same portals in the game (since 2026-11-13 the cast runs them: their own scripts capture Ed, cover the screen and
 * move him; docs/PORTALS.md "The arches run themselves"): a headless viewer at 60 frames per second with its real
 * screen transitions; Ed walks at the portal until it captures him (his normal behaviour changes: forced), then the
 * player lets go. Outcomes as run()'s; capture = his walk-in (or a zone portal's wait) began. 0 on a load error. */
typedef struct game_result {
    int captured, saw_228, saw_229, covers, record_driven, hidden_left, fixed_frames;
    float fixed_eye_moved; /* how far the fixed shot's eye travelled (a glide) */
    int looked_back;       /* with look_away: the shot dropped while the player turned its camera */
    float turned;          /* ... how far its eye moved meanwhile (it orbits the look point) */
    float teleport_at;
    char level[64];
    tc_vec3 end;
} game_result;

static int in_game_at(const char *dir, const char *name, int op, const char *level, int nth, const tc_vec3 *near,
                      game_result *res);
static int in_game(const char *dir, const char *name, int op, const char *level, int nth, game_result *res) {
    return in_game_at(dir, name, op, level, nth, NULL, res);
}

/* near: the portal at that place instead (x, y within 0.5), approached from behind (its local +Y, 6 away: a passage). */
static int in_game_at(const char *dir, const char *name, int op, const char *level, int nth, const tc_vec3 *near,
                      game_result *res) {
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level lv;
    tc_ai_model probe;
    tc_viewer v;
    tc_viewer_desc desc;
    tc_vec3 front, start, last;
    const tc_ai_portal *g;
    const tc_ed_ai *e;
    tc_viewer_phase phase = TC_VP_PLAY;
    int i, k, last_beh = -1, last_state = -1, flags = 0;
    int32_t first_forced;
    memset(res, 0, sizeof(*res));
    res->teleport_at = -1;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) { free(data); return 0; }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    if (!ai || tc_ai_model_load(&probe, ai, ai_size) != TC_OK) { free(ai); tc_level_free(&lv); return 0; }
    k = find_portal(&probe, op, level, nth);
    if (near)
        for (k = -1, i = 0; i < probe.portal_count; i++)
            if (fabsf(probe.portals[i].position.x - near->x) < 0.5f && fabsf(probe.portals[i].position.y - near->y) < 0.5f) k = i;
    if (k < 0) { tc_ai_model_free(&probe); free(ai); tc_level_free(&lv); return 0; }
    g = &probe.portals[k];
    flags = g->flags;
    memset(&desc, 0, sizeof(desc));
    desc.level = &lv;
    desc.level_name = name;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.seed = 7;
    if (tc_viewer_init(&v, &desc) != TC_OK) { tc_ai_model_free(&probe); free(ai); tc_level_free(&lv); return 0; }
    if (!v.playing && tc_viewer_can_play(&v)) tc_viewer_toggle_play(&v);
    if (near) { /* behind it, 6 away, 1 up, facing it (as passage()) */
        front = tc_v3_normalize(tc_v3(g->rows[1], g->rows[4], 0));
        start = tc_v3_add(g->position, tc_v3_scale(front, 6));
        start.z += 1;
    } else { /* 8 in front of it on the ground, facing it (as run()) */
        tc_hit hit;
        tc_collision *world = tc_viewer_player(&v)->world;
        front = tc_v3_normalize(tc_v3(-g->rows[1], -g->rows[4], 0));
        start = g->position;
        for (k = 0; k < 17; k++) {
            float a = (float)(k - 1) * TC_PI / 8;
            tc_vec3 dir = k == 0 ? front : tc_v3(cosf(a), sinf(a), 0), o = tc_v3_add(g->position, tc_v3_scale(dir, 8));
            if (tc_collision_raycast(world, tc_v3_add(o, tc_v3(0, 0, 3)), tc_v3(0, 0, -1), 6, &hit)) {
                start = tc_v3(o.x, o.y, o.z + 3 - hit.distance + 0.05f);
                front = dir;
                break;
            }
        }
    }
    tc_player_teleport((tc_player *)tc_viewer_player(&v), start, atan2f(-front.x, front.y));
    for (i = 0; i < 30; i++) tc_viewer_update(&v, 1.0f / 60); /* settle; the camera behind him */
    e = tc_viewer_driver(&v);
    first_forced = e ? e->ai.forced_by : 0;
    printf("%s (in the game): portal %d (perso %u%s) @(%.1f, %.1f, %.1f), Ed starts at (%.1f, %.1f, %.1f)\n", name,
           find_portal(&probe, op, level, nth), (unsigned)g->perso, flags & TC_AI_PORTAL_CAST ? ", the cast's" : "",
           (double)g->position.x, (double)g->position.y, (double)g->position.z, (double)start.x, (double)start.y,
           (double)start.z);
    res->record_driven = !(flags & TC_AI_PORTAL_CAST);
    last = tc_viewer_player(&v)->position;
    for (i = 0; i < 60 * 25 && e; i++) {
        const tc_viewer_carry *c;
        float t = (float)i / 60;
        tc_vec3 now;
        if (!res->captured && (e->portal >= 0 || e->ai.forced_by != first_forced ||
                               (e->walk_in[0] != TC_AI_NONE16 && e->ai.normal.behaviour == e->walk_in[0])))
            res->captured = 1;
        tc_viewer_set_action(&v, TC_VA_FORWARD, !res->captured);
        {
            int turning = look_away && res->fixed_frames >= 30 && res->fixed_frames < 60; /* half a second */
            tc_vec3 eye0 = v.camera.position;
            tc_viewer_set_action(&v, TC_VA_LOOK_LEFT, turning);
            tc_viewer_update(&v, 1.0f / 60);
            if (turning && tc_viewer_fixed_shot(&v)) res->turned += tc_v3_distance(eye0, v.camera.position);
            if (turning && !tc_viewer_fixed_shot(&v)) res->looked_back = 1; /* the shot dropped: not wanted */
        }
        e = tc_viewer_driver(&v);
        if (!e) break;
        now = tc_viewer_player(&v)->position;
        if (tc_viewer_fixed_shot(&v)) {
            static tc_vec3 prev_eye;
            if (res->fixed_frames++ == 0) printf("  %5.2f  fixed shot from (%.1f, %.1f, %.1f)\n", (double)t,
                                                 (double)v.camera.position.x, (double)v.camera.position.y,
                                                 (double)v.camera.position.z);
            else res->fixed_eye_moved += tc_v3_distance(prev_eye, v.camera.position);
            prev_eye = v.camera.position;
        }
        if (e->ai.state == 228) res->saw_228 = 1;
        if (e->ai.state == 229 && res->teleport_at >= 0) res->saw_229 = 1;
        if ((int)e->ai.normal.behaviour != last_beh || (int)e->ai.state != last_state) {
            last_beh = e->ai.normal.behaviour;
            last_state = e->ai.state;
            printf("  %5.2f  B%d state %d  (%.1f, %.1f, %.1f)\n", (double)t, last_beh, last_state, (double)now.x,
                   (double)now.y, (double)now.z);
        }
        if (v.phase != phase) {
            if (v.phase == TC_VP_COVER || (v.phase == TC_VP_BLACK && phase == TC_VP_PLAY)) {
                res->covers++;
                printf("  %5.2f  cover\n", (double)t);
            }
            phase = v.phase;
        }
        if (tc_v3_distance(now, last) > 5 && res->teleport_at < 0) {
            res->teleport_at = t;
            printf("  %5.2f  moved to (%.1f, %.1f, %.1f)\n", (double)t, (double)now.x, (double)now.y, (double)now.z);
        }
        last = now;
        if ((c = tc_viewer_pending_level(&v)) != NULL) {
            snprintf(res->level, sizeof(res->level), "%s", c->level);
            printf("  %5.2f  level %s%s%s\n", (double)t, c->level, c->place[0] ? " at " : "", c->place);
            break;
        }
        if (res->teleport_at >= 0 && t > res->teleport_at + 4) break;
    }
    res->end = tc_viewer_player(&v)->position;
    res->hidden_left = e && e->ai.transparency == 0;
    tc_viewer_free(&v);
    tc_ai_model_free(&probe);
    free(ai);
    tc_level_free(&lv);
    return 1;
}

/* cavedoc after the robot (docs/BUGS.md: the cutscene that never came): the lever by Doc's cage (SDO_OuvrePorte, 186)
 * pulled, its dsg0 1, with Ed by the last door (BEM_PorteDeFinDoc, 25): the door holds him and starts Doc (FRB_DocCave,
 * 105, running since 2026-11-13); Doc walks out (the fixed shot), comes to Ed, says his line (the player takes it away
 * with Jump), the cover, then sud0 before his house (PROC 29: heg_positionedcavedocdevantdoc). 0 on a load error. */
typedef struct doc_result { int doc_moved, fixed_frames, lines; char level[64], place[64]; float level_at; } doc_result;

static const tc_cast_actor *by_id(const tc_cast *c, uint32_t id) {
    return c && id < c->id_count && c->by_id[id] >= 0 && (uint32_t)c->by_id[id] < c->count ? &c->actors[c->by_id[id]] : NULL;
}

static int cavedoc_doc(const char *dir, doc_result *res) {
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level lv;
    tc_viewer v;
    tc_viewer_desc desc;
    const tc_cast *c;
    const tc_cast_actor *door, *doc, *lever;
    tc_vec3 doc_start;
    int i, tapped = 0, last_doc = -1;
    memset(res, 0, sizeof(*res));
    res->level_at = -1;
    snprintf(path, sizeof(path), "%s/cavedoc.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) { free(data); return 0; }
    free(data);
    snprintf(path, sizeof(path), "%s/cavedoc.ttai", dir);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &lv;
    desc.level_name = "cavedoc";
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.seed = 7;
    if (!ai || tc_viewer_init(&v, &desc) != TC_OK) { free(ai); tc_level_free(&lv); return 0; }
    if (!v.playing && tc_viewer_can_play(&v)) tc_viewer_toggle_play(&v);
    c = tc_viewer_cast(&v);
    door = by_id(c, 25), doc = by_id(c, 105), lever = by_id(c, 186);
    if (!door || !doc || !lever) {
        printf("  cavedoc: door %s, Doc %s, lever %s in the cast\n", door ? "yes" : "no", doc ? "yes" : "no", lever ? "yes" : "no");
        tc_viewer_free(&v);
        free(ai);
        tc_level_free(&lv);
        return 1;
    }
    { /* in front of the door, away from Doc's cell, on the ground */
        tc_vec3 out = tc_v3_sub(door->ai->position, doc->ai->position), at;
        tc_hit hit;
        out.z = 0;
        out = tc_v3_normalize(out);
        at = tc_v3_add(door->ai->position, tc_v3_scale(out, 3));
        if (tc_collision_raycast(tc_viewer_player(&v)->world, tc_v3_add(at, tc_v3(0, 0, 3)), tc_v3(0, 0, -1), 10, &hit))
            at.z = at.z + 3 - hit.distance + 0.05f;
        tc_player_teleport((tc_player *)tc_viewer_player(&v), at, atan2f(out.x, -out.y) + TC_PI);
    }
    for (i = 0; i < 60; i++) tc_viewer_update(&v, 1.0f / 60);
    doc_start = doc->ai->position;
    tc_ai_dsgvar_set_int(lever->ai, 0, 1); /* the lever pulled (its own scripts set it so when Ed works it) */
    printf("cavedoc (in the game): door at (%.1f, %.1f, %.1f), Doc at (%.1f, %.1f, %.1f)\n", (double)door->ai->position.x,
           (double)door->ai->position.y, (double)door->ai->position.z, (double)doc_start.x, (double)doc_start.y,
           (double)doc_start.z);
    for (i = 0; i < 60 * 60; i++) {
        const tc_viewer_carry *carry;
        float t = (float)i / 60;
        if (tc_viewer_speech(&v) || tapped) { /* the player takes each line away */
            tapped = !tapped && tc_viewer_speech(&v);
            res->lines += tapped;
            tc_viewer_set_action(&v, TC_VA_JUMP, tapped);
        }
        tc_viewer_update(&v, 1.0f / 60);
        c = tc_viewer_cast(&v);
        doc = by_id(c, 105);
        if (!doc) break;
        if ((int)doc->ai->normal.behaviour != last_doc) {
            last_doc = doc->ai->normal.behaviour;
            printf("  %5.2f  Doc B%d at (%.1f, %.1f, %.1f)\n", (double)t, last_doc, (double)doc->ai->position.x,
                   (double)doc->ai->position.y, (double)doc->ai->position.z);
        }
        if (tc_v3_distance(doc->ai->position, doc_start) > 1) res->doc_moved = 1;
        res->fixed_frames += tc_viewer_fixed_shot(&v);
        if ((carry = tc_viewer_pending_level(&v)) != NULL) {
            snprintf(res->level, sizeof(res->level), "%s", carry->level);
            snprintf(res->place, sizeof(res->place), "%s", carry->place);
            res->level_at = t;
            printf("  %5.2f  level %s at %s\n", (double)t, carry->level, carry->place);
            break;
        }
    }
    tc_viewer_free(&v);
    free(ai);
    tc_level_free(&lv);
    return 1;
}

static int tcv_same(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if ((*a | 32) != (*b | 32)) return 0;
    return *a == *b;
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
        int r = passage(dir);
        if (r < 0) printf("  (sud0 has no walk-in passages yet: re-export it; passage check skipped)\n");
        else check(r, "sud0 passage into Doc's house: cover, then out at the far end (HEG_TransMaison02)");
    }
    {
        game_result g;
        if (in_game(dir, "sud0", TC_AI_POP_LEVEL, "bar", 0, &g)) {
            check(!g.record_driven, "sud0 red arch: the cast runs it (TC_AI_PORTAL_CAST)");
            check(g.captured && g.saw_228, "sud0 red arch, in the game: captured, steps in (state 228)");
            check(g.covers >= 1 && strcmp(g.level, "bar") == 0, "sud0 red arch, in the game: cover, then the level \"bar\"");
        } else failures++;
        if (in_game(dir, "Land", TC_AI_POP_TELEPORT, NULL, 0, &g)) {
            check(g.captured && g.saw_228 && g.covers >= 1, "Land blue arch, in the game: walk-in and cover");
            check(g.teleport_at >= 0 && g.saw_229, "Land blue arch, in the game: teleported, steps out (state 229)");
            printf("  Land blue arch: the fixed shot %d frames, its eye moved %.1f\n", g.fixed_frames, (double)g.fixed_eye_moved);
            check(g.fixed_frames > 0 && g.fixed_eye_moved > 0.5f,
                  "Land blue arch, in the game: the camera glides to the arch's waypoint (the fixed shot)");
        } else failures++;
        { /* the passage into Doc's house: HEG_TransMaison01 -> 02 (11.6, -28.5); no cover in the game (Ed is made
           * invisible, SetTransparency 0, and put at the far end; the record's cover stands in for it) */
            tc_vec3 at = tc_v3(32.3f, -29.0f, 0);
            if (in_game_at(dir, "sud0", 0, NULL, 0, &at, &g)) {
                check(!g.record_driven && g.teleport_at >= 0 && g.end.x < 12 && g.end.x > 0 && fabsf(g.end.y + 28) < 3 &&
                      !g.hidden_left, "sud0 passage into Doc's house, in the game: out at the far end, seen again");
                printf("  sud0 passage: the fixed shot %d frames\n", g.fixed_frames);
                check(g.fixed_frames > 0, "sud0 passage into Doc's house, in the game: the fixed shot at the far end");
            } else failures++;
        }
        look_away = 1;
        if (in_game(dir, "Land", TC_AI_POP_TELEPORT, NULL, 0, &g)) {
            printf("  Land blue arch: turned for 0.5 s, the shot's eye moved %.1f\n", (double)g.turned);
            check(!g.looked_back && g.turned > 1,
                  "Land blue arch, in the game: turning the camera moves the fixed shot's eye, the shot stays (port)");
            check(g.teleport_at >= 0 && g.saw_229, "Land blue arch, in the game: the camera turned, Ed still goes through");
        } else failures++;
        look_away = 0;
        if (in_game(dir, "north", TC_AI_POP_TELEPORT, NULL, 2, &g)) {
            check(g.captured && !g.saw_228 && g.covers >= 1 && g.teleport_at >= 0,
                  "north zone portal, in the game: cover, no walk-in, teleport");
        } else failures++;
    }
    {
        doc_result d;
        if (cavedoc_doc(dir, &d)) {
            printf("  cavedoc: Doc moved %d, the fixed shot %d frames, %d lines\n", d.doc_moved, d.fixed_frames, d.lines);
            check(d.doc_moved && d.fixed_frames > 0 && d.lines >= 1,
                  "cavedoc after the robot: the door lets Doc out, he comes to Ed and speaks (the fixed shot)");
            check(tcv_same(d.level, "sud0") && tcv_same(d.place, "heg_positionedcavedocdevantdoc"),
                  "cavedoc after the robot: then sud0, before Doc's house");
        } else failures++;
    }
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
