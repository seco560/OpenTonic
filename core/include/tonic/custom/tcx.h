/* Custom gameplay: things Tonic Trouble never had, ours alone (docs/ENGINE.md "Custom gameplay"). Everything of it lives in
 * core/src/custom and core/include/tonic/custom, named tcx_*: nothing here is read from the exe or the game's data, and
 * none of the game's own code depends on it. This header is the one seam: the only calls the rest of the core makes into
 * it (grep "tcx_" in tc_viewer.c / tc_level.c to see every place).
 *
 * `make CUSTOM=0` builds the core without it (TC_NO_CUSTOM: core/src/custom left out, the calls below inline no-ops, the
 * custom layer's chunks skipped), into build-nocustom: its checkall shows the game doesn't lean on any of it.
 *
 * So far: the cars of the imported levels (tcx_vehicle.h: a toy car Ed drives; the user, 2026-11-12). */
#ifndef TONIC_TCX_H
#define TONIC_TCX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../tc_level.h"
#include "../tc_player.h"
#include "../tc_cam.h"
#include "tcx_vehicle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The custom layer's data in a level (tc_level.custom; NULL: none). */
typedef struct tcx_level {
    uint32_t vehicle_count;          /* .ttlv VEHI (tcx_vehicle.h) */
    tcx_vehicle_data *vehicles;
} tcx_level;

/* A play session's custom gameplay (opaque; tc_viewer_custom). */
typedef struct tcx_play tcx_play;
struct tc_obstacles;

/* What the host (tc_viewer) lends it each frame. */
typedef struct tcx_host {
    tc_player *player;               /* Ed's physics state: where he is; placed by it when he gets out of a car */
    uint32_t held;                   /* 1 << tc_view_action */
    float move_x, move_y;            /* the left stick */
    bool paused;
    bool held_back;                  /* the game holds Ed (a line, the director's shot, the pause's look round) */
    bool may_start;                  /* a new action may start (playing, not first person, no level change) */
    float animation_speed;
    int ed_clip;                     /* Ed's clip and phase last frame (an idle it keeps playing) */
    float ed_phase;
} tcx_host;

/* What it hands back while it holds Ed (tcx_update returned true): how to draw him. */
typedef struct tcx_ed {
    tc_mat4 model;                   /* Ed -> world */
    int clip;                        /* tc_level.ed.anims index, -1 none */
    float phase;
    bool loop;
    bool landed;                     /* (returned false) he was just put back on his feet: his own control again */
} tcx_ed;

#ifndef TC_NO_CUSTOM

/* .ttlv chunks of the custom layer (tag "VEHI"); the rest of the loader only finds them. Not TC_OK: a layout mismatch
 * (TC_ERR_STALE) or out of memory. */
tc_result tcx_level_read(tc_level *level, const char tag[4], const uint8_t *data, size_t size);
void tcx_level_free(tcx_level *l);

/* Made with the play session (NULL: the level has nothing custom, or out of memory); every call below takes NULL. */
tcx_play *tcx_play_create(const tc_level *level, tc_collision *world, struct tc_obstacles *obstacles, uint32_t perso_first);
void tcx_play_free(tcx_play *x);
/* A frame, before Ed's step: true while it holds Ed (he is not stepped; draw him from *ed). */
bool tcx_update(tcx_play *x, const tcx_host *h, float dt, tcx_ed *ed);
/* Leaving play mode: Ed out of whatever holds him, at once. */
void tcx_abort(tcx_play *x, tc_player *player);
/* The Action held now was its own (getting in or out): keep it from Ed's scripts until let go. */
bool tcx_holds_action(const tcx_play *x);
/* The play camera's target while it holds Ed (else untouched); its collision on or off while the camera moves (the cars
 * don't block the camera). */
void tcx_camera_target(const tcx_play *x, tc_cam_target *t);
void tcx_camera_obstacles(tcx_play *x, bool on);
/* A level perso it moves itself (a car): its world matrix now, and a channel's transform (channel -> perso). */
bool tcx_perso_pose(const tcx_play *x, uint32_t perso, tc_mat34 *world);
tc_mat34 tcx_perso_channel(const tcx_play *x, uint32_t perso, uint16_t channel);
/* A tc_viewer_mesh the camera's sight test leaves out. */
bool tcx_sight_skips(const tcx_play *x, uint32_t mesh);
/* Its part of the title bar (", driving 42 km/h"), false: none. */
bool tcx_status(const tcx_play *x, char *out, size_t size);

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
static inline tcx_play *tcx_play_create(const tc_level *level, tc_collision *world, struct tc_obstacles *obstacles,
                                        uint32_t perso_first) {
    (void)level, (void)world, (void)obstacles, (void)perso_first;
    return NULL;
}
static inline void tcx_play_free(tcx_play *x) { (void)x; }
static inline bool tcx_update(tcx_play *x, const tcx_host *h, float dt, tcx_ed *ed) {
    (void)x, (void)h, (void)dt;
    ed->landed = false;
    return false;
}
static inline void tcx_abort(tcx_play *x, tc_player *player) { (void)x, (void)player; }
static inline bool tcx_holds_action(const tcx_play *x) { (void)x; return false; }
static inline void tcx_camera_target(const tcx_play *x, tc_cam_target *t) { (void)x, (void)t; }
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
static inline bool tcx_status(const tcx_play *x, char *out, size_t size) { (void)x, (void)out, (void)size; return false; }
static inline uint32_t tcx_play_vehicle_count(const tcx_play *x) { (void)x; return 0; }
static inline const tcx_vehicle *tcx_play_vehicle(const tcx_play *x, uint32_t i) { (void)x, (void)i; return NULL; }
static inline const tcx_ride *tcx_play_ride(const tcx_play *x) { (void)x; return NULL; }

#endif

#ifdef __cplusplus
}
#endif
#endif
