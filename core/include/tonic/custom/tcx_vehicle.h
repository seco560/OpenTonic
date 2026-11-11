/* Vehicles: the port's own, not the original's (Tonic Trouble has none). A toy car for the imported levels (docs/ENGINE.md
 * "Vehicles"): the custom layer puts a car model in a level (tools/ttre/vehicle.py: a PRSO entry with the body on channel 0
 * and a channel per wheel, a PCOL box so Ed bumps into it and can stand on it, and the .ttlv VEHI record below); the core
 * drives it.
 *
 * The car: an arcade model at fixed ticks of TCX_VEHICLE_TICK, drawn between its last two (2026-11-12, second pass: the
 * user asked for it "tighter, more engaging and realistic"). A rear-wheel-drive engine whose push falls off with speed
 * against aero drag, brakes into reverse; two axles with tyres that grip up to a limit (understeer, power oversteer,
 * handbrake slides on Jump; constants below); a hop on Fast; the body drawn on springs. On the ground it follows the static collision under its wheels (a ray each, climbing steps up to
 * TCX_VEHICLE_STEP, tilting with the ground); off it, it falls; three spheres along the body keep it out of steep surfaces
 * (walls) and slide it along them. No obstacles, no persos, no damage. It is put back where it started after
 * TCX_VEHICLE_FALL_RESET seconds in the air (fell off the world).
 *
 * The ride (tcx_ride): Ed gets in with Action within TCX_RIDE_REACH of the car, hopping into the driver's seat over
 * TCX_RIDE_HOP seconds and shrinking to the car's rider_scale on the way (a toy: the user asked for a shrunken Ed inside,
 * 2026-11-12); there he stands in his idle while the move keys drive and Action hops him back out beside the driver's door (his left, the car's +X;
 * the other side, the back or the front when that spot is walled or has no floor), where his own control resumes. The host
 * (tc_viewer) holds his AI and mechanics meanwhile and draws the hop from tcx_ride_pose. */
#ifndef TONIC_TCX_VEHICLE_H
#define TONIC_TCX_VEHICLE_H
#include <stdbool.h>
#include <stdint.h>
#include "../tc_math.h"
#include "../tc_collision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* .ttlv VEHI (tools/ttre/ttlv.py, written by tools/ttre/vehicle.py): u16 n, u16 0, n x {
 *   u16 prso (its PRSO entry: channel 0 the body, the wheels' channels hold each wheel about its centre), u16 collision
 *   id (its PCOL entry's perso id, 0xFFFF none), f32 box_lo[3], f32 box_hi[3] (the body's bounds, car axes: front -Y,
 *   left +X, z = 0 where the wheels touch the ground), u16 n_wheels, u16 0, n_wheels x { u16 channel, u8 steers, u8 0,
 *   f32 centre[3], f32 radius } (car axes), f32 seat[3] (Ed's feet while he drives, car axes), f32 rider_scale (his
 *   size there), u16 name_len, utf8 name } */
#define TCX_VEHICLE_MAX_WHEELS 8
typedef struct tcx_vehicle_wheel {
    uint16_t channel;
    bool steers;
    tc_vec3 centre;
    float radius;
} tcx_vehicle_wheel;
typedef struct tcx_vehicle_data {
    uint16_t prso;
    uint16_t collision_id;           /* 0xFFFF: none */
    tc_vec3 box_lo, box_hi;
    uint16_t wheel_count;
    tcx_vehicle_wheel wheels[TCX_VEHICLE_MAX_WHEELS];
    tc_vec3 seat;                    /* where Ed's feet are while he drives (car axes) */
    float rider_scale;               /* his size there */
    char name[32];
} tcx_vehicle_data;

