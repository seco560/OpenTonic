/* Checks of the type 0 / 4 mechanics (tc_mec_walk) on hand-made geometry, no game data needed: Ed's ground card and
 * standing spheres as exported from the game. Run: make mechcheck. Test-only stdio. */
#include <stdio.h>
#include <string.h>
#include "tonic/tonic.h"

static int vertex_count, failures;
static tc_vec3 vertices[600];

static void quad(tc_vec3 a, tc_vec3 b, tc_vec3 c, tc_vec3 d) {
    tc_vec3 q[6] = {a, b, c, a, c, d};
    memcpy(&vertices[vertex_count], q, sizeof(q));
    vertex_count += 6;
}

/* A box on the floor from x0 to x1 (any y in -5..5), h high; its faces point out, as the levels' do (a face collides
 * only from its front, FUN_00465b60). */
static void box(float x0, float x1, float h) {
    quad(tc_v3(x0, -5, h), tc_v3(x1, -5, h), tc_v3(x1, 5, h), tc_v3(x0, 5, h));
    quad(tc_v3(x0, -5, h), tc_v3(x0, 5, h), tc_v3(x0, 5, 0), tc_v3(x0, -5, 0));
    quad(tc_v3(x1, -5, 0), tc_v3(x1, 5, 0), tc_v3(x1, 5, h), tc_v3(x1, -5, h));
}

static tc_ai_card card_of(uint32_t type, const float *f, uint32_t n) {
    tc_ai_card c;
    uint32_t i;
    memset(&c, 0, sizeof(c));
    c.type = type;
    c.word_count = n;
    for (i = 0; i < n; i++) memcpy(&c.words[i], &f[i], 4);
    return c;
}

static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* Swimming (card type 1): one sector listing a water surface at z 0 (env 0 the air, env 1 water below it, as the levels'
 * water sectors); Ed's main swim card with the given mass. mode / speed / dir: the request each frame (speed = proposal
 * [0], dir = proposal [7..9]). Returns the end position; *v the velocity. */
static float swim_lo, swim_hi, swim_mean; /* z over the last 10 s of a swim() */

static tc_vec3 swim(float mass, float z, float seconds, int mode, float speed, tc_vec3 dir, float fps, tc_vec3 *v,
                    tc_mec_swim *s) {
    static float surface_z[1] = {0};
    static uint16_t surfaces[1] = {0};
    static tc_sector_env envs[2] = {{9.81f, 1.3f, 1, 0, 0, 0, 0, {0}}, {9.81f, 1000, 1, 1, 0, 0, 0, {0}}};
    tc_sector sector;
    tc_sectors m;
    float f[17] = {990, 0, 0.1f, 0.1f, 0.1f, 0.1f, 2.5f, 2.5f, 2000, 1, 1, 0, 10, 0.99f, 160, 1, 0}, prop[32] = {0}, depth;
    tc_ai_card card;
    tc_vec3 p = tc_v3(0, 0, z);
    int i;
    f[0] = mass;
    card = card_of(1, f, 17);
    memset(&m, 0, sizeof(m));
    memset(&sector, 0, sizeof(sector));
    sector.env_count = 2;
    sector.envs = envs;
    sector.surface_count = 1;
    sector.surfaces = surfaces;
    m.count = 1;
    m.sectors = &sector;
    m.surface_count = 1;
    m.surface_z = surface_z;
    prop[0] = speed;
    prop[7] = dir.x, prop[8] = dir.y, prop[9] = dir.z;
    tc_mec_swim_enter(s, TC_PI / 2); /* facing +x */
    *v = tc_v3(0, 0, 0);
    swim_lo = 1e9f, swim_hi = -1e9f, swim_mean = 0;
    for (i = 0; i < (int)(seconds * fps); i++) {
        tc_mec_swim_request(s, &card, mode, prop, 1 / fps);
        *v = tc_mec_swim_velocity(s, &card, &m, 0, p, *v, 1 / fps, &depth);
        p = tc_v3_add(p, tc_v3_scale(*v, 1 / fps));
        if (i >= (int)((seconds - 10) * fps)) {
            swim_lo = fminf(swim_lo, p.z);
            swim_hi = fmaxf(swim_hi, p.z);
            swim_mean += p.z / (10 * fps);
        }
    }
    return p;
}

