/* The camera: the view (tc_camera) and the play camera that follows Ed (tc_cam). docs/CAMERA.md has the original's
 * camera, what is read from the exe, and which parts are the port's own.
 *
 * The play camera (2026-11-04, "Sixth pass": one camera, merged from the fourth pass's classic and the fifth pass's
 * faithful model). Per frame, round Ed as drawn:
 *   - The rig: where the camera would be with no walls. The orbit's direction (the player's, then the rope: Ed's speed
 *     across the view over the follow's distance turns it, as a camera dragged behind him would turn), the preset's
 *     elevation and distance (the original's data: camera B2, the close-up, flight, dying), stretched in front of Ed,
 *     its height held during jumps (camera B26 / B17). The point it orbits and aims at is Ed's head with its height
 *     smoothed (TC_CAM_HEIGHT_TIME: no lag on a steady climb), so steps, bumps and landings don't bob the world.
 *   - The eye: on the line from that point to the rig, never past a surface (the camera's sphere, r 0.37, swept from his
 *     head; the persos as drawn too, as the exe's sight ray tests them): in at once, back out eased. Walls never move the
 *     rig, so the jump's hold, the stretch and the rope don't change when a wall pulls the eye in (the fifth pass's
 *     split: the fourth pass held the pulled-in eye's height at a jump and snapped 0.4 rad).
 *   - The watchdog (the original's FUN_004a56e0, every 8 ticks): pulled in close by a wall, with the player not
 *     turning, the camera looks round Ed in steps of pi / 4 (FUN_004a4f20's table) for an open side and swings there;
 *     the original cuts.
 * The player's part: mouse, stick and look keys turn the camera round Ed and raise or lower it at once; it stays where
 * it was put while he stands; recentre (X, middle button, L2) swings it behind him; 5 the close-up preset, 0 first
 * person (at his head). The player's height change eases back while Ed moves.
 * The sliding stages: the level's scripts ask for the original's ski camera (TC_CAM_SHOT_SKI, camera B6); the port uses
 * this camera there (tc_cam_target.shot is passed on for a ski-tuned variant).
 * Not ported: the level's scripted shots (TC_CAM_SHOT_FIXED and the walking levels' directors), swimming / code 17
 * presets, the fall's speed-ups. */
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
/* What the level's scripts tell the camera to shoot (its dsg39; camera reflex 2 picks a behaviour by it). The sliding
 * stages (docs/CAMERA.md "Seventh pass"): the skier's B1, Totalski's start trigger and skymap's JET_InitSkymap set 5;
 * the finish / retry triggers, the skier's crash and respawn and skymap's arrival cut set 6 with a place (dsg92). */
#define TC_CAM_SHOT_SKI 5           /* camera B6 -> B23: the original's ski follow (docs/CAMERA.md "Seventh pass"; its port
                                       was dropped after the playtest) */
#define TC_CAM_SHOT_FIXED 6         /* camera B8 -> B7: a fixed place looking at the target (mode 3, PROC 183 / 192 / 194):
                                       not ported */
/* The ski preset (the port's, 2026-11-04, after the B6 port's playtest: this camera tuned for the slides, not the
 * original's B6): */
#define TC_CAM_SKI_DISTANCE 7.0f    /* behind him, plus the pull-back with speed (times the wheel's zoom) */
#define TC_CAM_SKI_PULLBACK 0.04f   /* units further per u/s of his horizontal speed ... */
#define TC_CAM_SKI_PULLBACK_MAX 3.0f /* ... at most (+2 at 50 u/s) */
#define TC_CAM_SKI_ELEVATION 0.35f  /* rad (the walking follow's 0.45; B6's 0.2) */
#define TC_CAM_SKI_ROPE_TIME 0.45f  /* s, his velocity's smoothing for the rope (walking 0.2): steering doesn't swing it */
#define TC_CAM_SKI_ROPE_DEAD 3.0f   /* u/s of his speed across the view the rope ignores (walking 0.5): his wobble */
#define TC_CAM_SKI_HEIGHT_TIME 0.1f /* s, the height filter's (walking 0.08): the bumps of the snow */
#define TC_CAM_DYING_DISTANCE 2.6f
#define TC_CAM_FLIGHT_DISTANCE 4.0f
#define TC_CAM_FLIGHT_ELEVATION 0.0f
#define TC_CAM_CLOSEUP_ELEVATION 0.15f /* Numpad 5's close-up (camera dsg54): elevation dsg116, distance dsg117 / 118, */
#define TC_CAM_CLOSEUP_DISTANCE 4.0f   /* stretched up to 2.5x in front of Ed (dsg58) */
#define TC_CAM_CLOSEUP_STRETCH 2.5f
/* The original's watchdog (docs/CAMERA.md "Fifth pass"): */
#define TC_CAM_WATCH_TIME (8.0f / 30) /* s between its looks: every 8 camera ticks (DAT_0052136c, B0: PROC_198(15, 8)) */
#define TC_CAM_NEAR_RANGE 1.3f      /* the second range's minimum below the distance (B2: -1.3; FUN_004a5600) */
#define TC_CAM_SEARCH_STEP 0.7853982f /* the candidate spots: the wanted offset turned by k pi / 4 (DAT_00521338) */
/* The port's own (not the original's): */
#define TC_CAM_GRACE 0.5f           /* s after the last look input before the follow turns it or eases the height back */
#define TC_CAM_RELAX_TIME 0.45f     /* s, the lag of the player's height change easing back */
#define TC_CAM_EYE_HEIGHT 1.9f      /* first person: the eye above Ed's origin (his model is 2.17 tall) */
#define TC_CAM_STRETCH_TIME 0.25f   /* s, each of the front stretch's two easing stages (Ed's facing steps at his AI's
                                       30 Hz; two stages start it gently) */
