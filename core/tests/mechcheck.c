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

/* A box on the floor from x0 to x1 (any y in -5..5), h high; faces wound both ways on purpose. */
static void box(float x0, float x1, float h) {
    quad(tc_v3(x0, -5, h), tc_v3(x1, -5, h), tc_v3(x1, 5, h), tc_v3(x0, 5, h));
    quad(tc_v3(x0, -5, 0), tc_v3(x0, 5, 0), tc_v3(x0, 5, h), tc_v3(x0, -5, h));
    quad(tc_v3(x1, -5, h), tc_v3(x1, 5, h), tc_v3(x1, 5, 0), tc_v3(x1, -5, 0));
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
    const tc_ai_sphere_set body = {4, {{{0, 0, 0.25f}, 0.25f}, {{0, -0.1f, 0.75f}, 0.375f},
                                       {{0, -0.15f, 1.28f}, 0.53f}, {{0, -0.3f, 1.95f}, 0.3f}}};
    tc_ai_card g = card_of(0, ground, 15), air = card_of(4, fall, 15);
    const float heights[3] = {0.5f, 0.9f, 1.5f};
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

    for (k = 0; k < 3; k++) {
        float h = heights[k];
        vertex_count = 0;
        quad(tc_v3(-50, -50, 0), tc_v3(50, -50, 0), tc_v3(50, 50, 0), tc_v3(-50, 50, 0));
        box(2, 6, h);
        tc_collision_init(&w, vertices, (uint32_t)vertex_count, NULL);
        p = run(&w, &g, &body, tc_v3(0, 0, 0.01f), 0.45f, &mw);
        if (h < 1) {
            snprintf(what, sizeof(what), "step %.1f (card +8 = 1): on top after 0.45 s, x %.2f z %.2f", (double)h, (double)p.x,
                     (double)p.z);
            check(p.x > 2 && p.z > h - 0.02f && p.z < h + 0.05f, what);
            p = run(&w, &g, &body, tc_v3(0, 0, 0.01f), 1.5f, &mw);
            snprintf(what, sizeof(what), "step %.1f: across and down the far side, x %.2f z %.2f", (double)h, (double)p.x,
                     (double)p.z);
            check(p.x > 9 && p.z < 0.05f, what);
        } else {
            snprintf(what, sizeof(what), "wall %.1f: stops in front of it, x %.2f z %.2f, no blocked frames (%u)", (double)h,
                     (double)p.x, (double)p.z, mw.blocked);
            check(p.x < 2 && p.x > 1.2f && p.z < 0.05f && mw.blocked == 0, what);
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

    printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures != 0;
}