/* Run along +x at 9 u/s for `seconds` (heading pi/2: Ed's front, local -Y, along +x). */
static tc_vec3 run(tc_collision *w, const tc_ai_card *card, const tc_ai_sphere_set *body, tc_vec3 start, float seconds,
                   tc_mec_walk *mw) {
    tc_vec3 pos = start, vel = tc_v3(0, 0, 0);
    int i;
    tc_mec_walk_reset(mw);
    mw->report = TC_AI_CONTACT_GROUND;
    for (i = 0; i < (int)(seconds * 60); i++)
        tc_mec_walk_step(mw, card, tc_mec_env_default(), w, body, TC_PI / 2, tc_v3(9, 0, 0), false, NULL, 1.0f / 60,
                         &pos, &vel);
    return pos;
}

int main(void) {
    /* Ed's ground card (state 1) and fall card (44), words after the type: mass, step height, ..., gravity factor */
    static const float ground[15] = {120, 1, 0, 0, 50, 192, 0.6428f, 0.342f, -0.5f, -0.9962f, 0.1219f, 0.9397f, 0.4f, 1, 0};
    static const float fall[15] = {120, 0, 0, 0, 100, 628.3f, 0.6428f, 0.342f, -0.5f, -0.9848f, 0.9998f, 0.9998f, 0.3f, 3, 0};
    /* Ed standing (bar's ZDM zones 0, 1, 16, 2), with their collide flags: the feet may step up (1) */
    /* (and their own mechanics materials: rebound, grip; the feet and head have none: 1, 1) */
    const tc_ai_sphere_set body = {4, {{{0, 0, 0.25f}, 0.25f, 1, 0, 1, 1}, {{0, -0.1f, 0.75f}, 0.375f, 2, 0, 0.85f, 0},
                                       {{0, -0.15f, 1.28f}, 0.53f, 2, 0, 0.85f, 0}, {{0, -0.3f, 1.95f}, 0.3f, 4, 0, 1, 1}}};
    tc_ai_card g = card_of(0, ground, 15), air = card_of(4, fall, 15);
    /* Steps: only a flag 1 sphere (the feet) may step up (MEC_004bbc4c), so Ed walks up what his feet meet first: up
     * to about 0.45. Higher, his middle sphere (flag 2, own grip 0) meets the step's edge first: no step-up, and as a
     * ground contact it has no grip (obstacle x own), so he stops in front of it. */
    const float heights[4] = {0.3f, 0.45f, 0.5f, 1.5f};
    tc_collision w;
    tc_mec_walk mw;
    tc_vec3 p, v;
    char what[160];
    int k, i;
    float t;

    printf("mechcheck: Ed's ground card and standing spheres\n");
    vertex_count = 0;
    quad(tc_v3(-50, -50, 0), tc_v3(50, -50, 0), tc_v3(50, 50, 0), tc_v3(-50, 50, 0));
    tc_collision_init(&w, vertices, (uint32_t)vertex_count, NULL);
    p = run(&w, &g, &body, tc_v3(0, 0, 0.01f), 1, &mw);
    snprintf(what, sizeof(what), "flat floor: 1 s at 9 u/s -> x %.2f (9), z %.3f, ground kept", (double)p.x, (double)p.z);
    check(p.x > 8.8f && p.x < 9.2f && p.z > 0 && p.z < 0.05f && (tc_mec_walk_contacts(&mw) & TC_AI_CONTACT_GROUND), what);
    tc_collision_free(&w);

    for (k = 0; k < 4; k++) {
        float h = heights[k];
        vertex_count = 0;
        quad(tc_v3(-50, -50, 0), tc_v3(50, -50, 0), tc_v3(50, 50, 0), tc_v3(-50, 50, 0));
        box(2, 6, h);
        tc_collision_init(&w, vertices, (uint32_t)vertex_count, NULL);
        p = run(&w, &g, &body, tc_v3(0, 0, 0.01f), 0.45f, &mw);
        if (h < 0.48f) {
            snprintf(what, sizeof(what), "step %.2f (card +8 = 1): on top after 0.45 s, x %.2f z %.2f", (double)h,
                     (double)p.x, (double)p.z);
            check(p.x > 2 && p.z > h - 0.02f && p.z < h + 0.05f, what);
            p = run(&w, &g, &body, tc_v3(0, 0, 0.01f), 1.5f, &mw);
            snprintf(what, sizeof(what), "step %.2f: across and down the far side, x %.2f z %.2f", (double)h, (double)p.x,
                     (double)p.z);
            check(p.x > 9 && p.z < 0.05f, what);
        } else {
            snprintf(what, sizeof(what), "step / wall %.2f: stops in front of it (at most on its edge), x %.2f z %.2f, "
                     "no blocked frames (%u)", (double)h, (double)p.x, (double)p.z, mw.blocked);
            check(p.x < 2 && p.x > 1.2f && p.z < 0.2f && mw.blocked == 0, what);
        }
        tc_collision_free(&w);
    }

    /* Falling 5 units on the fall card (gravity x 3): about 0.58 s, then a ground contact */
    vertex_count = 0;
    quad(tc_v3(-50, -50, 0), tc_v3(50, -50, 0), tc_v3(50, 50, 0), tc_v3(-50, 50, 0));
    tc_collision_init(&w, vertices, (uint32_t)vertex_count, NULL);
    tc_mec_walk_reset(&mw);
    p = tc_v3(0, 0, 5);
    v = tc_v3(0, 0, 0);
    for (i = 0, t = 0; i < 120; i++) {
        tc_mec_walk_air(&mw);
        tc_mec_walk_step(&mw, &air, tc_mec_env_default(), &w, &body, 0, tc_v3(0, 0, 0), true, NULL, 1.0f / 60, &p, &v);
        if (mw.report & TC_AI_CONTACT_GROUND) break;
        t += 1.0f / 60;
    }
    snprintf(what, sizeof(what), "fall 5 on the fall card: lands after %.2f s (0.58), z %.3f", (double)t, (double)p.z);
    check(t > 0.5f && t < 0.66f && p.z >= 0 && p.z < 0.05f, what);
    tc_collision_free(&w);

    /* Swimming: Ed's 990 card floats up and settles with 0.99 of a unit of volume under (origin -0.49); 1100 sinks at
     * the drag's terminal speed sqrt(100 g / 1000) = 0.99; above the water the body falls; a thrust along +x moves him
     * there with the head (model z) along it. The same at 30 and 144 fps. */
    {
        static const float rates[2] = {30, 144};
        int k;
        for (k = 0; k < 2; k++) {
            tc_mec_swim sw;
            tc_vec3 x, y, z, sv, sp;
            float r = rates[k];
            swim(990, -3, 30, 0, 0, tc_v3(0, 0, 0), r, &sv, &sw);
            snprintf(what, sizeof(what), "swim %g fps: 990 floats up from -3 and bobs at the surface: z %.3f .. %.3f, "
                     "mean %.3f (-0.49)", (double)r, (double)swim_lo, (double)swim_hi, (double)swim_mean);
            check(swim_mean > -0.53f && swim_mean < -0.45f && swim_lo > -0.7f && swim_hi < -0.3f, what);
            sp = swim(1100, -20, 20, 0, 0, tc_v3(0, 0, 0), r, &sv, &sw);
            snprintf(what, sizeof(what), "swim %g fps: 1100 sinks, speed %.3f (0.99)", (double)r, (double)-sv.z);
            check(sp.z < -30 && -sv.z > 0.95f && -sv.z < 1.03f, what);
            sp = swim(990, 5, 0.5f, 0, 0, tc_v3(0, 0, 0), r, &sv, &sw);
            snprintf(what, sizeof(what), "swim %g fps: in the air it falls, vz %.2f after 0.5 s (-4.9)", (double)r, (double)sv.z);
            check(sv.z < -4.7f && sv.z > -5.0f, what);
            sp = swim(990, -2, 5, 20, 2000, tc_v3(1, 0, 0), r, &sv, &sw);
            tc_mec_swim_model_axes(&sw, &(tc_ai_card){0}, &x, &y, &z);
            snprintf(what, sizeof(what), "swim %g fps: thrust along +x: x %.1f, polar %.0f deg, head (%.2f, %.2f, %.2f), back z %.2f",
                     (double)r, (double)sp.x, (double)sw.polar, (double)z.x, (double)z.y, (double)z.z, (double)y.z);
            check(sp.x > 1 && fabsf(sp.y) < 0.01f && sw.polar > 89 && sw.polar < 91 && z.x > 0.99f && y.z > 0.99f, what);
        }
    }

    printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures != 0;
}
