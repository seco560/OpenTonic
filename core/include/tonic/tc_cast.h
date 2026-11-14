/* The cast: the level persos whose own AI runs (.ttlv CAST, tools/ttre/cast.py; docs/NOTES.md "The cast"). Each perso
 * runs its scripts with tc_ai, its family's states drive its animation, and they all share one id space with the
 * main actor (Ed, or the skier): dsgvars, positions, behaviours and hit points of the others are the live ones
 * (tc_ai_perso.find).
 *
 * Per tick, as the exe's GAM_00428070 / GAM_PersoFrame_0042b520 (read), each active perso
 *   - is awake while its sector is the main actor's or in that sector's activity list (+0x64; tc_sectors_active);
 *   - thinks (runs its AI) while its activation radius holds: std game +0x2C -1, or above the squared distance between
 *     its origin and the main actor's (PROC 17-22 set it; the pickups and most props sleep until Ed comes close);
 *   - plays its state's animation while its mechanics or animation flag is set (+0x2A / +0x2B).
 * Their mechanics run for card types 0, 4, 6, 7, 10, 12 (tc_cast_mec.c; docs/ENGINE.md "Moving collision, read", "Walkers",
 * "Perso motion"), and their own collision (tc_obstacle.h) is posed where they are after each step.
 * Port choices, not the exe: with other card types (8, 9) the persos with the mechanics flag turn in place by the AI's
 * turn requests; at most TC_CAST_SOUNDS_PER_TICK audible sounds a tick (the throttle). Every sound is reported with where it
 * sounds from (tc_sound.h): the frontends place it from the camera, as the exe's micro hears it. */
#ifndef TONIC_TC_CAST_H
#define TONIC_TC_CAST_H
#include "tc_ai.h"
#include "tc_anim.h"
#include "tc_sector.h"
#include "tc_mec.h"
#include "tc_player.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_CAST_NEAR 10.0f         /* the removal guard's reach (tc_ai_perso.removal_guard): the pickups are taken within it */
#define TC_CAST_SOUNDS_PER_TICK 4  /* port: at most this many audible cast sounds a tick (heard from tc_cast.listener at
                                      TC_SOUND_AUDIBLE or more), and the same one from the same perso not again within
                                      TC_CAST_SOUND_GAP_MS (a script that replays one every tick must not flood the mixer).
                                      The inaudible ones go on (a loop is heard once its emitter comes near) */
#define TC_CAST_SOUND_GAP_MS 250u
#define TC_CAST_TALK_REACH 7.0f    /* port: how near a speaker Action talks to it, along the ground: the bar's counter keeps Ed
                                      5.8 off the barman, SpyCarota's ledge 6, and the mammoth towers 23 above him */

/* An actor's mechanics: the perso's dynamics (tc_mec.h). */
typedef tc_mec_dyn tc_cast_dyn;

/* A sector the scripts move (the exe's table DAT_005212f4, 0x48 a record: GAM_0042a000). world: its matrix, the identity
 * where the export has it (port: the exe's is the sector SuperObject's own, the identity in the levels looked at).
 * Translation (GAM_0042a1c0, stepped by GAM_00429d00): velocity per ms towards target; turn (GAM_00429e80, GAM_00429b40):
 * about axis through pivot, angle towards angle_target at rate per ms. */
#define TC_CAST_MAX_MOVING_SECTORS 16
typedef struct tc_cast_moving_sector {
    uint16_t sector;
    tc_mat34 world;
    bool moving, turning;
    tc_vec3 velocity, target;
    tc_vec3 axis, pivot;
    float angle, angle_target, rate;
} tc_cast_moving_sector;

