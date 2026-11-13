/* The viewer module without a GPU (docs/VIEWER_CORE.md): loads a level and its .ttai, runs tc_viewer_update with
 * scripted actions (Ed's AI, free-fly and drop, the simple controller, the settings keys) and checks every frame's
 * draw list (valid mesh indices and textures, transparent draws last). Prints draw counts, Ed's position and the
 * title readout. Usage: viewcheck [out/ttlv] [level]. Test-only stdio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"
#include "tonic/tc_obstacle.h"

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

static int failures;

static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* Every draw names a drawable mesh and an existing texture; [opaque, count) are exactly the transparent ones, back
 * to front (each mesh centre's distance from the eye never grows); a split static is never drawn itself; its index
 * range is whole triangles inside the mesh, all of it while every sector is drawn. */
static int bad_frames;
static uint32_t last_count, last_opaque, last_ed_draws;

static void frame(tc_viewer *v, float dt) {
    uint32_t count, opaque, i;
    const tc_view_draw *d;
    tc_view_frame f;
    float prev;
    int bad = 0, any_water = 0;
    tc_viewer_update(v, dt);
    d = tc_viewer_draws(v, &count, &opaque);
    tc_viewer_frame(v, 16.0f / 10, &f);
    if (opaque > count) bad = 1;
    for (i = 0; i < count; i++) any_water |= d[i].underwater != 0;
    last_ed_draws = 0;
    for (i = 0; i < count; i++)
        if (d[i].mesh >= v->ed_first && d[i].mesh < v->ed_first + v->level->ed.part_count) last_ed_draws++;
    for (i = 0; i < count && !bad; i++) {
        const tc_mesh *m = tc_viewer_mesh(v, d[i].mesh);
        int transparent = (d[i].flags & TC_FLAG_TRANSPARENT) != 0;
        if (!m || m->index_count == 0 || (d[i].flags & ~TC_FLAG_WATER) != m->surface.flags || transparent != (i >= opaque)) bad = 1;
        if (d[i].texture < -1 || d[i].texture >= (int32_t)v->level->texture_count) bad = 1;
        if (d[i].mesh < v->level->mesh_count && v->split && v->split[d[i].mesh]) bad = 1;
        if (m && (d[i].first_index % 3 || d[i].index_count % 3 || d[i].index_count == 0 ||
                  d[i].first_index + d[i].index_count > m->index_count))
            bad = 1;
        /* whole, unless split where the water effect's plane changes (the K test counts every triangle drawn) */
        if (m && v->sector_mode == TC_VS_ALL && (d[i].first_index != 0 || d[i].index_count != m->index_count) &&
            !any_water)
            bad = 1;
        if (d[i].underwater && !(d[i].water_z == d[i].water_z)) bad = 1; /* NaN plane */
    }
    /* blended: back to front by the farthest corner of their bounds along the view (sort_blended), checked here
     * corner by corner */
    for (i = opaque, prev = INFINITY; i < count && !bad; i++) {
        tc_vec3 c = v->mesh_centre[d[i].mesh], e = v->mesh_extent[d[i].mesh], fw = tc_camera_forward(&v->camera);
        const float *m = d[i].model.m;
        float far_depth = -INFINITY;
        int k;
        for (k = 0; k < 8; k++) {
            tc_vec3 q = tc_v3(c.x + (k & 1 ? e.x : -e.x), c.y + (k & 2 ? e.y : -e.y), c.z + (k & 4 ? e.z : -e.z));
            tc_vec3 w = tc_v3(m[0] * q.x + m[4] * q.y + m[8] * q.z + m[12], m[1] * q.x + m[5] * q.y + m[9] * q.z + m[13],
                              m[2] * q.x + m[6] * q.y + m[10] * q.z + m[14]);
            far_depth = fmaxf(far_depth, tc_v3_dot(tc_v3_sub(w, v->camera.position), fw));
        }
        if (far_depth > prev + 1e-3f * (1 + fabsf(prev))) bad = 1;
        prev = far_depth;
    }
    if (!(f.view_projection.m[0] == f.view_projection.m[0])) bad = 1; /* NaN */
    bad_frames += bad;
    last_count = count;
    last_opaque = opaque;
}

static void check_overlay_texture(const tc_viewer *v, const tc_view_overlay *o) {
    if (o->texture < 0 || !tc_viewer_effect_texture(v, (uint32_t)o->texture)) bad_frames++;
}

static void run(tc_viewer *v, float seconds, uint32_t actions) {
    int i, n = (int)(seconds * 60 + 0.5f), a;
    for (a = 0; a < TC_VA_COUNT; a++) tc_viewer_set_action(v, (tc_view_action)a, (actions >> a) & 1u);
    for (i = 0; i < n; i++) frame(v, 1.0f / 60);
    tc_viewer_release_all(v);
}

static tc_vec3 ed(const tc_viewer *v) { return tc_viewer_player(v)->position; }

/* The left stick held at (x, y), set every frame as a frontend does; returns Ed's top ground speed. */
static float run_stick(tc_viewer *v, float seconds, float x, float y) {
    int i, n = (int)(seconds * 60 + 0.5f);
    float top = 0;
    for (i = 0; i < n; i++) {
        tc_viewer_set_move_stick(v, x, y);
        frame(v, 1.0f / 60);
        top = fmaxf(top, tc_player_horizontal_speed(tc_viewer_player(v)));
    }
    tc_viewer_release_all(v);
    return top;
}

static void report(const tc_viewer *v, const char *label) {
    char status[512];
    tc_vec3 p = ed(v);
    tc_viewer_status(v, NULL, status, sizeof(status));
    printf("  %-22s Ed (%7.2f, %7.2f, %6.2f)  draws %u (%u opaque)  %s\n", label, (double)p.x, (double)p.y, (double)p.z,
           last_count, last_opaque, status);
}

#define BIT(a) (1u << (a))

/* The light persos (.ttlv LAIS) run without Ed: Land's lightning (YBA_LightMaker: sets 1 and 2 flashed, then a 1 s
 * blend back to 0, every 5-15 s), sud0's day / night cycle (a start set, then one set on over 10-60 s every 90-180
 * s). A number key holds a set, 0 follows them again. */
static void light_scenario(const char *dir, const char *name) {
    char path[1024], status[512];
    size_t size;
    uint8_t *data;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    float t, first_change = -1, longest = 0, blend_start = -1;
    uint32_t start, changes = 0, flashes = 0, prev, wrong_next = 0, still;
    int blends = 0;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); printf("  (no %s: light skipped)\n", name); return; }
    free(data);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.level_name = name;
    desc.seed = 7;
    if (tc_viewer_init(&v, &desc) != TC_OK) { tc_level_free(&level); return; }
    if (!v.light) {
        printf("  (%s: no light persos in the .ttlv: re-export; light skipped)\n", name);
        tc_viewer_free(&v);
        tc_level_free(&level);
        return;
    }
    printf("  %s: %u light set(s), %u light perso(s)\n", name, v.light_set_count, level.light_perso_count);
    frame(&v, 0.02f);
    start = prev = v.light_set;
    for (t = 0.02f; t < 400; t += 0.02f) { /* 50 frames a second: Land's 50 ms flashes last two or three */
        frame(&v, 0.02f);
        if (v.light_rate > 0 && blend_start < 0) { blend_start = t; blends++; }
        if (v.light_rate > 0 && v.light_next != (v.light_set + 1) % v.light_set_count) wrong_next++;
        if (v.light_rate <= 0 && blend_start >= 0) { longest = fmaxf(longest, t - blend_start); blend_start = -1; }
        if (v.light_set != prev) {
            changes++;
            if (first_change < 0) first_change = t;
            if (v.light_set != 0) flashes++;
            prev = v.light_set;
        }
    }
    printf("  %s: start set %u, first change at %.1f s, %u changes, %d blends (longest %.1f s), %u to sets 1-2\n", name,
           start + 1, first_change, changes, blends, longest, flashes);
    check(wrong_next == 0, "light: a change blends set by set (next = shown + 1)");
    if (strcmp(name, "Land") == 0) {
        check(start == 0 && first_change >= 2 && first_change <= 5.1f, "Land: set 0, first lightning after 2-5 s");
        check(changes >= 40 && flashes >= changes / 2 && longest > 0.9f && longest < 1.1f,
              "Land: lightning keeps flashing sets 1 and 2, fading back to 0 over 1 s");
    } else {
        check(start < 3 && first_change >= 90 && first_change <= 240, "sud0: a start set, the first change after 90-180 s + its blend");
        check(changes >= 1 && changes <= 4 && longest >= 10 && longest <= 60.1f, "sud0: changes blend over 10-60 s");
    }
    tc_viewer_set_light_set(&v, 1);
    still = v.light_set;
    for (t = 0; t < 200; t += 0.1f) frame(&v, 0.1f);
    check(v.light_set == still && v.light_rate == 0 && !v.light_scripted, "light: a number key holds its set");
    tc_viewer_status(&v, NULL, status, sizeof(status));
    printf("  status: %s\n", status);
    check(strstr(status, "light 2/3 held") != NULL, "status: the held set");
    tc_viewer_follow_light(&v);
    for (t = 0, changes = 0; t < 250; t += 0.02f) {
        frame(&v, 0.02f);
        if (v.light_set != still || v.light_rate > 0) changes++;
    }
    check(v.light_scripted && changes > 0, "0: the light persos set the light again");
    tc_viewer_free(&v);
    tc_level_free(&level);
}