#define TC_CAM_MOVING_SPEED 1.0f    /* units/s: Ed counts as moving above this (horizontal) */
#define TC_CAM_RECENTER_TIME 0.15f  /* s, the lag of a recentre (X, middle button, L2) */
#define TC_CAM_ROPE 1.0f            /* the rope's pull: 1 = turns as a camera left standing would (lateral speed / distance) */
#define TC_CAM_ROPE_TIME 0.2f       /* s, the smoothing of Ed's velocity for the rope */
#define TC_CAM_ROPE_DEAD 0.5f       /* units/s of his speed across the view the rope ignores (walking at the camera) */
#define TC_CAM_ROPE_ONSET 0.25f     /* s, the rope fading in after the player, a recentre or a swing had the camera (it
                                       came back at full strength in one frame: -4.4 rad/s at once on Totalski) */
#define TC_CAM_FOLLOW_TIME 0.35f    /* s, the ease between the presets' elevations, and of the jump's hold letting go */
#define TC_CAM_DISTANCE_TIME 0.4f   /* s, the ease between the presets' distances */
#define TC_CAM_HEIGHT_TIME 0.08f    /* s, the smoothing of the camera's height (steps, bumps, landings): a second-order
                                       filter with no lag at a steady climb or fall (slopes, lifts, the slides), so it
                                       only rounds off changes of vertical speed; teleports jump */
#define TC_CAM_RELEASE_TIME 0.6f    /* s, the eye easing back out after a wall pulled it in */
#define TC_CAM_SWING_TIME 0.3f      /* s, the swing to the watchdog's open spot (the original cuts): a critically damped
                                       turn with this time constant (gathers speed and slows down smoothly, ~1.4 s) */
#define TC_CAM_SWING_REST 1.0f      /* s after a swing before the watchdog may swing again (no hunting in small rooms) */
#define TC_CAM_SWING_NEAR 2.0f      /* the watchdog swings only when a wall has the eye nearer than this to Ed (the
                                       original looks from the second range's minimum, 5.7: with the swing turning his
                                       camera-relative controls, that turned him mid-run on every rock) */
#define TC_CAM_HIDE_DISTANCE 0.9f   /* the eye this near the pivot is inside Ed's model: he is not drawn */
#define TC_CAM_TELEPORT 5.0f        /* Ed moved more than this in a frame (a respawn, a portal): placed afresh */
#define TC_CAM_PITCH_LIMIT 1.5f     /* rad up or down: the view (free-fly too) */
#define TC_CAM_RISE_MIN -0.8f       /* the player's height change, rad of elevation: the camera as low as 0.45 - 0.8 */
#define TC_CAM_RISE_MAX 0.85f       /* (looking up at Ed) .. as high as 0.45 + 0.85 (1.3 rad, looking down on him) */