typedef struct tc_cast_actor {
    const tc_cast_entry *entry;
    tc_ai_model model;           /* its scripts; the states are its family's (owned by tc_cast) */
    tc_ai_perso *ai;             /* its AI: own_ai, or (the main character's actor) his driver's */
    tc_ai_perso own_ai;
    float heading0;              /* the placement's (entry->world) */
    bool awake, thought;         /* last tick: its sector awake; its AI ran */
    uint16_t state_prev;         /* the tick before: for drawing between ticks */
    float frame_prev;
    uint16_t last_sound;         /* the throttle */
    uint32_t last_sound_ms;
    struct tc_obstacle *obstacle; /* its own collision (tc_obstacle.h), posed after each of its steps; NULL none */
    tc_cast_dyn *dyn;            /* its dynamics: own_dyn, or (the main character's actor) his driver's */
    tc_cast_dyn own_dyn;
    tc_sector_track sector;      /* the sector it is in now (GAM_0042c810 moves a perso with mechanics through the
                                    portals every frame); placed at its snapshot sector */
    tc_vec3 position_prev;       /* the tick before: for drawing between ticks */
    int32_t generated;           /* its generated object waiting for the launch (the perso's +0x30 link, META 11): an
                                    actor index, -1 none */
    tc_vec3 rot_prev;
    bool talking;                /* TC_CAST_TALK: the main actor spoke to it: its scripts run */
    /* Its physics as the shared mechanics see it (tc_mec_perso: the card types Ed's code brought, 1 2 5 8 11), made at
     * their first step; position, heading and velocity are the AI's and dyn's, copied in and out round the step. The main
     * character's is his player (tc_main.player), his capsule: a perso with one falls back on it where it has no body. */
    tc_player *body;
    tc_player own_body;
    bool has_body;
    bool capsule;
} tc_cast_actor;


/* A perso the scripts name that does not run (World, the camera ...): its dsgvars, one copy for all. */
typedef struct tc_cast_ghost_perso {
    tc_ai_model model;
    tc_ai_perso ai;
    int32_t main_other;          /* the main actor keeps a copy of it (the camera, the fix game perso): index into his
                                    model's others, whose memory and fed position this ghost shares; -1 its own;
                                    TC_CAST_GHOST_OWN_MEMORY: its own memory, his model's layout (tc_cast_detach_main) */
} tc_cast_ghost_perso;
#define TC_CAST_GHOST_OWN_MEMORY (-2)

typedef struct tc_cast {
    const tc_level *level;
    uint32_t count;
    tc_cast_actor *actors;
    tc_ai_state **states;        /* per family */
    tc_ai_sound **sounds;
    uint32_t *sound_count;
    uint32_t ghost_count;
    tc_cast_ghost_perso *ghosts;
    int32_t *by_id;              /* perso id -> actor index (< count), count + ghost index, -1 */
    uint32_t id_count;
    int32_t *by_prso;            /* tc_level.persos index -> actor index, -1 */
    tc_ai_perso *main;
    int32_t main_id;
    /* The main character as an actor of the cast (actors[count], past the counted ones: no loop over the cast meets
     * him; tc_cast_tick steps him at his place in the walk): his AI, dynamics and physics are his driver's
     * (tc_cast_attach_main), his record main_entry (his id, no family: his states, cards and bodies are his model's).
     * NULL: none attached. */
    tc_cast_actor *main_actor;
    tc_cast_entry main_entry;
    uint8_t *alias_saved;        /* the fix Ed's dsgvars at the level's start, where he stands for the main actor (the ski
                                    levels): his player-saved ones go back to these at a restart (tc_cast_respawn) */
    uint32_t alias_saved_size;
    tc_ai_globals globals;
    uint16_t sounds_out[TC_AI_MAX_SOUNDS]; /* this step's sound events (global indices) */
    tc_sound_source sources_out[TC_AI_MAX_SOUNDS]; /* where each sounds from (tc_sound.h) */
    uint8_t sounds_out_count;
    uint8_t audible_out;         /* of them, heard from the listener (the throttle's count) */
    tc_sound_listener listener;  /* host-fed: the camera's micro, last frame's (the exe places its micro at the frame's
                                    start, before the camera moves): the throttle counts the sounds heard from it */
    uint32_t awake, thinking;    /* last step's counts */
    uint32_t sounds_dropped;     /* by the throttle, in all */
    uint32_t rng;
    struct tc_obstacles *obstacles; /* the perso obstacles its actors pose (tc_cast_set_obstacles), NULL none */
    struct tc_collision *world;  /* host-fed: the static world (and its obstacles) the actors' own sweeps meet, NULL none */
    const tc_sectors *sectors;   /* the step's (tc_cast_step*): an actor sweeps its own sector's collide list */
    int32_t main_carrier;        /* host-fed: the perso the main actor rides (tc_mec_walk.carrier), -1 none */
    uint16_t main_sector;        /* the main actor's sector, as the last step got it (a spawn by him starts there) */
    uint32_t spawned;            /* diagnostics: META 11's instances */
    uint32_t placed;             /* diagnostics: SetPositionFrom placements of actors (PROC 27 / 8) */
    bool talk_press;             /* host-fed: a fresh Action this tick (TC_CAST_TALK) */
    /* The game's requests of the host from the actors' scripts this tick (the engine states they set): GameAction2 starts
     * the cover; ChangeLevel / PROC 29 change level (lowercase as the scripts give it; NULL none), with PROC 29's arrival
     * place and camera place (level persos by instance name; NULL none). The host clears them. */
    bool game_cover;
    const char *game_level, *game_place, *game_camera_place;
    float main_mass;             /* host-fed: his mass (his card's +4): what he weighs on what he rides (FUN_004c32f6) */
    tc_cast_moving_sector moving_sectors[TC_CAST_MAX_MOVING_SECTORS]; /* in the order the scripts first moved them */
    uint32_t moving_sector_count;
} tc_cast;

