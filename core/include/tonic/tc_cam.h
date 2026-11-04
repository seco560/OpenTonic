/* The camera: the view (tc_camera) and the play camera that follows Ed (tc_cam). docs/CAMERA.md has the original's
 * camera, what is read from the exe, and which parts are the port's own.
 *
 * The play camera is the original's camera perso, ticked at the game's 30 Hz (TC_CAM_TICK) and drawn between its last
 * two ticks. Each tick, as the exe's camera module and type 3 mechanics do (docs/CAMERA.md "Fifth pass"):
 *   - Target: Ed's position averaged over the last 8 ticks within 0.4 s (GAM_0042b730, window DAT_00521344 = 0.4), so his
 *     30 Hz steps, bobbing and landings don't shake the view; the pivot is that plus the aim height.
 *   - Wanted spot (FUN_004a2380): the camera keeps its current direction from the pivot (the lazy follow) at the preset's
 *     elevation, its distance clamped into the preset's band (B2: 7 +- 0.3) and stretched in front of Ed.
 *   - The mover (DNM_RequestType3, FUN_004b29fa / FUN_004b2e4b): towards the wanted spot at most at the camera's speed
 *     (2 x Ed's averaged speed, 12 .. 200 u/s: FUN_004a2a10), easing in near it (a step <= d / (10 - d)) and gaining
 *     speed gradually (a step <= the last step + speed x tick / 10): camera B0's SetCameraPair70(10, 10).
 *   - The look-at point lags: 1/10 of the way to the pivot per tick (FUN_004b514b; B0's SetCameraPair7C(10, 10)),
 *     held within TC_CAM_LOOK_OFF_MAX of Ed (the port's: a pulled-in eye near him would otherwise look away).
 *   - Collision: the eye, on the line from the pivot to the rig, never stands past a surface (the camera's sphere,
 *     r 0.37): in at once, back out at the mover's pace, and checked again each frame from where Ed is drawn. That is
 *     the port's; the original's body slides along walls (FUN_004b3d33) and the watchdog below cuts away when Ed is
 *     hidden. The rig itself is not pulled in: its direction from Ed stays steady in tight rooms.
 *   - Watchdog (FUN_004a56e0 / 004a5600, every 8 ticks: B0's PROC_198(15, 8)): pulled in by a wall, it looks for an
 *     open spot among the wanted offset turned round Ed in steps of pi / 4 (FUN_004a4f20's table, FUN_004a3760). The
 *     original looks from the second range's minimum and cuts; the port only when the eye is nearer than
 *     TC_CAM_SWING_NEAR, and swings round.
 * The player's part (the port's, 2026-11-01): mouse, stick and look keys turn the camera round Ed and raise or lower it
 * at once (between ticks too); it stays where it was put (the lazy follow); recentre (X, middle button, L2) swings it
 * behind him; 5 the close-up preset, 0 first person (at his head). The player's height change eases back while Ed
 * moves. Not ported: the level's scripted shots, swimming / code 17 presets, the fall's speed-ups. */
#ifndef TONIC_TC_CAM_H
#define TONIC_TC_CAM_H
#include <stdbool.h>
#include "tc_math.h"
#include "tc_collision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Camera in engine space (Z up). Hor+: widescreen widens the view, never stretches it. */
typedef struct tc_camera {
    tc_vec3 position;
    float yaw;            /* radians, 0 = looking along +Y */
    float pitch;          /* radians, positive = up */
    float vertical_fov;   /* radians: the vertical field of view at 4:3 and wider (TC_CAMERA_FOV; tc_camera_fov_y) */
    float near_z, far_z;
} tc_camera;

/* The original's field of view: 1.5 rad horizontal (every engine camera, GLIGLOU_004420e0(cam, 1.5, 1.0); the camera
 * record's +0x8C), the vertical following from the viewport (GLIGLOU_004421a0 case 0x67): at 4:3, 2 atan(tan(0.75) 3/4)
 * = 69.88 degrees. The port keeps that vertical from 4:3 up (wider windows see more at the sides, "Hor+"; the original
 * would have cropped top and bottom) and below 4:3 holds the horizontal instead (never narrower than the original). */
#define TC_CAMERA_HFOV_43 1.5f
#define TC_CAMERA_FOV 1.21966f        /* 2 atan(tan(0.75) x 0.75): the vertical at 4:3 */
void tc_camera_init(tc_camera *c);                /* at the origin, the original's FOV, near 0.05, far 2000 */
/* The vertical field of view used at this aspect (width / height): vertical_fov from 4:3 up, else the horizontal of
 * 4:3 held. */