typedef struct tc_cam {
    float since_look;     /* seconds since the player last turned it (tc_cam_look) */
    float rise;           /* the player's elevation change (rad, + = higher), eased back while Ed moves */
    bool recentering;     /* swinging back behind Ed (TC_CAM_RECENTER_TIME), until it gets there */
    bool closeup;         /* 5 keys: the original's close-up preset (in the normal situations; flight and dying use
                             their own presets meanwhile, then it is back); kept through tc_cam_reset and portals */
    bool first_person;    /* 0 keys: at Ed's head, the player's direction */
    bool hide_target;     /* the eye is inside Ed (pulled in by a wall): don't draw him (TC_CAM_HIDE_DISTANCE) */
    bool placed;          /* false: the next update places it afresh behind Ed (no easing in) */
    float elevation;      /* the preset's elevation, eased between presets (rad, + = above Ed) */
    float distance;       /* the preset's distance, eased between presets */
    float stretch, stretch1; /* the front stretch, eased in two stages (no jolt as it starts: Ed turning to walk at the
                                camera opens the distance smoothly) */
    float rope_x, rope_y; /* Ed's velocity, smoothed for the rope */
    float rope_gain;      /* 0 .. 1: the rope's share, fading in (TC_CAM_ROPE_ONSET) whenever it takes over */
    tc_vec3 pivot;        /* Ed's head as drawn this frame (his origin + the aim height) */
    float height1, height2; /* the camera's height filter (TC_CAM_HEIGHT_TIME): two lags of pivot.z; 2 h1 - h2 is the
                               height the camera aims at and orbits round */
    tc_vec3 rig;          /* where the camera would be with no walls, this frame */
    float pull;           /* what walls take off the rig's distance (the eye is rig distance - pull from the pivot) */
    float reach;          /* the eye's distance from the pivot this frame */
    bool holding;         /* the jump's height hold (TC_CAM_SIT_JUMP) */
    float hold_z, hold_ref; /* the rig's height held, and Ed's take-off pivot - 0.2 */
    float hold_offset;    /* held height - the follow's; eased to 0 after the hold lets go (no jump) */
    float hold_rate;      /* its rate of change (u/s): letting go is a critically damped return (TC_CAM_FOLLOW_TIME /
                             2) that starts from the camera standing still, not at Ed's fall speed */
    float watch;          /* seconds to the watchdog's next look */
    bool swinging;        /* turning round to the watchdog's open spot */
    float swing_yaw;      /* its direction (tc_camera yaw convention: looking from the camera to Ed) */
    float swing_time;     /* how long it has swung: it gives up after 8 x TC_CAM_SWING_TIME, or ends once clear */
    float swing_rate;     /* its turning speed (rad/s): starts from turn_rate, carried on and dying out after it ends */
    float turn_rate;      /* how fast the orbit turned last frame (rad/s): a swing starts from it, no jolt */
    float swing_rest;     /* seconds before the watchdog may swing again (TC_CAM_SWING_REST) */
} tc_cam;

/* What else stands in the camera's view, beside the level's collision: the share (0..1) of the segment from -> to that
 * is clear of it, 1 = all. The original's sight ray tests the drawn geometry, persos' included (FUN_004a3a00 ->
 * FUN_004a3920 -> FUN_00484210, GLIGLOU_004427d0's visual objects); the viewer answers with the persos it drew.
 * Port (the user, 2026-11-11: the eye showed a character's inside): *inside = how far along the segment (units) the
 * solid goes on that `from` itself is in, 0 when it is in none (a character overlapping Ed: the cast does not collide
 * with him as the exe's does). The eye is then put beyond it, and the share is that of what comes after. */
typedef float (*tc_cam_sight_fn)(void *user, tc_vec3 from, tc_vec3 to, float *inside);

/* What the camera follows this frame. */
typedef struct tc_cam_target {
    tc_vec3 position;     /* Ed's origin as drawn */
    float behind;         /* the yaw looking the way Ed faces, from behind him (tc_camera convention) */
    float speed;          /* his horizontal speed (TC_CAM_MOVING_SPEED) */
    tc_vec3 velocity;     /* his velocity (the rope) */
    float distance;       /* the zoom: the distance behind him before the front stretch (presets scale with it) */
    int situation;        /* TC_CAM_SIT_* from Ed's scripts; 0 normal, -1 unknown (no AI): the normal follow */
    int shot;             /* TC_CAM_SHOT_* from the level's scripts (the camera's dsg39); 0 / -1: the walking camera */
    tc_collision *world;  /* NULL: no collision. Its solid row is honoured as set (the caller's choice) */
    tc_cam_sight_fn sight;   /* NULL: none. The eye stays this side of what it reports too */
    void *sight_user;
} tc_cam_target;

/* Follow mode, placed afresh at the next update (at once, no easing in). The close-up (5) is kept: a player's choice,
 * like the zoom (a new tc_cam must start zeroed). */
void tc_cam_reset(tc_cam *c);
/* The player turned it: yaw (rad, tc_camera convention) round Ed onto view, rise (rad, + = camera higher) onto its
 * height; at once (the camera's last two ticks turn with it). Ends a recentre and a swing. */
void tc_cam_look(tc_cam *c, tc_camera *view, float yaw, float rise);
/* Back behind Ed and to the default height, quickly. */
void tc_cam_recenter(tc_cam *c);
/* Moves view (yaw, pitch, position; FOV and depth range untouched) for this frame. */
void tc_cam_update(tc_cam *c, tc_camera *view, const tc_cam_target *t, float dt);

#ifdef __cplusplus
}
#endif
#endif