/* Where a started sound is heard from l (tc_sound.h): its volume 0..127, *pan and *dolby 0..127, or -1 when it is to
 * stop (a TC_SOUND_OWN sound whose perso was deactivated or removed since: its sound_serial moved on). The emitter's
 * position is its perso's now; the main actor (TC_SOUND_EMITTER_MAIN or his id) is always heard, at his own position; a
 * perso whose sector is not known is not (volume 0), as the exe's callback (0x422860) has it. */
int tc_cast_sound_place(const tc_cast *c, const tc_sound_source *s, const tc_sound_listener *l, int *pan, int *dolby);

/* main: the main actor's AI (it gets the cast's lookup; must outlive the cast). Levels without a CAST chunk give an
 * empty cast (count 0). */
tc_result tc_cast_init(tc_cast *c, const tc_level *level, tc_ai_perso *main);
void tc_cast_free(tc_cast *c);
/* The main actor is about to be freed or replaced (the Special Edition's walk / ski switch, tc_viewer play_mode_zones
 * re-creates Ed's driver with the other model): the ghosts sharing his memory keep a copy of their own, his links to the
 * cast are cleared. tc_cast_set_main then gives the cast its new main actor (the links tc_cast_init sets, and the ghosts
 * he keeps a copy of share his again). */
void tc_cast_detach_main(tc_cast *c);
void tc_cast_set_main(tc_cast *c, tc_ai_perso *main);
/* One tick (TC_ED_AI_TICK), after the main actor's: the clock is his (main->time_ms). s / main_sector: the level's
 * sectors and his sector (NULL / TC_SECTOR_NONE: everything is awake). */
void tc_cast_step(tc_cast *c, float dt, const tc_sectors *s, uint16_t main_sector);
/* The same in the exe's order around the main actor: the run-first persos (tc_cast_step_first), his step, the others
 * (tc_cast_step_rest). */
void tc_cast_step_first(tc_cast *c, float dt, const tc_sectors *s, uint16_t main_sector);
void tc_cast_step_rest(tc_cast *c, float dt, const tc_sectors *s, uint16_t main_sector);
/* The main character as the cast steps it (tc_cast_tick; tc_main.h): its own tick, and the sector it is in. */
typedef struct tc_cast_main_ops {
    void (*step)(void *self, float dt);
    uint16_t (*sector)(const void *self);    /* TC_SECTOR_NONE: none known */
} tc_cast_main_ops;
/* One tick of the game, the exe's frame (docs/ENGINE.md "The frame"): the run-first persos (tc_cast_step_first), then
 * the dynamic world's walk in tree order with the main character's step (ops->step(self, dt)) at his own place in it
 * (main_id: the persos before him in the walk run first), each perso from his sector and position as they are when it
 * runs; then the moved sectors. */
void tc_cast_tick(tc_cast *c, float dt, const tc_sectors *s, const tc_cast_main_ops *ops, void *self);
/* The main character as the cast's actor (tc_main.h): his AI, his dynamics and his physics (his capsule, tc_player) are
 * his driver's; his record: his id, always awake, his own collide set (the level's PCOL main). Again after his driver is
 * made anew (the Special Edition's walk / ski switch); a cast without actors has none. */
