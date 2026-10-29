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

/* Every draw names a drawable mesh and an existing texture; [opaque, count) are exactly the transparent ones. */
static int bad_frames;
static uint32_t last_count, last_opaque, last_ed_draws;

static void frame(tc_viewer *v, float dt) {
    uint32_t count, opaque, i;
    const tc_view_draw *d;
    tc_view_frame f;
    int bad = 0;
    tc_viewer_update(v, dt);
    d = tc_viewer_draws(v, &count, &opaque);
    tc_viewer_frame(v, 16.0f / 10, &f);
    if (opaque > count) bad = 1;
    last_ed_draws = 0;
    for (i = 0; i < count; i++)
        if (d[i].mesh >= v->ed_first && d[i].mesh < v->ed_first + v->level->ed.part_count) last_ed_draws++;
    for (i = 0; i < count && !bad; i++) {
        const tc_mesh *m = tc_viewer_mesh(v, d[i].mesh);
        int transparent = (d[i].flags & TC_FLAG_TRANSPARENT) != 0;
        if (!m || m->index_count == 0 || (d[i].flags & ~TC_FLAG_WATER) != m->surface.flags || transparent != (i >= opaque)) bad = 1;
        if (d[i].texture < -1 || d[i].texture >= (int32_t)v->level->texture_count) bad = 1;
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

/* Portals through the viewer (docs/PORTALS.md): Ed walks into sud0's Cocktail arch; the overlay must go cover ->
 * black -> loading screen, then the level change is pending: the arch leads to the cutscene level bar, so cock01
 * (tc_level_after_cutscene). bar's viewer, started with the carry, reveals and Ed steps out (state 229). */
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
    tc_viewer_step_fov(&v, 1); /* 60 degrees and a zoomed-out camera: kept across the level change (the carry) */
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
    check(fabsf(w.camera.vertical_fov - 60 * TC_PI / 180) < 1e-4f, "bar keeps sud0's field of view (60 degrees)");
    {
        char what[96];
        snprintf(what, sizeof(what), "bar keeps sud0's camera zoom (%.2f, the default 4.5)", (double)zoom);
        check(zoom != 4.5f && fabsf(w.orbit_distance - zoom) < 1e-4f, what);
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
    check(fabsf(v.camera.yaw - fov - v.turn_speed) < 0.05f, "right stick: turns the camera at turn_speed");
    for (int i = 0; i < 60; i++) { /* and back, so the runs below face where they did */
        tc_viewer_set_look_stick(&v, -1, 0);
        frame(&v, 1.0f / 60);
    }
    tc_viewer_release_all(&v);

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
    check(fabsf(v.animation_speed - 2.0f / 3) < 1e-6f && fabsf(v.camera.vertical_fov - fov - 5 * TC_PI / 180) < 1e-4f,
          "[ [ .: speed 2/3, FOV +5");
    tc_viewer_status(&v, ", AA 4x", status, sizeof(status));
    printf("  status: %s\n", status);
    check(tc_viewer_status(&v, NULL, status, 8) > 8 && strlen(status) == 7, "status truncates");

done:
    portal_scenario(dir);
    check(bad_frames == 0, "every frame's draw list is valid");
    tc_viewer_free(&v);
    tc_level_free(&level);
    printf(failures ? "FAIL\n" : "ok\n");
    return failures != 0;
}
