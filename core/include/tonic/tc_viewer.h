/* The level viewer's platform-independent half, shared by every frontend (Swift/Metal, SDL3, later Vita): the
 * free-fly and orbit cameras, held actions -> camera motion and Ed's inputs, the play session (collision, Ed's
 * controller or his AI), Ed's clip and its playback rate, the viewer settings and their title readout, and per frame
 * the list of meshes to draw with their matrix and texture. The frontend keeps the window, key codes -> actions, the
 * GPU (meshes uploaded once in tc_viewer_mesh order, then drawn from tc_viewer_draws), file and texture loading,
 * audio. See docs/VIEWER_CORE.md. */
#ifndef TONIC_TC_VIEWER_H
#define TONIC_TC_VIEWER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_math.h"
#include "tc_level.h"
#include "tc_player.h"
#include "tc_transition.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Camera in engine space (Z up). Hor+: widescreen widens the view, never stretches it. */
typedef struct tc_camera {
    tc_vec3 position;
    float yaw;            /* radians, 0 = looking along +Y */
    float pitch;          /* radians, positive = up */
    float vertical_fov;   /* radians */
    float near_z, far_z;
} tc_camera;

void tc_camera_init(tc_camera *c);                /* at the origin, FOV 55 degrees, near 0.05, far 2000 */
tc_vec3 tc_camera_forward(const tc_camera *c);    /* (sin yaw cos pitch, cos yaw cos pitch, sin pitch) */
tc_vec3 tc_camera_right(const tc_camera *c);      /* (cos yaw, -sin yaw, 0) */
/* Right-handed view, perspective with 0..1 clip-space depth (Metal, SDL_GPU). Column-major. */
tc_mat4 tc_camera_view_projection(const tc_camera *c, float aspect);

/* What the frontend's key bindings map to. Defaults: WASD move, R/F up/down, arrow keys look, Shift fast (in play
 * mode with Ed's AI: SpeedUp, he walks), Space jump, E Action, Q Shoot, C Lateral. */
typedef enum tc_view_action {
    TC_VA_FORWARD, TC_VA_BACK, TC_VA_LEFT, TC_VA_RIGHT, TC_VA_UP, TC_VA_DOWN,
    TC_VA_LOOK_LEFT, TC_VA_LOOK_RIGHT, TC_VA_LOOK_UP, TC_VA_LOOK_DOWN, TC_VA_FAST, TC_VA_JUMP,
    TC_VA_ACTION, TC_VA_SHOOT, TC_VA_LATERAL, TC_VA_COUNT
} tc_view_action;

typedef struct tc_view_stats { uint32_t meshes, persos, parts, triangles; } tc_view_stats;

/* One mesh to draw this frame: indices [first_index, first_index + index_count) of it (all of them, unless a sector
 * mode draws only some sectors' part of a static mesh). */
typedef struct tc_view_draw {
    uint32_t mesh;        /* tc_viewer_mesh index */
    int32_t texture;      /* level texture at this frame (animated textures stepped), -1 = untextured */
    uint32_t flags;       /* the surface's TC_FLAG_* */
    uint32_t first_index, index_count;
    tc_mat4 model;
} tc_view_draw;

/* Which sectors are drawn (tc_sector.h), all from Ed's sector: his sector and the sectors of its list (the default: the
 * game's way of drawing, FUN_00451d80, sky included), every sector (the Tonic Viewer's view: the whole level at once),
 * or his sector alone (debug: which one he is in). Levels without sectors draw everything. */
typedef enum tc_view_sectors { TC_VS_SECTOR, TC_VS_ALL, TC_VS_ALONE, TC_VS_COUNT } tc_view_sectors;

