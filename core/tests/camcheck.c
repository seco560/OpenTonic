/* Debug traces (env): JIT_DEBUG=1 the no-shaking phases frame by frame; CLIP_DEBUG=1 the frames whose near plane
 * touches something drawn; BLOCK_DEBUG=1 the persos drawn between Ed's head and the eye.
 * The play camera (tc_cam.h) on hand-made geometry: no game data needed. Follow placement, the lazy tolerance and its
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

static float yaw_from(tc_vec3 cam, tc_vec3 pivot) { return atan2f(pivot.x - cam.x, pivot.y - cam.y); }

/* The angle between the view and the line to Ed's pivot (how far off-centre he is). */
static float off_centre(const tc_camera *view, tc_vec3 pivot) {
    tc_vec3 to = tc_v3_normalize(tc_v3_sub(pivot, view->position)), f = tc_camera_forward(view);
    return acosf(fmaxf(-1, fminf(1, tc_v3_dot(to, f))));
}

static void synthetic(void) {
    tc_cam c;
    tc_camera view;
    tc_cam_target t;
    tc_collision col;
    tc_vec3 tris[6];
    char what[256];
    float d, off, before;
    int i;

    printf("placement, orbit and the lazy follow (no collision; the faithful model, 9):\n");
    tc_camera_init(&view);
    c.faithful = true;
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
    check(fabsf(wrap(view.yaw)) < 1e-5f && fabsf(view.pitch + TC_CAM_ELEVATION) < 1e-4f && fabsf(d - TC_CAM_DISTANCE) < 1e-3f, what);
    check(view.position.y < 0 && view.position.z > TC_CAM_AIM_HEIGHT, "behind him (-y) and above the pivot");

    tc_cam_look(&c, &view, 1.0f, 0.2f); /* the player turns it 1 rad and raises it */
    off = wrap(yaw_from(view.position, pivot_of(t.position)) - t.behind);
    tc_cam_update(&c, &view, &t, 1.0f / 60);
    off = wrap(yaw_from(view.position, pivot_of(t.position)) - t.behind);
    snprintf(what, sizeof(what), "the player's turn shows at once, the same frame (off %.4f)", (double)off);
    check(fabsf(off - 1.0f) < 1e-3f, what);
    run(&c, &view, &t, 3);  /* Ed stands */
    off = wrap(yaw_from(view.position, pivot_of(t.position)) - t.behind);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "Ed standing: it stays where the player turned it (off %.4f, pitch %.3f), in the band (%.2f)",
             (double)off, (double)view.pitch, (double)d);
    check(fabsf(off - 1.0f) < 1e-3f && fabsf(view.pitch + TC_CAM_ELEVATION + 0.2f) < 0.01f &&
              d > TC_CAM_DISTANCE - TC_CAM_BAND - 0.01f && d < (TC_CAM_DISTANCE + TC_CAM_BAND) * TC_CAM_FRONT_STRETCH, what);

    {   /* Ed walking: his controls are camera-relative, so his way comes from the camera's yaw each frame (as the AI
         * makes it), his position steps at the AI's 30 Hz (every other frame here) */
        float max_step = 0, prev, max_move = 0;
        tc_vec3 ed = t.position, last;
        tc_cam_recenter(&c);
        run(&c, &view, &t, 1);
        t.speed = 5;
        prev = view.yaw;
        last = view.position;
        for (i = 0; i < 240; i++) { /* straight at the camera ("back" held) */
            t.velocity = tc_v3(-5 * sinf(view.yaw), -5 * cosf(view.yaw), 0);
            t.behind = atan2f(t.velocity.x, t.velocity.y);
            ed = tc_v3_add(ed, tc_v3_scale(t.velocity, 1.0f / 60));
            if (i % 2 == 0) t.position = ed;
            tc_cam_update(&c, &view, &t, 1.0f / 60);
            max_step = fmaxf(max_step, fabsf(wrap(view.yaw - prev)));
            max_move = fmaxf(max_move, tc_v3_distance(view.position, last));
            prev = view.yaw;
            last = view.position;
        }
        d = tc_v3_distance(view.position, pivot_of(t.position));
        snprintf(what, sizeof(what), "walking at the camera: it doesn't turn (largest step %.5f rad), backs off (%.2f away), "
                 "no frame's move over 5 / 60 x 1.5 (%.3f)", (double)max_step, (double)d, (double)max_move);
        check(max_step < 1e-3f && d > TC_CAM_DISTANCE - TC_CAM_BAND - 1.0f && max_move < 5.0f / 60 * 1.5f, what);

        max_step = 0; /* running sideways (across the view): the lazy follow brings it round behind his way */
        t.velocity = tc_v3(5 * cosf(view.yaw), -5 * sinf(view.yaw), 0);
        t.behind = atan2f(t.velocity.x, t.velocity.y);
        prev = view.yaw;
        for (i = 0; i < 60 * 8; i++) {
            ed = tc_v3_add(ed, tc_v3_scale(t.velocity, 1.0f / 60));
            if (i % 2 == 0) t.position = ed;
            tc_cam_update(&c, &view, &t, 1.0f / 60);
            max_step = fmaxf(max_step, fabsf(wrap(view.yaw - prev)));
            prev = view.yaw;
        }
        off = wrap(yaw_from(c.body, c.pivot) - t.behind);
        snprintf(what, sizeof(what), "running sideways: comes round behind his way (off %.3f after 8 s), smoothly (largest "
                 "step %.4f)", (double)off, (double)max_step);
        check(fabsf(off) < 0.15f && max_step < 0.02f, what);
        check(fabsf(c.rise) < 0.01f, "running: the player's height change has eased back");
        t.speed = 0;
        t.velocity = tc_v3(0, 0, 0);
        t.position = tc_v3(0, 0, 0);
        t.behind = 0;
        tc_cam_reset(&c);
        run(&c, &view, &t, 1);
    }

    {   /* Ed's 30 Hz steps and a bob: the averaged target keeps the view steady */
        float zmin = 1e9f, zmax = -1e9f, yaw_min = 1e9f, yaw_max = -1e9f;
        for (i = 0; i < 120; i++) {
            if (i % 2 == 0) t.position = tc_v3(0, 0, 0.15f * sinf(2 * TC_PI * 5 * (float)i / 60)); /* 5 Hz, 0.3 tall */
            tc_cam_update(&c, &view, &t, 1.0f / 60);
            if (i < 30) continue;
            zmin = fminf(zmin, view.position.z), zmax = fmaxf(zmax, view.position.z);
            yaw_min = fminf(yaw_min, view.pitch), yaw_max = fmaxf(yaw_max, view.pitch);
        }
        snprintf(what, sizeof(what), "Ed bobbing 0.3 at 5 Hz: the camera moves %.3f up and down, its pitch %.4f rad",
                 (double)(zmax - zmin), (double)(yaw_max - yaw_min));
        check(zmax - zmin < 0.1f && yaw_max - yaw_min < 0.02f, what);
        t.position = tc_v3(0, 0, 0);
        run(&c, &view, &t, 2);
    }

    {   /* a step of 3 (under the teleport's 5): the mover gains speed gradually, no frame jumps */
        tc_vec3 last = view.position;
        float max_move = 0;
        t.position = tc_v3(3, 0, 0);
        for (i = 0; i < 60; i++) {
            tc_cam_update(&c, &view, &t, 1.0f / 60);
            max_move = fmaxf(max_move, tc_v3_distance(view.position, last));
            last = view.position;
        }
        snprintf(what, sizeof(what), "Ed moved 3 at once: the camera follows without a jump (largest move %.3f / frame)",
                 (double)max_move);
        check(max_move < 0.5f, what);
        t.position = tc_v3(0, 0, 0);
        tc_cam_reset(&c);
        run(&c, &view, &t, 1);
    }

    tc_cam_look(&c, &view, -2.0f, -0.3f);
    tc_cam_recenter(&c);
    run(&c, &view, &t, 1);
    off = wrap(yaw_from(c.body, c.pivot) - t.behind);
    snprintf(what, sizeof(what), "recentre: behind him, the default height (off %.5f, rise %.5f)", (double)off, (double)c.rise);
    check(fabsf(off) < 1e-3f && c.rise == 0 && !c.recentering, what);
    tc_cam_look(&c, &view, 0, 5);
    check(c.rise <= TC_CAM_RISE_MAX + 1e-6f, "height change held within its limits");
    tc_cam_recenter(&c);
    run(&c, &view, &t, 2);

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
    run(&c, &view, &t, 1.0f / 60);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "leaving it: the follow placed behind him again at once (%.2f away)", (double)d);
    check(fabsf(d - TC_CAM_DISTANCE) < 0.01f, what);

    printf("presets:\n");
    c.closeup = true;
    run(&c, &view, &t, 3);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "close-up (Numpad 5): %.2f away (%.1f -+ %.1f), pitch %.3f (-%.2f)", (double)d,
             (double)TC_CAM_CLOSEUP_DISTANCE, (double)TC_CAM_BAND, (double)view.pitch, (double)TC_CAM_CLOSEUP_ELEVATION);
    check(fabsf(d - TC_CAM_CLOSEUP_DISTANCE) < TC_CAM_BAND + 0.01f && fabsf(view.pitch + TC_CAM_CLOSEUP_ELEVATION) < 0.01f, what);
    c.closeup = false;
    run(&c, &view, &t, 3);

    t.situation = TC_CAM_SIT_FLIGHT;
    before = tc_v3_distance(view.position, pivot_of(t.position));
    run(&c, &view, &t, 0.1f);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "flight: the distance moves towards %.1f (%.2f -> %.2f after 0.1 s)",
             (double)TC_CAM_FLIGHT_DISTANCE, (double)before, (double)d);
    check(d < before - 0.05f && d > TC_CAM_FLIGHT_DISTANCE + TC_CAM_BAND, what);
    run(&c, &view, &t, 3);
    d = tc_v3_distance(view.position, pivot_of(t.position));
    snprintf(what, sizeof(what), "flight: %.2f behind, level with the pivot (pitch %.3f)", (double)d, (double)view.pitch);
    check(fabsf(d - TC_CAM_FLIGHT_DISTANCE) < TC_CAM_BAND + 0.01f && fabsf(view.pitch) < 0.01f, what);
    t.situation = 0;
    run(&c, &view, &t, 3);

    {   /* the jump: Ed rises 2 and comes back; the camera's height stays, Ed stays in view (the look-at point lags) */
        float z0 = view.position.z, zmax = z0, zmin = z0, look_err = 0;
        t.situation = TC_CAM_SIT_JUMP;
        for (i = 0; i < 60; i++) {
            t.position.z = 2 * sinf(TC_PI * (float)i / 60);
            t.situation = i < 30 ? TC_CAM_SIT_JUMP : TC_CAM_SIT_FALL;
            tc_cam_update(&c, &view, &t, 1.0f / 60);
            zmax = fmaxf(zmax, view.position.z), zmin = fminf(zmin, view.position.z);
            look_err = fmaxf(look_err, off_centre(&view, pivot_of(t.position)));
        }
        snprintf(what, sizeof(what), "jump: the camera's height held (%.3f .. %.3f) while Ed rises 2, him in view (at most "
                 "%.3f rad off centre)", (double)zmin, (double)zmax, (double)look_err);
        check(zmax - zmin < 1e-3f && look_err < 0.4f, what);
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
            run(&c, &view, &t, 1.0f / 30);
            check(!c.holding, "fall: let go 2 below the take-off (3 below the take-off pivot - 0.2)");
            snprintf(what, sizeof(what), "fall: no jump as it lets go (the camera moved %.3f that tick)",
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
    c.since_look = 0;        /* the player just looked: the watchdog leaves it (below: it doesn't) */
    run(&c, &view, &t, 0.4f);
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
    snprintf(what, sizeof(what), "cleared: back in the band after 4 s (%.3f, the band's near edge %.1f)", (double)d,
             (double)(TC_CAM_DISTANCE - TC_CAM_BAND));
    check(d > TC_CAM_DISTANCE - TC_CAM_BAND - 0.01f, what);
    tc_collision_free(&col);
    wall(tris, 3);           /* and back: in at once */
    if (tc_collision_init(&col, tris, 6, NULL) != 0) { check(0, "collision init"); return; }
    t.world = &col;
    c.since_look = 0;
    run(&c, &view, &t, 1.0f / 60);
    snprintf(what, sizeof(what), "blocked again: in at once, the same frame (y %.3f)", (double)view.position.y);
    check(view.position.y <= 3 - TC_CAM_RADIUS + 0.001f, what);

    /* the watchdog: left alone, a wall pulling the eye nearer than TC_CAM_SWING_NEAR, it swings round to an open side */
    tc_collision_free(&col);
    wall(tris, 1.8f);
    if (tc_collision_init(&col, tris, 6, NULL) != 0) { check(0, "collision init"); return; }
    t.world = &col;
    c.since_look = 1e6f;
    run(&c, &view, &t, 2);
    {
        tc_hit h;
        tc_vec3 p = pivot_of(t.position), to = tc_v3_sub(view.position, p);
        float len = tc_v3_length(to);
        snprintf(what, sizeof(what), "watchdog: swung round to an open side (%.2f away, %.2f rad round), nothing between",
                 (double)len, (double)fabsf(wrap(yaw_from(view.position, p) - t.behind)));
        check(len > TC_CAM_SWING_NEAR + 1 && !tc_collision_raycast(&col, p, tc_v3_scale(to, 1 / len), len, &h),
              what);
    }
    tc_collision_free(&col);

    /* a wall right at his head: the camera never goes past it (the old 0.5 minimum put it inside), he is not drawn */
    wall(tris, 0.3f);
    if (tc_collision_init(&col, tris, 6, NULL) != 0) { check(0, "collision init"); return; }
    t.world = &col;
    tc_cam_reset(&c);
    c.since_look = 0;
    run(&c, &view, &t, 0.3f);
    snprintf(what, sizeof(what), "a wall 0.3 behind his head: the camera stays this side (y %.3f), Ed hidden (%d)",
             (double)view.position.y, c.hide_target);
    check(view.position.y < 0.3f && c.hide_target, what);
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

/* ---- clipping: the view against what is drawn (not only the collision) ---- */

/* The nearest a drawn triangle may come to the eye before it cuts the near plane: the plane is 0.05 away and its
 * corners ~0.07 off the axis at 16:9 (TC_CAMERA_FOV), so 0.09 is a touch. */
#define CLIP_NEAR 0.09f
enum { KIND_LEVEL, KIND_PERSO, KIND_ED, KIND_COUNT };

typedef struct clip_stats {
    int frames;
    int clipped[KIND_COUNT];      /* the near plane touches a drawn triangle of this kind */
    int clipped_no_col;           /* ... of the level or a perso, with no collision triangle within 0.4 of the eye */
    int hidden;                   /* Ed not drawn: the eye pulled into him (tc_cam.hide_target) */
    int blocked[KIND_COUNT];      /* a drawn triangle (either face) between Ed's head and the eye */
    int blocked_no_col;           /* ... level or perso, where the collision (both faces) sees nothing */
    float nearest;                /* the nearest any level / perso triangle came to the eye (all frames) */
} clip_stats;

static int draw_kind(const tc_viewer *v, uint32_t mesh) {
    if (mesh >= v->ed_first && mesh < v->ed_first + v->level->ed.part_count) return KIND_ED;
    if (mesh >= v->perso_first && mesh < v->ed_first) return KIND_PERSO;
    return KIND_LEVEL;
}

static tc_vec3 xform(const tc_mat4 *m, const float *p) {
    const float *a = m->m;
    return tc_v3(a[0] * p[0] + a[4] * p[1] + a[8] * p[2] + a[12], a[1] * p[0] + a[5] * p[1] + a[9] * p[2] + a[13],
                 a[2] * p[0] + a[6] * p[1] + a[10] * p[2] + a[14]);
}

/* Segment o + t d (t in (0, 1)) against triangle abc, both faces (Moller-Trumbore). */
static bool segment_tri(tc_vec3 o, tc_vec3 d, tc_vec3 a, tc_vec3 b, tc_vec3 c) {
    tc_vec3 e1 = tc_v3_sub(b, a), e2 = tc_v3_sub(c, a), p = tc_v3_cross(d, e2), s, q;
    float det = tc_v3_dot(e1, p), inv, u, w, t;
    if (fabsf(det) < 1e-9f) return false;
    inv = 1 / det;
    s = tc_v3_sub(o, a);
    u = tc_v3_dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    q = tc_v3_cross(s, e1);
    w = tc_v3_dot(d, q) * inv;
    if (w < 0 || u + w > 1) return false;
    t = tc_v3_dot(e2, q) * inv;
    return t > 0 && t < 1;
}

static bool collision_near(const tc_collision *col, tc_vec3 p, float r) {
    uint32_t i;
    for (i = 0; i < col->tri_count; i++) {
        const tc_tri *t = &col->tris[i];
        tc_vec3 q = tc_closest_point_on_triangle(t->a, t->b, t->c, p);
        if (tc_v3_length(tc_v3_sub(q, p)) < r) return true;
    }
    return false;
}

/* One frame: every drawn triangle (not sprites, not water) against the eye and the line from Ed's head to it. */
static void clip_frame(tc_viewer *v, tc_collision *col, tc_vec3 head, clip_stats *st) {
    uint32_t count, opaque, i, k;
    const tc_view_draw *draws = tc_viewer_draws(v, &count, &opaque);
    tc_vec3 eye = v->camera.position, seg = tc_v3_sub(eye, head);
    tc_vec3 lo = tc_v3(fminf(eye.x, head.x), fminf(eye.y, head.y), fminf(eye.z, head.z));
    tc_vec3 hi = tc_v3(fmaxf(eye.x, head.x), fmaxf(eye.y, head.y), fmaxf(eye.z, head.z));
    bool clipped[KIND_COUNT] = {false}, blocked[KIND_COUNT] = {false};
    float nearest = 1e9f;
    tc_hit h;
    float len = tc_v3_length(seg);
    st->frames++;
    for (i = 0; i < count; i++) {
        const tc_view_draw *d = &draws[i];
        const tc_mesh *m = tc_viewer_mesh(v, d->mesh);
        int kind = draw_kind(v, d->mesh);
        if (!m || (d->flags & (TC_FLAG_BILLBOARD | TC_FLAG_WATER))) continue;
        if (d->flags & TC_FLAG_TRANSPARENT) continue; /* see-through (glass, beams, smoke): not a blocker, as for the camera */
        for (k = d->first_index; k + 2 < d->first_index + d->index_count && k + 2 < m->index_count; k += 3) {
            tc_vec3 a = xform(&d->model, m->vertices[m->indices[k]].position);
            tc_vec3 b = xform(&d->model, m->vertices[m->indices[k + 1]].position);
            tc_vec3 c = xform(&d->model, m->vertices[m->indices[k + 2]].position);
            float r = CLIP_NEAR;
            /* near the eye? */
            if (!(fminf(a.x, fminf(b.x, c.x)) > eye.x + r || fmaxf(a.x, fmaxf(b.x, c.x)) < eye.x - r ||
                  fminf(a.y, fminf(b.y, c.y)) > eye.y + r || fmaxf(a.y, fmaxf(b.y, c.y)) < eye.y - r ||
                  fminf(a.z, fminf(b.z, c.z)) > eye.z + r || fmaxf(a.z, fmaxf(b.z, c.z)) < eye.z - r)) {
                float dist = tc_v3_length(tc_v3_sub(tc_closest_point_on_triangle(a, b, c, eye), eye));
                if (kind != KIND_ED && dist < nearest && getenv("CLIP_DEBUG") && dist < r) {
                    tc_vec3 n = tc_v3_normalize(tc_v3_cross(tc_v3_sub(b, a), tc_v3_sub(c, a)));
                    printf("      tri kind %d mesh %u flags 0x%x n (%.2f %.2f %.2f) a (%.2f %.2f %.2f) dist %.3f col %d\n", kind,
                           d->mesh, d->flags, (double)n.x, (double)n.y, (double)n.z, (double)a.x, (double)a.y,
                           (double)a.z, (double)dist, collision_near(col, eye, 0.4f));
                }
                if (kind != KIND_ED) nearest = fminf(nearest, dist);
                if (dist < r) clipped[kind] = true;
            }
            /* across the line of sight (from 0.3 out of Ed's head: his own head and hair are around it) */
            if (kind != KIND_ED && len > 0.6f &&
                !(fminf(a.x, fminf(b.x, c.x)) > hi.x || fmaxf(a.x, fmaxf(b.x, c.x)) < lo.x ||
                  fminf(a.y, fminf(b.y, c.y)) > hi.y || fmaxf(a.y, fmaxf(b.y, c.y)) < lo.y ||
                  fminf(a.z, fminf(b.z, c.z)) > hi.z || fmaxf(a.z, fmaxf(b.z, c.z)) < lo.z)) {
                tc_vec3 o = tc_v3_add(head, tc_v3_scale(seg, 0.3f / len));
                if (segment_tri(o, tc_v3_sub(eye, o), a, b, c)) {
                    blocked[kind] = true;
                    if (getenv("BLOCK_DEBUG") && kind == KIND_PERSO) {
                        printf("      head (%.2f %.2f %.2f) eye (%.2f %.2f %.2f) hide %d\n", (double)head.x, (double)head.y,
                               (double)head.z, (double)eye.x, (double)eye.y, (double)eye.z, v->cam.hide_target);
                        uint32_t pi = 0, first = v->perso_first;
                        while (pi < v->level->perso_count && first + v->level->persos[pi].part_count <= d->mesh)
                            first += v->level->persos[pi].part_count, pi++;
                        printf("      blocker perso %u (mesh %u) flags 0x%x\n", pi, d->mesh, d->flags);
                    }
                }
            }
        }
    }
    for (k = 0; k < KIND_COUNT; k++) st->clipped[k] += clipped[k], st->blocked[k] += blocked[k];
    if ((clipped[KIND_LEVEL] || clipped[KIND_PERSO]) && !collision_near(col, eye, 0.4f)) st->clipped_no_col++;
    if (getenv("CLIP_DEBUG") && (clipped[KIND_LEVEL] || clipped[KIND_PERSO] || clipped[KIND_ED]) && st->frames % 7 == 0)
        printf("    clip f%d eye (%.2f %.2f %.2f) head (%.2f %.2f %.2f) len %.2f body %.2f speed %.2f nearest %.3f "
               "kinds %d%d%d sit %d\n", st->frames, (double)eye.x, (double)eye.y, (double)eye.z, (double)head.x,
               (double)head.y, (double)head.z, (double)len, (double)tc_v3_distance(v->cam.body, v->cam.pivot), (double)v->cam.speed, (double)nearest,
               clipped[0], clipped[1], clipped[2], tc_viewer_camera_situation(v));
    if ((blocked[KIND_LEVEL] || blocked[KIND_PERSO]) &&
        !(len > 0.6f && tc_collision_raycast(col, tc_v3_add(head, tc_v3_scale(seg, 0.3f / len)),
                                             tc_v3_scale(seg, 1 / len), len - 0.3f, &h)))
        st->blocked_no_col++;
    st->nearest = fminf(st->nearest, nearest);
}

static void clip_report(const char *level, const char *what, const clip_stats *st) {
    printf("  info  %s %s: %d frames (Ed hidden, the eye pulled into him: %d more); near plane in a drawn triangle: level %d, perso %d, Ed %d (%d of them with no "
           "collision near); something drawn between Ed's head and the eye: level %d, perso %d (%d with no collision "
           "there); nearest level / perso triangle %.3f\n",
           level, what, st->frames, st->hidden, st->clipped[KIND_LEVEL], st->clipped[KIND_PERSO], st->clipped[KIND_ED],
           st->clipped_no_col, st->blocked[KIND_LEVEL], st->blocked[KIND_PERSO], st->blocked_no_col,
           (double)st->nearest);
}

/* A walk with Ed's AI as a player would make it (the follow camera, not the demo's): forward with turns, back towards
 * the camera, jumps, into corners. */
static const struct { tc_view_action a, b; int frames; } WALK[] = {
    {TC_VA_FORWARD, TC_VA_COUNT, 180}, {TC_VA_FORWARD, TC_VA_LEFT, 120}, {TC_VA_LEFT, TC_VA_COUNT, 120},
    {TC_VA_BACK, TC_VA_COUNT, 150},    {TC_VA_RIGHT, TC_VA_JUMP, 90},    {TC_VA_FORWARD, TC_VA_RIGHT, 150},
    {TC_VA_FORWARD, TC_VA_JUMP, 60},   {TC_VA_BACK, TC_VA_LEFT, 120},    {TC_VA_FORWARD, TC_VA_COUNT, 240},
    {TC_VA_LEFT, TC_VA_COUNT, 90},     {TC_VA_BACK, TC_VA_COUNT, 200},   {TC_VA_RIGHT, TC_VA_COUNT, 200},
};

static void level_clip(const char *dir, const char *name) {
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level level;
    tc_viewer v;
    tc_viewer_desc desc;
    tc_collision col;
    clip_stats demo, walk;
    int i, j, rep;
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
    memset(&demo, 0, sizeof(demo));
    memset(&walk, 0, sizeof(walk));
    demo.nearest = walk.nearest = 1e9f;
    if (tc_viewer_init(&v, &desc) != TC_OK) { free(ai); tc_collision_free(&col); tc_level_free(&level); return; }
    for (i = 0; i < 60; i++) { /* settle */
        uint32_t c, o;
        tc_viewer_update(&v, 1.0f / 60);
        tc_viewer_draws(&v, &c, &o);
    }
    for (rep = 0; rep < 2; rep++)
        for (j = 0; j < (int)(sizeof(WALK) / sizeof(WALK[0])); j++) {
            for (i = 0; i < WALK[j].frames; i++) {
                tc_viewer_set_action(&v, WALK[j].a, true);
                if (WALK[j].b != TC_VA_COUNT) tc_viewer_set_action(&v, WALK[j].b, (i / 10) % 3 != 2);
                tc_viewer_update(&v, 1.0f / 60);
                walk.hidden += v.cam.hide_target;
                if (v.ed_draw_count == 0 && i > 0) continue;
                clip_frame(&v, &col, pivot_of(tc_viewer_player(&v)->position), &walk);
            }
            tc_viewer_release_all(&v);
        }
    tc_viewer_free(&v);
    if (tc_viewer_init(&v, &desc) == TC_OK) {
        tc_viewer_set_demo(&v, true, 4242);
        for (i = 0; i < 60 * 60; i++) {
            uint32_t c, o;
            tc_viewer_update(&v, 1.0f / 60);
            tc_viewer_draws(&v, &c, &o);
            demo.hidden += v.cam.hide_target;
            if (v.ed_draw_count == 0) continue;
            clip_frame(&v, &col, pivot_of(tc_viewer_player(&v)->position), &demo);
        }
        tc_viewer_free(&v);
    }
    free(ai);
    clip_report(name, "walk (follow camera)", &walk);
    clip_report(name, "demo 60 s", &demo);
    tc_collision_free(&col);
    tc_level_free(&level);
}

/* Ed's AI walking as a player would (towards the camera, sideways, away): the camera's yaw must not jump or shake. */
static void jitter_phase(tc_viewer *v, const char *level, const char *name, tc_view_action a, int frames) {
    float prev = v->camera.yaw, largest = 0, largest_swing = 0, last = 0, last_swing = 0;
    int i, reversals = 0, swing_reversals = 0, swing_frames = 0;
    char what[256];
    for (i = 0; i < frames; i++) {
        uint32_t c, o;
        float d;
        bool swinging;
        tc_viewer_set_action(v, a, true);
        tc_viewer_update(v, 1.0f / 60);
        tc_viewer_draws(v, &c, &o);
        d = remainderf(v->camera.yaw - prev, 2 * TC_PI);
        prev = v->camera.yaw;
        swinging = v->cam.swinging;
        if (getenv("JIT_DEBUG") && (swinging || i % 15 == 0))
            printf("    %s %s f%d yaw %.4f d %.4f eye-head %.2f body-pivot %.2f speed %.1f look-pivot %.2f reach %.2f hide %d swing %d\n",
                   level, name, i, (double)v->camera.yaw, (double)d,
                   (double)tc_v3_distance(v->camera.position, tc_v3_add(tc_viewer_player(v)->position, tc_v3(0, 0, 1.35f))),
                   (double)tc_v3_distance(v->cam.body, v->cam.pivot), (double)v->cam.speed,
                   (double)tc_v3_distance(v->cam.look, v->cam.pivot), (double)v->cam.reach, v->cam.hide_target, swinging);
        if (swinging) { /* the watchdog's deliberate swing round to an open side: its own bound, no back and forth */
            swing_frames++;
            largest_swing = fmaxf(largest_swing, fabsf(d));
            /* a reversal you could see: both steps over 0.002 rad (the swing's start from a standstill's last 1e-3
             * drift is not one) */
            if (d * last_swing < 0 && fabsf(d) > 0.002f) swing_reversals++;
            if (fabsf(d) > 0.002f) last_swing = d;
            continue;
        }
        largest = fmaxf(largest, fabsf(d));
        if (d * last < 0 && fabsf(d) > 1e-4f) reversals++;
        if (fabsf(d) > 1e-5f) last = d;
    }
    tc_viewer_release_all(v);
    snprintf(what, sizeof(what), "%s, Ed's AI %s: largest yaw step %.4f rad / frame, %d reversals; watchdog swing %d frames "
             "(largest step %.4f, %d reversals)", level, name, (double)largest, reversals, swing_frames,
             (double)largest_swing, swing_reversals);
    check(largest < 0.03f && reversals <= 3 && largest_swing < 0.1f && swing_reversals == 0, what);
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
        const char *clip_levels[] = {"bar", "sud0", "north", "Land", "cavedoc", "pyramide"};
        size_t k;
        printf("the demo on levels (the line of sight, both faces):\n");
        for (k = 0; k < sizeof(levels) / sizeof(levels[0]); k++) level_demo(argv[1], levels[k]);
        printf("no shaking (2026-11-01: the user saw it jitter as Ed walked towards the camera):\n");
        level_jitter(argv[1], "bar");
        level_jitter(argv[1], "north");
        printf("clipping (what is drawn against the eye, not only the collision):\n");
        for (k = 0; k < sizeof(clip_levels) / sizeof(clip_levels[0]); k++) level_clip(argv[1], clip_levels[k]);
    }
    printf(failures ? "FAIL\n" : "ok\n");
    return failures ? 1 : 0;
}
