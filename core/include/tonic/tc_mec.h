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

/* Mechanical environment (MecEnv, docs/NOTES.md "Swimming"): gravity and the fluid's density (+0x14): air 1.3 (every
 * level), water 1000 (below a sector's water surface). */
typedef struct tc_mec_env { float gravity, density, drag; } tc_mec_env;
tc_mec_env tc_mec_env_default(void);
/* The environment at p (FUN_00451b90 with the .ttai WATR water planes): among the planes one of whose sector boxes
 * holds p, the one nearest p in height; p below it: its environment and the depth (> 0, the scripts' EnvHeight), else
 * the air and 0 (the exe writes the depth only below a surface). The nearest-in-height rule stands in for the
 * engine's choice of the perso's sector, where sector boxes overlap (sud0's top pool above the falls' foot).
 * world (NULL: not checked): p counts as dry when solid ground lies straight above it below the surface (a ceiling,
 * i.e. a face turned down towards p). The water planes are bounded only by sector boxes, which overlap the dry places
 * under a level's water (sud0: the tunnel under Doc's house below the river, the cave under the well); the exe asks the
 * perso's sector, which the port does not have (docs/NOTES.md "Ghost water"). */
float tc_mec_env_at(const tc_ai_model *m, tc_collision *world, tc_vec3 p, tc_mec_env *env);

/* The exe applies some fractions once per mechanics frame (friction, the board turning towards the ground): a fixed
 * share k of the way each frame, so their effect over a second depends on the frame rate. Its mechanics frame is
 * 1/30 s (the re-snap scales by 30 dt past it; 1/30 and 30 sit with the mechanics constants). The port steps once per
 * displayed frame, so such a k is converted to the frame's dt: 1 - (1 - k)^(dt / TC_MEC_FRAME), the same share per
 * 1/30 s at any frame rate. k outside (0, 1) is returned as is (0: nothing, 1: all at once). */
#define TC_MEC_FRAME (1.0f / 30)
float tc_mec_per_frame(float k, float dt);

/* Type 5 control state (dynamics +0x2B0..+0x2C4). */
typedef struct tc_mec_fly {
    float attack;                /* +0x2B0 angle of attack, degrees: Up (mode 1) lowers it, Down (2) raises it */
    float bank;                  /* +0x2B4 bank angle, degrees: Left (3) lowers it, Right (4) raises it */
    float thrust;                /* +0x2B8 mode 10's boost target (no force uses it in the code decoded so far) */
    float attack_lagged;         /* +0x2BC attack, rad, after a first-order lag (FUNC_91 Report2BC) */
    float bank_lagged;           /* +0x2C0 bank, rad, lagged (FUNC_92 Report2C0); the frame rolls by this */
    float thrust_lagged;         /* +0x2C4 */
    /* The flight frame (dynamics +0x14, FUN_004a9993 in the request handler): x along the velocity, y to the left,
     * z up in the vertical plane through x, then rolled about x by bank_lagged. The forces act in it and the model is
     * drawn from it (tc_mec_fly_model_axes). Kept while the speed is too low to build one. */
    tc_vec3 frame_x, frame_y, frame_z;
    tc_vec3 pending_dv;          /* mode 21's velocity change: a force in the exe's force list, applied by the integrator */
} tc_mec_fly;

/* Switching to a type 5 card (GAM_0042ab20): controls reset, the velocity becomes card +0x48 (Ed: 10) along the
 * facing direction. forward = Ed's front in world space (unit). */
void tc_mec_fly_enter(tc_mec_fly *f, const tc_ai_card *card, tc_vec3 forward, tc_vec3 *velocity);
/* One request (FUN_004aea7c): mode = the AI's SetMechMode (0 = the engine's default request); proposal = the
 * AI's proposal slots ([0xB] bank step, [0x18] attack step, [1..3] mode 21's velocity change); velocity = the
 * perso's (read only: the frame is built along it). */
void tc_mec_fly_request(tc_mec_fly *f, const tc_ai_card *card, int mode, const float *proposal, float dt,
                        const tc_vec3 *velocity);
/* The integrator's dynamics (FUN_004ac0da up to the collision step): gravity, lift and drag in the frame along the
 * velocity rolled by bank_lagged, new velocity capped at card +0x44. Returns the new velocity; the caller moves
 * the perso by velocity x dt with collisions. */
tc_vec3 tc_mec_fly_velocity(tc_mec_fly *f, const tc_ai_card *card, tc_mec_env env, tc_vec3 velocity, float dt);
/* The model's axes in flight (FUN_004c8dfc, the integrator's last step), world space: with the card's roll byte
 * (+0x4C; Ed's: 1) model x = frame y, model y = frame z, model z = frame x: the head leads, the back faces the
 * frame's up (Ed's script sets his sight axis to +Z in flight, B26); without it the same from the frame levelled
 * (its z turned onto world up). */