/* Portals through the viewer (docs/PORTALS.md): Ed walks into sud0's Cocktail arch; the overlay must go cover ->
 * black -> loading screen, then the level change is pending: the arch leads to the hint scene bar (cock01 from an export
 * without SPCH: tc_level_after_cutscene). bar's viewer, started with the carry, reveals and Ed steps out (state 229). */
/* The sector modes (K) on sud0, playing: everything (the statics' triangles all drawn), the game's (the camera's
 * sector and its list: fewer), Ed's alone (fewer still); the title names the sector. Then Ed runs a while (sud0's start
 * sector is the long river stretch behind the gate: he may stay in it; aicheck's wander counts his crossings), the
 * camera flies through a portal of its sector in free-fly (and enters the sector beyond), and back to everything. */
static uint32_t static_triangles(const tc_viewer *v) {
    uint32_t count, opaque, i, n = 0;
    const tc_view_draw *d = tc_viewer_draws((tc_viewer *)v, &count, &opaque);
    for (i = 0; i < count; i++)
        if (d[i].mesh < v->level->mesh_count || (d[i].mesh >= v->piece_first && d[i].mesh < v->piece_first + v->piece_count))
            n += d[i].index_count / 3;
    return n;
}

static void sector_scenario(const char *dir) {
    char path[1024], status[512], what[256];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    uint32_t all, near, alone, i, total = 0, changes = 0;
    uint16_t last;
    snprintf(path, sizeof(path), "%s/sud0.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); printf("  (no sud0: sectors skipped)\n"); return; }
    free(data);
    snprintf(path, sizeof(path), "%s/sud0.ttai", dir);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = "sud0";
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "sud0 viewer"); free(ai); tc_level_free(&level); return; }
    free(ai);
    printf("sectors (sud0): %u sectors\n", level.sectors.count);
    check(v.sectors && v.mesh_ranges, "sud0 has sectors and its statics have ranges");
    check(v.sector_mode == TC_VS_SECTOR, "starts with Ed's sector and its list (the default)");
    for (i = 0; i < level.mesh_count; i++) total += level.meshes[i].index_count / 3;
    run(&v, 1, 0);
    near = static_triangles(&v);
    tc_viewer_status(&v, NULL, status, sizeof(status));
    check(tc_viewer_ed_sector(&v) != TC_SECTOR_NONE, "Ed has a sector");
    snprintf(what, sizeof(what), "Ed's sector and its list: %u of %u triangles; title: %s", near, total, status);
    check(near > 0 && near < total && strstr(status, ", sector ") != NULL && !strstr(status, "alone"), what);
    check(tc_viewer_status_live(&v), "the title refreshes by itself while sectors are drawn by Ed's");
    tc_viewer_cycle_sectors(&v);
    run(&v, 0.1f, 0);
    all = static_triangles(&v);
    tc_viewer_status(&v, NULL, status, sizeof(status));
    snprintf(what, sizeof(what), "K: all sectors: every static triangle drawn (%u of %u)", all, total);
    check(v.sector_mode == TC_VS_ALL && all == total && !strstr(status, "sector"), what);
    tc_viewer_cycle_sectors(&v);
    run(&v, 0.1f, 0);
    alone = static_triangles(&v);
    tc_viewer_status(&v, NULL, status, sizeof(status));
    snprintf(what, sizeof(what), "K: Ed's sector alone: %u triangles, fewer; title: %s", alone, status);
    check(v.sector_mode == TC_VS_ALONE && alone > 0 && alone < near && strstr(status, " alone") != NULL, what);
    tc_viewer_cycle_sectors(&v);
    check(v.sector_mode == TC_VS_SECTOR, "K again: Ed's sector and its list");
    last = tc_viewer_ed_sector(&v);
    for (i = 0; i < 60 * 6; i++) {
        tc_viewer_set_action(&v, TC_VA_FORWARD, true);
        tc_viewer_set_action(&v, TC_VA_LOOK_RIGHT, i % 120 < 30);
        frame(&v, 1.0f / 60);
        if (tc_viewer_ed_sector(&v) != last) {
            changes++;
            printf("  %.2f s: Ed into %s\n", (double)(i / 60.0f), tc_sectors_name(v.sectors, tc_viewer_ed_sector(&v)));
            last = tc_viewer_ed_sector(&v);
        }
    }
    tc_viewer_release_all(&v);
    snprintf(what, sizeof(what), "Ed runs 6 s (%u sector changes), the draw lists stay valid", changes);
    check(bad_frames == 0, what);
    tc_viewer_free(&v);
    tc_level_free(&level);
}

/* The Special Edition's perso parts stay their size: a misread compressed scale (type 0xF, a symmetric matrix: level.py
 * se_matrix) drew them up to 256 times too big, shards across the screen. Every sector, 20 s, the largest scale of a perso
 * part's model matrix; the data's largest is about 5 (the outro's stretches). */
static void se_scale_level(const char *dir, const char *name) {
    char path[512], what[160];
    uint8_t *data, *ai;
    size_t size, ai_size = 0;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    float worst = 0;
    int f;
    snprintf(path, sizeof(path), "%s/../se/ttlv/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size))) {
        printf("  (no Special Edition %s: skipped)\n", name);
        return;
    }
    if (tc_level_load(&level, data, size) != TC_OK) { check(0, "SE level loads"); free(data); return; }
    free(data);
    snprintf(path, sizeof(path), "%s/../se/ttlv/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = name;
    desc.seed = 5;
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "SE viewer"); free(ai); tc_level_free(&level); return; }
    tc_viewer_cycle_sectors(&v); /* every sector: every perso */
    for (f = 0; f < 1200; f++) {
        uint32_t count, opaque, i;
        const tc_view_draw *d;
        tc_viewer_update(&v, 1.0f / 60);
        d = tc_viewer_draws(&v, &count, &opaque);
        for (i = 0; i < count; i++) {
            const float *m = d[i].model.m;
            int c;
            if (d[i].mesh < v.perso_first || d[i].mesh >= v.ed_first) continue;
            for (c = 0; c < 3; c++) worst = fmaxf(worst, sqrtf(m[4 * c] * m[4 * c] + m[4 * c + 1] * m[4 * c + 1] + m[4 * c + 2] * m[4 * c + 2]));
        }
    }
    snprintf(what, sizeof(what), "SE %s: perso parts at most %.2f x their size", name, (double)worst);
    check(worst < 16, what);
    tc_viewer_free(&v);
    free(ai);
    tc_level_free(&level);
}

static void se_scale_scenario(const char *dir) {
    static const char *const levels[] = {"carota", "cavedoc", "Land", "outro", "pyramide", "sud0"};
    size_t i;
    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) se_scale_level(dir, levels[i]);
}

