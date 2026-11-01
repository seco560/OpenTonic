/* The viewer module without a GPU (docs/VIEWER_CORE.md): loads a level and its .ttai, runs tc_viewer_update with
 * scripted actions (Ed's AI, free-fly and drop, the simple controller, the settings keys) and checks every frame's
 * draw list (valid mesh indices and textures, transparent draws last). Prints draw counts, Ed's position and the
 * title readout. Usage: viewcheck [out/ttlv] [level]. Test-only stdio. */
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
 * black -> loading screen, then the level change is pending: the arch leads to the cutscene level bar, so cock01
 * (tc_level_after_cutscene). bar's viewer, started with the carry, reveals and Ed steps out (state 229). */
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

/* The demo (tc_viewer_set_demo): Ed wanders by himself for 60 s on a few levels, through frames as the frontends draw
 * them: he gets about, varies what he does (DEMO_* acts in tc_viewer.c), and the camera stays mostly behind him. North
 * (no red portal near Ed's start) is checked; the others print what happened. DEMO_TRACE=<level> prints its timeline. */
static void demo_level(const char *dir, const char *name, bool checked) {
    char path[1024], what[768];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    tc_vec3 last, p, start;
    float travelled = 0, far_away = 0;
    int i, k, jumps = 0, acts[16] = {0}, kinds = 0, flaps = 0, behind = 0, moving = 0, respawns = 0, last_act = -1;
    int was_flap = 0;
    float last_time = 0;
    uint32_t placed;
    bool was_jump = false;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) {
        free(data);
        printf("  (no %s: demo skipped)\n", name);
        return;
    }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = name;
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "demo viewer"); free(ai); tc_level_free(&level); return; }
    free(ai);
    /* The demo is chaotic: anything that changes Ed's inputs (the camera's position feeds his AI) reshuffles the run. 12345
     * gave north 4 kinds of act after the play camera (2026-11-01) and 6 before; over seeds 1-8 the spread is the same
     * either side. Seed 2 has 5 jumps and 8 kinds on north with both cameras. */
    tc_viewer_set_demo(&v, true, 2);
    if (checked) check(tc_viewer_demo(&v) && v.playing, "demo: on, in play mode");
    last = start = ed(&v);
    placed = tc_viewer_player(&v)->placed;
    for (i = 0; i < 60 * 60; i++) {
        const tc_player *pl;
        float vx, vy, off;
        frame(&v, 1.0f / 60);
        pl = tc_viewer_player(&v);
        p = ed(&v);
        if (pl->placed != placed) { respawns++; placed = pl->placed; last = p; }
        travelled += sqrtf((p.x - last.x) * (p.x - last.x) + (p.y - last.y) * (p.y - last.y));
        far_away = fmaxf(far_away, tc_v3_distance(p, start));
        last = p;
        jumps += !was_jump && ((v.held >> TC_VA_JUMP) & 1u);
        was_jump = (v.held >> TC_VA_JUMP) & 1u;
        if ((v.demo_act != last_act || v.demo_act_time < last_time) && v.demo_act >= 0 && v.demo_act < 16)
            acts[v.demo_act]++, last_act = v.demo_act; /* a new act (one can follow its own kind) */
        last_time = v.demo_act_time;
        if (getenv("DEMO_TRACE") && strcmp(getenv("DEMO_TRACE"), name) == 0 && i % 15 == 0)
            printf("    %5.2f act %d t %.1f pos %.1f %.1f %.1f vel %.1f %.1f %.1f ground %d heading-yaw %.2f cam %.2f aim %.2f "
                   "flap %d\n", i / 60.0, v.demo_act, v.demo_act_time, p.x, p.y, p.z, pl->velocity.x, pl->velocity.y,
                   pl->velocity.z, pl->grounded, TC_PI - pl->heading, v.camera.yaw, v.demo_aim, v.demo_flap);
        flaps += v.demo_flap == 3 && !was_flap;
        was_flap = v.demo_flap == 3;
        vx = pl->velocity.x, vy = pl->velocity.y;
        if (vx * vx + vy * vy > 4) { /* moving: is the camera behind him (within 50 degrees of his way)? */
            off = fmodf(fabsf(v.camera.yaw - atan2f(vx, vy)), 2 * TC_PI);
            if (off > TC_PI) off = 2 * TC_PI - off;
            moving++;
            behind += off < 50 * TC_PI / 180;
        }
    }
    for (k = 0; k < 16; k++) kinds += acts[k] > 0;
    snprintf(what, sizeof(what), "demo %s 60 s: %.0f units walked, %.0f from the start at most, %d jumps, %d kinds of act "
             "(run %d walk %d pause %d hop %d leap %d pogo %d swing %d shoot %d side %d), %d fly attempts, %d respawns, "
             "camera behind him %.0f %% of the time he moves", name, travelled, far_away, jumps, kinds, acts[0], acts[1],
             acts[2], acts[3], acts[4], acts[5], acts[6], acts[7], acts[8], flaps, respawns,
             moving ? 100.0 * behind / moving : 0.0);
    if (checked) check(isfinite(p.x) && travelled > 100 && far_away > 15 && jumps >= 5 && kinds >= 5
                       && behind > moving * 3 / 4, what);
    else printf("  info  %s\n", what);
    tc_viewer_set_demo(&v, false, 0);
    if (checked)
        check(!tc_viewer_demo(&v) && v.move_y == 0 && !(v.held & ((1u << TC_VA_JUMP) | (1u << TC_VA_ACTION) |
              (1u << TC_VA_SHOOT) | (1u << TC_VA_LATERAL))), "demo off: the stick and the buttons let go");
    tc_viewer_free(&v);
    tc_level_free(&level);
}

static void demo_scenario(const char *dir) {
    demo_level(dir, "north", true);
    demo_level(dir, "sud0", false);
    demo_level(dir, "Land", false);
    demo_level(dir, "LeCanyon", false);
    demo_level(dir, "pyramide", false);
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
    check(pending && strcmp(pending->level, "cock01") == 0, "sud0 Cocktail arch: bar is a cutscene, cock01 pending");
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
    demo_scenario(dir);
    portal_scenario(dir);
    light_scenario(dir, "Land");
    light_scenario(dir, "sud0");
    check(bad_frames == 0, "every frame's draw list is valid");
    tc_viewer_free(&v);
    tc_level_free(&level);
    printf(failures ? "FAIL\n" : "ok\n");
    return failures != 0;
}
