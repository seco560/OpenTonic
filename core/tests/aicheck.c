/* Headless run of Ed's AI on a level's collision (tc_ed_ai = tc_ai + tc_player; no window): loads <level>.ttlv
 * and <level>.ttai, feeds a scripted button sequence, prints every behaviour, reflex, state and mechanics-mode
 * change with Ed's position, then the primitives the interpreter skipped.
 * Usage: aicheck [out/ttlv] [level]  (default ../files/out/ttlv bar); AI_TRACE=<frame> prints executed nodes.
 * Test-only stdio. */
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

static int tracing;
static void trace(void *user, uint32_t node, uint32_t flags) {
    (void)user;
    if (tracing) printf("          node %u flags %#x\n", node, flags);
}

typedef struct step { float until; uint32_t inputs; float sx, sy; const char *what; } step;

static const char *beh_name(const tc_ai_model *m, uint32_t b, char *buf) {
    if (b == 0xFFFF) return "none";
    if (b < m->normal_count) snprintf(buf, 16, "B%u", b);
    else snprintf(buf, 16, "R%u", b - m->normal_count);
    return buf;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv", *level_name = argc > 2 ? argv[2] : "bar";
    char path[1024], b1[16], b2[16];
    size_t size;
    uint8_t *data;
    tc_ai_model model;
    tc_level level;
    tc_collision world;
    tc_player player;
    tc_ed_ai ed;
    tc_ai_perso *p;
    tc_result r;
    tc_mat34 start;
    uint32_t i, k, frame, last_changes = 0;
    uint16_t last_normal = 0xFFFE, last_reflex = 0xFFFE, last_wanted = 0xFFFF;
    int32_t last_mode = -2;
    const float dt = 1.0f / 60;
    const step script[] = {
        {1.0f, 0, 0, 0, "idle"},
        {2.0f, 1u << TC_AI_IN_UP, 0, 0, "hold up (run)"},
        {3.0f, (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_SPEEDUP), 0, 0, "add speedup (run -> walk)"},
        {3.5f, 0, 0, 0, "release"},
        {3.6f, 1u << TC_AI_IN_JUMP, 0, 0, "jump"},
        {5.0f, 0, 0, 0, "release"},
        {7.0f, (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_SPEEDUP), 0, 0, "hold up + speedup (walk)"},
        {8.0f, 1u << TC_AI_IN_LEFT, 0, 0, "hold left"},
        {9.0f, 0, 0, 0.9f, "stick forward"},
        {9.1f, 1u << TC_AI_IN_ACTION, 0, 0, "action"},
        {10.0f, 0, 0, 0, "release"},
        {10.1f, 1u << TC_AI_IN_SHOOT, 0, 0, "shoot"},
        {12.0f, 0, 0, 0, "release"},
    };

    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, level_name);
    data = read_file(path, &size);
    if (!data) { fprintf(stderr, "cannot read %s\n", path); return 1; }
    r = tc_level_load(&level, data, size);
    free(data);
    if (r != TC_OK || !level.has_ed) { fprintf(stderr, "%s: %s / no Ed\n", path, tc_result_string(r)); return 1; }
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, level_name);
    data = read_file(path, &size);
    if (!data) { fprintf(stderr, "cannot read %s\n", path); return 1; }
    r = tc_ai_model_load(&model, data, size);
    free(data);
    if (r != TC_OK) { fprintf(stderr, "%s: %s\n", path, tc_result_string(r)); return 1; }
    printf("%s: %u nodes, %u scripts, %u behaviours + %u reflexes, %u dsgvars, %u states, %u buttons, %u others\n",
           path, model.node_count, model.script_count, model.normal_count, model.reflex_count, model.dsg.count,
           model.state_count, model.button_count, model.other_count);
    if (tc_collision_init(&world, level.collision, 3 * level.collision_triangle_count, level.collision_floors) != 0) return 1;
    start = level.ed.start;
    tc_player_init(&player, &world, tc_v3(start.m[3], start.m[7], start.m[11]), tc_heading_of(&start));
    tc_player_apply_floors(&player); /* as the viewer does (sliding profile on zero-grip levels) */
    if (tc_ed_ai_init(&ed, &model, &player, level_name) != TC_OK) { fprintf(stderr, "init failed\n"); return 1; }
    p = &ed.ai;
    p->trace = trace;
    tracing = getenv("AI_TRACE") != NULL;

    for (frame = 0, k = 0; frame < (uint32_t)(script[sizeof(script) / sizeof(script[0]) - 1].until / dt); frame++) {
        float t = frame * dt;
        tc_vec3 cam;
        while (t >= script[k].until) {
            k++;
            printf("%6.2f  -- %s\n", (double)t, script[k].what);
        }
        if (frame == 0) printf("%6.2f  -- %s\n", 0.0, script[0].what);
        if (tracing && frame >= (uint32_t)atoi(getenv("AI_TRACE")) + 3) tracing = 0;
        if (tracing && frame >= (uint32_t)atoi(getenv("AI_TRACE"))) printf("frame %u\n", frame);
        /* camera 4.5 behind the start heading, following Ed's position (does not swing round) */
        cam = tc_v3_add(player.position, tc_v3(-4.5f * sinf(tc_heading_of(&start)), 4.5f * cosf(tc_heading_of(&start)), 1.5f));
        tc_ed_ai_step(&ed, dt, script[k].inputs, script[k].sx, script[k].sy, cam);

        if (p->normal.behaviour != last_normal || p->reflex.behaviour != last_reflex) {
            printf("%6.2f  behaviour %s  reflex %s\n", (double)t, beh_name(&model, p->normal.behaviour, b1),
                   beh_name(&model, p->reflex.behaviour, b2));
            last_normal = p->normal.behaviour;
            last_reflex = p->reflex.behaviour;
        }
        if (p->state_changes != last_changes || p->wanted_state != last_wanted) {
            printf("%6.2f  state %u (wanted %u) anim s%u rate %u  pos (%.2f, %.2f, %.2f) v %.2f %s\n", (double)t, p->state,
                   p->wanted_state, tc_ed_ai_anim(&ed), p->frame_rate, (double)player.position.x, (double)player.position.y,
                   (double)player.position.z, (double)tc_player_horizontal_speed(&player), player.grounded ? "ground" : "air");
            last_changes = p->state_changes;
            last_wanted = p->wanted_state;
        }
        if (p->motion.mech_mode != last_mode) {
            printf("%6.2f  mech mode %d\n", (double)t, p->motion.mech_mode);
            last_mode = p->motion.mech_mode;
        }
        if (frame % 30 == 29)
            printf("%6.2f      pos (%.2f, %.2f, %.2f) v %.2f vz %.2f heading %.0f %s\n", (double)t, (double)player.position.x,
                   (double)player.position.y, (double)player.position.z, (double)tc_player_horizontal_speed(&player),
                   (double)player.velocity.z, (double)(player.heading * 180 / TC_PI), player.grounded ? "ground" : "air");
    }
    printf("skipped primitives:");
    for (i = 0; i < 10 * TC_AI_UNKNOWN_STRIDE; i++)
        if (p->unknown[i]) printf(" %u_%u(%u)", i / TC_AI_UNKNOWN_STRIDE, i % TC_AI_UNKNOWN_STRIDE, p->unknown[i]);
    printf("\n");
    tc_ed_ai_free(&ed);
    tc_collision_free(&world);
    tc_ai_model_free(&model);
    tc_level_free(&level);
    return 0;
}
