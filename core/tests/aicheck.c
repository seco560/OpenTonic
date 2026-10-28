/* Headless run of Ed's AI on a level's collision (tc_ed_ai = tc_ai + tc_player; no window): loads <level>.ttlv
 * and <level>.ttai, feeds a scripted button sequence, prints every behaviour, reflex, state and mechanics-mode
 * change with Ed's position, then the primitives the interpreter skipped.
 * Usage: aicheck [out/ttlv] [level]  (default ../files/out/ttlv bar); AI_TRACE=<frame> prints executed nodes.
 * AI_SCRIPT=fly | ski | pogo | swim | hang | shimmy | airturn | wander picks another input script (wander: 60 s of running, turning and jumping, then a
 * summary: distance, largest step, teleports, stalls, frames the collide loop gave up); AI_FPS=<n> sets the tick rate
 * (default 30, the game's: TC_ED_AI_TICK); AI_FROM=<seconds> prints every frame's position, velocity and contacts for 0.25 s from then;
 * AI_START=x,y,z starts Ed there (AI_SCRIPT=swim with a point in sud0's river); AI_PATH=<file> writes every frame's
 * time, position, state and heading.
 * Test-only stdio. */
#include <math.h>
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
static FILE *path_log; /* AI_PATH */
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
    float start_heading;
    uint32_t i, k, frame, last_changes = 0;
    uint16_t last_normal = 0xFFFE, last_reflex = 0xFFFE, last_wanted = 0xFFFF;
    int32_t last_mode = -2;
    int quiet = 0, respawns = 0, stalls = 0, board_frames = 0, board_under = 0;
    float board_min = 1e9f, board_sum = 0, fly_head = 0, fly_off = 0;
    int fly_frames = 0, steps_generic = 0, steps_by[24] = {0};
    float travelled = 0, max_step = 0, stalled_for = 0;
    tc_vec3 before;
    const float dt = getenv("AI_FPS") ? 1.0f / (float)atof(getenv("AI_FPS")) : TC_ED_AI_TICK; /* the game's tick */
    const step base_script[] = {
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
    /* AI_SCRIPT=fly: jump, jump again in the air (Ed's B89 -> B26 flight), then steer. */
    /* AI_SCRIPT=swim (with AI_START in water): float, then swim forward, turn, dive (Down), stop. */
    const step swim_script[] = {
        {3.0f, 0, 0, 0, "idle (in the water)"},
        {7.0f, 1u << TC_AI_IN_UP, 0, 0, "hold up (swim forward)"},
        {8.5f, (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_LEFT), 0, 0, "hold up + left"},
        {10.0f, 1u << TC_AI_IN_JUMP, 0, 0, "hold jump"},
        {13.0f, 0, 0, 0, "release"},
        {16.0f, 1u << TC_AI_IN_ACTION, 0, 0, "hold action (dive?)"},
        {19.0f, (1u << TC_AI_IN_ACTION) | (1u << TC_AI_IN_UP), 0, 0, "hold action + up"},
        {21.0f, 1u << TC_AI_IN_DOWN, 0, 0, "hold down"},
        {23.0f, 1u << TC_AI_IN_SPEEDUP, 0, 0, "hold speedup"},
        {26.0f, 0, 0, 0, "release"},
    };
    const step fly_script[] = {
        {1.0f, 0, 0, 0, "idle"},
        {1.1f, 1u << TC_AI_IN_JUMP, 0, 0, "jump"},
        {1.4f, 0, 0, 0, "release"},
        {1.5f, 1u << TC_AI_IN_JUMP, 0, 0, "jump in the air (fly)"},
        {2.5f, 0, 0, 0, "release"},
        {5.0f, 1u << TC_AI_IN_UP, 0, 0, "hold up"},
        {6.0f, 1u << TC_AI_IN_LEFT, 0, 0, "hold left"},
        {7.0f, 1u << TC_AI_IN_DOWN, 0, 0, "hold down"},
        {10.0f, 0, 0, 0, "release"},
    };
    /* AI_SCRIPT=pogo: jump, Action in the air (Ed's B89 -> B23 + R22 pogo), hold it, then bounce forward. */
    const step pogo_script[] = {
        {1.0f, 0, 0, 0, "idle"},
        {1.1f, 1u << TC_AI_IN_JUMP, 0, 0, "jump"},
        {1.4f, 0, 0, 0, "release"},
        {1.6f, 1u << TC_AI_IN_ACTION, 0, 0, "action in the air (pogo)"},
        {4.0f, 1u << TC_AI_IN_ACTION, 0, 0, "hold action"},
        {6.0f, (1u << TC_AI_IN_ACTION) | (1u << TC_AI_IN_UP), 0, 0, "action + up"},
        {8.0f, 0, 0, 0, "release"},
    };
    /* AI_SCRIPT=ski (Totalski, skymap): the skier waits for Jump, then rides; carve, Action, jump. */
    const step ski_script[] = {
        {1.0f, 0, 0, 0, "wait"},
        {1.2f, 1u << TC_AI_IN_JUMP, 0, 0, "jump (start)"},
        {4.0f, 0, 0, 0, "ride"},
        {5.0f, 1u << TC_AI_IN_LEFT, 0, 0, "hold left"},
        {6.0f, 1u << TC_AI_IN_RIGHT, 0, 0, "hold right"},
        {7.0f, 1u << TC_AI_IN_ACTION, 0, 0, "hold action"},
        {7.1f, 1u << TC_AI_IN_JUMP, 0, 0, "jump"},
        {12.0f, 0, 0, 0, "ride"},
    };
    /* AI_SCRIPT=hang / shimmy (AI_START a few units from a ledge 2.3-3 high, facing it): run, jump, his hand point
     * lands on the ledge (GroundMaterialIs(8): B19 -> B82, hanging); then climb (up: B43, states 153-154) or shimmy
     * (left). */
    const step hang_script[] = {
        {1.0f, 0, 0, 0, "idle"},
        {1.3f, 1u << TC_AI_IN_UP, 0, 0, "run at the ledge"},
        {1.37f, (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_JUMP), 0, 0, "jump (a tap: a held jump clears these ledges)"},
        {1.9f, 1u << TC_AI_IN_UP, 0, 0, "hold up"},
        {3.5f, 0, 0, 0, "release (hang)"},
        {3.8f, 1u << TC_AI_IN_UP, 0, 0, "up (climb)"},
        {7.0f, 0, 0, 0, "release"},
    };
    const step shimmy_script[] = {
        {1.0f, 0, 0, 0, "idle"},
        {1.3f, 1u << TC_AI_IN_UP, 0, 0, "run at the ledge"},
        {1.37f, (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_JUMP), 0, 0, "jump (a tap: a held jump clears these ledges)"},
        {1.9f, 1u << TC_AI_IN_UP, 0, 0, "hold up"},
        {3.5f, 0, 0, 0, "release (hang)"},
        {4.5f, 1u << TC_AI_IN_RIGHT, 0, 0, "right (shimmy)"},
        {6.0f, 0, 0, 0, "release"},
    };
    /* AI_SCRIPT=airturn: run, jump, then steer left in the air (B89's air control: PROC 35 every frame). */
    const step airturn_script[] = {
        {1.0f, 0, 0, 0, "idle"},
        {1.5f, 1u << TC_AI_IN_UP, 0, 0, "run"},
        {1.6f, (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_JUMP), 0, 0, "jump"},
        {1.8f, 1u << TC_AI_IN_UP, 0, 0, "hold up"},
        {2.6f, 1u << TC_AI_IN_LEFT, 0, 0, "steer left in the air"},
        {3.5f, 0, 0, 0, "release"},
    };
    /* AI_SCRIPT=wander: 60 s of running with turns and jumps (collision stress test; prints a summary). */
    step wander_script[48];
    const step *script = base_script;
    size_t steps = sizeof(base_script) / sizeof(base_script[0]);
    if (getenv("AI_SCRIPT") && strcmp(getenv("AI_SCRIPT"), "fly") == 0) {
        script = fly_script;
        steps = sizeof(fly_script) / sizeof(fly_script[0]);
    }
    if (getenv("AI_SCRIPT") && strcmp(getenv("AI_SCRIPT"), "swim") == 0) {
        script = swim_script;
        steps = sizeof(swim_script) / sizeof(swim_script[0]);
    }
    if (getenv("AI_SCRIPT") && strcmp(getenv("AI_SCRIPT"), "pogo") == 0) {
        script = pogo_script;
        steps = sizeof(pogo_script) / sizeof(pogo_script[0]);
    }
    if (getenv("AI_SCRIPT") && strcmp(getenv("AI_SCRIPT"), "ski") == 0) {
        script = ski_script;
        steps = sizeof(ski_script) / sizeof(ski_script[0]);
    }
    if (getenv("AI_SCRIPT") && strcmp(getenv("AI_SCRIPT"), "hang") == 0) {
        script = hang_script;
        steps = sizeof(hang_script) / sizeof(hang_script[0]);
    }
    if (getenv("AI_SCRIPT") && strcmp(getenv("AI_SCRIPT"), "airturn") == 0) {
        script = airturn_script;
        steps = sizeof(airturn_script) / sizeof(airturn_script[0]);
    }
    if (getenv("AI_SCRIPT") && strcmp(getenv("AI_SCRIPT"), "shimmy") == 0) {
        script = shimmy_script;
        steps = sizeof(shimmy_script) / sizeof(shimmy_script[0]);
    }
    if (getenv("AI_SCRIPT") && strcmp(getenv("AI_SCRIPT"), "wander") == 0) {
        static const uint32_t pattern[6] = {1u << TC_AI_IN_UP, (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_LEFT),
                                            (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_JUMP), 1u << TC_AI_IN_UP,
                                            (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_RIGHT),
                                            (1u << TC_AI_IN_UP) | (1u << TC_AI_IN_SPEEDUP)};
        static const float lengths[6] = {2.0f, 0.45f, 0.3f, 1.5f, 0.7f, 1.2f};
        float until = 1.0f;
        wander_script[0] = (step){1.0f, 0, 0, 0, "idle"};
        for (i = 1; i < 48; i++) {
            until += lengths[i % 6];
            wander_script[i] = (step){until, pattern[i % 6], 0, 0, "wander"};
        }
        script = wander_script;
        steps = 48;
        quiet = 1;
    }

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
    tc_ai_model_link(&model, &level);
    printf("%s: %u nodes, %u scripts, %u behaviours + %u reflexes, %u dsgvars, %u states, %u buttons, %u others\n",
           path, model.node_count, model.script_count, model.normal_count, model.reflex_count, model.dsg.count,
           model.state_count, model.button_count, model.other_count);
    if (tc_collision_init(&world, level.collision, 3 * level.collision_triangle_count, level.collision_floors) != 0) return 1;
    start = level.ed.start;
    start_heading = NAN;
    if (getenv("AI_START")) { /* x,y,z[,heading in degrees]: start there instead (e.g. in the water) */
        float x, y, z;
        if (sscanf(getenv("AI_START"), "%f,%f,%f,%f", &x, &y, &z, &start_heading) >= 3)
            start.m[3] = x, start.m[7] = y, start.m[11] = z;
    }
    tc_player_init(&player, &world, tc_v3(start.m[3], start.m[7], start.m[11]),
                   start_heading == start_heading ? start_heading * TC_PI / 180 : tc_heading_of(&start));
    if (model.water_count) printf("water: %u surfaces\n", model.water_count);
    tc_player_apply_floors(&player); /* as the viewer does (sliding profile on zero-grip levels) */
    if (tc_ed_ai_init(&ed, &model, &player, level_name) != TC_OK) { fprintf(stderr, "init failed\n"); return 1; }
    p = &ed.ai;
    p->trace = trace;
    tracing = getenv("AI_TRACE") != NULL;
    path_log = getenv("AI_PATH") ? fopen(getenv("AI_PATH"), "w") : NULL; /* every frame: t, position, state, heading */

    for (frame = 0, k = 0; frame < (uint32_t)(script[steps - 1].until / dt); frame++) {
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
        {   /* the camera behind the start heading (AI_START's when given): "up" runs that way */
            float h = start_heading == start_heading ? start_heading * TC_PI / 180 : tc_heading_of(&start);
            cam = tc_v3_add(player.position, tc_v3(-4.5f * sinf(h), 4.5f * cosf(h), 1.5f));
        }
        before = player.position;
        tc_ed_ai_step(&ed, dt, script[k].inputs, script[k].sx, script[k].sy, cam);
        if (path_log) fprintf(path_log, "%.3f %.5f %.5f %.5f %u %.4f\n", (double)t, (double)player.position.x,
                          (double)player.position.y, (double)player.position.z, p->state, (double)player.heading);
        {
            float step_len = tc_v3_distance(before, player.position);
            if (step_len > 5) {
                respawns++;
                printf("%6.2f  ! jump of %.2f from (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n", (double)t, (double)step_len,
                       (double)before.x, (double)before.y, (double)before.z, (double)player.position.x,
                       (double)player.position.y, (double)player.position.z);
            } else {
                travelled += step_len;
                if (step_len > max_step) max_step = step_len;
            }
            if (quiet && (script[k].inputs & (1u << TC_AI_IN_UP)) && step_len < 0.3f * dt) {
                stalled_for += dt;
                if (stalled_for >= 2 && stalled_for - dt < 2) {
                    stalls++;
                    printf("%6.2f  ! stalled 2 s at (%.2f, %.2f, %.2f) state %u contacts %#x\n", (double)t,
                           (double)player.position.x, (double)player.position.y, (double)player.position.z, p->state,
                           ed.contacts);
                }
            } else {
                stalled_for = 0;
            }
        }
        if (ed.card_type == 2) { /* the board's clearance: snow along its down axis, minus the pose's reach */
            tc_hit hit;
            tc_vec3 down = tc_v3_scale(ed.ski.z, -1);
            float drop = model.states[p->state].drop, gap;
            if (tc_collision_raycast(&world, player.position, down, 3, &hit)) {
                gap = hit.distance - drop;
                board_frames++;
                board_sum += gap;
                if (gap < board_min) board_min = gap;
                if (gap < -0.02f) board_under++;
            }
        }
        if (getenv("AI_FROM") && t >= (float)atof(getenv("AI_FROM")) && t < (float)atof(getenv("AI_FROM")) + 0.25f) {
            printf("%6.3f  state %u frame %.2f rate %u pos (%.3f, %.3f, %.3f) v (%.2f, %.2f, %.2f) contacts %#x passes %u ground n (%.2f, %.2f, %.2f) own %#x wall n (%.2f, %.2f, %.2f) own %#x\n",
                   (double)t, p->state, (double)p->state_frame, p->frame_rate, (double)player.position.x, (double)player.position.y, (double)player.position.z,
                   (double)player.velocity.x, (double)player.velocity.y, (double)player.velocity.z, ed.contacts, ed.walk.passes,
                   (double)ed.walk.ground_normal.x, (double)ed.walk.ground_normal.y, (double)ed.walk.ground_normal.z,
                   ed.walk.ground_own, (double)ed.walk.wall_normal.x, (double)ed.walk.wall_normal.y,
                   (double)ed.walk.wall_normal.z, ed.walk.wall_own);
        }
        for (i = 0; i < p->sound_count; i++) { /* footsteps: the generic step, or by floor sound type */
            if (p->sounds[i] == TC_AI_FOOTSTEP_EVENT) steps_generic++;
            else if (p->sounds[i] > TC_AI_STEP_SOUND && p->sounds[i] < TC_AI_STEP_SOUND + 24) steps_by[p->sounds[i] - TC_AI_STEP_SOUND]++;
        }
        if (quiet) continue;

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
        if (ed.card_type == 5 && tc_v3_length(player.velocity) > 1 && !player.grounded && !player.against_wall) {
            /* the drawn flight pose: head (model z) along the velocity, axes a proper rotation */
            tc_mat4 mm = tc_ed_ai_model_matrix(&ed);
            tc_vec3 mx = tc_v3(mm.m[0], mm.m[1], mm.m[2]), my = tc_v3(mm.m[4], mm.m[5], mm.m[6]);
            tc_vec3 mz = tc_v3(mm.m[8], mm.m[9], mm.m[10]), vd = tc_v3_div(player.velocity, tc_v3_length(player.velocity));
            float head = acosf(fminf(1, fmaxf(-1, tc_v3_dot(mz, vd)))) * 180 / TC_PI;
            float off = fabsf(tc_v3_dot(tc_v3_cross(mx, my), mz) - 1) + fabsf(tc_v3_dot(mx, my)) + fabsf(tc_v3_dot(my, mz)) +
                        fabsf(tc_v3_dot(mx, mz)) + fabsf(tc_v3_length(mx) - 1) + fabsf(tc_v3_length(my) - 1);
            fly_frames++;
            if (head > fly_head) fly_head = head;
            if (off > fly_off) fly_off = off;
        }
        if (frame % 30 == 29) {
            printf("%6.2f      pos (%.2f, %.2f, %.2f) v %.2f vz %.2f heading %.0f %s%s", (double)t, (double)player.position.x,
                   (double)player.position.y, (double)player.position.z, (double)tc_player_horizontal_speed(&player),
                   (double)player.velocity.z, (double)(player.heading * 180 / TC_PI), player.grounded ? "ground" : "air",
                   player.against_wall ? " wall" : "");
            if (ed.card_type == 2)
                printf("  ski up (%.2f, %.2f, %.2f) grip %.2f%s", (double)ed.ski.z.x, (double)ed.ski.z.y, (double)ed.ski.z.z, (double)player.ground_grip,
                       ed.ski.near_ground ? " near" : "");
            if (ed.card_type == 5)
                printf("  fly attack %.1f bank %.1f", (double)ed.fly.attack, (double)ed.fly.bank);
            if (ed.card_type == 1)
                printf("  swim polar %.0f heading %.0f thrust %.0f", (double)ed.swim.polar, (double)ed.swim.heading,
                       (double)ed.swim.thrust_lagged);
            if (ed.env_depth > 0) printf("  depth %.2f", (double)ed.env_depth);
            printf("\n");
        }
    }
    if (quiet)
        printf("wander: travelled %.1f, largest step %.3f, jumps %d, stalls %d, blocked frames %u, end (%.2f, %.2f, %.2f) state %u\n",
               (double)travelled, (double)max_step, respawns, stalls, ed.walk.blocked, (double)player.position.x,
               (double)player.position.y, (double)player.position.z, p->state);
    if (board_frames)
        printf("board clearance over %d frames near the snow: mean %.3f, min %.3f, %d frames more than 0.02 into it\n",
               board_frames, (double)(board_sum / (float)board_frames), (double)board_min, board_under);
    {
        int any = steps_generic;
        for (i = 0; i < 24; i++) any += steps_by[i];
        if (any) {
            printf("footsteps: %d generic", steps_generic);
            for (i = 0; i < 24; i++) if (steps_by[i]) printf(", %d of floor sound %d", steps_by[i], (int)i);
            printf("\n");
        }
    }
    if (fly_frames)
        printf("flight pose over %d frames in the air: head vs velocity at most %.1f deg (the frame is built from the\n"
               "  velocity before the step), axes off a rotation by at most %.5f\n", fly_frames, (double)fly_head,
               (double)fly_off);
    if (model.trigger_count) printf("proximity triggers: %u, fired %u\n", model.trigger_count, ed.triggers_fired);
    printf("skipped primitives:");
    for (i = 0; i < 10 * TC_AI_UNKNOWN_STRIDE; i++)
        if (p->unknown[i]) printf(" %u_%u(%u)", i / TC_AI_UNKNOWN_STRIDE, i % TC_AI_UNKNOWN_STRIDE, p->unknown[i]);
    printf("\n");
    tc_ed_ai_free(&ed);
    tc_collision_free(&world);
    tc_ai_model_free(&model);
    if (path_log) fclose(path_log);
    tc_level_free(&level);
    return 0;
}