/* Shading values for one frame; each frontend packs them into its own uniform layout. */
typedef struct tc_view_frame {
    tc_mat4 view_projection;
    tc_vec3 light_direction;  /* normalize(0.4, -0.3, 0.85) */
    float ambient;            /* 0.45 */
    float time;               /* animation clock (UV scroll) */
    float baked_scale;        /* vertex colour (0..1) -> light factor: 255/128, see level.RADIOSITY_ONE */
    uint32_t light_set;       /* baked set shown, blended towards light_next by light_blend (0..1: the original's */
    uint32_t light_next;      /* lerp of the radiosity, FUN_00449860); shaders fall back to set 0 per vertex */
    float light_blend;
    float gamma;              /* display gamma, 1 = unchanged */
    bool fog_on;
    float fog_near, fog_far;  /* 0 when the level has no fog */
    float fog_color[3];
    tc_vec3 eye, cam_right, cam_up;  /* camera axes, for billboards */
    float clear_color[3];     /* the fog colour with fog (the horizon fades into it), else (0.08, 0.09, 0.14) */
} tc_view_frame;

/* What to draw over the frame (docs/PORTALS.md): a screen transition, black, or the loading screen. */
/* How long the loading screen shows its bar filling (it is not tied to real loading: the frontends also show it
 * after the intro, before Totalski). */
#define TC_VIEWER_LOADING_SECONDS 3
typedef enum tc_view_overlay_kind { TC_VO_NONE, TC_VO_MASK, TC_VO_LOGO, TC_VO_BLACK, TC_VO_LOADING } tc_view_overlay_kind;
typedef struct tc_view_overlay {
    tc_view_overlay_kind kind;
    int32_t texture;          /* MASK / LOGO: tc_viewer_effect_texture index */
    float threshold;          /* MASK: a full-viewport black quad where the mask's alpha <= threshold (0..1) */
    float corners[4][2];      /* LOGO: the texture on this quad, viewport units (0..1, y down), triangles 0 1 2 and */
    float uv[4][2];           /*       1 3 2, black tinted by the texture's alpha, over the frame */
    float progress;           /* LOADING: the bar, 0..1 */
    const char *level;        /* LOADING: the level coming ("bar"); its vignette is Vignette/Level/<level>.png */
} tc_view_overlay;

/* A level change through a portal: what the next level's viewer needs (tc_viewer_desc.carry). Ed lives in the fix,
 * so his dsgvars survive (B63's arrival branch reads them: dsg134 = 2 -> heading dsg136, the step-out). */
#define TC_VIEWER_CARRY_DSG 4096
typedef struct tc_viewer_carry {
    char level[64];           /* the level to load, lowercase (match the .ttlv case-insensitively) */
    char place[64];           /* PROC_29: Ed arrives at the level perso of this instance name; "" = his start */
    char camera_place[64];
    uint32_t dsg_size;        /* 0: no dsgvars carried */
    uint8_t dsg[TC_VIEWER_CARRY_DSG];
    float vertical_fov;       /* the camera's field of view (, .) kept across the change, radians; 0 = the default */
    float orbit_distance;     /* the play camera's zoom (wheel) kept across the change; 0 = the default */
} tc_viewer_carry;

typedef struct tc_viewer_desc {
    const tc_level *level;    /* loaded by the frontend; must outlive the viewer */
    const void *ai_data;      /* the .ttai next to the .ttlv (written by `tools.ttre level-ttai`), or NULL */
    size_t ai_size;
    const char *level_name;   /* "bar": the AI's per-level settings (copied) */
    const tc_viewer_carry *carry; /* arriving through a portal (tc_viewer_pending_level of the last level), or NULL */
    uint32_t seed;            /* random effects of the screen transitions */
} tc_viewer_desc;

typedef enum tc_viewer_phase {
    TC_VP_PLAY, TC_VP_COVER, TC_VP_BLACK, TC_VP_LOADING, TC_VP_PENDING, TC_VP_REVEAL
} tc_viewer_phase;

typedef struct tc_viewer_play tc_viewer_play;  /* private: collision, Ed's controller and AI at stable addresses */
typedef struct tc_viewer_light tc_viewer_light; /* private: the light persos' AI */

