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
 * Their mechanics run for card types 6 and 7 (tc_cast_mec.c: lifts, pushed and floating things; docs/ENGINE.md "Moving
 * collision, read"), and their own collision (tc_obstacle.h) is posed where they are after each step.
 * Port choices, not the exe: with other card types the persos with the mechanics flag turn in place by the AI's turn
 * requests; only sounds within TC_CAST_HEARING of the main actor are reported (the frontends play them flat). */
#ifndef TONIC_TC_CAST_H
#define TONIC_TC_CAST_H
#include "tc_ai.h"
#include "tc_anim.h"
#include "tc_sector.h"
#include "tc_mec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_CAST_HEARING 30.0f
#define TC_CAST_NEAR 10.0f         /* the removal guard's reach (tc_ai_perso.removal_guard): the pickups are taken within it */
#define TC_CAST_SOUNDS_PER_TICK 4  /* port: at most this many cast sounds a tick, and the same one from the same perso
                                      not again within TC_CAST_SOUND_GAP_MS (a script that replays one every tick must
                                      not flood the mixer) */
#define TC_CAST_SOUND_GAP_MS 250u

/* An actor's mechanics (the exe's dynamics, for the card types the cast runs: tc_cast_mec.c). */
typedef struct tc_cast_dyn {
    uint32_t card_type;          /* last step's (0xFFFF none) */
    tc_vec3 velocity;            /* dynamics +0x38 (linear) */
    tc_vec3 omega;               /* its angle x axis: angular velocity as a rotation vector (rad/s) */
    tc_vec3 rot;                 /* the position's angle x axis: the orientation as a rotation vector (type 7 tilts) */
    bool tilted;                 /* rot is not a turn about up only: the pose uses it whole */
    bool moving;                 /* velocity_snap != 0 (type 6: on its way; MoveLift's end test) */
    float lift_s, lift_elapsed, lift_x, lift_rate; /* type 6: +0x2B0 s, +0x2B4, the lag {x, rate} (+0x2BC, +0x2C8) */
    float unk_70;                /* type 7: < 0 after a move request: the spring's rest is set again */
    float rest_z;                /* type 7: the spring's rest height (+0x88) */
    bool substeps;               /* type 7: dynamics flag 0x20000 (5 ms substeps) */
    tc_vec3 plane_n;             /* type 7 mode 0x15: the plane spring */
    float plane_k, plane_offset;
    tc_mec_walk walk;            /* type 7: its own sweep's contacts (MEC_CollideMoveNoTurn_004bedd6, flags 4) */
} tc_cast_dyn;

typedef struct tc_cast_actor {
    const tc_cast_entry *entry;
    tc_ai_model model;           /* its scripts; the states are its family's (owned by tc_cast) */
    tc_ai_perso ai;
    float heading0;              /* the placement's (entry->world) */
    bool awake, thought;         /* last tick: its sector awake; its AI ran */
    uint16_t state_prev;         /* the tick before: for drawing between ticks */
    float frame_prev;
    uint16_t last_sound;         /* the throttle */
    uint32_t last_sound_ms;
    struct tc_obstacle *obstacle; /* its own collision (tc_obstacle.h), posed after each of its steps; NULL none */
    tc_cast_dyn dyn;
    tc_sector_track sector;      /* the sector it is in now (GAM_0042c810 moves a perso with mechanics through the
                                    portals every frame); placed at its snapshot sector */
    tc_vec3 position_prev;       /* the tick before: for drawing between ticks */
    tc_vec3 rot_prev;
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
    tc_ai_globals globals;
    uint16_t sounds_out[TC_AI_MAX_SOUNDS]; /* this step's sound events (global indices) */
    uint8_t sounds_out_count;
    uint32_t awake, thinking;    /* last step's counts */
    uint32_t sounds_dropped;     /* by the throttle, in all */
    uint32_t rng;
    struct tc_obstacles *obstacles; /* the perso obstacles its actors pose (tc_cast_set_obstacles), NULL none */
    struct tc_collision *world;  /* host-fed: the static world (and its obstacles) the actors' own sweeps meet, NULL none */
    const tc_sectors *sectors;   /* the step's (tc_cast_step*): an actor sweeps its own sector's collide list */
    int32_t main_carrier;        /* host-fed: the perso the main actor rides (tc_mec_walk.carrier), -1 none */
    float main_mass;             /* host-fed: his mass (his card's +4): what he weighs on what he rides (FUN_004c32f6) */
} tc_cast;

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
/* The actor's mechanics for this tick, after its AI (GAM_MecFrame_0042b2b0: the queued requests through the state's
 * card type's request handler, then its integrator). Card types 6 (MoveLift) and 7 (pushed and floating things) are
 * ported (docs/ENGINE.md "Moving collision, read"); with another card, or none, it turns in place as before (port).
 * Returns whether it ran a card type's mechanics. */
bool tc_cast_mechanics(tc_cast *c, tc_cast_actor *a, float dt);
/* The perso obstacles (tc_obstacle.h) its actors pose where they are after each step; NULL: none. Poses them now. */
struct tc_obstacles;
void tc_cast_set_obstacles(tc_cast *c, struct tc_obstacles *o);
/* Where perso `id` is now (an actor, a ghost, the main actor), or where the level data places it (a ghost or an
 * actor's CAST record: the camera's snapshot position); false when the cast does not know it. */
bool tc_cast_position(const tc_cast *c, int32_t id, tc_vec3 *out);
bool tc_cast_placed_position(const tc_cast *c, int32_t id, tc_vec3 *out);
/* The actor that draws level perso `prso` (tc_level.persos index), or NULL. */
const tc_cast_actor *tc_cast_actor_of(const tc_cast *c, uint32_t prso);
/* How to draw an actor, t (0..1) into the time since the last tick: its world matrix and its state animation with the
 * sample. false: not drawn (inactive, or no animation). */
bool tc_cast_pose(const tc_cast *c, const tc_cast_actor *a, float t, tc_mat34 *world, const tc_cast_anim **anim,
                  tc_sample *s);
/* Diagnostics: the primitives the cast's scripts skipped, summed (as tc_ai_perso.unknown, 10 * TC_AI_UNKNOWN_STRIDE). */
void tc_cast_unknown(const tc_cast *c, uint32_t *out);
/* After the main actor's death and the port's respawn: the original reloads the level, so the level logic that took
 * itself out comes back. Port: the undrawn persos that started active and are out (or frozen by the removal guard)
 * now (the pit markers: they fire once and RemovePerso themselves) start again; drawn ones (the pickups) stay as they are. */
void tc_cast_respawn(tc_cast *c);

#ifdef __cplusplus
}
#endif
#endif
