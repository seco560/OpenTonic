/* The game session: one level being played, its rules, apart from how it is shown (docs/ARCHITECTURE.md stage 2,
 * 2026-11-19). Until then these lived in the viewer (tc_viewer.h), which now uses a tc_game and keeps the camera, the
 * drawing, the menus, the debug menu and the demo tapes.
 *
 * What it holds: the collision world and the persos' obstacles, the main character (tc_main.h: Ed, his driver, the
 * character in his slot), the cast (tc_cast.h), the game's tick on its clock (TC_ED_AI_TICK), the lines and the hint
 * scenes (tc_speech.h), the scripts' pictures, the HUD's state (tc_hud.h), the screen transitions and level changes
 * (tc_transition.h, docs/PORTALS.md), the sounds the frame started and their random picks, what a saved game needs.
 *
 * The host (the viewer) gives it each frame what the player does and where the camera is (tc_game_input), and takes
 * from it what to show: Ed's pose after the last ticks (tc_main), the phase and its transition, the line, the HUD, the
 * sounds, and the camera moves the scripts ask for (tc_game_camera_request). One frame, in the viewer's order:
 *   tc_game_begin_frame, tc_game_picture_hold, [tc_game_custom_frame], tc_game_step, tc_game_update_speech,
 *   tc_game_update_scene, ..., tc_game_update_portals, tc_game_pick_sounds.
 * A tc_game exists only for a level that can be played (Ed, collision, an idle clip); it must not move once made (the
 * driver, the player and the cast keep pointers into it). */