/* Fields are readable; change them through the functions below so every frontend behaves the same. */
typedef struct tc_viewer {
    const tc_level *level;
    tc_camera camera;
    tc_view_stats stats;      /* drawable statics, animated persos and their parts, their triangles */
    tc_result ai_result;      /* loading desc->ai_data: TC_OK, or why Ed has no AI (worth a log line) */

    /* Settings. */
    bool paused;              /* animation clock and Ed's AI stopped */
    float animation_speed;    /* viewer-only playback rate for comparing against the original; 1 = engine speed */
    float animation_time;     /* seconds */
    /* Baked lighting (light_set_count sets per vertex: 3 in sud0 and Land). light_set is shown, blending towards
     * light_next by light_blend while a change runs (light_rate sets per second, on to light_target; 0 = none). The
     * level's light persos (sud0's day / night cycle, Land's lightning) change it with SetLightSet while
     * light_scripted; a number key picks a set and stops following them, 0 follows them again. */
    uint32_t light_set, light_set_count, light_next, light_target;
    float light_blend, light_rate;
    bool light_scripted;
    tc_viewer_light *light;   /* NULL: the level has no light persos */
    float gamma;
    bool has_fog, fog_enabled;  /* the level's fog (.ttlv LEVL) and its toggle */
    float fog_near, fog_far, fog_color[3];

    /* Input. */
    uint32_t held;            /* 1 << tc_view_action */
    float mouse_dx, mouse_dy; /* accumulated since the last update */
    float move_x, move_y;     /* left stick, x right, y forward, 0 in the dead zone (tc_viewer_set_move_stick) */
    float look_x, look_y;     /* right stick after the dead zone, x right, y up (tc_viewer_set_look_stick) */
    float fly_speed;          /* free-fly units per second (x4 with TC_VA_FAST) */
    float turn_speed;         /* radians per second */

    /* Play mode: the default when the level has Ed, collision and an idle clip. */
    tc_viewer_play *play;     /* NULL: free-fly only */
    bool playing;
    float orbit_distance;
    bool has_exit_camera;     /* where the camera was when play mode was last left */
    tc_vec3 exit_camera;
    int ed_clip;              /* index into level->ed.anims, -1 none */
    int ed_state;             /* simple controller: tc_anim_state of ed_clip, -1 = restart the clip */
    float ed_phase;           /* samples into ed_clip */
    bool ed_loop;

    /* Portals and screen transitions. */
    tc_viewer_phase phase;
    tc_transition transition;
    float loading_time;       /* seconds into the loading screen */
    uint32_t rng;
    const char **effects;     /* distinct effect texture paths (relative to the level folder) */
    uint32_t effect_count;
    tc_viewer_carry *carry;   /* the level change in progress (LOADING / PENDING) */
    int walk_in;              /* simple controller at a portal: 1 the step-in clip, 2 the cover and the portal firing */
    int walk_clip;            /* the step-in clip (state 228's animation), -1 none */
    uint16_t sounds[TC_AI_MAX_SOUNDS]; /* sound events started by the last update */
    uint32_t ed_draw_count;   /* Ed's parts in the last tc_viewer_draws (0: hidden, e.g. stepped into a portal) */
    uint32_t sound_count;

    /* Sectors (tc_sector.h; none: the level has no SECT and everything is drawn). Ed's sector is his AI's (it looks his
     * water up there), else tracked here for the simple controller. */
    const tc_sectors *sectors;
    tc_view_sectors sector_mode;
    tc_sector_track ed_sector;
    uint8_t *sector_shown;        /* this frame's TC_SHOWN_* per sector */
    uint32_t *mesh_ranges;        /* per tc_viewer_mesh: first and count of its index ranges in `ranges` (count 0: one */
    tc_sector_range *ranges;      /* whole, drawn always: persos, Ed; static meshes and the pieces have them) */
    uint32_t range_count, range_cap;

    /* Mesh table and this frame's draw list. */
    const tc_mesh **meshes;
    uint8_t *mesh_water;          /* per static mesh: 1 = a water surface (TC_FLAG_WATER on its draws) */
    uint32_t mesh_count, perso_first, ed_first;
    tc_view_draw *draws, *blended;
    uint32_t draw_cap, blended_cap;
    /* Transparent statics drawn as pieces (TC_VIEW_PIECE_SIZE cells), so they sort back to front: the pieces follow
     * Ed's parts in the mesh table, piece_origin[k] = the static mesh piece k came from, split[i] = 1 when static
     * mesh i is drawn as its pieces. mesh_centre: per mesh, the centre of its vertex bounds (mesh space), the
     * point blended draws sort by. */
    tc_mesh *pieces;
    uint32_t *piece_origin;
    uint8_t *split;
    tc_vec3 *mesh_centre;
    uint32_t piece_first, piece_count, piece_cap;
    float *blended_depth;     /* this frame's blended draws: squared distance of their centre from the eye */
    uint32_t blended_depth_cap;
} tc_viewer;

