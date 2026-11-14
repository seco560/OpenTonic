/* Custom gameplay: things Tonic Trouble never had, ours alone (docs/ENGINE.md "Custom gameplay"). Everything of it lives in
 * core/src/custom and core/include/tonic/custom, named tcx_*: nothing here is read from the exe or the game's data, and
 * none of the game's own code depends on it. This header is the one seam: the only calls the rest of the core makes into
 * it (grep "tcx_" in tc_viewer*.c / tc_level.c to see every place).
 *
 * `make CUSTOM=0` builds the core without it (TC_NO_CUSTOM: core/src/custom left out, the calls below inline no-ops, the
 * custom layer's chunks skipped), into build-nocustom: its checkall shows the game doesn't lean on any of it.
 *
 * So far: the cars of the imported levels (tcx_vehicle.h: a toy car Ed drives; the user, 2026-11-12); skies drawn round
 * the camera (TCX_FLAG_ROUND_CAMERA: the SSX courses'); the skier's cruise push on those courses (tcx_ski_tick). */
#ifndef TONIC_TCX_H
#define TONIC_TCX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../tc_level.h"
#include "../tc_player.h"
#include "../tc_cam.h"
#include "../tc_mec.h"
#include "../tc_main.h"
#include "tcx_vehicle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A tc_surface flag of the custom layer (above the game's TC_FLAG_*; the renderers test their own bits only): the mesh is a sky drawn round the
 * camera, its vertices relative to the eye (SSX Tricky's skies are: tools/ttre/ssx.py add_sky; Tonic Trouble's are
 * level geometry). Without the custom layer it stays where its vertices are: round the origin. */
#define TCX_FLAG_ROUND_CAMERA (1u << 8)
/* A car's part with this bit in its object is the far model of the part without it (tools/ttre/vehicle.py: a few
 * thousand triangles for the whole car instead of some 94 thousand), drawn in its place from TC_VIEW_CAR_FAR on. No
 * sample shows such an object, so nothing but the cars' drawing (tc_viewer_draw.c) ever draws one. */
#define TCX_PART_FAR 0x4000u

/* The custom layer's data in a level (tc_level.custom; NULL: none). */
typedef struct tcx_level {
    uint32_t vehicle_count;          /* .ttlv VEHI (tcx_vehicle.h) */
    tcx_vehicle_data *vehicles;
    float ski_cruise, ski_push;      /* .ttlv SKIA (tcx_ski_tick); 0: none */
    float ski_air_gravity;           /* SKIA's third value: off the snow his gravity times this (1: as the card has it) */
} tcx_level;

/* A play session's custom gameplay (opaque; tc_viewer_custom). */
typedef struct tcx_play tcx_play;
struct tc_obstacles;

/* What the host (tc_viewer) lends it each frame. */
typedef struct tcx_host {
    tc_player *player;               /* Ed's physics state: where he is */
    uint32_t held;                   /* 1 << tc_view_action */
    float move_x, move_y;            /* the left stick */
    bool paused;
    bool held_back;                  /* the game holds Ed (a line, the director's shot, the pause's look round) */
    bool may_start;                  /* a new action may start (playing, not first person, no level change, Ed main) */
} tcx_host;

#ifndef TC_NO_CUSTOM

/* .ttlv chunks of the custom layer (tag "VEHI"); the rest of the loader only finds them. Not TC_OK: a layout mismatch
 * (TC_ERR_STALE) or out of memory. */
tc_result tcx_level_read(tc_level *level, const char tag[4], const uint8_t *data, size_t size);
void tcx_level_free(tcx_level *l);

/* A level mesh the custom layer places itself (TCX_FLAG_ROUND_CAMERA): its model matrix for a camera at eye. false:
 * the mesh stays where it is. */
bool tcx_mesh_model(const tc_mesh *mesh, tc_vec3 eye, tc_mat4 *model);

/* Before a tick of the skier's mechanics (Ed on a type 2 card), on a level with SKIA (the SSX Tricky courses): on the
 * ground and slower along the board than ski_cruise (u/s), a push along its front of up to ski_push (u/s^2), fading out
 * over the upper half of that speed. SSX's riders propel themselves over the flats and dips their courses are built
 * with; Tonic Trouble's skier has gravity only (his slopes never level out), and sat still on them. Off the snow (no
 * contact last tick) his weight is lightened to ski_air_gravity of the card's (factor +0x34, times +0x40 clear of the
 * ground, as tc_mec_ski_velocity): SSX's gaps are built for its riders' long jumps (user, 2026-11-14). Added to the
 * board's force list, which the integrator takes with this tick. l NULL or no SKIA: nothing. */
