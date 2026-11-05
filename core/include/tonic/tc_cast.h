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
 * Port choices, not the exe: the persos do not move (their mechanics are not run); those with the mechanics flag turn
 * in place by the AI's turn requests; only sounds within TC_CAST_HEARING of the main actor are reported (the frontends
 * play them flat, not in 3D). */
#ifndef TONIC_TC_CAST_H
#define TONIC_TC_CAST_H
#include "tc_ai.h"
#include "tc_anim.h"
#include "tc_sector.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_CAST_HEARING 30.0f
#define TC_CAST_NEAR 10.0f         /* the removal guard's reach (tc_ai_perso.removal_guard): the pickups are taken within it */
#define TC_CAST_SOUNDS_PER_TICK 4  /* port: at most this many cast sounds a tick, and the same one from the same perso
                                      not again within TC_CAST_SOUND_GAP_MS (a script that replays one every tick must
                                      not flood the mixer) */
#define TC_CAST_SOUND_GAP_MS 250u

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
} tc_cast_actor;

/* A perso the scripts name that does not run (World, the camera ...): its dsgvars, one copy for all. */
typedef struct tc_cast_ghost_perso {
    tc_ai_model model;
    tc_ai_perso ai;
    int32_t main_other;          /* the main actor keeps a copy of it (the camera, the fix game perso): index into his
                                    model's others, whose memory and fed position this ghost shares; -1 its own */
} tc_cast_ghost_perso;

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
} tc_cast;

/* main: the main actor's AI (it gets the cast's lookup; must outlive the cast). Levels without a CAST chunk give an
 * empty cast (count 0). */
tc_result tc_cast_init(tc_cast *c, const tc_level *level, tc_ai_perso *main);
void tc_cast_free(tc_cast *c);
/* One tick (TC_ED_AI_TICK), after the main actor's: the clock is his (main->time_ms). s / main_sector: the level's
 * sectors and his sector (NULL / TC_SECTOR_NONE: everything is awake). */
void tc_cast_step(tc_cast *c, float dt, const tc_sectors *s, uint16_t main_sector);
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
