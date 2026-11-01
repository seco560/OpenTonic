/* The play camera (tc_cam.h) on hand-made geometry: no game data needed. Follow placement, the lazy tolerance and its
 * orbit (stays while Ed stands, eases back as he runs), the front stretch, recentring, presets, and collision (in at once, out eased, never past a wall). Then, with a
 * converted level folder (camcheck [out/ttlv]), Ed's demo on a few levels: how often the camera ends up behind a surface
 * as seen from Ed's head (a ray from the pivot to the camera, both faces). Test-only stdio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

static int failures;

static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static float wrap(float a) {
    a = fmodf(a + TC_PI, 2 * TC_PI);
    return a < 0 ? a + TC_PI : a - TC_PI;
}

static tc_vec3 pivot_of(tc_vec3 ed) { return tc_v3_add(ed, tc_v3(0, 0, TC_CAM_AIM_HEIGHT)); }

/* A big wall square on the plane y = y0 (|x|, |z| <= 50), its front facing -y (towards the origin when y0 > 0), as
 * two triangles. */
static void wall(tc_vec3 *v, float y0) {
    tc_vec3 a = tc_v3(-50, y0, -50), b = tc_v3(50, y0, -50), c = tc_v3(50, y0, 50), d = tc_v3(-50, y0, 50);
    /* counter-clockwise seen from -y: normal (b - a) x (c - a) = (100, 0, 0) x (100, 0, 100) = (0, -10000, 0) */
    v[0] = a, v[1] = b, v[2] = c;
    v[3] = a, v[4] = c, v[5] = d;
}

static void run(tc_cam *c, tc_camera *view, const tc_cam_target *t, float seconds) {
    int i, n = (int)(seconds * 60 + 0.5f);
    for (i = 0; i < n; i++) tc_cam_update(c, view, t, 1.0f / 60);
}

/* The original's field of view (tc_cam.h TC_CAMERA_FOV): 1.5 rad horizontal at 4:3; Hor+ wider, the horizontal held
 * narrower. */
static void field_of_view(void) {
    tc_camera c;
    char what[256];
    float h43, h169, h11;
    tc_camera_init(&c);
    printf("field of view:\n");
    h43 = 2 * atanf(tanf(tc_camera_fov_y(&c, 4.0f / 3) / 2) * (4.0f / 3));
    h169 = 2 * atanf(tanf(tc_camera_fov_y(&c, 16.0f / 9) / 2) * (16.0f / 9));
    h11 = 2 * atanf(tanf(tc_camera_fov_y(&c, 1) / 2));
    snprintf(what, sizeof(what), "4:3: %.2f deg horizontal (the original's 1.5 rad = 85.94), %.2f vertical",
             (double)(h43 * 180 / TC_PI), (double)(tc_camera_fov_y(&c, 4.0f / 3) * 180 / TC_PI));
    check(fabsf(h43 - TC_CAMERA_HFOV_43) < 1e-4f, what);
    snprintf(what, sizeof(what), "16:9: the same vertical, %.1f deg horizontal (wider: Hor+)", (double)(h169 * 180 / TC_PI));
    check(fabsf(tc_camera_fov_y(&c, 16.0f / 9) - TC_CAMERA_FOV) < 1e-6f && h169 > h43, what);
    snprintf(what, sizeof(what), "1:1: the horizontal held at %.2f deg (never narrower than the original)", (double)(h11 * 180 / TC_PI));
    check(fabsf(h11 - TC_CAMERA_HFOV_43) < 1e-4f, what);
}