#define TCX_VEHICLE_TICK (1.0f / 60)
#define TCX_VEHICLE_SUBSTEPS 2           /* the tyres' forces per tick */
/* Engine and brakes (accelerations, u/s^2; the units are about metres): */
#define TCX_VEHICLE_ENGINE_ACCEL 7.0f    /* from standing, while the power allows (0-100 km/h in about 5 s) */
#define TCX_VEHICLE_ENGINE_POWER 230.0f  /* u^2/s^3: the drive is at most this / speed (falls off as it speeds up) */
#define TCX_VEHICLE_AERO 0.0028f         /* drag x speed^2: with the power, a top speed of about 41 u/s (148 km/h) */
#define TCX_VEHICLE_ROLL 0.6f            /* rolling resistance */
#define TCX_VEHICLE_BRAKE 13.0f          /* the tyres' limit, as the cornering's */
#define TCX_VEHICLE_REVERSE_ACCEL 6.0f
#define TCX_VEHICLE_MAX_REVERSE 9.0f
#define TCX_VEHICLE_HANDBRAKE 5.0f       /* the rear wheels locked: slows it ... */
#define TCX_VEHICLE_HANDBRAKE_REAR 0.4f  /* ... and leaves the rear this share of its grip: it slides out */
#define TCX_VEHICLE_HANDBRAKE_ON 0.1f    /* s: the handbrake eased in (no snap as it bites) ... */
#define TCX_VEHICLE_HANDBRAKE_OFF 0.35f  /* ... and the rear's grip back (no snap as it hooks up) */
/* The tyres (above TCX_VEHICLE_DYNAMIC_SPEED forward): per axle a sideways force from its slip angle, stiffness
 * TCX_VEHICLE_CORNERING x the axle's share of the weight, up to TCX_VEHICLE_GRIP x that share; the drive (rear wheels) uses
 * up the rear's grip first (the friction circle: power oversteer). The yaw follows from the two forces about the centre
 * of mass (inertia = mass x a x b, the axles' distances from it). Below that speed, and in reverse, the plain bicycle
 * model (no slip) with the sideways speed damped. */
#define TCX_VEHICLE_GRIP 16.0f           /* u/s^2 sideways at the limit, the whole car (user 11-12: 13 too slippery) */
#define TCX_VEHICLE_CORNERING 150.0f     /* u/s^2 per radian of slip, the whole car */
#define TCX_VEHICLE_DYNAMIC_SPEED 7.0f   /* the tyres' model from here up; below TCX_VEHICLE_KINEMATIC_SPEED the bicycle's, */
#define TCX_VEHICLE_KINEMATIC_SPEED 3.0f /* blended between (no snap as a slide slows down) */
#define TCX_VEHICLE_FRONT_GRIP 0.92f     /* the front's limit a little under its share: it runs wide before it spins */
#define TCX_VEHICLE_REAR_STIFFNESS 1.25f /* ... and the rear tyres stiffer: settles straight when let go */
#define TCX_VEHICLE_DRIVE_SLIP 0.4f      /* the share of the drive that uses up the rear's grip */
#define TCX_VEHICLE_INERTIA 1.3f         /* x mass x a x b: the yaw's inertia (less twitchy than a bare ab)*/
#define TCX_VEHICLE_STEER_MAX 0.55f      /* rad at a standstill; / (1 + speed / TCX_VEHICLE_STEER_FADE) */
#define TCX_VEHICLE_STEER_FADE 16.0f
#define TCX_VEHICLE_SLIDE_LOCK 0.5f      /* rad of slip at which the full lock is back (to catch a slide) */
#define TCX_VEHICLE_STEER_RATE 3.5f      /* rad/s towards the wanted angle (back to straight at twice that) */
#define TCX_VEHICLE_GRAVITY 20.0f
#define TCX_VEHICLE_HOP 7.2f             /* u/s up on Jump (about 1.3 high at this gravity) */
#define TCX_VEHICLE_STEP 0.7f            /* the wheels' rays start this far above them: kerbs and steps it climbs */
#define TCX_VEHICLE_SNAP 0.45f           /* ground this far below the wheels still holds it (down a ramp, off a kerb) */
#define TCX_VEHICLE_FALL_RESET 4.0f      /* s in the air: put back at the start */
/* The body on its springs (drawn only: the wheels and the collision don't move with it): it pitches with the
 * acceleration, leans out of the turns, bounces on landing. */
#define TCX_VEHICLE_BODY_PITCH 0.004f    /* rad per u/s^2 along */
#define TCX_VEHICLE_BODY_ROLL 0.005f     /* rad per u/s^2 across */
#define TCX_VEHICLE_BODY_FREQ 9.0f       /* rad/s, the springs' */
#define TCX_VEHICLE_BODY_DAMP 0.45f      /* share of critical damping: a little overshoot */
#define TCX_VEHICLE_CHASE_TIME 0.35f     /* s: the play camera turns in behind the car being driven (tc_cam_target.chase) */

typedef struct tcx_vehicle_input {
    float throttle;                  /* -1 (brake, then reverse) .. 1 */
    float steer;                     /* -1 left .. 1 right */
    bool handbrake;
    bool hop;                        /* held: a hop starts on the press, on the ground */
} tcx_vehicle_input;

