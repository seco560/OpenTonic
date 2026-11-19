/* A character's brain: what moves a perso other than its own scripts run by the cast (docs/ARCHITECTURE.md stage 3; the
 * user's plan of 2026-11-13: every character has a brain, Tonic Trouble's scripts or native code, and the player's input
 * goes to whichever is the main character). Two today:
 *   Ed's driver (tc_main.c: his scripts, his portals, his mechanics through the cast's): the cast steps him by it at his
 *     place in its walk (tc_cast_tick), and asks it his sector;
 *   custom gameplay's car (core/src/custom): it holds Ed's slot (tc_main.h) while he is in it, moving on the host's
 *     frames, and says where the camera looks.
 * Until 2026-11-19 these were two tables, tc_cast_main_ops and tc_character_ops. A brain fills the parts it has; the
 * others are NULL. */
#ifndef TONIC_TC_BRAIN_H
#define TONIC_TC_BRAIN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_math.h"
#include "tc_player.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A frame of the player's, for a character in the slot (it moves on the host's frames). */
typedef struct tc_character_input {
    tc_player *ed;               /* Ed's physics: where he rides; placed by it when he gets out */
    uint32_t held;               /* 1 << tc_view_action (tc_game.h) */
    float move_x, move_y;        /* the left stick */
    bool paused;
    bool held_back;              /* the game holds the player (a line, the director's shot, the pause's look round) */
    float animation_speed;
    int ed_clip;                 /* how Ed was drawn last frame (an idle it keeps playing) */
    float ed_phase;
} tc_character_input;

/* How Ed is drawn while a character holds the slot. */
typedef struct tc_character_pose {
    tc_mat4 model;               /* Ed -> world */
    int clip;                    /* tc_level.ed.anims index, -1 none */
    float phase;
    bool loop;
} tc_character_pose;

struct tc_cam_target;
typedef struct tc_brain_ops {
    /* ---- on the game's ticks, in the cast's walk ---- */
    /* A tick (TC_ED_AI_TICK) at its place in the walk (tc_cast_tick). */
    void (*tick)(void *self, float dt);
    /* The sector it is in now (TC_SECTOR_NONE: none known). */
    uint16_t (*sector)(const void *self);

    /* ---- on the host's frames, holding Ed's slot (tc_main.h) ---- */
    /* A frame: moves it by the player's input and says how Ed is drawn. false: it gives the slot back this frame (Ed is
     * on his feet at in->ed's place: his own control from now). */
    bool (*frame)(void *self, const tc_character_input *in, float dt, tc_character_pose *pose);
    /* The play camera's target (what the host made for Ed: it changes what it needs). */
    void (*camera_target)(const void *self, struct tc_cam_target *t);
    /* Its part of the title bar (", driving 42 km/h"); false: none. */
    bool (*status)(const void *self, char *out, size_t size);
    /* Leaving play mode: Ed out at once, placed on `ed` (the slot is given back by tc_main_abort). */
    void (*abort)(void *self, tc_player *ed);
} tc_brain_ops;

/* A brain and what it runs on. ops NULL: none (a cast actor run by its own scripts). */
typedef struct tc_brain {
    const tc_brain_ops *ops;
    void *self;
} tc_brain;

#ifdef __cplusplus
}
#endif
#endif