float tc_camera_fov_y(const tc_camera *c, float aspect);
/* The horizontal field of view at 4:3 of a vertical_fov (what the title shows and , . step: the original's measure). */
float tc_camera_hfov_43(float vertical_fov);
float tc_camera_vfov_from_hfov_43(float hfov);
tc_vec3 tc_camera_forward(const tc_camera *c);    /* (sin yaw cos pitch, cos yaw cos pitch, sin pitch) */
tc_vec3 tc_camera_right(const tc_camera *c);      /* (cos yaw, -sin yaw, 0) */
/* Right-handed view, perspective with 0..1 clip-space depth (Metal, SDL_GPU), reversed: 1 at near, 0 at far. Column-major.
 * Frontends clear depth to 0 and test GREATER_OR_EQUAL (the original's D3D ZFUNC), and pull decals towards the camera
 * with a positive depth bias. */
#define TC_DEPTH_REVERSED 1
tc_mat4 tc_camera_view_projection(const tc_camera *c, float aspect);

/* The follow's numbers. From the original's data (the same in every level checked): */
#define TC_CAM_AIM_HEIGHT 1.35f     /* pivot above Ed's origin: the record's +0x2C, camera B2's PROC 172 offset */
#define TC_CAM_ELEVATION 0.45f      /* rad above the horizontal, looking down at the pivot: camera dsg113 (B2) */
#define TC_CAM_DISTANCE 7.0f        /* camera dsg114 (B2); the wheel zooms it (tc_viewer.orbit_distance) */
#define TC_CAM_FRONT_STRETCH 1.5f   /* camera dsg58: distance x 1 behind Ed .. x this in front of him, linear in angle */
#define TC_CAM_RADIUS 0.37f         /* the camera perso's collision sphere (its ZDM, both states; Camera.cst's dev value 0.35) */
/* What Ed's scripts tell the camera (its dsg40; camera reflex 2 picks a preset by it). The presets read so far: */
#define TC_CAM_SIT_DYING 11         /* camera B12: distance 2.2 .. 3 */
#define TC_CAM_SIT_FLIGHT 12        /* camera B16: distance 3.5 .. 4.5, all angles 0 (level with the pivot); its two
                                       offsets ((0, 0, 5), (0, 0, -2.5)) are not understood and not used */
#define TC_CAM_SIT_FALL 15          /* camera B18 -> B17: the jump's height hold goes on (below) */
#define TC_CAM_SIT_JUMP 16          /* camera B26 -> B30: the follow's distance; the camera's height held where it was
                                       (PROC 188 / 190 set its z band to its z), until Ed is 3 below his take-off pivot
                                       less 0.2 (B17: dsg74 - 3), flies (B15 clears it) or is back to normal */