/* Sets up the start camera (the middle of the level), the play session (with Ed's AI when ai_data loads) and
 * starts in play mode when there is one. TC_ERR_OUT_OF_MEMORY leaves the viewer empty (tc_viewer_free is safe). */
tc_result tc_viewer_init(tc_viewer *v, const tc_viewer_desc *desc);
void tc_viewer_free(tc_viewer *v);

/* Every drawable mesh, numbered once: the level's statics, each perso's parts, Ed's parts, then the pieces of the
 * transparent statics (TC_VIEW_PIECE_SIZE). Upload them in this order; empty meshes keep their number but are never
 * drawn, and a split static is drawn as its pieces instead. */
uint32_t tc_viewer_mesh_count(const tc_viewer *v);
const tc_mesh *tc_viewer_mesh(const tc_viewer *v, uint32_t i);

/* Input. Frontends map their key codes and gamepad buttons to actions; focus lost: release_all (sticks too). Mouse
 * deltas in points. */
void tc_viewer_set_action(tc_viewer *v, tc_view_action a, bool down);
void tc_viewer_release_all(tc_viewer *v);
void tc_viewer_mouse_look(tc_viewer *v, float dx, float dy);
/* Gamepad sticks, raw -1..1 each frame, inside a radial dead zone of TC_VIEWER_STICK_DEAD_ZONE read as 0. Move: x
 * right, y forward (up on the stick), passed on as is outside the dead zone. With Ed's AI it is the game's own stick:
 * the scripts have their own thresholds (directions beyond 0.2 / 0.3, as the original's DirectInput axes), make it
 * camera-relative, and its magnitude sets the pace from a walk to a run; the simple controller and free-fly take it as
 * a camera-relative direction, as WASD. Look: x right, y up, rescaled to 0..1 past the dead zone (a slight push turns
 * slowly); turns the camera at up to turn_speed, as the arrow keys. */
#define TC_VIEWER_STICK_DEAD_ZONE 0.15f
void tc_viewer_set_move_stick(tc_viewer *v, float x, float y);
void tc_viewer_set_look_stick(tc_viewer *v, float x, float y);

/* One frame: look, free-fly or Ed (AI or simple controller) with the orbit camera, Ed's clip, animation clock. */
void tc_viewer_update(tc_viewer *v, float dt);
/* This frame's draws: opaque ones first, then [opaque_count, count) with TC_FLAG_TRANSPARENT, to draw after
 * everything opaque without depth writes, back to front: by the distance of each mesh's centre from the camera,
 * farthest first (ties keep the list's order). The original sorts its blended elements by depth too
 * (GliDX6vr.dll's xSendListToViewport walks depth buckets). The .ttlv holds one mesh per material for the whole
 * level, so transparent statics are cut into pieces of TC_VIEW_PIECE_SIZE cells (by triangle centroid) at init.
 * Storage is the viewer's, valid until the next call. */
#define TC_VIEW_PIECE_SIZE 16.0f
const tc_view_draw *tc_viewer_draws(tc_viewer *v, uint32_t *count, uint32_t *opaque_count);
void tc_viewer_frame(const tc_viewer *v, float aspect, tc_view_frame *out);

/* A simple round shadow under Ed (a stand-in until the game's own is read): where straight down from him meets the
 * ground, within TC_VIEW_SHADOW_REACH. With his height above it the disc shrinks (to 35 % by 6 units up: a jump
 * shows), lightens and softens, gone at the reach. false while not playing, with none of Ed drawn (call after
 * tc_viewer_draws), while swimming, over nothing or over a wall-steep face. */