void tc_cast_attach_main(tc_cast *c, tc_ai_perso *ai, tc_mec_dyn *dyn, tc_player *body);
/* His mechanics for this step, the cast's as any actor's (his driver's step calls it in place of its own: tc_ed_ai's
 * mechanics hook): by his state's card, the walkers with a body, 6, 7, 10 and 12 through the cast's integrators (his
 * physics copied in and out), the rest through the shared step (tc_mec_perso_step: 1 2 5 8 11, his capsule). x: his
 * driver's view of the step. */
void tc_cast_main_mechanics(tc_cast *c, const tc_mec_perso *x, float dt);
/* The actor's mechanics for this tick, after its AI (GAM_MecFrame_0042b2b0: the queued requests through the state's
 * card type's request handler, then its integrator). Card types 6 (MoveLift), 7 (pushed and floating things), the
 * walkers 0 / 4 / 12 and the projectiles 10 are ported (docs/ENGINE.md "Moving collision, read", "Walkers",
 * "Shooting"); with another card, or none, it turns in place as before (port).
 * Returns whether it ran a card type's mechanics. */
bool tc_cast_mechanics(tc_cast *c, tc_cast_actor *a, float dt);
/* The perso obstacles (tc_obstacle.h) its actors pose where they are after each step; NULL: none. Poses them now. */
struct tc_obstacles;
void tc_cast_set_obstacles(tc_cast *c, struct tc_obstacles *o);
/* Where perso `id` is now (an actor, a ghost, the main actor), or where the level data places it (a ghost or an
 * actor's CAST record: the camera's snapshot position); false when the cast does not know it. */
bool tc_cast_position(const tc_cast *c, int32_t id, tc_vec3 *out);
/* Perso `id`'s hit points and their maximum (a boss's life bar: tc_hud.h); false when the cast does not run it. */
bool tc_cast_hit_points(const tc_cast *c, int32_t id, int32_t *points, int32_t *max);
bool tc_cast_placed_position(const tc_cast *c, int32_t id, tc_vec3 *out);
/* The matrix of a sector the scripts have moved (the identity: where the export has it), NULL for one they have not.
 * The static world's triangles follow it (c->world); the host draws the sector's geometry with it. */
const tc_mat34 *tc_cast_sector_matrix(const tc_cast *c, uint16_t sector);
/* The actor that draws level perso `prso` (tc_level.persos index), or NULL. */
const tc_cast_actor *tc_cast_actor_of(const tc_cast *c, uint32_t prso);
/* How to draw an actor, t (0..1) into the time since the last tick: its world matrix and its state animation with the
 * sample. false: not drawn (inactive, or no animation). */
bool tc_cast_pose(const tc_cast *c, const tc_cast_actor *a, float t, tc_mat34 *world, const tc_cast_anim **anim,
                  tc_sample *s);
/* Diagnostics: the primitives the cast's scripts skipped, summed (as tc_ai_perso.unknown, 10 * TC_AI_UNKNOWN_STRIDE). */
void tc_cast_unknown(const tc_cast *c, uint32_t *out);
/* After the main actor's death (or the ski levels' retry) and the port's respawn: the original initialises the map
 * again (GameAction0), so its level logic starts over. Port: the undrawn persos that started active start again, with
 * their level-saved variables as they are (the start text, World and its race clock, the finish gates, the pit markers
 * that fire once and RemovePerso themselves); drawn ones (the pickups, the characters) stay as they are. Before
 * 2026-11-13 only those that were out came back, and after a retry the start text and the clock did not. */
void tc_cast_respawn(tc_cast *c);
/* The level's saved values (tc_ai_dsgvar.save & TC_AI_DSG_SAVE_LEVEL: a pickup's "still here"): the exe writes them to
 * the level's own file on the way out of a level and reads them on the way in. Port: kept in memory for the run.
 * tc_cast_level_save on the way out (the actors' current values); an actor's start takes them back; a new run clears. */
void tc_cast_level_save(const tc_cast *c);
void tc_cast_level_saves_clear(void);

#ifdef __cplusplus
}
#endif
#endif