#define TC_CAM_DYING_DISTANCE 2.6f
#define TC_CAM_FLIGHT_DISTANCE 4.0f
#define TC_CAM_FLIGHT_ELEVATION 0.0f
#define TC_CAM_CLOSEUP_ELEVATION 0.15f /* Numpad 5's close-up (camera dsg54): elevation dsg116, distance dsg117 / 118, */
#define TC_CAM_CLOSEUP_DISTANCE 4.0f   /* stretched up to 2.5x in front of Ed (dsg58) */
#define TC_CAM_CLOSEUP_STRETCH 2.5f
/* The original's camera module (the exe's numbers; docs/CAMERA.md "Fifth pass"): */
#define TC_CAM_TICK (1.0f / 30)     /* the camera perso runs once per engine frame: 30 Hz (the AI's tick) */
#define TC_CAM_HISTORY 8            /* positions the dynamics keep (GAM_DynamHistPosition_0042af40) */
#define TC_CAM_AVERAGE_WINDOW 0.4f  /* s: the target's averaged position (DAT_00521344; Camera.cst says 0.75) */
#define TC_CAM_SPEED_MIN 12.0f      /* u/s: the camera's linear speed, 2 x the target's averaged one, within these */
#define TC_CAM_SPEED_MAX 200.0f     /* (record +0x6C / +0x68, PROC 165) */
#define TC_CAM_EASE 10.0f           /* the mover's ease-in: a step <= d / (EASE - d) (record +0x74, B0: 10) */
#define TC_CAM_ACCEL 10.0f          /* the mover's speeding up: a step <= last step + speed x tick / ACCEL (+0x70) */
#define TC_CAM_LOOK_LAG 10.0f       /* the look-at point closes 1 / LOOK_LAG of the way per tick (+0x7C, B0: 10) */
#define TC_CAM_BAND 0.3f            /* the distance band round the preset's (B2: dsg114 -+ 0.3) */
#define TC_CAM_NEAR_RANGE 1.3f      /* the second range's minimum below the distance (B2: -1.3; FUN_004a5600) */
#define TC_CAM_WATCH_TICKS 8        /* ticks between the watchdog's looks (DAT_0052136c, B0: PROC_198(15, 8)) */
#define TC_CAM_SEARCH_STEP 0.7853982f /* the candidate spots: the wanted offset turned by k pi / 4 (DAT_00521338) */
/* The port's own (not the original's): */
#define TC_CAM_GRACE 0.5f           /* s after the last look input before the player's height change eases back */
#define TC_CAM_RELAX_TIME 0.45f     /* s, the lag of the height easing back */
#define TC_CAM_EYE_HEIGHT 1.9f      /* first person: the eye above Ed's origin (his model is 2.17 tall) */
#define TC_CAM_STRETCH_TIME 0.5f    /* s, the ease of the front stretch (Ed's facing steps at his AI's 30 Hz) */
#define TC_CAM_MOVING_SPEED 1.0f    /* units/s: Ed counts as moving above this (horizontal) */
#define TC_CAM_RECENTER_TIME 0.15f  /* s, the lag of a recentre (X, middle button, L2) */
#define TC_CAM_SWING_TIME 0.6f      /* s, the lag of the swing to the watchdog's open spot (the original cuts) */
#define TC_CAM_SWING_NEAR 2.0f      /* the watchdog swings only when a wall has the eye nearer than this to Ed (the
                                       original looks from the second range's minimum, 5.7: with the swing turning his
                                       camera-relative controls, that turned him mid-run on every rock) */
#define TC_CAM_LOOK_OFF_MAX 0.2f    /* rad: the lagging look-at point keeps Ed's pivot within this of the view's centre */
#define TC_CAM_HIDE_DISTANCE 0.9f   /* the eye this near the pivot is inside Ed's model: he is not drawn */
#define TC_CAM_PITCH_LIMIT 1.5f     /* rad up or down: the view (free-fly too) */
#define TC_CAM_RISE_MIN -0.8f       /* the player's height change, rad of elevation: the camera as low as 0.45 - 0.8 */
#define TC_CAM_RISE_MAX 0.85f       /* (looking up at Ed) .. as high as 0.45 + 0.85 (1.3 rad, looking down on him) */

/* The classic camera (the fourth pass, 2026-11-01: a rope follow round Ed as drawn, eased presets, a boom pulled in by
 * walls); the default since the user found the fifth pass's faithful model too lazy and janky (2026-11-04). Its own
 * numbers, the port's: */
#define TC_CAM_ROPE 1.0f            /* the rope's pull: 1 = turns as a camera left standing would (lateral speed / distance) */
#define TC_CAM_ROPE_TIME 0.2f       /* s, the smoothing of Ed's velocity for the rope */
#define TC_CAM_ROPE_DEAD 0.5f       /* units/s of his speed across the view the rope ignores (walking at the camera) */
#define TC_CAM_FOLLOW_TIME 0.35f    /* s, the ease between the presets' elevations */
#define TC_CAM_DISTANCE_TIME 0.4f   /* s, the ease between the presets' distances */
#define TC_CAM_RELEASE_TIME 0.6f    /* s, easing back out after the way was blocked */
#define TC_CAM_CLASSIC_MIN 0.0f     /* nearest to the pivot a wall brings it (was 0.5: that put the eye in walls; now Ed
                                       is hidden instead, TC_CAM_HIDE_DISTANCE) */
typedef struct tc_cam_classic {
    float since_look, rise;
    bool recentering, closeup, first_person;
    float stretch, rope_x, rope_y, pitch, distance, reach, pull;
    bool holding;
    float hold_z, hold_ref, hold_offset;
    bool hide;            /* the eye nearer than 0.5 to the pivot: inside Ed */
} tc_cam_classic;

