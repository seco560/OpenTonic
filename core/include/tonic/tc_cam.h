/* The camera: the view (tc_camera) and the play camera that follows Ed (tc_cam). docs/CAMERA.md has the original's
 * camera these numbers come from, and the improvements this implements (A1, B1, B2, C1, D1, E1 there).
 *
 * The play camera, each frame (tc_cam_update). One camera, no modes (the user's choice, 2026-11-01: "option 2"):
 *   - The player turns it round Ed (mouse, right stick, look keys, Numpad 4 / 6: the original's orbit keys) and raises or
 *     lowers it round him (the original had no vertical control), at once (tc_cam_look).
 *   - Ed standing: it stays where the player put it (the original's numpad orbit works only then).
 *   - Ed moving, and no turning for TC_CAM_GRACE: the camera is dragged round as if on a rope (the port's, not the
 *     original's): it turns by Ed's speed across its view over its distance, so it comes round behind him as he runs
 *     away or sideways, and doesn't turn at all as he walks towards it (it backs off). The player's height eases back.
 *     The original snaps the orbit back to 0 as soon as he moves (camera B0) and then swings behind his facing, which
 *     with camera-relative controls fed back on itself here (jitter walking towards the camera: 2026-11-01).
 *   - First person (tc_cam.first_person; the 0 keys): at Ed's head, looking where the player turns it (the original's
 *     camera B1 for its stick mode, which the port can't enter through Ed's scripts: their gate dsg25 is never set).
 *   - Recentre (tc_cam_recenter): behind him and at the default height, quickly.
 *   - Presets: what Ed's scripts tell the camera (TC_CAM_SIT_*) picks the distance and elevation, eased between, and
 *     in a jump holds the camera's height (it looks at Ed, but doesn't rise and fall with him). Numpad 5: the original's
 *     close-up (tc_cam.closeup).
 *   - Collision: the original camera's own body (its ZDM: one sphere, r 0.37) swept out from the pivot towards the
 *     wanted spot with the exe's sphere sweep (tc_mec_sweep). Blocked: the camera comes in at once; the way clearing,
 *     it eases back out. It never stands past the first surface the sphere meets.
 * The pivot is Ed's drawn origin raised by the original's aim height. Not ported: the original's cuts when Ed is
 * hidden (FUN_004a56e0's search and jump), the presets not read yet (swimming, code 17, the fall's speed-ups), the
 * level's scripted shots. */
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
#define TC_CAM_RADIUS 0.37f         /* the camera perso's collision sphere (its ZDM, both states) */
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
/* The port's own (not the original's): */
#define TC_CAM_GRACE 0.5f           /* s after the last look input before the camera may ease back behind a moving Ed */
#define TC_CAM_RELAX_TIME 0.45f     /* s, the lag of the height easing back */
#define TC_CAM_ROPE 1.0f            /* the rope's pull: 1 = turns as a camera left standing would (lateral speed / distance) */
#define TC_CAM_ROPE_TIME 0.2f       /* s, the smoothing of Ed's velocity for the rope */
#define TC_CAM_ROPE_DEAD 0.5f       /* units/s of his speed across the view the rope ignores (walking at the camera) */
#define TC_CAM_EYE_HEIGHT 1.9f      /* first person: the eye above Ed's origin (his model is 2.17 tall) */
#define TC_CAM_STRETCH_TIME 0.5f    /* s, the ease of the front stretch (Ed's facing steps at his AI's 30 Hz) */
#define TC_CAM_MOVING_SPEED 1.0f    /* units/s: Ed counts as moving above this (horizontal) */
#define TC_CAM_FOLLOW_TIME 0.35f    /* s, the ease between the presets' elevations */
#define TC_CAM_RECENTER_TIME 0.15f  /* s, the lag of a recentre (X, middle button, L2) */
#define TC_CAM_DISTANCE_TIME 0.4f   /* s, the ease between the presets' distances */
#define TC_CAM_RELEASE_TIME 0.6f    /* s, easing back out after the way was blocked */
#define TC_CAM_MIN_DISTANCE 0.5f    /* nearest to the pivot the collision brings it */
#define TC_CAM_PITCH_LIMIT 1.5f     /* rad up or down: the view (free-fly too) */
#define TC_CAM_RISE_MIN -0.8f       /* the player's height change, rad of elevation: the camera as low as 0.45 - 0.8 */
#define TC_CAM_RISE_MAX 0.85f       /* (looking up at Ed) .. as high as 0.45 + 0.85 (1.3 rad, looking down on him) */

typedef struct tc_cam {
    float since_look;     /* seconds since the player last turned it (tc_cam_look) */
    float rise;           /* the player's elevation change (rad, + = higher), eased back with the orbit */
    bool recentering;     /* easing back behind Ed (TC_CAM_RECENTER_TIME), until it gets there */
    bool closeup;         /* 5 keys: the original's close-up preset (in the normal situations) */
    bool first_person;    /* 0 keys: at Ed's head, the player's direction */
    float stretch;        /* the front stretch, eased (< 0: none yet) */
    float rope_x, rope_y; /* Ed's horizontal velocity, smoothed (TC_CAM_ROPE_TIME) */
    float pitch;          /* the presets' elevation (negative: from above), eased; the view looks at the pivot */
    float distance;       /* the follow's distance, eased between presets; < 0: none yet */
    float reach;          /* the distance used last frame; < 0: none yet, the next update places it */
    float pull;           /* how much of the wanted distance the level takes away (eased out, at once in) */
    bool holding;         /* the jump's height hold (TC_CAM_SIT_JUMP) */
    float hold_z, hold_ref; /* the camera's height held, and Ed's take-off pivot - 0.2 */
    float hold_offset;    /* held height - the follow's; eased to 0 after the hold lets go (no jump) */
} tc_cam;

/* What the camera follows this frame. */
typedef struct tc_cam_target {
    tc_vec3 position;     /* Ed's origin as drawn */
    float behind;         /* the yaw looking the way Ed faces, from behind him (tc_camera convention) */
    float speed;          /* his horizontal speed (TC_CAM_MOVING_SPEED) */
    tc_vec3 velocity;     /* his velocity (the rope uses its horizontal part) */
    float distance;       /* the zoom: the distance behind him before the front stretch (presets scale with it) */
    int situation;        /* TC_CAM_SIT_* from Ed's scripts; 0 normal, -1 unknown (no AI): the normal follow */
    bool manual;          /* the direction is someone else's this frame (the demo's own camera): position and collision only */
    tc_collision *world;  /* NULL: no collision. Its solid row is honoured as set (the caller's choice) */
} tc_cam_target;

/* Follow mode, placed afresh at the next update (at once, no easing in). */
void tc_cam_reset(tc_cam *c);
/* The player turned it: yaw (rad, tc_camera convention) round Ed onto view, rise (rad, + = camera higher) onto its
 * height; ends a recentre, restarts TC_CAM_GRACE. */
void tc_cam_look(tc_cam *c, tc_camera *view, float yaw, float rise);
/* Back behind Ed and to the default height, quickly. */
void tc_cam_recenter(tc_cam *c);
/* Moves view (yaw, pitch, position; FOV and depth range untouched) for this frame. */
void tc_cam_update(tc_cam *c, tc_camera *view, const tc_cam_target *t, float dt);

#ifdef __cplusplus
}
#endif
#endif