static void portal_scenario(const char *dir) {
    char path[1024];
    size_t size, ai_size;
    uint8_t *data, *ai;
    tc_level sud0, bar;
    tc_ai_model model;
    tc_viewer v, w;
    tc_viewer_desc desc;
    tc_viewer_carry carry;
    tc_view_overlay o;
    const tc_viewer_carry *pending = NULL;
    const tc_ai_portal *g = NULL;
    int i, k, seen[5] = {0, 0, 0, 0, 0}, saw_229 = 0, ed_under_cover = 0;
    float zoom;
    tc_vec3 front;
    snprintf(path, sizeof(path), "%s/sud0.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&sud0, data, size) != TC_OK) { printf("  (no sud0: portals skipped)\n"); return; }
    free(data);
    snprintf(path, sizeof(path), "%s/sud0.ttai", dir);
    if (!(ai = read_file(path, &ai_size)) || tc_ai_model_load(&model, ai, ai_size) != TC_OK) return;
    for (i = 0; i < model.portal_count && !g; i++)
        for (k = 0; k < model.portals[i].fire_count; k++) {
            const tc_ai_portal_op *op = &model.portals[i].ops[model.portals[i].capture_count + k];
            if (op->op == TC_AI_POP_LEVEL && strcmp(model.names[op->b], "bar") == 0) g = &model.portals[i];
        }
    desc.level = &sud0;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = "sud0";
    desc.carry = NULL;
    desc.seed = 1;
    if (!g || tc_viewer_init(&v, &desc) != TC_OK) { check(0, "sud0: the Cocktail arch and a viewer"); return; }
    {
        uint32_t n = 0, m;
        char what[96];
        for (m = 0; v.mesh_water && m < sud0.mesh_count; m++) n += v.mesh_water[m];
        snprintf(what, sizeof(what), "sud0: %u water materials found (the sheen, TC_FLAG_WATER)", n);
        check(n > 0, what);
    }
    front = tc_v3_normalize(tc_v3(-g->rows[1], -g->rows[4], 0));
    tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3_add(g->position, tc_v3_scale(front, 8)),
                       atan2f(-front.x, front.y));
    v.camera.yaw = TC_PI - tc_viewer_player(&v)->heading;
    tc_viewer_step_fov(&v, 1); /* 91 degrees and a zoomed-out camera: kept across the level change (the carry) */
    tc_viewer_wheel(&v, -2);
    tc_viewer_toggle_closeup(&v); /* and the close-up (5) */
    zoom = v.orbit_distance;
    for (i = 0; i < 60 * 25 && !pending; i++) {
        tc_viewer_set_action(&v, TC_VA_FORWARD, i < 60);
        frame(&v, 1.0f / 60);
        tc_viewer_overlay(&v, &o);
        seen[o.kind] = 1;
        if (o.kind != TC_VO_NONE && last_ed_draws) ed_under_cover++;
        if (o.kind == TC_VO_MASK || o.kind == TC_VO_LOGO) check_overlay_texture(&v, &o);
        pending = tc_viewer_pending_level(&v);
    }
    printf("  sud0 -> %s after %.1f s; overlays: mask %d, logo %d, black %d, loading %d\n", pending ? pending->level : "-",
           (double)i / 60, seen[TC_VO_MASK], seen[TC_VO_LOGO], seen[TC_VO_BLACK], seen[TC_VO_LOADING]);
    check(pending && strcmp(pending->level, sud0.speech.dialogue_count ? "bar" : "cock01") == 0,
          "sud0 Cocktail arch: Ed goes on into the bar's scene");
    check((seen[TC_VO_MASK] || seen[TC_VO_LOGO]) && seen[TC_VO_BLACK] && seen[TC_VO_LOADING], "overlays: cover, black, loading");
    /* The step-in (state 228) ends with every part hidden and holds that frame: no Ed left at the arch (BUGS.md). */
    check(ed_under_cover == 0, "sud0: Ed stays gone after stepping in (none of him drawn under the cover)");
    if (pending) carry = *pending;
    tc_viewer_free(&v);
    tc_ai_model_free(&model);
    free(ai);
    tc_level_free(&sud0);
    if (!pending) return;

    snprintf(path, sizeof(path), "%s/bar.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&bar, data, size) != TC_OK) return;
    free(data);
    snprintf(path, sizeof(path), "%s/bar.ttai", dir);
    ai = read_file(path, &ai_size);
    desc.level = &bar;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = "bar";
    desc.carry = &carry;
    if (tc_viewer_init(&w, &desc) != TC_OK) { check(0, "bar with the carry"); return; }
    free(ai);
    check(fabsf(tc_camera_hfov_43(w.camera.vertical_fov) - 91 * TC_PI / 180) < 1e-4f,
          "bar keeps sud0's field of view (91 degrees horizontal at 4:3, one step up from the original's 86)");
    {
        char what[96];
        snprintf(what, sizeof(what), "bar keeps sud0's camera zoom (%.2f, the default %.0f)", (double)zoom, (double)TC_CAM_DISTANCE);
        check(zoom != TC_CAM_DISTANCE && fabsf(w.orbit_distance - zoom) < 1e-4f, what);
    }
    check(w.cam.closeup && w.playing, "bar keeps sud0's close-up (5)");
    tc_viewer_overlay(&w, &o);
    check(o.kind == TC_VO_MASK || o.kind == TC_VO_LOGO, "bar starts with the reveal");
    for (i = 0; i < 60 * 4; i++) {
        frame(&w, 1.0f / 60);
        if (tc_viewer_player(&w) && w.phase == TC_VP_PLAY) break;
    }
    for (i = 0; i < 60 * 2; i++) {
        frame(&w, 1.0f / 60);
        if (strcmp(w.level->ed.anims[w.ed_clip < 0 ? 0 : w.ed_clip].name, "s229") == 0) saw_229 = 1;
    }
    check(saw_229, "bar: Ed steps out of the portal (state 229)");
    tc_viewer_free(&w);
    tc_level_free(&bar);
}

/* The Special Edition's moving textures: its VisualMaterial tail sits 12 bytes lower than retail's, misread they all
 * stood still (docs/SE.md "Moving textures"). LeCanyon's floor scrolls at engine (0, -0.006) a tick: with the exe's
 * uv + offset and v' = 1 - v, +0.36 UV/s here. Its Kyou and arches are flipbooks. */
static void se_moving_textures_scenario(const char *dir) {
    char path[512], what[160];
    uint8_t *data;
    size_t size;
    tc_level level;
    uint32_t i, floors = 0, books = 0;
    snprintf(path, sizeof(path), "%s/../se/ttlv/LeCanyon.ttlv", dir);
    if (!(data = read_file(path, &size))) {
        printf("  (no Special Edition LeCanyon: its moving textures skipped)\n");
        return;
    }
    if (tc_level_load(&level, data, size) != TC_OK) { check(0, "SE LeCanyon loads"); free(data); return; }
    free(data);
    for (i = 0; i < level.mesh_count; i++) {
        const tc_surface *s = &level.meshes[i].surface;
        if (s->scroll[0] == 0 && s->scroll[1] > 0.35f && s->scroll[1] < 0.37f) floors++;
        if (s->frame_count) books++;
    }
    for (i = 0; i < level.perso_count; i++) {
        uint32_t p;
        for (p = 0; p < level.persos[i].part_count; p++) books += level.persos[i].parts[p].mesh.surface.frame_count != 0;
    }
    snprintf(what, sizeof(what), "SE LeCanyon: %u floor meshes scroll at +0.36 UV/s, %u flipbook surfaces", floors, books);
    check(floors >= 3 && books >= 3, what);
    tc_level_free(&level);
}

/* The Special Edition's Totalski switches Ed between walking and skiing (tc_ai_mode_zone, docs/SE.md "Sliding in the
 * Special Edition"), with its pickups and World running as a cast (docs/SE.md "Pickups"). Regression (user,
 * 2026-11-05: "he no longer gets his plate out on totalski"): the switch was skipped on levels with a cast. The start
 * zone hands him to the skier on the first tick; then each zone's swap, walking and skiing, with the cast linked to the
 * new driver. Its own folder: ../files/out/se/ttlv (skipped when it is not there). */
static void se_ski_scenario(const char *dir) {
    char path[1024], what[256];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    tc_ai_model m;
    float at[8][3];
    uint32_t k, nz = 0, i, swaps = 0, linked = 1;
    snprintf(path, sizeof(path), "%s/../se/ttlv/Totalski.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) {
        free(data);
        printf("  (no Special Edition Totalski: its walk / ski switch skipped)\n");
        return;
    }
    free(data);
    snprintf(path, sizeof(path), "%s/../se/ttlv/Totalski.ttai", dir);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = "totalski";
    if (!ai || tc_viewer_init(&v, &desc) != TC_OK) { check(0, "SE Totalski viewer"); free(ai); tc_level_free(&level); return; }
    if (tc_ai_model_load(&m, ai, ai_size) == TC_OK) { /* where each zone is (its first primitive's centre) */
        for (k = 0; k < m.mode_zone_count && nz < 8; k++, nz++) {
            const tc_ai_mode_zone *z = &m.mode_zones[k];
            const float *q = z->inverse;
            tc_vec3 c = z->prim[0].kind == 8 ? tc_v3_scale(tc_v3_add(z->prim[0].a, z->prim[0].b), 0.5f) : z->prim[0].a;
            tc_vec3 l = tc_v3(c.x - q[3], c.y - q[7], c.z - q[11]);
            at[nz][0] = q[0] * l.x + q[4] * l.y + q[8] * l.z;
            at[nz][1] = q[1] * l.x + q[5] * l.y + q[9] * l.z;
            at[nz][2] = q[2] * l.x + q[6] * l.y + q[10] * l.z;
        }
        tc_ai_model_free(&m);
    }
    free(ai);
    printf("Special Edition Totalski: %u mode zones, cast of %u\n", nz, tc_viewer_cast(&v) ? tc_viewer_cast(&v)->count : 0);
    frame(&v, 1.0f / 60);
    frame(&v, 1.0f / 60);
    check(tc_viewer_play_mode(&v) == TC_AI_MODE_SKI, "SE Totalski: the start zone hands Ed to the skier at once");
    run(&v, 3, 0);
    snprintf(what, sizeof(what), "... and he rides down (%.1f u/s after 3 s)", (double)tc_v3_length(tc_viewer_player(&v)->velocity));
    check(tc_v3_length(tc_viewer_player(&v)->velocity) > 10, what);
    for (i = 0; i < nz; i++) { /* every zone that fires from the mode he is in, in turn: walk <-> ski */
        uint32_t want = tc_viewer_play_mode(&v) == TC_AI_MODE_SKI ? TC_AI_MODE_WALK : TC_AI_MODE_SKI, j;
        const struct tc_cast *c;
        const struct tc_ed_ai *e;
        for (k = 0; k < nz; k++) {
            tc_player *p = (tc_player *)tc_viewer_player(&v);
            p->position = tc_v3(at[k][0], at[k][1], at[k][2]);
            p->velocity = tc_v3(0, 0, 0);
            p->placed++;
            for (j = 0; j < 30; j++) frame(&v, 1.0f / 60);
            if (tc_viewer_play_mode(&v) == want) break;
        }
        if (k == nz) break;
        swaps++;
        c = tc_viewer_cast(&v);
        e = tc_viewer_driver(&v);
        if (c && (!e || c->main != &e->ai || e->ai.find_user != c)) linked = 0;
    }
    snprintf(what, sizeof(what), "SE Totalski: %u swaps between walking and skiing through its zones", swaps);
    check(swaps >= 4, what);
    check(linked, "... the cast follows each new driver (tc_cast_set_main)");
    tc_viewer_free(&v);
    tc_level_free(&level);
}

/* A draw that the frame's own view-projection puts wholly outside one side of the clip volume: its mesh's vertex box
 * (computed here, not the core's spheres) at its model matrix, all eight corners beyond the same plane. */
static int box_outside(const tc_mat4 *vp, const tc_view_draw *d, const float lo[3], const float hi[3]) {
    int out[6] = {1, 1, 1, 1, 1, 1}, k, j;
    for (k = 0; k < 8; k++) {
        float p[3] = {k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2]}, w[3], c[4];
        const float *m = d->model.m, *q = vp->m;
        for (j = 0; j < 3; j++) w[j] = m[j] * p[0] + m[4 + j] * p[1] + m[8 + j] * p[2] + m[12 + j];
        for (j = 0; j < 4; j++) c[j] = q[j] * w[0] + q[4 + j] * w[1] + q[8 + j] * w[2] + q[12 + j];
        out[0] &= c[0] > c[3]; out[1] &= c[0] < -c[3];
        out[2] &= c[1] > c[3]; out[3] &= c[1] < -c[3];
        out[4] &= c[2] < 0;    out[5] &= c[2] > c[3];   /* before the near plane, past the far one (0..1 depth) */
    }
    for (k = 0; k < 6; k++) if (out[k]) return 1;
    return 0;
}