static void synthetic(void) {
    tc_cam c;
    tc_camera view;
    tc_cam_target t;
    tc_collision col;
    tc_vec3 tris[6];
    char what[256];
    float d, off, stretch, before;

    printf("orbit and follow (no collision):\n");
    tc_camera_init(&view);
    tc_cam_reset(&c);
    memset(&t, 0, sizeof(t));
    t.position = tc_v3(0, 0, 0);
    t.behind = 0;            /* Ed faces +y: the camera behind him looks along +y, from -y */
    t.distance = TC_CAM_DISTANCE;
    view.yaw = 2.0f;         /* anywhere: the first update places it */
    view.pitch = 0.3f;
    tc_cam_update(&c, &view, &t, 1.0f / 60);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "placed at once behind him: off %.4f, pitch %.3f (-%.2f), distance %.3f", (double)wrap(view.yaw),
             (double)view.pitch, (double)TC_CAM_ELEVATION, (double)d);
    check(fabsf(wrap(view.yaw)) < 1e-6f && fabsf(view.pitch + TC_CAM_ELEVATION) < 1e-4f && fabsf(d - TC_CAM_DISTANCE) < 1e-3f, what);
    check(view.position.y < 0 && view.position.z > TC_CAM_AIM_HEIGHT, "behind him (-y) and above the pivot");

    tc_cam_look(&c, &view, 1.0f, 0.2f); /* the player turns it 1 rad and raises it */
    run(&c, &view, &t, 3);  /* Ed stands */
    off = wrap(view.yaw - t.behind);
    snprintf(what, sizeof(what), "Ed standing: the camera stays where the player turned it (off %.4f, pitch %.3f)", (double)off,
             (double)view.pitch);
    check(fabsf(off - 1.0f) < 1e-5f && fabsf(view.pitch + TC_CAM_ELEVATION + 0.2f) < 1e-3f, what);
    stretch = 1 + (TC_CAM_FRONT_STRETCH - 1) * 1.0f / TC_PI;
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "the front stretch at 1 rad: %.3f = %.1f x %.3f", (double)d, (double)TC_CAM_DISTANCE, (double)stretch);
    check(fabsf(d - TC_CAM_DISTANCE * stretch) < 0.01f, what); /* the stretch eases (TC_CAM_STRETCH_TIME) */

    {   /* Ed running; his controls are camera-relative, so each frame his way comes from the camera's yaw (as the AI
         * makes it), his facing only every other frame (the AI's 30 Hz) */
        int i;
        float max_step = 0, prev, facing;
        tc_cam_recenter(&c);
        run(&c, &view, &t, 1);
        tc_cam_look(&c, &view, 1.2f, 0); /* turned 1.2 rad round, to his side */
        before = view.yaw;
        t.speed = 5;
        for (i = 0; i < (int)(TC_CAM_GRACE * 60) - 3; i++) {
            t.velocity = tc_v3(5 * sinf(view.yaw), 5 * cosf(view.yaw), 0); /* "forward": away from the camera */
            tc_cam_update(&c, &view, &t, 1.0f / 60);
        }
        check(fabsf(wrap(view.yaw - before)) < 1e-5f, "running, the player just turned: left alone for the grace time");

        /* walking straight at the camera ("back" held): it must not turn, nor shake */
        prev = view.yaw;
        facing = t.behind;
        for (i = 0; i < 240; i++) {
            t.velocity = tc_v3(-5 * sinf(view.yaw), -5 * cosf(view.yaw), 0);
            if (i % 2 == 0) facing = atan2f(t.velocity.x, t.velocity.y);
            t.behind = facing;
            t.position = tc_v3_add(t.position, tc_v3_scale(t.velocity, 1.0f / 60));
            tc_cam_update(&c, &view, &t, 1.0f / 60);
            max_step = fmaxf(max_step, fabsf(wrap(view.yaw - prev)));
            prev = view.yaw;
        }
        d = tc_v3_distance(view.position, pivot_of(t.position));
        snprintf(what, sizeof(what), "walking at the camera: it doesn't turn (largest step %.6f rad), it backs off (%.2f away)",
                 (double)max_step, (double)d);
        check(max_step < 1e-4f && d > TC_CAM_DISTANCE - 0.01f, what);

        /* running sideways (across the view): the rope brings it round behind his way, smoothly */
        max_step = 0;
        t.velocity = tc_v3(5 * cosf(view.yaw), -5 * sinf(view.yaw), 0); /* along the camera's right, fixed */
        t.behind = atan2f(t.velocity.x, t.velocity.y);
        prev = view.yaw;
        for (i = 0; i < 60 * 8; i++) {
            t.position = tc_v3_add(t.position, tc_v3_scale(t.velocity, 1.0f / 60));
            tc_cam_update(&c, &view, &t, 1.0f / 60);
            max_step = fmaxf(max_step, fabsf(wrap(view.yaw - prev)));
            prev = view.yaw;
        }
        off = wrap(view.yaw - atan2f(t.velocity.x, t.velocity.y));
        snprintf(what, sizeof(what), "running sideways: comes round behind his way (off %.3f after 8 s), smoothly (largest step %.4f)",
                 (double)off, (double)max_step);
        check(fabsf(off) < asinf(TC_CAM_ROPE_DEAD / 5) + 0.01f && max_step < 0.02f, what); /* to the rope's dead zone */
        check(fabsf(c.rise) < 0.01f, "running: the player's height change has eased back");
        t.speed = 0;
        t.velocity = tc_v3(0, 0, 0);
        t.position = tc_v3(0, 0, 0);
        t.behind = 0;
        tc_cam_recenter(&c);
        run(&c, &view, &t, 1);
    }

    tc_cam_look(&c, &view, -2.0f, -0.3f);
    tc_cam_recenter(&c);
    run(&c, &view, &t, 1);
    snprintf(what, sizeof(what), "recentre: exactly behind him, the default height (off %.5f, rise %.5f)",
             (double)wrap(view.yaw - t.behind), (double)c.rise);
    check(fabsf(wrap(view.yaw - t.behind)) < 1e-6f && c.rise == 0 && !c.recentering, what);
    tc_cam_look(&c, &view, 0, 5);
    check(c.rise <= TC_CAM_RISE_MAX + 1e-6f, "height change held within its limits");
    tc_cam_recenter(&c);
    run(&c, &view, &t, 1);

    printf("first person:\n");
    c.first_person = true;
    view.yaw = 0.3f, view.pitch = 0;
    tc_cam_look(&c, &view, 0.5f, -0.2f); /* looking right and up */
    run(&c, &view, &t, 1);
    snprintf(what, sizeof(what), "first person: at his head (z %.2f), looking where turned (yaw %.2f, pitch %.2f)",
             (double)view.position.z, (double)view.yaw, (double)view.pitch);
    check(fabsf(view.position.z - TC_CAM_EYE_HEIGHT) < 1e-5f && fabsf(view.yaw - 0.8f) < 1e-5f &&
              fabsf(view.pitch - 0.2f) < 1e-5f, what);
    c.first_person = false;
    view.yaw = t.behind, view.pitch = -TC_CAM_ELEVATION;
    run(&c, &view, &t, 1.0f / 60);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "leaving it: the follow placed behind him again at once (%.2f away)", (double)d);
    check(fabsf(d - TC_CAM_DISTANCE) < 0.01f, what);

    printf("presets:\n");
    c.closeup = true;
    run(&c, &view, &t, 3);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "close-up (Numpad 5): %.2f away, pitch %.3f (-%.2f)", (double)d, (double)view.pitch,
             (double)TC_CAM_CLOSEUP_ELEVATION);
    check(fabsf(d - TC_CAM_CLOSEUP_DISTANCE) < 0.01f && fabsf(view.pitch + TC_CAM_CLOSEUP_ELEVATION) < 0.01f, what);
    c.closeup = false;
    run(&c, &view, &t, 3);

    t.situation = TC_CAM_SIT_FLIGHT;
    run(&c, &view, &t, 0.1f);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "flight: the distance eases towards %.1f (%.2f after 0.1 s)", (double)TC_CAM_FLIGHT_DISTANCE, (double)d);
    check(d < TC_CAM_DISTANCE - 0.1f && d > TC_CAM_FLIGHT_DISTANCE + 0.5f, what);
    run(&c, &view, &t, 3);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "flight: %.2f behind, level with the pivot (pitch %.3f)", (double)d, (double)view.pitch);
    check(fabsf(d - TC_CAM_FLIGHT_DISTANCE) < 0.01f && fabsf(view.pitch) < 0.01f, what);
    t.situation = 0;
    run(&c, &view, &t, 3);

    {   /* the jump: Ed rises 2 and comes back; the camera's height stays, it keeps looking at him */
        float z0 = view.position.z, zmax = z0, zmin = z0, look_err = 0;
        int i;
        t.situation = TC_CAM_SIT_JUMP;
        for (i = 0; i < 60; i++) {
            tc_vec3 to;
            t.position.z = 2 * sinf(TC_PI * (float)i / 60);
            t.situation = i < 30 ? TC_CAM_SIT_JUMP : TC_CAM_SIT_FALL;
            tc_cam_update(&c, &view, &t, 1.0f / 60);
            zmax = fmaxf(zmax, view.position.z), zmin = fminf(zmin, view.position.z);
            to = tc_v3_normalize(tc_v3_sub(pivot_of(t.position), view.position));
            look_err = fmaxf(look_err, tc_v3_distance(to, tc_camera_forward(&view)));
        }
        snprintf(what, sizeof(what), "jump: the camera's height held (%.3f .. %.3f) while Ed rises 2, looking at him (off %.4f)",
                 (double)zmin, (double)zmax, (double)look_err);
        check(zmax - zmin < 1e-4f && look_err < 1e-3f, what);
        t.position.z = 0;
        t.situation = 0;
        run(&c, &view, &t, 0.1f);
        check(!c.holding, "jump: the hold ends when he is back to normal");
        t.situation = TC_CAM_SIT_JUMP;
        run(&c, &view, &t, 0.1f);
        t.situation = TC_CAM_SIT_FALL;
        t.position.z = -1;
        run(&c, &view, &t, 0.1f);
        check(c.holding, "fall: still held 1 below the take-off");
        {
            float before_z = view.position.z;
            t.position.z = -2;
            run(&c, &view, &t, 1.0f / 60);
            check(!c.holding, "fall: let go 2 below the take-off (3 below the take-off pivot - 0.2)");
            snprintf(what, sizeof(what), "fall: no jump as it lets go (the camera moved %.3f that frame)",
                     (double)fabsf(view.position.z - before_z));
            check(fabsf(view.position.z - before_z) < 0.2f, what);
            run(&c, &view, &t, 2);
            snprintf(what, sizeof(what), "fall: then back at the follow's height (offset %.4f)", (double)c.hold_offset);
            check(fabsf(c.hold_offset) < 0.01f, what);
        }
        t.position.z = 0;
        t.situation = 0;
        run(&c, &view, &t, 2);
    }

    printf("collision (a wall behind Ed):\n");
    t.position = tc_v3(0, 0, 0);
    t.behind = TC_PI;        /* Ed faces -y: the camera behind him is on +y */
    tc_cam_reset(&c);
    wall(tris, 3);           /* 3 units behind him, facing him */
    if (tc_collision_init(&col, tris, 6, NULL) != 0) { check(0, "collision init"); return; }
    t.world = &col;
    run(&c, &view, &t, 1);
    snprintf(what, sizeof(what), "blocked: the camera stays in front of the wall (y %.3f < %.3f)", (double)view.position.y,
             (double)(3 - TC_CAM_RADIUS + 0.001f));
    check(view.position.y <= 3 - TC_CAM_RADIUS + 0.001f && view.position.y > 3 - TC_CAM_RADIUS - 0.1f, what);
    {
        tc_hit h;
        tc_vec3 p = pivot_of(t.position), to = tc_v3_sub(view.position, p);
        float len = tc_v3_length(to);
        check(!tc_collision_raycast(&col, p, tc_v3_scale(to, 1 / len), len, &h), "blocked: nothing between Ed's head and the camera");
    }
    tc_collision_free(&col);
    wall(tris, 30);          /* the wall moves away: the camera eases back out, not at once */
    if (tc_collision_init(&col, tris, 6, NULL) != 0) { check(0, "collision init"); return; }
    t.world = &col;
    before = tc_v3_distance(view.position, pivot_of(t.position));
    run(&c, &view, &t, 0.1f);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "cleared: eases out (%.2f -> %.2f after 0.1 s, not yet %.1f)", (double)before, (double)d,
             (double)TC_CAM_DISTANCE);
    check(d > before + 0.05f && d < TC_CAM_DISTANCE - 1, what);
    run(&c, &view, &t, 4);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    stretch = 1 + (TC_CAM_FRONT_STRETCH - 1) * fabsf(wrap(view.yaw - t.behind)) / TC_PI;
    snprintf(what, sizeof(what), "cleared: back at the full distance after 4 s (%.3f = %.1f x the stretch %.3f)", (double)d,
             (double)TC_CAM_DISTANCE, (double)stretch);
    check(fabsf(d - TC_CAM_DISTANCE * stretch) < 0.01f, what);
    tc_collision_free(&col);
    wall(tris, 3);           /* and back: in at once */
    if (tc_collision_init(&col, tris, 6, NULL) != 0) { check(0, "collision init"); return; }
    t.world = &col;
    run(&c, &view, &t, 1.0f / 60);
    snprintf(what, sizeof(what), "blocked again: in at once, the same frame (y %.3f)", (double)view.position.y);
    check(view.position.y <= 3 - TC_CAM_RADIUS + 0.001f, what);
    tc_collision_free(&col);
}