void tcx_ski_tick(const tcx_level *l, tc_mec_ski *ski, tc_vec3 velocity, uint32_t contacts, const tc_ai_card *card);

/* Made with the play session (NULL: the level has nothing custom, or out of memory); every call below takes NULL. */
tcx_play *tcx_play_create(const tc_level *level, tc_collision *world, struct tc_obstacles *obstacles, uint32_t perso_first);
void tcx_play_free(tcx_play *x);
/* A frame, before the main character's: the cars move (the one Ed drives by the player's keys, the others coast), their
 * collision posed where they are; Action within reach of one gets Ed in. true: he just got in: the host hands the main
 * character's slot to the car (tc_main_take(m, tcx_character(), x)), whose frames (from this one on) hop him in, drive,
 * and hop him out on Action, giving the slot back when he lands. */
bool tcx_update(tcx_play *x, const tcx_host *h, float dt);
/* The car Ed is in, as the main character (tc_main.h; self: the tcx_play). */
const tc_character_ops *tcx_character(void);
/* The Action held now was its own (getting in or out): keep it from Ed's scripts until let go. */
bool tcx_holds_action(const tcx_play *x);
/* The cars' collision on or off while the camera moves (the cars don't block the camera). */
void tcx_camera_obstacles(tcx_play *x, bool on);
/* A level perso it moves itself (a car): its world matrix now, and a channel's transform (channel -> perso). */
bool tcx_perso_pose(const tcx_play *x, uint32_t perso, tc_mat34 *world);
tc_mat34 tcx_perso_channel(const tcx_play *x, uint32_t perso, uint16_t channel);
/* A tc_viewer_mesh the camera's sight test leaves out. */
bool tcx_sight_skips(const tcx_play *x, uint32_t mesh);

/* For checks and frontends: the cars in play and Ed's ride. */
uint32_t tcx_play_vehicle_count(const tcx_play *x);
const tcx_vehicle *tcx_play_vehicle(const tcx_play *x, uint32_t i);
const tcx_ride *tcx_play_ride(const tcx_play *x);

#else /* TC_NO_CUSTOM: none of it */

static inline tc_result tcx_level_read(tc_level *level, const char tag[4], const uint8_t *data, size_t size) {
    (void)level, (void)tag, (void)data, (void)size;
    return TC_OK;
}
static inline void tcx_level_free(tcx_level *l) { (void)l; }
static inline bool tcx_mesh_model(const tc_mesh *mesh, tc_vec3 eye, tc_mat4 *model) {
    (void)mesh, (void)eye, (void)model;
    return false;
}
static inline void tcx_ski_tick(const tcx_level *l, tc_mec_ski *ski, tc_vec3 velocity, uint32_t contacts,
                                const tc_ai_card *card) {
    (void)l, (void)ski, (void)velocity, (void)contacts, (void)card;
}
static inline tcx_play *tcx_play_create(const tc_level *level, tc_collision *world, struct tc_obstacles *obstacles,
                                        uint32_t perso_first) {
    (void)level, (void)world, (void)obstacles, (void)perso_first;
    return NULL;
}
static inline void tcx_play_free(tcx_play *x) { (void)x; }
static inline bool tcx_update(tcx_play *x, const tcx_host *h, float dt) {
    (void)x, (void)h, (void)dt;
    return false;
}
static inline const tc_character_ops *tcx_character(void) { return NULL; }
static inline bool tcx_holds_action(const tcx_play *x) { (void)x; return false; }
static inline void tcx_camera_obstacles(tcx_play *x, bool on) { (void)x, (void)on; }
static inline bool tcx_perso_pose(const tcx_play *x, uint32_t perso, tc_mat34 *world) {
    (void)x, (void)perso, (void)world;
    return false;
}
static inline tc_mat34 tcx_perso_channel(const tcx_play *x, uint32_t perso, uint16_t channel) {
    tc_mat34 m = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    (void)x, (void)perso, (void)channel;
    return m;
}
static inline bool tcx_sight_skips(const tcx_play *x, uint32_t mesh) { (void)x, (void)mesh; return false; }
static inline uint32_t tcx_play_vehicle_count(const tcx_play *x) { (void)x; return 0; }
static inline const tcx_vehicle *tcx_play_vehicle(const tcx_play *x, uint32_t i) { (void)x, (void)i; return NULL; }
static inline const tcx_ride *tcx_play_ride(const tcx_play *x) { (void)x; return NULL; }

#endif

#ifdef __cplusplus
}
#endif
#endif