/* Every triangle of the draw's index range [first, first + count) has its three corners, by the frame's matrix, beyond
 * one same plane of the clip volume (each triangle its own plane): then nothing of it can show. The exact test, for when
 * the mesh's box (box_outside) reaches into the view. */
static int vertices_outside(const tc_viewer *v, const tc_mat4 *vp, const tc_view_draw *d, uint32_t first, uint32_t count) {
    const tc_mesh *m = tc_viewer_mesh(v, d->mesh);
    uint32_t t;
    if (!m) return 0;
    for (t = first; t + 2 < first + count && t + 2 < m->index_count; t += 3) {
        int out[6] = {1, 1, 1, 1, 1, 1}, j, c, any = 0;
        for (c = 0; c < 3; c++) {
            const float *p = m->vertices[m->indices[t + c]].position, *mm = d->model.m, *q = vp->m;
            float w[3], x[4];
            for (j = 0; j < 3; j++) w[j] = mm[j] * p[0] + mm[4 + j] * p[1] + mm[8 + j] * p[2] + mm[12 + j];
            for (j = 0; j < 4; j++) x[j] = q[j] * w[0] + q[4 + j] * w[1] + q[8 + j] * w[2] + q[12 + j];
            out[0] &= x[0] > x[3]; out[1] &= x[0] < -x[3];
            out[2] &= x[1] > x[3]; out[3] &= x[1] < -x[3];
            out[4] &= x[2] < 0;    out[5] &= x[2] > x[3];
        }
        for (j = 0; j < 6; j++) any |= out[j];
        if (!any) return 0;
    }
    return 1;
}

/* The vertex box of mesh i (lo xyz, hi xyz), made when first asked for. */
static const float *mesh_box(const tc_viewer *v, uint32_t i, float *boxes, uint8_t *have) {
    float *b = boxes + 6 * i;
    const tc_mesh *m = tc_viewer_mesh(v, i);
    uint32_t q;
    int c;
    if (have[i]) return b;
    b[0] = b[1] = b[2] = INFINITY;
    b[3] = b[4] = b[5] = -INFINITY;
    for (q = 0; m && q < m->vertex_count; q++)
        for (c = 0; c < 3; c++) b[c] = fminf(b[c], m->vertices[q].position[c]), b[3 + c] = fmaxf(b[3 + c], m->vertices[q].position[c]);
    have[i] = 1;
    return b;
}

/* Culling on a level without sectors (tc_viewer_set_aspect): flying round LibertyCity2001 at three window shapes, every
 * frame's culled list is the whole list (culling off) in order with draws left out, the opaque ones still first, and
 * everything left out (whole draws, a celled static's cells) is outside the view by the frame's own matrix (box_outside,
 * else vertices_outside, triangle by triangle). */
