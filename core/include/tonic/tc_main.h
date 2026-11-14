/* The main character: the perso the player plays (the exe's main-character list, DAT_008c1720: one perso; docs/ENGINE.md
 * "The frame"). Input goes to it, the camera follows it, and the cast measures who is awake and who thinks from it.
 *
 * Ed is the main character unless another has taken the slot (tc_main_take): by his own scripts (tc_ed_ai: his driver,
 * with the level's .ttai) or, without them, by the simple controller (tc_player alone). Both move the same physics,
 * `player`. His tick is the game's: the cast steps him between its run-first persos (lifts, platforms) and the others
 * (tc_main_tick -> tc_cast_tick), as the exe's frame orders them.
 *
 * Another character (tc_character_ops: custom gameplay's car, core/src/custom) takes the slot when Ed gets into it, and
 * gives it back when he is on his feet again. Meanwhile Ed rides along: his scripts and mechanics sleep, his physics'
 * place is where it puts him (his sector and the lights follow it), and it says how to draw him (his hop in and out,
 * his seat). It moves on the host's frames, by its own clock; the game's tick goes on round it (tc_main_tick: the cast's
 * passes, Ed's AI record where it carries him, the game's clock).
 *
 * Port, staged (docs/DIRECTION.md point 2, the user's plan of 2026-11-13): until 2026-11-13 the viewer held Ed's parts and
 * stepped him itself. */
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

/* A frame of the player's, for a character in the slot (it moves on the host's frames). */
typedef struct tc_character_input {
    tc_player *ed;               /* Ed's physics: where he rides; placed by it when he gets out */
    uint32_t held;               /* 1 << tc_view_action (tc_viewer.h) */
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
typedef struct tc_character_ops {
    /* A frame while it holds the slot: moves it by the player's input and says how Ed is drawn. false: it gives the
     * slot back this frame (Ed is on his feet at in->ed's place: his own control from now). */
    bool (*frame)(void *self, const tc_character_input *in, float dt, tc_character_pose *pose);
    /* The play camera's target (what the host made for Ed: it changes what it needs). */
    void (*camera_target)(const void *self, struct tc_cam_target *t);
    /* Its part of the title bar (", driving 42 km/h"); false: none. */
    bool (*status)(const void *self, char *out, size_t size);
    /* Leaving play mode: Ed out at once, placed on `ed` (the slot is given back by tc_main_abort). */
    void (*abort)(void *self, tc_player *ed);
} tc_character_ops;

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
    /* The slot: the character that is main instead of Ed (NULL: Ed), and how Ed is drawn riding it (its last frame). */
    const tc_character_ops *ops;
    void *self;
    tc_character_pose rider;
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
 * mechanics, the other persos, what the cast hands him (a speaker's cover or level change), what to draw. With another
 * character in the slot: the cast's passes round Ed riding it (his scripts asleep, his AI record carried, the clock on;
 * not without his driver: false). Sound events
 * started (his, then the cast's) are appended to sounds[*count < cap], where each sounds from to sources[] (tc_sound.h).
 * false: he has no driver (nothing ran past the zones: the host's own controller moves him). *mode_switch:
 * tc_main_mode_zones'. */
bool tc_main_tick(tc_main *m, tc_cast *cast, const tc_main_input *in, uint16_t *sounds, tc_sound_source *sources,
                  uint32_t *count, uint32_t cap, int *mode_switch);

/* The slot. Ed is main (no other character holds it). */
static inline bool tc_main_is_ed(const tc_main *m) { return m->ops == NULL; }
/* A character takes the slot (Ed gets in); it runs from the next tc_main_frame. */
void tc_main_take(tc_main *m, const tc_character_ops *ops, void *self);
/* A frame of the character in the slot (Ed's own control otherwise: false, nothing done). false also when it gave the
 * slot back this frame: Ed is main again. m->rider: how to draw Ed. */
bool tc_main_frame(tc_main *m, const tc_character_input *in, float dt);
/* Leaving play mode: the character in the slot puts Ed out at once and the slot is Ed's. */
void tc_main_abort(tc_main *m);
/* The character in the slot's camera target and title bar part (nothing / false when Ed is main). */
void tc_main_camera_target(const tc_main *m, struct tc_cam_target *t);
bool tc_main_status(const tc_main *m, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