#ifndef TONIC_TC_GAME_H
#define TONIC_TC_GAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_math.h"
#include "tc_level.h"
#include "tc_collision.h"
#include "tc_obstacle.h"
#include "tc_main.h"
#include "tc_cast.h"
#include "tc_sound.h"
#include "tc_transition.h"
#include "tc_speech.h"
#include "tc_hud.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the player does, as the frontends' key bindings map to it. Defaults: WASD move, R/F up/down, arrow keys look,
 * Shift fast (in play mode with Ed's AI: SpeedUp, he walks), Space jump, E Action, Q Shoot, C Lateral. */
typedef enum tc_view_action {
    TC_VA_FORWARD, TC_VA_BACK, TC_VA_LEFT, TC_VA_RIGHT, TC_VA_UP, TC_VA_DOWN,
    TC_VA_LOOK_LEFT, TC_VA_LOOK_RIGHT, TC_VA_LOOK_UP, TC_VA_LOOK_DOWN, TC_VA_FAST, TC_VA_JUMP,
    TC_VA_ACTION, TC_VA_SHOOT, TC_VA_LATERAL, TC_VA_COUNT
} tc_view_action;
/* The presses that end a line, skip a picture, take the camera back from a director: a fresh one of these. */
#define TC_GAME_SKIP_MASK ((1u << TC_VA_JUMP) | (1u << TC_VA_ACTION) | (1u << TC_VA_SHOOT))

/* A level change: what the next level's game needs (tc_viewer_desc.carry; the frontends know it as tc_viewer_carry). Ed
 * lives in the fix, so his dsgvars survive (B63's arrival branch reads them: dsg134 = 2 -> heading dsg136, the
 * step-out). */
#define TC_GAME_CARRY_DSG 4096
#define TC_GAME_CARRY_FIX_DSG 1024
typedef struct tc_game_carry {
    char level[64];           /* the level to load, lowercase (match the .ttlv case-insensitively) */
    char place[64];           /* PROC_29: Ed arrives at the level perso of this instance name; "" = his start */
    char camera_place[64];
    uint32_t dsg_size;        /* 0: no dsgvars carried */
    uint8_t dsg[TC_GAME_CARRY_DSG];
    float vertical_fov;       /* the camera's field of view (, .) kept across the change, radians; 0 = the default */
    float orbit_distance;     /* the play camera's zoom on arrival (a portal: TC_CAM_PORTAL_DISTANCE); 0 = the default */
    uint8_t closeup;          /* the play camera's close-up (5) kept across the change */
    /* The game's progress (tc_ai_globals: the exe keeps these in its save block, beyond any one level), from a level
     * with a cast into the next. Kept at the end: a recording files this struct's tail by its size, so one made before
     * these were here reads as has_progress 0, and its levels start as they did (tc_rec.c read_segment). */
    uint8_t has_progress;
    uint8_t lives;
    uint16_t powers;
    int16_t values[31];
    /* A saved game's (tc_save.h; a portal's carry has none): Ed comes back where its last save point left him (and that
     * is the level's checkpoint again), with his life as it was; hit_points_max 0: as the level starts him. */
    uint8_t has_checkpoint;
    float checkpoint[3], checkpoint_heading;
    int32_t hit_points, hit_points_max; /* since 2026-11-17 a portal's carry has them too: Ed's std game lives in the fix */
    /* The cheat-code reader's dsgvars (a fix perso: tc_cast_fix_dsg role 2; its dsg26 is the thermometers' next reward,
     * HEG_Inventaire), from a level with a cast into the next and into a saved game; fix_dsg_size 0: as the level has
     * them. Kept at the end (a recording's older carry reads as 0). */
    uint32_t fix_dsg_size;
    uint8_t fix_dsg[TC_GAME_CARRY_FIX_DSG];
    /* Ed's clock (GetTime, tc_ai_perso.time_ms) as the last level left it; 0: the level's own start. His dsgvars come
     * along with their timestamps (dsg50, the blink after a hit: TimeElapsed(dsg50, dsg51)), which only mean something
     * on a clock that goes on; the exe's engine timer is not restarted by a level change (**inferred**: GetTime reads
     * the engine's running timer). Until 2026-11-20 every level restarted it at 60 s, and a hit just before a portal
     * left his blink (and its invulnerability) on for as long as the last level had run: cavedoc's opening showed him
     * see-through to XYZ. Kept at the end (a recording's older carry reads as 0). */
    uint32_t clock_ms;
} tc_game_carry;

/* Where the game is between levels and screen transitions (docs/PORTALS.md): playing, covering the screen, covered,
 * on the loading screen (TC_GAME_LOADING_SECONDS), waiting for the host to load the next level (tc_game_pending_level),
 * revealing. */
typedef enum tc_game_phase {
    TC_GP_PLAY, TC_GP_COVER, TC_GP_BLACK, TC_GP_LOADING, TC_GP_PENDING, TC_GP_REVEAL
} tc_game_phase;
/* How long the loading screen shows its bar filling (it is not tied to real loading: the frontends also show it after
 * the intro, before Totalski). */
#define TC_GAME_LOADING_SECONDS 3

/* The host camera's settings a level change and a save keep (into tc_game_carry). */
typedef struct tc_game_view {
    float vertical_fov;       /* radians */
    float orbit_distance;
    bool closeup;
} tc_game_view;

/* A camera move the game asks of the host, in order (tc_game_arrive, tc_game_update_portals): AIM, the camera turned to
 * `at` (Ed's place when asked, 1.1 up) from `eye` (PROC 8's camera position, PROC 29's camera place); YAW, behind him at
 * `yaw` (a saved game's arrival: the follow camera's yaw is his heading). */
enum { TC_GAME_CAMERA_AIM = 1, TC_GAME_CAMERA_YAW };
typedef struct tc_game_camera_request { uint8_t kind; tc_vec3 eye, at; float yaw; } tc_game_camera_request;
#define TC_GAME_MAX_CAMERA_REQUESTS 4

/* A frame of the player's and of the host's camera, as the game reads them. */
typedef struct tc_game_input {
    uint32_t held;            /* 1 << tc_view_action */
    float move_x, move_y;     /* the left stick after its dead zone, y forward */
    uint32_t ai_inputs;       /* held as Ed's scripts' buttons (1 << TC_AI_IN_*; first person: Action and Shoot only) */
    bool director;            /* the host shows a director's shot: Ed waits, as the SE parks its Ed */
    bool first_person;        /* Ed stands; he faces where the camera looks and shoots along its pitch */
    bool paused, inspecting;  /* the host's pause, its look round */
    tc_vec3 eye;              /* the camera, last frame's (his scripts make the direction keys camera-relative) */
    float yaw, pitch;         /* tc_camera's */
    tc_sound_listener listener; /* the camera's micro (tc_sound.h), last frame's: the cast's sounds are heard from it */
    /* How Ed was drawn last frame (a character in his slot keeps an idle playing: tc_character_input). */
    float animation_speed;
    int ed_clip;
    float ed_phase;
} tc_game_input;

typedef struct tc_game_desc {
    const tc_level *level;    /* must outlive the game */
    const void *ai_data;      /* the level's .ttai, or NULL */
    size_t ai_size;
    const char *level_name;   /* "bar": the AI's per-level settings (copied) */
    const tc_game_carry *carry; /* arriving through a portal or from a saved game, NULL: a new run */
    uint32_t seed;            /* the random stream (screen transitions; tc_game.rng) */
    uint32_t perso_first;     /* custom gameplay: the host's mesh number of the first perso part (tcx_play_create) */
} tc_game_desc;

struct tcx_play;
typedef struct tc_game {
    const tc_level *level;
    /* The world and who is in it. */
    tc_collision world;
    /* The static world's drawn triangles (tc_level.meshes), for the scripts' ray FUNC 162: the exe's (FUN_00484130) walks
     * the sectors' objects' visual geometry (LOD 0), not their collision. Empty when out of memory (then: world). */
    tc_collision sight;
    tc_obstacles obstacles;      /* the persos' own collision (.ttlv PCOL), met by Ed's sweeps (world.obstacles) */
    tc_main main;                /* the main character: Ed, his physics and his scripts (tc_main.h) */
    tc_cast cast;                /* the level persos that run their own AI (.ttlv CAST), with Ed as the main actor */
    bool has_cast;
    bool use_ai;                 /* his scripts drive him (else the host's simple controller) */
    float tick_time;             /* seconds not yet stepped: the host draws between the last two ticks by it */
    uint32_t tick_serial;        /* + 1 each tick run (the viewer's particles step with it) */
    /* Custom gameplay, not the game's (core/src/custom, tcx.h; NULL: none). Its car takes main's slot while Ed is in it:
     * his AI and mechanics (and the cast, which steps on his clock) hold, and he is drawn at main.rider. */
    struct tcx_play *custom;
    tc_result ai_result;         /* making it: TC_OK, or why Ed has no AI, or no cast */
    uint32_t walk_serial;        /* + 1 when a mode zone hands Ed back to walking (the host's ski camera ends) */

    /* Screen transitions and level changes. */
    tc_game_phase phase;
    tc_transition transition;
    float loading_time;          /* seconds into the loading screen */
    uint32_t rng;                /* the session's random stream: the light persos' seeds (the host's, at its start) and
                                    the transitions' effects */
    tc_game_carry *carry;        /* the level change in progress (LOADING / PENDING); on a level without a cast, the
                                    progress it arrived with (tc_game_arrive) */
    tc_game_camera_request camera_requests[TC_GAME_MAX_CAMERA_REQUESTS];
    uint8_t camera_request_count;

    /* Sounds started this frame (tc_game_sounds) and where each sounds from; the random picks' own stream (.ttlv SPIK). */
    uint16_t sounds[TC_AI_MAX_SOUNDS];
    tc_sound_source sound_sources[TC_AI_MAX_SOUNDS];
    uint32_t sound_count;
    uint32_t sound_rng;

    /* The level's scripted pictures (PROC 210; docs/SE.md "The outro"). A picture holds the game (Ed, the cast, the
     * clock) for its time, as the original's main loop waits on it; port: a fresh Jump, Action or Shoot skips it. */
    char picture[64];
    float picture_left;          /* seconds; 0: none */
    uint32_t picture_serial;

    /* What a character is saying (docs/ENGINE.md "Speech"). Port: Ed's inputs are held back meanwhile, until the keys
     * that ended it are let go: the scripts' own hold (ForceBehaviour(Ed, ...)) is refused for most characters. */
    tc_speech speech;
    uint32_t speech_event;       /* its dialogue's next event */
    bool speech_held;
    bool talk_press;             /* a fresh Action since the last tick (TC_CAST_TALK) */
    bool talk_held;              /* that Action talked to a speaker: kept from Ed's scripts until it is let go */
    bool scene_said;             /* a hint scene's line is over */
    bool scene_leaving;          /* its speaker was left waiting for the scene's Ed: the port covers and goes on */
    int scene_exit;              /* the way out Ed walks now (tc_speech_data.exits), -1 none */
    uint32_t scene_exits_done;   /* bit k: exits[k] started */

    /* The game's HUD (tc_hud.h): the scripts' text slots and the standard elements, stepped each game tick. */
    tc_hud hud;
    bool hud_waits;              /* tc_viewer_hud_waits: the sliding stages wait behind their start text */

    /* What a save of the game needs (tc_save.h): the level's name (tc_game_desc.level_name) and where Ed came in (the
     * carry's place and camera place; "": his start). */
    char level_name[64], arrival_place[64], arrival_camera_place[64];
} tc_game;

/* NULL when the level cannot be played (no Ed, no collision, no idle clip) or out of memory. Not yet arrived: a carry
 * is taken by tc_game_arrive (the host turns its camera to Ed first). */
tc_game *tc_game_create(const tc_game_desc *desc);
void tc_game_free(tc_game *g);
/* Arriving with desc->carry: Ed's dsgvars from the last level, the progress, the arrival place, a saved game's
 * checkpoint and life, then the reveal. Camera requests for the host. */
void tc_game_arrive(tc_game *g, const tc_game_carry *c);

/* The frame's start: the last frame's sounds are gone; action_fresh: Action pressed since the last frame (a speaker's). */
void tc_game_begin_frame(tc_game *g, bool action_fresh);
/* A scripted picture on show holds the game: true while it does (its time runs down; a fresh press ends it). */
bool tc_game_picture_hold(tc_game *g, float dt, bool fresh);
/* Custom gameplay's frame, before the tick: its cars move; Action gets Ed in or out. TC_GAME_CUSTOM_HELD: a character
 * holds Ed's slot (draw him as main.rider says); _GIVEN_BACK: it gave the slot back this frame (his drawing starts over);
 * _NONE: neither. */
enum { TC_GAME_CUSTOM_NONE, TC_GAME_CUSTOM_HELD, TC_GAME_CUSTOM_GIVEN_BACK };
int tc_game_custom_frame(tc_game *g, const tc_game_input *in, float dt);
/* The game's ticks this frame (TC_ED_AI_TICK each, at most 8): the cast and Ed (tc_main_tick), the HUD, the pictures. */
void tc_game_step(tc_game *g, float dt, const tc_game_input *in);
/* The line on show: started, voiced, its events, ended by a fresh press (docs/ENGINE.md "Speech"). Then a hint scene's
 * ways out and its end. */
void tc_game_update_speech(tc_game *g, float dt, uint32_t held, bool fresh);
void tc_game_update_scene(tc_game *g);
/* Ed is within reach of a hint scene's speaker who is waiting for him (the host adds its own conditions: a director's
 * shot, a demo tape). */
bool tc_game_talk_prompt(const tc_game *g);
/* Starts the cover or the reveal; without transition data, it is done at once. */
void tc_game_start_transition(tc_game *g, bool reveal);
/* After the frame's moves: Ed's portal events drive the phases; a level change fills the carry (view: the host camera's
 * settings it keeps). Camera requests for the host. */
void tc_game_update_portals(tc_game *g, float dt, const tc_game_view *view);
/* A level change into this level, at Ed's start, with the progress as it is (the debug menu's "Reload the level"). */
void tc_game_reload(tc_game *g, const tc_game_view *view);
/* The next level to load (after the loading screen), NULL none. */
const tc_game_carry *tc_game_pending_level(const tc_game *g);
/* The fix persos' dsgvars (the cheat-code reader's, role 2) into a carry: as this level's cast holds them, else what this
 * level arrived with. */
void tc_game_carry_fix(const tc_game *g, tc_game_carry *c);
/* A sound started this frame (NULL source: flat). */
void tc_game_push_sound(tc_game *g, uint16_t event, const tc_sound_source *s);
/* The events that play one of their clips at random (.ttlv SPIK): the clip this time, or nothing. After the frame. */
void tc_game_pick_sounds(tc_game *g);
/* No HUD over a covered screen or the loading screen; it comes back, sliding in, with the reveal. */
bool tc_game_hud_covered(const tc_game *g);
/* Who the player plays now: the one place the host asks (docs/ARCHITECTURE.md stage 3). The exe's main-character list
 * holds one perso (DAT_008c1720); here it is
 *   ED         Ed, by his scripts or the simple controller;
 *   PERSO      a cast perso his scripts handed the player to (PROC 4 Proc_SetMainActor: a chameleon; tc_cast_character),
 *              while Ed sleeps where he stood;
 *   CHARACTER  a character in Ed's slot (tc_main.h: custom gameplay's car), Ed riding it.
 * (Never PERSO and CHARACTER at once: riding, Ed runs no scripts to hand the player on; PERSO is asked first.) The camera
 * follows it (a CHARACTER says where to look: tc_main_camera_target), its sector is drawn, lit and heard. */
typedef enum tc_game_main_kind { TC_GAME_MAIN_ED, TC_GAME_MAIN_PERSO, TC_GAME_MAIN_CHARACTER } tc_game_main_kind;
typedef struct tc_game_main_info {
    tc_game_main_kind kind;
    const tc_cast_actor *actor;  /* PERSO: its actor (drawn between the ticks: tc_cast_drawn_position); else NULL */
    tc_vec3 position;            /* after the last tick (CHARACTER: where it carries Ed) */
    uint16_t sector;             /* TC_SECTOR_NONE: none known, or a level without sectors (PERSO: Ed's while its own
                                    is not known) */
} tc_game_main_info;
tc_game_main_info tc_game_main(const tc_game *g);
/* A save of this game (tc_save.h), NULL: nothing to save (no Ed's AI). */
uint8_t *tc_game_save_make(tc_game *g, const tc_game_view *view, const char *name, uint64_t time, size_t *size);

#ifdef __cplusplus
}
#endif
#endif