static void cull_scenario(const char *dir) {
    static const float aspects[3] = {16.0f / 9, 32.0f / 9, 9.0f / 16};
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    float *box;
    uint8_t *have;
    uint32_t frames = 0, bad = 0, wrong_order = 0, dropped = 0, dropped_before = 0, partial = 0, whole = 0, tris_whole = 0,
             tris_kept = 0;
    int a, i, sectors_untouched;
    snprintf(path, sizeof(path), "%s/LibertyCity2001.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); printf("  (no LibertyCity2001: culling skipped)\n"); return; }
    free(data);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.level_name = "LibertyCity2001";
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "LibertyCity2001 viewer"); tc_level_free(&level); return; }
    box = (float *)calloc(6 * (size_t)tc_viewer_mesh_count(&v), sizeof(float));
    have = (uint8_t *)calloc(tc_viewer_mesh_count(&v), 1);
    if (v.playing) tc_viewer_toggle_play(&v);
    for (i = 0; i < 12; i++) tc_viewer_wheel(&v, 1);
    for (a = 0; a < 3; a++) {
        tc_viewer_set_action(&v, TC_VA_FORWARD, true);
        for (i = 0; i < 240; i++) {
            uint32_t count, opaque, full_count, full_opaque, k, j;
            const tc_view_draw *d;
            tc_view_draw *full;
            tc_view_frame fr;
            tc_viewer_mouse_look(&v, 9, (i / 40) % 2 ? 2.5f : -2.5f); /* turning, looking up and down */
            tc_viewer_update(&v, 1.0f / 30);
            tc_viewer_set_aspect(&v, aspects[a]);
            tc_viewer_set_culling(&v, false);
            d = tc_viewer_draws(&v, &full_count, &full_opaque);
            full = (tc_view_draw *)malloc((full_count ? full_count : 1) * sizeof(*full));
            memcpy(full, d, full_count * sizeof(*full));
            tc_viewer_set_culling(&v, true);
            d = tc_viewer_draws(&v, &count, &opaque);
            tc_viewer_frame(&v, aspects[a], &fr);
            frames++;
            whole += full_count;
            for (k = 0; k < full_count; k++) tris_whole += full[k].index_count / 3;
            for (k = 0; k < count; k++) tris_kept += d[k].index_count / 3;
            /* d is full in order, each full draw kept whole, left out, or (a celled static) as some of its index range:
             * pieces in order, inside it, otherwise the same draw. What is left out must be outside the view. */
            for (j = 0, k = 0; j < full_count; j++) {
                const tc_view_draw *w = &full[j];
                uint32_t at = w->first_index, end = w->first_index + w->index_count, pieces = 0;
                tc_view_draw a = *w;
                a.first_index = a.index_count = 0;
                while (k < count) {
                    tc_view_draw b = d[k];
                    b.first_index = b.index_count = 0;
                    if (memcmp(&a, &b, sizeof(a)) != 0 || d[k].first_index < at || d[k].first_index + d[k].index_count > end) break;
                    if ((k < opaque) != (j < full_opaque)) wrong_order++;
                    if (d[k].first_index > at && !vertices_outside(&v, &fr.view_projection, w, at, d[k].first_index - at)) bad++;
                    at = d[k].first_index + d[k].index_count;
                    k++;
                    pieces++;
                }
                if (pieces == 0) {
                    const float *b = mesh_box(&v, w->mesh, box, have);
                    dropped++;
                    if (!box_outside(&fr.view_projection, w, b, b + 3) &&
                        !vertices_outside(&v, &fr.view_projection, w, w->first_index, w->index_count))
                        bad++;
                } else {
                    if (at < end && !vertices_outside(&v, &fr.view_projection, w, at, end - at)) bad++;
                    if (pieces > 1 || at != end || d[k - 1].first_index != w->first_index) partial++;
                }
            }
            if (k != count) wrong_order++;
            if (v.culled_count != dropped - dropped_before) wrong_order++;
            dropped_before = dropped;
            free(full);
        }
        tc_viewer_release_all(&v);
    }
    printf("culling (LibertyCity2001, %u frames at 16:9, 32:9, 9:16): %.0f triangles a frame of %.0f; %.0f draws of %.0f "
           "left out, %.0f kept in part (cells)\n", frames, (double)tris_kept / frames, (double)tris_whole / frames,
           (double)dropped / frames, (double)whole / frames, (double)partial / frames);
    check(dropped > 0 && partial > 0, "culling leaves draws out, and parts of the wide statics (cells)");
    check(wrong_order == 0, "the culled list is the whole one in order (with parts of draws), the opaque draws first");
    {   /* each celled static's copy holds the level's triangles, in another order */
        uint32_t i, same = 1, celled = 0;
        for (i = 0; i < level.mesh_count; i++) {
            const tc_mesh *a = &level.meshes[i], *b = tc_viewer_mesh(&v, i);
            uint64_t sa = 0, sb = 0, xa = 0, xb = 0;
            uint32_t t;
            if (a == b) continue;
            celled++;
            if (a->index_count != b->index_count || a->vertices != b->vertices) { same = 0; continue; }
            for (t = 0; t + 2 < a->index_count; t += 3) { /* order-free sums of each triangle (its corners in order) */
                uint64_t ha = ((uint64_t)a->indices[t] * 2654435761u) ^ ((uint64_t)a->indices[t + 1] * 40503u << 20) ^ ((uint64_t)a->indices[t + 2] << 40);
                uint64_t hb = ((uint64_t)b->indices[t] * 2654435761u) ^ ((uint64_t)b->indices[t + 1] * 40503u << 20) ^ ((uint64_t)b->indices[t + 2] << 40);
                sa += ha, sb += hb, xa ^= ha * 0x9E3779B97F4A7C15ull, xb ^= hb * 0x9E3779B97F4A7C15ull;
            }
            if (sa != sb || xa != xb) same = 0;
        }
        printf("  %u statics cut into %u cells\n", celled, v.cell_count);
        check(celled > 0 && same, "each celled static's copy has the level's triangles, reordered");
    }
    check(bad == 0, "everything left out is outside the view (each triangle by the frame's matrix)");
    tc_viewer_free(&v);
    tc_level_free(&level);
    free(box);
    free(have);
    /* a level with sectors: the list is whole whatever the aspect */
    snprintf(path, sizeof(path), "%s/sud0.ttlv", dir);
    sectors_untouched = 1;
    if ((data = read_file(path, &size)) && tc_level_load(&level, data, size) == TC_OK) {
        uint32_t c1, o1, c2, o2;
        free(data);
        data = NULL;
        memset(&desc, 0, sizeof(desc));
        desc.level = &level;
        desc.level_name = "sud0";
        if (tc_viewer_init(&v, &desc) == TC_OK) {
            run(&v, 0.2f, 0);
            tc_viewer_draws(&v, &c1, &o1);
            tc_viewer_set_aspect(&v, 16.0f / 9);
            tc_viewer_draws(&v, &c2, &o2);
            sectors_untouched = c1 == c2 && o1 == o2 && v.culled_count == 0;
            tc_viewer_free(&v);
        }
        tc_level_free(&level);
    }
    free(data);
    check(sectors_untouched, "a level with sectors (sud0) is never culled");
}

/* FNV-1a over raw bytes: the ride's trace, to compare builds by. */
static uint64_t fnv(uint64_t h, const void *data, size_t n) {
    const uint8_t *b = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

/* The toy car (custom gameplay, tcx.h; docs/ENGINE.md "Vehicles") in LibertyCity2001: Ed beside the first car presses
 * Action, hops in (his jump clip, shrinking into the seat), drives (throttle, a turn), presses Action again and hops out
 * onto his feet. Prints a hash of every frame's drawing of Ed (his parts' matrices, clip, frame), the car's pose and the
 * camera: two builds that print the same hash ride the same, frame for frame. */
static void car_ride_scenario(const char *dir) {
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    const tcx_vehicle *car;
    tc_vec3 start, spot = {0, 0, 0}, car0, ed_end;
    uint64_t h = 0xcbf29ce484222325ull, part[5] = {h, h, h, h, h}; /* on foot before, hop in, driving, hop out, after */
    uint32_t frames = 0, seen[4] = {0, 0, 0, 0}, jump_frames = 0, hidden = 0, stage = 0;
    int i, k, jump;
    bool placed = false;
    snprintf(path, sizeof(path), "%s/LibertyCity2001.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); printf("  (no LibertyCity2001: car ride skipped)\n"); return; }
    free(data);
    snprintf(path, sizeof(path), "%s/LibertyCity2001.ttai", dir);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = "LibertyCity2001";
    desc.seed = 9;
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "LibertyCity2001 viewer"); free(ai); tc_level_free(&level); return; }
    free(ai);
    car = tcx_play_vehicle(tc_viewer_custom(&v), 0);
    if (!car || !tc_viewer_can_play(&v)) {
        printf("  (LibertyCity2001 has no car in play: car ride skipped)\n");
        tc_viewer_free(&v);
        tc_level_free(&level);
        return;
    }
    /* beside it: the first spot round it within reach */
    for (k = 0; k < 16 && !placed; k++) {
        float a = (float)k * TC_PI / 8;
        spot = tc_v3_add(car->position, tc_v3(cosf(a) * 2.2f, sinf(a) * 2.2f, 0.4f));
        placed = tcx_ride_in_reach(car, spot);
    }
    check(placed, "car ride: a spot beside the car within reach");
    tc_player_teleport((tc_player *)tc_viewer_player(&v), spot, 0);
    car0 = tcx_vehicle_drawn_position(car);
    start = tc_viewer_player(&v)->position;
    jump = tc_ed_find_anim(&level.ed, "jump");
    for (i = 0; i < 60 * 12; i++) {
        float t = (float)i / 60;
        uint32_t count, opaque, d;
        const tc_view_draw *dr;
        const tcx_ride *r;
        tc_vec3 cp;
        tc_viewer_release_all(&v);
        if ((t >= 1.0f && t < 1.05f) || (t >= 8.0f && t < 8.05f)) tc_viewer_set_action(&v, TC_VA_ACTION, true); /* in; out */
        if (t >= 3.0f && t < 6.5f) tc_viewer_set_action(&v, TC_VA_FORWARD, true);
        if (t >= 4.5f && t < 5.5f) tc_viewer_set_action(&v, TC_VA_RIGHT, true);
        tc_viewer_update(&v, 1.0f / 60);
        dr = tc_viewer_draws(&v, &count, &opaque);
        r = tcx_play_ride(tc_viewer_custom(&v));
        if (r && (unsigned)r->phase < 4) seen[r->phase]++;
        if (r && r->phase != TCX_RIDE_NONE) stage = (uint32_t)r->phase;
        else if (stage) stage = 4;
        if (r && r->phase == TCX_RIDE_BOARDING && v.ed_clip == jump) jump_frames++;
        if (v.ed_draw_count == 0) hidden++;
        for (d = 0; d < count; d++)
            if (dr[d].mesh >= v.ed_first && dr[d].mesh < v.ed_first + level.ed.part_count) h = fnv(h, dr[d].model.m, sizeof(dr[d].model.m));
        h = fnv(h, &v.ed_clip, sizeof(v.ed_clip));
        h = fnv(h, &v.ed_phase, sizeof(v.ed_phase));
        cp = tcx_vehicle_drawn_position(car);
        h = fnv(h, &cp, sizeof(cp));
        h = fnv(h, &v.camera.position, sizeof(v.camera.position));
        { /* the same, by stage */
            uint64_t g = part[stage];
            for (d = 0; d < count; d++)
                if (dr[d].mesh >= v.ed_first && dr[d].mesh < v.ed_first + level.ed.part_count) g = fnv(g, dr[d].model.m, sizeof(dr[d].model.m));
            g = fnv(g, &v.ed_clip, sizeof(v.ed_clip));
            g = fnv(g, &v.ed_phase, sizeof(v.ed_phase));
            g = fnv(g, &cp, sizeof(cp));
            g = fnv(g, &v.camera.position, sizeof(v.camera.position));
            part[stage] = g;
        }
        frames++;
    }
    ed_end = tc_viewer_player(&v)->position;
    printf("car ride (LibertyCity2001): %u frames: boarding %u, driving %u, leaving %u, on foot %u; jump clip on %u hop "
           "frames; Ed undrawn %u; the car went %.1f, Ed %.1f from his start; trace %016llx\n", frames, seen[1], seen[2],
           seen[3], seen[0], jump_frames, (unsigned)hidden, (double)tc_v3_length(tc_v3_sub(tcx_vehicle_drawn_position(car), car0)),
           (double)tc_v3_length(tc_v3_sub(ed_end, start)), (unsigned long long)h);
    printf("  by stage: on foot %016llx, hop in %016llx, driving %016llx, hop out %016llx, on foot after %016llx\n",
           (unsigned long long)part[0], (unsigned long long)part[1], (unsigned long long)part[2], (unsigned long long)part[3],
           (unsigned long long)part[4]);
    check(seen[1] > 0 && seen[2] > 0 && seen[3] > 0, "car ride: Ed hops in, drives, hops out");
    check(jump_frames > 0, "car ride: his hop in plays his jump clip");
    check(tcx_play_ride(tc_viewer_custom(&v))->phase == TCX_RIDE_NONE && v.ed_draw_count > 0, "car ride: back on his feet, drawn");
    check(tc_v3_length(tc_v3_sub(tcx_vehicle_drawn_position(car), car0)) > 5, "car ride: the car was driven");
    tc_viewer_free(&v);
    tc_level_free(&level);
}

