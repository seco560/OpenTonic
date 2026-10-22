/* Play-mode character controller for Ed: walk/run/jump with gravity against a tc_collision.
 * Deliberately simple, NOT the original MEC dynamics. Ed's body is two stacked spheres (feet and
 * head end of a capsule); fixed 1/120 s substeps, each moving at most half a radius, so nothing
 * tunnels at any frame rate. Contacts within max_slope of up count as ground and only push up;
 * steeper ones push along the normal and remove the velocity into them (wall sliding). While
 * grounded, a short downward ray snaps Ed onto slopes and down small steps. */
#ifndef TONIC_TC_PLAYER_H
#define TONIC_TC_PLAYER_H
#include <stdbool.h>
#include "tc_math.h"
#include "tc_collision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What a platform's input mapping produces each frame. */
typedef struct tc_player_input {
    tc_vec2 move; /* world-space XY direction, length <= 1 (camera-relative already) */
    bool run;
    bool jump;    /* held; a jump starts on the press */
} tc_player_input;

typedef enum tc_anim_state {
    TC_ANIM_IDLE, TC_ANIM_WALK, TC_ANIM_RUN, TC_ANIM_JUMP, TC_ANIM_JUMP_AIR, TC_ANIM_FALL, TC_ANIM_LAND,
    TC_ANIM_COUNT
} tc_anim_state;

/* "idle", "walk", "run", "jump", "jump_air", "fall", "land" (the .ttlv animation names). */
const char *tc_anim_state_name(tc_anim_state s);

typedef struct tc_player {
    tc_collision *world;
    tc_vec3 position;      /* feet */
    tc_vec3 velocity;
    float heading;         /* radians; Ed's local -Y (his front) points along (sin, -cos) */
    bool grounded;
    tc_vec3 start_position;
    float start_heading;

    /* Tunables (engine units, about metres; Ed is ~1.3 tall). */
    float radius, height, walk_speed, run_speed, jump_speed, gravity, max_slope, snap_distance;
    /* Gravity multipliers from Ed's MEC ID cards (field +0x38): airborne and rising, airborne and falling.
     * For takeoff_time after a jump Ed is still in his take-off state, which uses the ground card (x1). */
    float rise_gravity_scale, fall_gravity_scale, takeoff_time;
    bool use_floor_grip;   /* scale ground steering by the floor's grip^4 (off in the sliding profile) */
    float ground_grip;     /* lowest grip among the ground contacts of the last substep (1 = none) */

    tc_anim_state animation;  /* chosen from the movement state */
    float animation_time;     /* seconds since `animation` started */
    /* Lengths of the one-shot clips in seconds (defaults: jump 0.3, land 0.5); set from baked data. */
    float clip_length[TC_ANIM_COUNT];

    bool jump_held;
    float air_time, since_jump, land_timer;
} tc_player;

void tc_player_init(tc_player *p, tc_collision *world, tc_vec3 start, float heading);
void tc_player_respawn(tc_player *p);
/* Level-wide tunables from the world's floor materials: a level with zero-grip floors (Totalski,
 * skymap) gets the sliding profile; otherwise per-triangle grip applies while grounded. */
void tc_player_apply_floors(tc_player *p);
/* Put Ed somewhere else (e.g. below the free-fly camera) with movement state reset. */
void tc_player_teleport(tc_player *p, tc_vec3 position, float heading);
void tc_player_step(tc_player *p, float dt, const tc_player_input *input);
float tc_player_horizontal_speed(const tc_player *p);
/* Ed -> world: translate to the feet, turn by heading (Z up). */
tc_mat4 tc_player_model_matrix(const tc_player *p);
/* Heading of a row-major 3x4 placement: where its local -Y points. */
float tc_heading_of(const tc_mat34 *m);

#ifdef __cplusplus
}
#endif
#endif