typedef struct tcx_vehicle {
    const tcx_vehicle_data *data;
    tc_vec3 position;                /* the ground point under the body's centre (car z = 0) */
    float heading;                   /* as Ed's: the front, local -Y, points along (sin, -cos) */
    tc_vec3 velocity;
    tc_vec3 up;                      /* the body's up (tilted with the ground), unit */
    float steer;                     /* the front wheels' angle, rad, + = to the right */
    float spin;                      /* the wheels' roll, rad */
    bool grounded;
    float air_time;
    tc_vec3 start_position;
    float start_heading;
    float tick_time;                 /* seconds not yet stepped */
    tc_vec3 position_prev, up_prev;  /* the tick before: drawn between it and now */
    float heading_prev, spin_prev, steer_prev;
    float wheelbase;                 /* front axle to rear, for the steering */
    float to_front, to_rear;         /* the centre of mass (the body's middle) to each axle */
    float yaw_rate;                  /* rad/s, + = turning left (the heading rises) */
    float forward_prev;              /* last tick's forward speed (the body's pitch) */
    bool hop_held;
    float handbrake;                 /* 0..1, eased (TCX_VEHICLE_HANDBRAKE_ON / _OFF) */
    float pitch, roll, heave;        /* the body on its springs: nose up, left side down (rad), up (units) */
    float pitch_rate, roll_rate, heave_rate;
    float pitch_prev, roll_prev, heave_prev;
} tcx_vehicle;

/* At `placement` (row-major 3x4, as a perso's world: its local -Y the front), dropped onto the ground below it. */
void tcx_vehicle_init(tcx_vehicle *c, const tcx_vehicle_data *d, const tc_mat34 *placement, tc_collision *world);
/* dt seconds on, in fixed ticks: in = NULL coasts (nobody inside). */
void tcx_vehicle_update(tcx_vehicle *c, tc_collision *world, const tcx_vehicle_input *in, float dt);
/* Body -> world now (between the last two ticks), and a channel's transform (channel -> car: a wheel's roll and
 * steering about its centre; identity for the body). */
tc_mat34 tcx_vehicle_world(const tcx_vehicle *c);
tc_mat34 tcx_vehicle_channel(const tcx_vehicle *c, uint16_t channel);
/* Forward speed (signed), and the drawn position and heading (between the ticks). */
float tcx_vehicle_speed(const tcx_vehicle *c);
tc_vec3 tcx_vehicle_drawn_position(const tcx_vehicle *c);
float tcx_vehicle_drawn_heading(const tcx_vehicle *c);
/* A point in car axes -> world, at the drawn pose. */
tc_vec3 tcx_vehicle_point(const tcx_vehicle *c, tc_vec3 local);

/* ---- Ed and the car ---- */
#define TCX_RIDE_REACH 1.6f               /* from the body's footprint, along the ground */
#define TCX_RIDE_HEIGHT 2.5f              /* Ed's feet within this of the car's ground, up or down */
#define TCX_RIDE_HOP 0.55f                /* s, each hop */
#define TCX_RIDE_HOP_HEIGHT 0.9f          /* above the higher end, less what a ceiling over either end leaves */
#define TCX_RIDE_ED_HEIGHT 2.2f           /* his head above his feet, for those ceilings (his model is 2.17 tall) */
#define TCX_RIDE_DOOR 1.1f                /* where he lands: this far out from the body's side */

typedef enum tcx_ride_phase { TCX_RIDE_NONE, TCX_RIDE_BOARDING, TCX_RIDE_DRIVING, TCX_RIDE_LEAVING } tcx_ride_phase;

typedef struct tcx_ride {
    tcx_ride_phase phase;
    int32_t vehicle;                 /* the host's index, -1 none */
    float time;                      /* seconds into the hop */
    tc_vec3 from, to;                /* the hop's ends; boarding: `to` in car axes (the car may be rolling) */
    float heading;                   /* Ed's while hopping */
    float lift;                      /* the arc's top above the higher end */
} tcx_ride;

/* Whether Ed at `feet` is close enough to get in. */
bool tcx_ride_in_reach(const tcx_vehicle *c, tc_vec3 feet);
/* Start the hop in, from feet; the hop out of car c (choosing where he lands; false: nowhere free, he stays in). */
void tcx_ride_board(tcx_ride *r, int32_t vehicle, const tcx_vehicle *c, tc_vec3 feet, tc_collision *world);
bool tcx_ride_leave(tcx_ride *r, const tcx_vehicle *c, tc_collision *world);
/* dt on: true when the hop has ended this call (BOARDING -> DRIVING, LEAVING -> NONE: put Ed at r->to, his control back). */
bool tcx_ride_step(tcx_ride *r, float dt);
/* Ed's feet, heading and size during the ride (in the seat while driving), and the hop's share done (0..1; 1 driving). */
void tcx_ride_pose(const tcx_ride *r, const tcx_vehicle *c, tc_vec3 *feet, float *heading, float *scale, float *share);
/* Ed -> world in the seat: the car's pose, the seat, his scale (his front, local -Y, the car's). */
tc_mat34 tcx_ride_seat_matrix(const tcx_vehicle *c);

#ifdef __cplusplus
}
#endif
#endif