/* Riding (docs/ENGINE.md "Moving collision, read"): cavedoc's lift platform SDO_PFCaveDoc1 (perso 166, card type 6).
 * Ed dropped onto it lands on the perso (its own collision, .ttlv PCOL), rides it (tc_mec_walk.carrier), and its script
 * sees him through FUNC 54 (StandingOnPerso == Me): MoveLift (META 28) takes it to its other stop, Ed on it. Needs an
 * export with PCOL (skipped otherwise). */
static void ride_scenario(const char *dir) {
    char path[1024], what[256];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    const struct tc_obstacles *os;
    struct tc_obstacle *ob;
    tc_player *p;
    tc_vec3 start, lift0;
    uint32_t i, rode = 0, seen = 0;
    snprintf(path, sizeof(path), "%s/cavedoc.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); printf("  (no cavedoc: riding skipped)\n"); return; }
    free(data);
    if (!level.pcol.count) { printf("  (cavedoc has no PCOL, an older export: riding skipped)\n"); tc_level_free(&level); return; }
    snprintf(path, sizeof(path), "%s/cavedoc.ttai", dir);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = "cavedoc";
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "cavedoc viewer"); free(ai); tc_level_free(&level); return; }
    free(ai);
    run(&v, 0.5f, 0);
    p = (tc_player *)tc_viewer_player(&v);
    os = p->world->obstacles;
    ob = os ? tc_obstacles_find(os, 166) : NULL;
    printf("riding (cavedoc): %u perso obstacles\n", os ? os->count : 0);
    check(ob && ob->live && ob->part_count, "the lift platform (166) is a live obstacle");
    if (!ob) { tc_viewer_free(&v); tc_level_free(&level); return; }
    lift0 = tc_v3(ob->world.m[3], ob->world.m[7], ob->world.m[11]);
    p->position = tc_v3((ob->lo.x + ob->hi.x) / 2, (ob->lo.y + ob->hi.y) / 2, ob->hi.z + 1.5f);
    p->velocity = tc_v3(0, 0, 0);
    p->placed++;
    start = p->position;
    for (i = 0; i < 60 * 8; i++) {
        const struct tc_ed_ai *e = tc_viewer_driver(&v);
        frame(&v, 1.0f / 60);
        e = tc_viewer_driver(&v);
        rode += e->dyn.walk.carrier == 166;
        seen += e->ai.standing_on == 166;
    }
    snprintf(what, sizeof(what), "Ed lands on it and rides it (%u of %u frames), FUNC 54 names it (%u)", rode, 60 * 8, seen);
    check(rode > 60 * 6 && seen > 60 * 6, what);
    {
        tc_vec3 lift = tc_v3(ob->world.m[3], ob->world.m[7], ob->world.m[11]);
        float moved = tc_v3_distance(lift, lift0), carried = tc_v3_distance(p->position, start);
        snprintf(what, sizeof(what), "its script moves it (MoveLift): %.1f from its start, Ed carried %.1f, %.2f above it", moved,
                 carried, p->position.z - lift.z);
        check(moved > 20 && fabsf(carried - moved) < 3 && p->position.z > lift.z, what);
    }
    tc_viewer_free(&v);
    tc_level_free(&level);
}

/* The cavedoc ruby (BNE_PFGuide, gro_pf01; docs/ENGINE.md "Ways and waypoints"): Ed stands on it, its reflex R0 puts
 * its way's cursor on the nearest waypoint (Func_CloserWP, Proc_InitWayWithWp) and B0 takes it along the way
 * (Action_GoOverWay: mode 0x14 towards each waypoint in turn, type 7 card) until R1 sees the last one within 2. */
static void guide_scenario(const char *dir) {
    char path[1024], what[256];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    const struct tc_obstacles *os;
    struct tc_obstacle *ob;
    const tc_perso_ways *pw;
    const tc_way *way;
    tc_player *p;
    uint32_t i, k, next = 0, order_ok = 1, rode = 0, frames = 60 * 50;
    float last_d = 1e9f;
    const uint16_t guide = 50; /* cavedoc's perso 50 */
    snprintf(path, sizeof(path), "%s/cavedoc.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); printf("  (no cavedoc: the guide skipped)\n"); return; }
    free(data);
    if (!level.ways.waypoint_count || !level.pcol.count) { printf("  (cavedoc has no WAYS / PCOL, an older export: the guide skipped)\n"); tc_level_free(&level); return; }
    snprintf(path, sizeof(path), "%s/cavedoc.ttai", dir);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = "cavedoc";
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "cavedoc viewer"); free(ai); tc_level_free(&level); return; }
    free(ai);
    pw = tc_ways_of(&level.ways, guide);
    way = pw && pw->count ? &level.ways.ways[pw->first] : NULL;
    check(way && way->count == 10, "the ruby has its way (10 waypoints)");
    run(&v, 0.5f, 0);
    p = (tc_player *)tc_viewer_player(&v);
    os = p->world->obstacles;
    ob = os ? tc_obstacles_find(os, guide) : NULL;
    check(ob && ob->live, "the ruby (50) is a live obstacle");
    if (!ob || !way) { tc_viewer_free(&v); tc_level_free(&level); return; }
    p->position = tc_v3((ob->lo.x + ob->hi.x) / 2, (ob->lo.y + ob->hi.y) / 2, ob->hi.z + 1.0f);
    p->velocity = tc_v3(0, 0, 0);
    p->placed++;
    printf("the guide (cavedoc ruby) from (%.1f, %.1f, %.1f):\n", ob->world.m[3], ob->world.m[7], ob->world.m[11]);
    for (i = 0; i < frames; i++) {
        tc_vec3 at;
        frame(&v, 1.0f / 60);
        rode += tc_viewer_driver(&v)->dyn.walk.carrier == guide;
        at = tc_v3(ob->world.m[3], ob->world.m[7], ob->world.m[11]);
        for (k = next; k < way->count; k++) { /* the waypoints it comes within 2.5 of, in order */
            const tc_waypoint *w = &level.ways.waypoints[level.ways.points[way->first + k] - 1];
            if (tc_v3_distance(at, w->position) < 2.5f) {
                if (k != next && next != 0) order_ok = 0;
                printf("    waypoint %u at %.1f s\n", k, (float)i / 60);
                next = k + 1;
                break;
            }
        }
        {
            const tc_waypoint *w = &level.ways.waypoints[level.ways.points[way->first + way->count - 1] - 1];
            last_d = fminf(last_d, tc_v3_distance(at, w->position));
        }
    }
    snprintf(what, sizeof(what), "it follows its way in order (to waypoint %u of %u), nearest to the last %.1f; Ed rode %u of %u frames",
             next, way->count, last_d, rode, frames);
    check(order_ok && next >= way->count - 1 && last_d < 3, what);
    tc_viewer_free(&v);
    tc_level_free(&level);
}

/* Ed shoots (docs/ENGINE.md "Shooting"): on north, Action takes the blowgun out (B69), Shoot fires: META 11 instances the
 * bee (LOL_Actor1, a spawnable template: TC_CAST_SPAWN), which flies on its type 10 card; put 6 in front of the guard
 * KEV_Garde_debut (46), the bee's ZDE zone meets his and his reflex takes it from its hit list (CollectNearby, ListSort-
 * ByFamily): B6. */