/* ---- the demo on real levels: the camera behind a surface? ---- */

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

/* The collision the viewer's play session uses is private: a second copy of the level's, to look with. */
static void level_demo(const char *dir, const char *name) {
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    tc_collision col;
    int i, frames = 0, hidden = 0, close = 0;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) {
        free(data);
        printf("  (no %s: skipped)\n", name);
        return;
    }
    free(data);
    if (tc_collision_init(&col, level.collision, 3 * level.collision_triangle_count, level.collision_floors) != 0) {
        tc_level_free(&level);
        return;
    }
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = name;
    if (tc_viewer_init(&v, &desc) != TC_OK) { free(ai); tc_collision_free(&col); tc_level_free(&level); return; }
    free(ai);
    tc_viewer_set_demo(&v, true, 4242);
    for (i = 0; i < 60 * 60; i++) {
        uint32_t c, o;
        tc_vec3 ed, pivot, to;
        float len;
        tc_hit h;
        tc_viewer_update(&v, 1.0f / 60);
        tc_viewer_draws(&v, &c, &o);
        if (v.ed_draw_count == 0) continue; /* in a portal */
        /* where the camera aims: Ed as drawn, which the player position follows closely */
        ed = tc_viewer_player(&v)->position;
        pivot = pivot_of(ed);
        to = tc_v3_sub(v.camera.position, pivot);
        len = tc_v3_length(to);
        frames++;
        close += len < 2;
        if (len > 0.3f && tc_collision_raycast(&col, pivot, tc_v3_scale(to, 1 / len), len - 0.25f, &h)) hidden++;
    }
    printf("  info  %s demo 60 s: %d frames, a surface between Ed's head and the camera in %d (%.1f %%), camera nearer "
           "than 2 in %d\n", name, frames, hidden, frames ? 100.0 * hidden / frames : 0.0, close);
    tc_viewer_free(&v);
    tc_collision_free(&col);
    tc_level_free(&level);
}