void tc_mec_fly_model_axes(const tc_mec_fly *f, const tc_ai_card *card, tc_vec3 *x, tc_vec3 *y, tc_vec3 *z);

/* ---- Type 1: swimming (Ed's states 6, 7, 8, 16, 17, 20-24, 36, 37, 151, 152, 211-214) ----
 * A body in a fluid (docs/NOTES.md "Swimming"): weight, buoyancy by the immersed volume, quadratic drag, and a thrust
 * the AI aims with a direction. Request handler FUN_004af8f0, integrator FUN_004abbb8 with forces FUN_004c4af0. */
typedef struct tc_mec_swim {
    float polar;                 /* +0x2B0 degrees from +Z (0 up, 90 level, 180 down) */
    float heading;               /* +0x2B4 degrees; the direction is (cos h sin p, sin h sin p, cos p) */
    float thrust;                /* +0x2B8 grows by proposal [0] per request, reset when the lag catches up (strokes) */
    float polar_lagged;          /* +0x2BC rad */
    float heading_lagged;        /* +0x2C0 rad */
    float thrust_lagged;         /* +0x2C4 */
    tc_vec3 thrust_force;        /* this step's thrust (the request's entry in the exe's force list) */
} tc_mec_swim;
/* Entering a type 1 card (GAM_0042ab20): heading = the perso's facing (yaw - pi / 2 in the exe's terms), polar 0
 * (clamped to the card's range at the first request: an idle swimmer is upright). heading = tc_player's. */
void tc_mec_swim_enter(tc_mec_swim *s, float heading);
/* One request: mode 0 decays the polar angle (x card +0x38), 1 / 2 polar + / - proposal [0x18], 3 / 4
 * heading +/- [0xB], 10 thrust += [0], 0x14 polar / heading from the direction [7..9] and thrust += [0]. */
void tc_mec_swim_request(tc_mec_swim *s, const tc_ai_card *card, int mode, const float *proposal, float dt);
/* The forces and the new velocity: the environment and depth at the probe 0.5 x body size below position (*depth =
 * the scripts' EnvHeight while swimming), weight, buoyancy rho g V, drag -rho Cd s^2 min(|v|, 10) v, the thrust. */
tc_vec3 tc_mec_swim_velocity(tc_mec_swim *s, const tc_ai_card *card, const tc_ai_model *m, tc_collision *world,
                             tc_vec3 position, tc_vec3 velocity, float dt, float *depth);
/* The model's axes (FUN_004c91d2 = the frame Rz(heading + pi / 2) Rx(polar + offset)): z along the swim direction (the
 * head), y the back (up when level), x the left. */
void tc_mec_swim_model_axes(const tc_mec_swim *s, const tc_ai_card *card, tc_vec3 *x, tc_vec3 *y, tc_vec3 *z);

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

/* ---- Types 0 and 4: walking, jumping, falling (Ed's ground and air cards) ----
 * Integrator FUN_004aa6af (both types) with forces/grip FUN_004c41c5 and the energy cut-off FUN_004c39f0; request
 * handlers FUN_004ae392 (type 0) and FUN_004b0f2a (type 4; it clears the contact report every frame, so an air
 * card never counts as grounded); collide-and-move MEC_004be1bd with contact classification MEC_004bc477 and the
 * responses MEC_004bb904 (ground, steep slope), MEC_004bbc4c (wall, with the step-up MEC_004bb06e), MEC_004bc20a
 * (overhang, ceiling); ground re-snap MEC_004bf9f0. The body is the state's ZDM spheres (tc_ai_sphere_set), swept
 * against the world's triangles. */

/* Report bits (dynamics +0xC4; the AI's contact tests read them): as TC_AI_CONTACT_*, plus 0x40 = stepped up. */
#define TC_MEC_STEPPED 0x40u

typedef struct tc_mec_walk {
    uint32_t report;             /* this frame's contacts (+0xC4) */
    uint32_t report_prev;        /* last frame's (+0xC0) */
    tc_vec3 ground_normal;       /* contact record +0xCC (ground, steep slope) */
    tc_vec3 wall_normal;         /* contact record +0x148 (wall, overhang, ceiling) */
    float ground_grip;           /* the ground contact's floor material mat[2] (1 = none) */
    uint32_t ground_tri, wall_tri; /* the contacts' triangles (tc_tri.source; UINT32_MAX none): their materials */
    uint16_t ground_own, wall_own; /* the collide flags of the body part that made them (tc_ai_sphere.flags) */
    uint32_t blocked;            /* frames the collide loop gave up (6 responses, still blocked: no move), debug */
    uint32_t passes;             /* responses in the last frame's collide loop, debug */
} tc_mec_walk;