static void shoot_scenario(const char *dir) {
    char path[1024], what[256];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    const tc_cast *c;
    const tc_cast_actor *guard = NULL, *bee = NULL;
    tc_player *p;
    uint32_t i, spawned;
    float flew = 0;
    snprintf(path, sizeof(path), "%s/north.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); printf("  (no north: shooting skipped)\n"); return; }
    free(data);
    snprintf(path, sizeof(path), "%s/north.ttai", dir);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = "north";
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "north viewer"); free(ai); tc_level_free(&level); return; }
    free(ai);
    c = tc_viewer_cast(&v);
    for (i = 0; c && i < c->count; i++) {
        if (c->actors[i].entry->id == 46) guard = &c->actors[i];
        if ((c->actors[i].entry->flags & TC_CAST_SPAWN) && c->actors[i].model.zde.zone_count && !bee) bee = &c->actors[i];
    }
    if (!guard || !bee) { printf("  (north has no spawnable bee / guard 46, an older export: shooting skipped)\n"); tc_viewer_free(&v); tc_level_free(&level); return; }
    ((tc_cast *)c)->globals.values[0] = 20; /* bees to shoot: the inventory is real since 2026-11-12, and he starts with none */
    run(&v, 1, 0);
    run(&v, 0.2f, BIT(TC_VA_ACTION));
    run(&v, 1, 0);
    p = (tc_player *)tc_viewer_player(&v);
    p->position = tc_v3_add(guard->ai->position, tc_v3(0, 6, 0));
    p->velocity = tc_v3(0, 0, 0);
    p->heading = 0; /* facing -Y: the guard */
    p->placed++;
    run(&v, 0.1f, 0); /* no longer: the guard turns and comes for him (2.8 away after 0.5 s: the bee would start inside its
                       * zone), and further back Ed stands off the platform's edge */
    spawned = c->spawned;
    for (i = 0; i < 60; i++) {
        tc_vec3 a = bee->ai->position;
        tc_viewer_set_action(&v, TC_VA_SHOOT, 1);
        frame(&v, 1.0f / 60);
        if (bee->ai->active && tc_v3_distance(a, bee->ai->position) < 5) flew += tc_v3_distance(a, bee->ai->position);
    }
    tc_viewer_set_action(&v, TC_VA_SHOOT, 0);
    snprintf(what, sizeof(what), "Ed shoots: %u bees instanced, %.1f flown; the guard hit (behaviour %u, reflex %u)",
             c->spawned - spawned, flew, guard->ai->normal.behaviour, guard->ai->reflex.behaviour);
    check(c->spawned > spawned && flew > 3 && guard->ai->normal.behaviour == 6, what);
    tc_viewer_free(&v);
    tc_level_free(&level);
}

/* A hint scene (docs/ENGINE.md "Walkable scenes", "Speech"): Ed stands where the scene's own Ed would and walks; the speaker
 * waits for him; Action within reach starts it; its line shows, in the game's font, Ed held; a fresh Jump dismisses it;
 * its script covers the screen and hands over to the main level. */