/* Ed's AI walking as a player would (towards the camera, sideways, away): the camera's yaw must not jump or shake. */
static void jitter_phase(tc_viewer *v, const char *level, const char *name, tc_view_action a, int frames) {
    float prev = v->camera.yaw, largest = 0, last = 0;
    int i, reversals = 0;
    char what[256];
    for (i = 0; i < frames; i++) {
        uint32_t c, o;
        float d;
        tc_viewer_set_action(v, a, true);
        tc_viewer_update(v, 1.0f / 60);
        tc_viewer_draws(v, &c, &o);
        d = remainderf(v->camera.yaw - prev, 2 * TC_PI);
        prev = v->camera.yaw;
        largest = fmaxf(largest, fabsf(d));
        if (d * last < 0 && fabsf(d) > 1e-4f) reversals++;
        if (fabsf(d) > 1e-5f) last = d;
    }
    tc_viewer_release_all(v);
    snprintf(what, sizeof(what), "%s, Ed's AI %s: largest yaw step %.4f rad / frame, %d reversals", level, name,
             (double)largest, reversals);
    check(largest < 0.03f && reversals <= 3, what);
}

static void level_jitter(const char *dir, const char *name) {
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    int i;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) { free(data); return; }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &level;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = name;
    if (tc_viewer_init(&v, &desc) != TC_OK) { free(ai); tc_level_free(&level); return; }
    free(ai);
    for (i = 0; i < 60; i++) {
        uint32_t c, o;
        tc_viewer_update(&v, 1.0f / 60);
        tc_viewer_draws(&v, &c, &o);
    }
    jitter_phase(&v, name, "walking at the camera 2 s", TC_VA_BACK, 120);
    jitter_phase(&v, name, "sideways 3 s", TC_VA_LEFT, 180);
    jitter_phase(&v, name, "away 2 s", TC_VA_FORWARD, 120);
    tc_viewer_free(&v);
    tc_level_free(&level);
}

int main(int argc, char **argv) {
    field_of_view();
    synthetic();
    if (argc > 1) {
        const char *levels[] = {"north", "sud0", "Land", "bar", "LeCanyon"};
        size_t k;
        printf("the demo on levels (the line of sight, both faces):\n");
        for (k = 0; k < sizeof(levels) / sizeof(levels[0]); k++) level_demo(argv[1], levels[k]);
        printf("no shaking (2026-11-01: the user saw it jitter as Ed walked towards the camera):\n");
        level_jitter(argv[1], "bar");
        level_jitter(argv[1], "north");
    }
    printf(failures ? "FAIL\n" : "ok\n");
    return failures ? 1 : 0;
}