void tc_mec_walk_reset(tc_mec_walk *w);
/* What the AI sees (+0xC8 = +0xC0 | +0xC4): contacts of this frame and the one before. */
uint32_t tc_mec_walk_contacts(const tc_mec_walk *w);
/* Request handler side for an air card (type 4, FUN_004b0f2a): forget the contacts. */
void tc_mec_walk_air(tc_mec_walk *w);
/* Contact kinds of a sphere against a triangle (FUN_00465b60, hit record +0x7A). */
enum { TC_MEC_VERTEX = 0, TC_MEC_FACE = 1, TC_MEC_EDGE = 2 };
/* One sphere moving from p0 by d against one triangle (n its unit normal, the winding's), FUN_00465b60: the face when
 * the sphere moves into its front (touching counts), else the earliest of the three edges, else the earliest of the
 * three corners. On a hit: t in 0..1, *kind TC_MEC_*, the normal (face: n; edge, corner: from the touched point to the
 * centre, divided by r) and the touched point. Returns 0, or 1 (the face, the corners, edge a-b), 2 (edge a-c),
 * 3 (edge b-c). The exe runs it in the obstacle's axes; the port's triangles are in world space already. */
int tc_mec_sphere_tri(tc_vec3 p0, tc_vec3 d, float r, tc_vec3 n, tc_vec3 a, tc_vec3 b, tc_vec3 c, int *kind, float *t,
                      tc_vec3 *normal, tc_vec3 *point);
/* Earliest contact of the body (spheres and points in the perso's axes, turned by heading as tc_player) moving
 * from -> to, as
 * FUN_0043bff0: t in 0..1, the contact normal (away from the surface), point, kind (TC_MEC_*), the touching sphere's
 * zone type (FUN_0043bf70 on its collide flags: 1, 4, 3, 2 for flag 1, 2, 4, 8; -1 none) and triangle. Spheres
 * flagged 0x10 do not collide; those flagged 8 sweep apart and count only on floors (normal z >= 0.93). No motion:
 * no contact (FUN_00464c80). */
typedef struct tc_mec_hit {
    float t;
    tc_vec3 normal, point;
    int kind, zone_type;
    uint16_t own_flags;          /* the touching part's collide flags (the record's own material, +0x70) */
    float own_rebound, own_grip; /* its mechanics material (record +0x50, +0x54): times the obstacle's */
    bool own_point;              /* a point made it (own shape 0; spheres 3) */
    const tc_tri *tri;
} tc_mec_hit;
/* A point moving from p0 by d against one triangle, FUN_0046ff70 -> FUN_004683b0 -> FUN_00467d40: only the face, only
 * moving into its front, strictly from in front of the plane (no edges or corners). On a hit t in 0..1 and the point. */
int tc_mec_point_tri(tc_vec3 p0, tc_vec3 d, tc_vec3 n, tc_vec3 a, tc_vec3 b, tc_vec3 c, float *t, tc_vec3 *point);
bool tc_mec_sweep(tc_collision *world, const tc_ai_sphere_set *body, float heading, tc_vec3 from, tc_vec3 to,
                  tc_mec_hit *hit);
/* Collide-and-move (MEC_004be1bd; MEC_004bedd6 for the type 2 integrator, same loop): move by velocity x dt, up to 6
 * sweep-and-respond passes, the report shifted (report_prev := report, report := this move's bits). card NULL = no
 * card (the skier's integrator passes none): the default classes (cos 45 / 60 / 120 / 135 deg), no step-up, no
 * softening on steep slopes and walls. Returns the bits. */
uint32_t tc_mec_collide_move(tc_mec_walk *w, const tc_ai_card *card, tc_collision *world, const tc_ai_sphere_set *body,
                             float heading, float dt, tc_vec3 *position, tc_vec3 *velocity);
/* MEC_004bf9f0: ground within distance below with normal . up >= min_cos -> stand 0.01 above it (report |= ground). */
bool tc_mec_resnap(tc_mec_walk *w, tc_collision *world, const tc_ai_sphere_set *body, float heading, float distance,
                   float min_cos, tc_vec3 *position);
/* One frame of FUN_004aa6af. wanted = the request handlers' motion this frame (world velocity: root motion or a
 * move direction; ignored in the air unless wanted_in_air, the type 4 handler's +0xB8 = 8); impulse != NULL: a
 * velocity that replaces the old one (mode 7, the jump). Gravity = env x card +0x38 in the air or on grip < 1;
 * on a grippy ground (grip 1) last frame's velocity is dropped and only wanted moves Ed. Then collide-and-move
 * and, when the ground was lost two frames running, the re-snap within card +0x34. */
void tc_mec_walk_step(tc_mec_walk *w, const tc_ai_card *card, tc_mec_env env, tc_collision *world,
                      const tc_ai_sphere_set *body, float heading, tc_vec3 wanted, bool wanted_in_air,
                      const tc_vec3 *impulse, float dt, tc_vec3 *position, tc_vec3 *velocity);

#ifdef __cplusplus
}
#endif
#endif
