/* The exe's mechanics (MEC/DNM), reimplemented one ID card type at a time (docs/NOTES.md "MEC physics").
 *
 * Every frame the engine runs, per perso, the request queue the AI filled (SetMechMode*: {mode, the state's ID card};
 * an empty queue gets one default request): first the card type's request handler once per request (it turns the
 * mode and the proposal into control values), then the card type's integrator once (forces -> velocity -> position,
 * collisions). Handlers come in pairs per card type (table GAM_0042a7e0).
 *
 * Done: type 5, Ed's flight (states 54, 183, 185, 268 ...): a glider. Request handler FUN_004aea7c, integrator
 * FUN_004ac0da with forces FUN_004c478d and polars FUN_004caa9b / FUN_004caae9.
 * Type 2, the skier's board (Totalski, skymap): a sled. Request handler FUN_004b00bf, integrator FUN_004ab91d with
 * forces FUN_004c4ecd and speed cap FUN_004c3af0. */
#ifndef TONIC_TC_MEC_H
#define TONIC_TC_MEC_H
#include <stdbool.h>
#include "tc_math.h"
#include "tc_ai.h"
#include "tc_collision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mechanical environment of the sector (sector data +4 -> {gravity, direction, ...}); bar and Totalski: 9.81, 1. */
typedef struct tc_mec_env { float gravity, density; } tc_mec_env;
tc_mec_env tc_mec_env_default(void);

/* Type 5 control state (dynamics +0x2B0..+0x2C4). */
typedef struct tc_mec_fly {
    float attack;                /* +0x2B0 angle of attack, degrees: Up (mode 1) lowers it, Down (2) raises it */
    float bank;                  /* +0x2B4 bank angle, degrees: Left (3) lowers it, Right (4) raises it */
    float thrust;                /* +0x2B8 mode 10's boost target (no force uses it in the code decoded so far) */
    float attack_lagged;         /* +0x2BC attack, rad, after a first-order lag (FUNC_91 Report2BC) */
    float bank_lagged;           /* +0x2C0 bank, rad, lagged (FUNC_92 Report2C0); the frame rolls by this */
    float thrust_lagged;         /* +0x2C4 */
} tc_mec_fly;

/* Switching to a type 5 card (GAM_0042ab20): controls reset, the velocity becomes card +0x48 (Ed: 10) along the
 * facing direction. forward = Ed's front in world space (unit). */
void tc_mec_fly_enter(tc_mec_fly *f, const tc_ai_card *card, tc_vec3 forward, tc_vec3 *velocity);
/* One request (FUN_004aea7c): mode = the AI's SetMechMode (0 = the engine's default request); proposal = the
 * AI's proposal slots ([0xB] bank step, [0x18] attack step, [1..3] mode 21's velocity change). */
void tc_mec_fly_request(tc_mec_fly *f, const tc_ai_card *card, int mode, const float *proposal, float dt,
                        tc_vec3 *velocity);
/* The integrator's dynamics (FUN_004ac0da up to the collision step): gravity, lift and drag in the frame along the
 * velocity rolled by bank_lagged, new velocity capped at card +0x44. Returns the new velocity; the caller moves
 * the perso by velocity x dt with collisions. */
tc_vec3 tc_mec_fly_velocity(const tc_mec_fly *f, const tc_ai_card *card, tc_mec_env env, tc_vec3 velocity, float dt);

/* ---- Type 2: sliding (the skier) ---- */

/* The contacts of the last collision step, as the handlers read them (dynamics report +0xC4 and the contact
 * records +0xCC ground / +0x148 other). */
typedef struct tc_mec_contact {
    bool ground, wall;           /* report bits 1|2 (ground, steep slope) and 4|8|0x10 (wall, overhang, ceiling) */
    tc_vec3 ground_normal, wall_normal;
    float ground_grip;           /* floor material mat[2] (snow 0; no material 1) */
    float ground_factor;         /* floor material mat[0] (snow 1; no material 1): scales steering and edge grip */
} tc_mec_contact;

typedef struct tc_mec_ski {
    tc_vec3 x, y, z;             /* orientation: the perso's local axes in world space (front = -y) */
    tc_vec3 omega;               /* angular velocity (world, rad/s), set from scratch every frame */
    tc_vec3 force;               /* this frame's force list, summed */
    bool near_ground;            /* dynamics +0x2F0: the downward probe found ground last frame */
} tc_mec_ski;

/* Switching to a type 2 card: upright, facing heading (as tc_player: front along (sin, -cos)). */
void tc_mec_ski_enter(tc_mec_ski *s, float heading);
/* One request (FUN_004b00bf): steering torque (modes 3/4: proposal [0xD]), push and jump forces (mode 10:
 * [2] along y, [1] along the move direction [7..9], [3] along z), edge grip and weathervane. A jump (mode 10
 * with [3] != 0) clears *c (the exe clears the report). */
void tc_mec_ski_request(tc_mec_ski *s, const tc_ai_card *card, int mode, const float *proposal, tc_mec_contact *c,
                        tc_vec3 velocity);
/* The integrator (FUN_004ab91d up to the collision step): gravity x card +0x34 (x +0x40 more in the air), the
 * ground's reaction and friction (grip^2), the probe below the board, alignment of z to the surface; returns the
 * new velocity (capped at card +0x14) and turns the axes by omega. feet = the perso's position. */
tc_vec3 tc_mec_ski_velocity(tc_mec_ski *s, const tc_ai_card *card, tc_mec_env env, const tc_mec_contact *c,
                            tc_collision *world, tc_vec3 feet, tc_vec3 velocity, float dt);
/* tc_player-style heading of the board's front (-y). */
float tc_mec_ski_heading(const tc_mec_ski *s);

#ifdef __cplusplus
}
#endif
#endif