static void scene_scenario(const char *dir) {
    char path[1024], what[200];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    const tc_cast *c;
    const tc_cast_actor *speaker = NULL;
    const tc_viewer_carry *pending = NULL;
    tc_view_speech_panel panel;
    uint32_t i, k, lit = 0, lines = 0;
    float widest = 0;
    tc_vec3 at;
    snprintf(path, sizeof(path), "%s/bar.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); return; }
    free(data);
    if (!level.speech.dialogue_count) { printf("  (bar has no SPCH, an older export: scene skipped)\n"); tc_level_free(&level); return; }
    snprintf(path, sizeof(path), "%s/bar.ttai", dir);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.level_name = "bar";
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.seed = 7;
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "bar viewer"); free(ai); tc_level_free(&level); return; }
    free(ai);
    if (!v.playing && tc_viewer_can_play(&v)) tc_viewer_toggle_play(&v);
    c = tc_viewer_cast(&v);
    for (i = 0; c && i < c->count; i++)
        if (c->actors[i].entry->flags & TC_CAST_TALK) speaker = &c->actors[i];
    check(speaker != NULL, "bar: the barman is in the cast, a speaker");
    if (!speaker) { tc_viewer_free(&v); tc_level_free(&level); return; }
    run(&v, 1, 0);
    check(tc_v3_distance(tc_viewer_player(&v)->position, level.speech.arrival) < 1, "bar: Ed arrives where the scene's Ed stood");
    run(&v, 3, 0);
    check(!tc_viewer_speech(&v) && !speaker->talking, "bar: the barman waits for Ed");
    check(!tc_viewer_talk_prompt(&v) && !tc_viewer_speech_panel(&v, 4.0f / 3, &panel), "bar: no prompt out of his reach");
    for (i = 0; i < level.speech.dialogue_count; i++) {
        tc_speech s;
        tc_speech_init(&s, &level.speech);
        tc_speech_start(&s, &level.speech.dialogues[i]);
        for (k = 0; s.text[k]; k++)
            if (k == 0 || s.text[k - 1] == '^') widest = fmaxf(widest, tc_speech_line_width(&level.speech, s.text, k));
        lines += s.lines;
        tc_speech_free(&s);
    }
    snprintf(what, sizeof(what), "bar: every line wrapped to the window (%u lines, widest %.2f of 8.2)", lines, (double)widest);
    check(widest > 4 && widest <= 8.2f, what);
    tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3_add(speaker->ai->position, tc_v3(0, -6, 0.5f)), 0);
    run(&v, 1, 0);
    lit = 0;
    if (tc_viewer_talk_prompt(&v) && tc_viewer_speech_panel(&v, 4.0f / 3, &panel))
        for (i = 0; i < panel.width * panel.height; i++) lit += panel.rgba[i * 4 + 1] > 128;
    snprintf(what, sizeof(what), "bar: within reach the prompt shows (%u text pixels)", lit);
    check(lit > 500, what);
    lit = 0;
    run(&v, 0.1f, BIT(TC_VA_ACTION));
    check(speaker->talking, "bar: Action within reach starts the scene");
    check(!tc_viewer_talk_prompt(&v), "bar: the prompt goes when he talks");
    for (i = 0; i < 60 * 15 && !tc_viewer_speech(&v); i++) frame(&v, 1.0f / 60);
    check(tc_viewer_speech(&v) != NULL, "bar: the barman speaks");
    run(&v, 6, 0); /* more than five lines rise from below: by now they show */
    if (tc_viewer_speech_panel(&v, 16.0f / 9, &panel)) {
        for (i = 0; i < panel.width * panel.height; i++) lit += panel.rgba[i * 4 + 1] > 128;
        snprintf(what, sizeof(what), "bar: the text window (%u text pixels, x %.2f .. %.2f at 16:9)", lit, (double)panel.rect[0], (double)panel.rect[2]);
        check(lit > 1000 && panel.rect[0] > 0.15f && panel.rect[2] < 0.85f && panel.rect[1] == 0.75f, what);
    } else check(0, "bar: the text window");
    at = tc_viewer_player(&v)->position;
    run(&v, 1, BIT(TC_VA_FORWARD));
    check(tc_v3_distance(at, tc_viewer_player(&v)->position) < 0.05f, "bar: Ed waits while he listens");
    run(&v, 0.1f, BIT(TC_VA_JUMP));
    check(!tc_viewer_speech(&v) && speaker->ai->speaking == 0, "bar: a fresh Jump dismisses the line");
    for (i = 0; i < 60 * 30 && !pending; i++) {
        frame(&v, 1.0f / 60);
        pending = tc_viewer_pending_level(&v);
    }
    check(pending && strcmp(pending->level, "cock01") == 0, "bar: the barman's script hands over to cock01");
    tc_viewer_free(&v);
    tc_level_free(&level);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv", *name = argc > 2 ? argv[2] : "bar";
    char path[1024], status[512];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    tc_vec3 a, b, exit_cam;
    float fov;

    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { fprintf(stderr, "%s?\n", path); return 1; }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = name;
    desc.carry = NULL;
    desc.seed = 0;
    if (tc_viewer_init(&v, &desc) != TC_OK) { fprintf(stderr, "tc_viewer_init failed\n"); return 1; }

    printf("viewcheck: %s: %u meshes in the table; %u statics, %u persos (%u parts), %u tris; ai %s\n", name,
           tc_viewer_mesh_count(&v), v.stats.meshes, v.stats.persos, v.stats.parts, v.stats.triangles,
           tc_result_string(v.ai_result));
    {   /* every triangle of a split transparent static is in exactly one of its pieces */
        uint32_t i, k, split = 0, ok = 1;
        for (i = 0; i < level.mesh_count; i++) {
            uint32_t tris = 0;
            if (!v.split || !v.split[i]) continue;
            split++;
            for (k = 0; k < v.piece_count; k++)
                if (v.piece_origin[k] == i) tris += v.pieces[k].index_count / 3;
            if (tris != level.meshes[i].index_count / 3) ok = 0;
        }
        printf("  %u transparent statics split into %u pieces\n", split, v.piece_count);
        check(ok, "transparent pieces cover their statics' triangles exactly");
    }
    check(tc_viewer_can_play(&v) && v.playing, "starts in play mode");
    if (!tc_viewer_can_play(&v)) goto done;
    check(!ai || tc_viewer_ai_active(&v), "with Ed's AI when the .ttai loads");

    run(&v, 1, 0);
    report(&v, "AI, idle 1 s");
    a = ed(&v);
    run(&v, 2, BIT(TC_VA_FORWARD));
    report(&v, "AI, forward 2 s");
    b = ed(&v);
    check(tc_v3_distance(a, b) > 1, "Ed walks forward (AI)");
    run(&v, 1, BIT(TC_VA_FORWARD) | BIT(TC_VA_JUMP));
    report(&v, "AI, forward + jump");

    /* Gamepad: the left stick is the game's own stick (its scripts: nothing inside their threshold, the pace from the
     * push), the right stick turns the camera. From a fresh start: after the runs above Ed stands against bar's wall,
     * and the stick's speeds there measure the wall slide, not the stick. */
    tc_viewer_free(&v);
    if (tc_viewer_init(&v, &desc) != TC_OK) { fprintf(stderr, "tc_viewer_init failed\n"); return 1; }
    free(ai);  /* the model is a copy */
    if (level.speech.has_arrival) /* a scene's arrival is against a wall, which the camera swings away from: the open floor */
        tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3(level.ed.start.m[3], level.ed.start.m[7], level.ed.start.m[11] + 0.3f), tc_heading_of(&level.ed.start));
    run(&v, 1, 0);
    a = ed(&v);
    b.x = run_stick(&v, 0.5f, 0, 0.1f);
    check(b.x == 0 && tc_v3_distance(a, ed(&v)) < 0.05f, "stick: a slight push (dead zone) leaves Ed standing");
    b.y = run_stick(&v, 1.5f, 0, 0.5f);
    run(&v, 1, 0);
    b.z = run_stick(&v, 1.5f, 0, 1);
    printf("  stick top speed: half %.2f, full %.2f\n", b.y, b.z);
    check(b.y > 0.5f && b.z > 1.5f * b.y, "stick: half a push walks, a full one runs faster");
    run(&v, 1, 0);
    fov = v.camera.yaw;
    for (int i = 0; i < 60; i++) {
        tc_viewer_set_look_stick(&v, 1, 0);
        frame(&v, 1.0f / 60);
    }
    tc_viewer_release_all(&v);
    printf("  right stick: yaw %.3f -> %.3f (turn_speed %.2f)\n", (double)fov, (double)v.camera.yaw, (double)v.turn_speed);
    check(fabsf(remainderf(v.camera.yaw - fov - v.turn_speed, 2 * TC_PI)) < 0.05f, "right stick: turns the camera at turn_speed");
    for (int i = 0; i < 60; i++) { /* and back, so the runs below face where they did */
        tc_viewer_set_look_stick(&v, -1, 0);
        frame(&v, 1.0f / 60);
    }
    tc_viewer_release_all(&v);
    {   /* the play camera (tc_cam.h; camcheck has the details): turned by the player, it stays while Ed stands */
        char status[512];
        float yaw = v.camera.yaw;
        run(&v, 1, 0);
        check(fabsf(v.camera.yaw - yaw) < 1e-4f, "Ed standing: the camera stays where the stick turned it");
        tc_viewer_recenter_camera(&v);
        run(&v, 1, 0);
        check(fabsf(remainderf(v.camera.yaw - (TC_PI - tc_viewer_player(&v)->heading), 2 * TC_PI)) < 0.01f &&
                  fabsf(v.camera.pitch + TC_CAM_ELEVATION) < 0.01f,
              "recentre: behind Ed, at the follow's elevation");
        tc_viewer_toggle_closeup(&v);
        tc_viewer_status(&v, NULL, status, sizeof(status));
        check(v.cam.closeup && strstr(status, ", close-up") != NULL, "Numpad 5: the close-up (title too)");
        tc_viewer_toggle_closeup(&v);
        tc_viewer_set_look(&v, 2, false, true);
        fov = v.camera.pitch;
        tc_viewer_mouse_look(&v, 0, 10); /* the mouse 10 points down, inverted: up, twice as far */
        frame(&v, 1.0f / 60);
        check(fabsf(v.camera.pitch - fov - 0.06f) < 0.03f, "look settings: x2, invert Y (the mouse moved down looks up)");
        tc_viewer_status(&v, NULL, status, sizeof(status));
        check(strstr(status, ", look 200 %, invert Y") != NULL, "look settings in the title");
        tc_viewer_set_look(&v, 1, false, false);
        tc_viewer_recenter_camera(&v);
        run(&v, 1, 0);
    }

    tc_viewer_toggle_play(&v);
    check(!v.playing, "Tab: free-fly");
    a = v.camera.position;
    run(&v, 1, BIT(TC_VA_FORWARD));
    check(tc_v3_distance(a, v.camera.position) > 1, "free-fly moves the camera");
    a = v.camera.position;
    run_stick(&v, 1, 0, 1);
    b = v.camera.position;
    run_stick(&v, 1, 0, -1); /* and back, so Ed is dropped where he was before */
    check(tc_v3_distance(a, b) > 1 && tc_v3_distance(a, v.camera.position) < 0.01f, "free-fly: the stick flies there and back");
    exit_cam = v.camera.position;
    tc_viewer_toggle_play(&v);
    b = ed(&v);
    check(v.playing && fabsf(b.x - exit_cam.x) < 0.01f && fabsf(b.y - exit_cam.y) < 0.01f && b.z <= exit_cam.z,
          "Tab again: Ed dropped below the camera");
    run(&v, 1, 0);
    report(&v, "AI, after the drop");

    tc_viewer_toggle_ai(&v);
    check(!tc_viewer_ai_active(&v), "I: simple controller");
    a = ed(&v);
    run(&v, 2, BIT(TC_VA_FORWARD) | BIT(TC_VA_FAST));
    report(&v, "simple, run 2 s");
    check(tc_v3_distance(a, ed(&v)) > 1, "Ed runs forward (simple controller)");
    tc_viewer_toggle_ai(&v);

    tc_viewer_step_speed(&v, -1);
    tc_viewer_step_speed(&v, -1);
    fov = v.camera.vertical_fov;
    tc_viewer_step_fov(&v, 1);
    tc_viewer_step_gamma(&v, true);
    tc_viewer_toggle_pause(&v);
    a = ed(&v);
    b.x = v.animation_time;
    run(&v, 0.5f, 0);
    check(v.animation_time == b.x && tc_v3_distance(a, ed(&v)) < 1e-4f, "P: clock and AI stop");
    tc_viewer_toggle_pause(&v);
    check(fabsf(v.animation_speed - 2.0f / 3) < 1e-6f && fabsf(tc_camera_hfov_43(v.camera.vertical_fov) - roundf(tc_camera_hfov_43(fov) * 180 / TC_PI) * TC_PI / 180 - 5 * TC_PI / 180) < 1e-4f,
          "[ [ .: speed 2/3, FOV +5");
    tc_viewer_status(&v, ", AA 4x", status, sizeof(status));
    printf("  status: %s\n", status);
    check(tc_viewer_status(&v, NULL, status, 8) > 8 && strlen(status) == 7, "status truncates");
    if (v.level->has_lighting) {
        static tc_view_frame fr;
        tc_viewer_cycle_lighting(&v);
        tc_viewer_frame(&v, 1, &fr);
        tc_viewer_status(&v, NULL, status, sizeof(status));
        check(fr.lighting == TC_VL_PORT && strstr(status, ", old lighting") != NULL, "L: the port's old lighting");
        tc_viewer_cycle_lighting(&v);
        tc_viewer_frame(&v, 1, &fr);
        check(fr.lighting == TC_VL_GAME && fr.sky_light[0] > 0, "L again: the original's");
    }

done:
    sector_scenario(dir);
    se_scale_scenario(dir);
    se_moving_textures_scenario(dir);
    portal_scenario(dir);
    se_ski_scenario(dir);
    ride_scenario(dir);
    guide_scenario(dir);
    shoot_scenario(dir);
    scene_scenario(dir);
    light_scenario(dir, "Land");
    light_scenario(dir, "sud0");
    cull_scenario(dir);
    car_ride_scenario(dir);
    check(bad_frames == 0, "every frame's draw list is valid");
    tc_viewer_free(&v);
    tc_level_free(&level);
    printf(failures ? "FAIL\n" : "ok\n");
    return failures != 0;
}