#define TC_VIEW_SHADOW_REACH 12.0f
typedef struct tc_view_shadow {
    tc_vec3 position, normal; /* on the ground, its normal (facing up) */
    float radius;
    float alpha;              /* darkness in the middle, 0..1 */
    float inner;              /* share of the radius at full darkness; the rest fades to the rim */
} tc_view_shadow;
bool tc_viewer_shadow(const tc_viewer *v, tc_view_shadow *out);

/* Screen transitions and portals. Load the effect textures once (paths relative to the level folder, as the level's
 * textures); each frame draw the overlay over the 3D frame. When tc_viewer_pending_level returns non-NULL (after the
 * loading screen), load that level and start its viewer with the carry (copy it first: it is freed with this one). */
uint32_t tc_viewer_effect_count(const tc_viewer *v);
const char *tc_viewer_effect_texture(const tc_viewer *v, uint32_t i);
void tc_viewer_overlay(const tc_viewer *v, tc_view_overlay *out);
const tc_viewer_carry *tc_viewer_pending_level(const tc_viewer *v);
/* Sound events started by the last tc_viewer_update (global event indices: audio/sfx/events.tsv, written by
 * tools/ttre/sfx.py: play, loop or stop a file). */
uint32_t tc_viewer_sounds(const tc_viewer *v, const uint16_t **events);

/* Settings (keys in the Swift and SDL frontends in brackets). */
void tc_viewer_step_speed(tc_viewer *v, int dir);   /* [ ]: 1/4, 1/3, 1/2, 2/3, 3/4, 1, 1.5, 2 */
void tc_viewer_toggle_pause(tc_viewer *v);          /* P */
void tc_viewer_set_light_set(tc_viewer *v, uint32_t n);  /* 1-9: that set, held; ignored past light_set_count */
void tc_viewer_follow_light(tc_viewer *v);   /* 0: the level's light persos set the light again (if it has any) */
void tc_viewer_step_gamma(tc_viewer *v, bool up);   /* - =: x1.1, 0.5-2.5 */
void tc_viewer_step_fov(tc_viewer *v, int dir);     /* , .: 5 degree steps, 30-100 */
void tc_viewer_toggle_fog(tc_viewer *v);            /* G */
void tc_viewer_cycle_sectors(tc_viewer *v);         /* K: Ed's sector + its list (default) -> all -> his alone -> ... */
/* Ed's sector (TC_SECTOR_NONE: none); tc_sectors_name for its name. */
uint16_t tc_viewer_ed_sector(const tc_viewer *v);
/* Wheel notches (positive = up): orbit zoom while playing, fly speed in free-fly; x1.1 per notch. */
void tc_viewer_wheel(tc_viewer *v, float notches);
/* Tab: enter/leave play mode. Entering puts the orbit camera behind Ed; leaving keeps the camera where it is.
 * Re-entering after flying more than 1 unit away drops Ed onto the ground below the camera, facing the view. */
void tc_viewer_toggle_play(tc_viewer *v);
/* I: Ed's own AI <-> the simple controller (levels with a .ttai). */
void tc_viewer_toggle_ai(tc_viewer *v);

bool tc_viewer_can_play(const tc_viewer *v);   /* the level has a play session (Tab works) */
bool tc_viewer_has_ai(const tc_viewer *v);     /* ... with Ed's AI */
bool tc_viewer_ai_active(const tc_viewer *v);  /* playing with the AI: its status changes every frame */
/* The status line changes by itself (the AI is active, or the light persos change the light): refresh it often. */
bool tc_viewer_status_live(const tc_viewer *v);
/* Ed's physics state, NULL without a play session. */
const tc_player *tc_viewer_player(const tc_viewer *v);

/* The adjustable settings both title bars show (UTF-8):
 * "anim 1.00×, gamma 1.00, FOV 55°<extra>[, light 1/2|1→2[ held]][, fog on|off][, sector sctX[ alone]]
 * [, free-fly][, simple controller]" (light: the set shown, or the change running; held = a number key's pick in a level
 * with light persos; sector: Ed's, unless all sectors are drawn; the modes only when not the default: playing as Ed with
 * his AI). extra (NULL = none) is the frontend's own settings, e.g. ", AA 4x".
 * Returns the length snprintf would have written. */
size_t tc_viewer_status(const tc_viewer *v, const char *extra, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