typedef struct tc_cam {
    bool faithful;        /* the fifth pass's model (9 keys); false: the classic camera (tc_cam_classic), the default */
    tc_cam_classic classic;
    float since_look;     /* seconds since the player last turned it (tc_cam_look) */
    float rise;           /* the player's elevation change (rad, + = higher), eased back while Ed moves */
    bool recentering;     /* swinging back behind Ed (TC_CAM_RECENTER_TIME), until it gets there */
    bool closeup;         /* 5 keys: the original's close-up preset (in the normal situations) */
    bool first_person;    /* 0 keys: at Ed's head, the player's direction */
    bool hide_target;     /* the eye is inside Ed (pulled in by a wall): don't draw him (TC_CAM_HIDE_DISTANCE) */
    bool placed;          /* false: the next update places it afresh (no easing in) */
    float clock;          /* time since the last tick (0 .. TC_CAM_TICK) */
    int history_count;
    tc_vec3 history[TC_CAM_HISTORY]; /* Ed's positions at the last ticks, newest first */
    tc_vec3 pivot;        /* the averaged target + the aim height, at the last tick */
    float speed;          /* the camera's linear speed this tick (u/s) */
    tc_vec3 body, body_prev;   /* the camera rig at the last two ticks: the original's camera perso, followed and moved
                                  by its mechanics; walls don't pull it in (that is the eye's, below) */
    float reach, reach_len;    /* the eye's distance from the pivot along the rig's direction, and the rig's, at the
                                  last tick: the wall takes pull = reach_len - reach, in at once, back at the mover's pace */
    float pull, pull_prev;     /* that pull at the last two ticks (the eye is drawn between them) */
    tc_vec3 look, look_prev;   /* the lagging look-at point at the last two ticks */
    float step_last;      /* the last tick's move (the mover's speeding up: DynamWork +0xC) */
    float stretch;        /* the front stretch, eased (< 0: none yet) */
    int watch;            /* ticks to the watchdog's next look */
    bool swinging;        /* turning round to the watchdog's open spot */
    float swing_yaw;      /* its direction (tc_camera yaw convention: looking from the camera to Ed) */
    float swing_time;     /* how long it has swung: it gives up after 4 x TC_CAM_SWING_TIME, or ends once clear */
    bool holding;         /* the jump's height hold (TC_CAM_SIT_JUMP) */
    float hold_z, hold_ref; /* the camera's height held, and Ed's take-off pivot - 0.2 */
    float hold_offset;    /* held height - the follow's; eased to 0 after the hold lets go (no jump) */
} tc_cam;

/* What else stands in the camera's view, beside the level's collision: the share (0..1) of the segment from -> to that
 * is clear of it, 1 = all. The original's sight ray tests the drawn geometry, persos' included (FUN_004a3a00 ->
 * FUN_004a3920 -> FUN_00484210, GLIGLOU_004427d0's visual objects); the viewer answers with the persos it drew. */
typedef float (*tc_cam_sight_fn)(void *user, tc_vec3 from, tc_vec3 to);

/* What the camera follows this frame. */
typedef struct tc_cam_target {
    tc_vec3 position;     /* Ed's origin as drawn */
    float behind;         /* the yaw looking the way Ed faces, from behind him (tc_camera convention) */
    float speed;          /* his horizontal speed (TC_CAM_MOVING_SPEED) */
    tc_vec3 velocity;     /* his velocity (unused by the follow since 2026-11-04: the averaged positions give it) */
    float distance;       /* the zoom: the distance behind him before the front stretch (presets scale with it) */
    int situation;        /* TC_CAM_SIT_* from Ed's scripts; 0 normal, -1 unknown (no AI): the normal follow */
    bool manual;          /* the direction is someone else's this frame (the demo's own camera): position and collision only */
    tc_collision *world;  /* NULL: no collision. Its solid row is honoured as set (the caller's choice) */
    tc_cam_sight_fn sight;   /* NULL: none. The eye stays this side of what it reports too */
    void *sight_user;
} tc_cam_target;

/* Follow mode, placed afresh at the next update (at once, no easing in). */
void tc_cam_reset(tc_cam *c);
/* The player turned it: yaw (rad, tc_camera convention) round Ed onto view, rise (rad, + = camera higher) onto its
 * height; at once (the camera's last two ticks turn with it). Ends a recentre and a swing. */
void tc_cam_look(tc_cam *c, tc_camera *view, float yaw, float rise);
/* Back behind Ed and to the default height, quickly. */
void tc_cam_recenter(tc_cam *c);
/* Moves view (yaw, pitch, position; FOV and depth range untouched) for this frame: ticks the camera as many times as
 * dt holds, then draws it between its last two ticks. */
void tc_cam_update(tc_cam *c, tc_camera *view, const tc_cam_target *t, float dt);

#ifdef __cplusplus
}
#endif
#endif
