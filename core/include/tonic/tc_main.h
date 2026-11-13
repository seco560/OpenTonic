/* The main character: the perso the player plays (the exe's main-character list, DAT_008c1720: one perso; docs/ENGINE.md
 * "The frame"). Input goes to it, the camera follows it, and the cast measures who is awake and who thinks from it.
 *
 * Today it is Ed: by his own scripts (tc_ed_ai: his driver, with the level's .ttai) or, without them, by the simple
 * controller (tc_player alone). Both move the same physics, `player`. Its tick is the game's: the cast's run-first persos
 * (lifts, platforms), then the main character, then the other persos (tc_main_tick), as the exe's frame orders them.
 *
 * Port, staged (docs/DIRECTION.md point 2, the user's plan of 2026-11-13): until 2026-11-13 the viewer held Ed's parts and
 * stepped him itself. Next: the main character as a slot that custom characters fill too (a car Ed gets into), then Ed
 * in the cast's own perso table and perso step, then the exe's tree order. */
#ifndef TONIC_TC_MAIN_H
#define TONIC_TC_MAIN_H
#include "tc_level.h"
#include "tc_player.h"
#include "tc_ai.h"
#include "tc_ed_ai.h"
#include "tc_cast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_main {
    const tc_level *level;
    const tc_sectors *sectors;   /* the level's (NULL: none) */
    tc_player player;            /* his physics, whoever drives it */
    tc_ai_model model;
    bool has_model;
    /* A Special Edition level that switches walking / skiing (.ttai MODZ + ALTM): the skier's model, which one drives
     * Ed now (TC_AI_MODE_*), and which single-shot zones have fired (tc_main_mode_zones). */
    tc_ai_model alt;
    bool has_alt;
    uint8_t mode;
    uint8_t zone_fired[64];
    tc_ed_ai driver;             /* his scripts; has_driver false: the simple controller only */
    bool has_driver;
    char level_name[128];        /* tc_ed_ai keeps a pointer to it */
    /* What to draw, after the last two ticks (tc_main_capture): his model matrix, his state's clip (level->ed.anims
     * index), its frame, its rate and whether it loops. */
    bool have_pose;
    tc_mat4 pose_prev, pose_cur;
    int clip_prev, clip_cur;
    float phase_prev, phase_cur;
    float rate_cur;              /* animation frames per second of the state after the last tick */
    bool loop_cur;
} tc_main;

/* The host's part of one tick. */
typedef struct tc_main_input {
    uint32_t inputs;             /* 1 << TC_AI_IN_* (0 while the host holds him back) */
    float stick_x, stick_y;      /* -1..1, y forward */
    tc_vec3 eye;                 /* the camera (his scripts make the direction keys camera-relative) */
    bool face;                   /* turn him to `heading` first (first person: where the player looks) */
    float heading;
    bool talk_press;             /* a fresh Action since the last tick, for the cast (TC_CAST_TALK) */
    bool hud_waits;              /* the start text shows (the host's HUD): the player presses Jump himself */
} tc_main_input;

/* Ed at the level's start (l->ed.start): his physics on `world` (must outlive him; he must not move once made: his
 * driver keeps pointers into him), his scripts when ai_data loads (*ai_result: why not, else TC_OK; untouched without
 * ai_data). The level must have Ed (tc_level.has_ed). */
void tc_main_init(tc_main *m, const tc_level *l, tc_collision *world, const void *ai_data, size_t ai_size,
                  const char *level_name, tc_result *ai_result);
void tc_main_free(tc_main *m);
/* The Special Edition's walk / ski switch (.ttai MODZ): his origin in a zone hands him to the other model. cast (NULL:
 * none) is handed the new driver. 0: nothing; 1: switched (m->mode is the new one); -1: the new driver did not start
 * (has_driver false: the host goes on without his scripts). */
int tc_main_mode_zones(tc_main *m, tc_cast *cast);
/* Takes what to draw from his driver now (pose_cur and the rest); the first time the previous tick's too. */
void tc_main_capture(tc_main *m);
/* One tick (TC_ED_AI_TICK) of the game with him: the mode zones, the cast's run-first persos, his scripts and
 * mechanics, the other persos, what the cast hands him (a speaker's cover or level change), what to draw. Sound events
 * started (his, then the cast's) are appended to sounds[*count < cap]. false: he has no driver (nothing ran past the
 * zones: the host's own controller moves him). *mode_switch: tc_main_mode_zones'. */
bool tc_main_tick(tc_main *m, tc_cast *cast, const tc_main_input *in, uint16_t *sounds, uint32_t *count, uint32_t cap,
                  int *mode_switch);

#ifdef __cplusplus
}
#endif
#endif
