/* Private to tc_viewer*.c: the viewer module, split by subsystem (2026-11-13; one file until then). The public side is
 * include/tonic/tc_viewer.h. Here: the session's private structs, the helpers every part uses, and the functions one
 * part lends another (tcv_*: not static, so not bare names in the library's symbol table).
 *   tc_viewer.c          init / free, the frame (tc_viewer_update), settings, the title readout
 *   tc_viewer_play.c     the play session: input, Ed's ticks (his AI and the cast), the simple controller, custom gameplay
 *   tc_viewer_camera.c   the play camera, its sight test against the drawn persos, the director's shot
 *   tc_viewer_speech.c   speech and hint scenes
 *   tc_viewer_portals.c  portals, level changes, screen transitions and overlays, the gunsight
 *   tc_viewer_light.c    baked light sets and the light persos, the game's lighting (sectors' lights and fog)
 *   tc_viewer_draw.c     the mesh table, transparent pieces, sector ranges, the draw list, the frame's shading values
 *   tc_viewer_tape.c     the original demo tapes
 *   tc_viewer_debug.c    the debug menu and the HUD's glue */
#ifndef TONIC_TC_VIEWER_INTERNAL_H
#define TONIC_TC_VIEWER_INTERNAL_H
#include "tc_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "tonic/tc_viewer.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_anim.h"
#include "tonic/tc_ed_ai.h"
#include "tonic/tc_cast.h"
#include "tonic/tc_obstacle.h"
#include "tonic/tc_tape.h"
#include "tonic/tc_main.h"
#include "tonic/custom/tcx.h"
#include "tc_rec_hooks.h"
#include "tc_prof_hooks.h"

/* A settings door, for the recording: only the public functions, so one that calls another isn't written twice. */
#define REC_DOOR(door, a, x) do { if (v->rec) tc_rec_door(v->rec, door, a, x, 0); } while (0)

/* The tc_player points at its world and the AI driver at the player, so the session stays at one address. */
struct tc_viewer_play {
    tc_collision world;
    tc_obstacles obstacles;      /* the persos' own collision (.ttlv PCOL), met by Ed's sweeps (world.obstacles) */
    tc_main main;                /* the main character: Ed, his physics and his scripts (tc_main.h) */
    tc_cast cast;                /* the level persos that run their own AI (.ttlv CAST), with Ed as the main actor */
    bool has_cast;
    bool use_ai;                 /* his scripts drive him (else the simple controller) */
    /* The game steps in fixed ticks of TC_ED_AI_TICK; drawing interpolates between the last two. */
    float tick_time;             /* seconds not yet stepped */
    bool ski_shot;          /* the skier has ridden: the camera's shot is the ski follow (stands for the start trigger,
                               tc_viewer_camera_shot) */
    tc_mat4 pose_draw;           /* Ed's model matrix this frame, between main.pose_prev and main.pose_cur */
    /* Custom gameplay, not the game's (core/src/custom, tcx.h; NULL: none). Its car takes main's slot while Ed is in it:
     * his AI and mechanics (and the cast, which steps on his clock) hold, and he is drawn at main.rider. */
    struct tcx_play *custom;
};

static inline bool level_has_ed(const tc_level *l) { return l->has_ed && l->collision_triangle_count > 0; }

/* The level persos that set the light (tc_level.light_persos), run by the interpreter on whole milliseconds of the
 * game clock: sud0's day / night cycle, Land's lightning. */
struct tc_viewer_light {
    uint32_t count;
    tc_ai_model *models;
    tc_ai_perso *persos;
    double clock;          /* seconds they have run */
    uint64_t stepped_ms;   /* of it, stepped */
};

static inline bool drawable(const tc_mesh *m) { return m->index_count > 0 && m->vertex_count > 0 && m->vertices && m->indices; }

/* What a sector's characters get on top of the frame's lights (tc_sector.char_lights). */
/* A cell of a wide static (tc_viewer.cells): its triangles' index range in the celled copy, their bounding sphere. */
struct tc_viewer_cell {
    uint32_t first, count;
    float sphere[4];        /* centre xyz, radius (mesh space) */
};

struct tc_viewer_char_lit {
    uint32_t first, count;  /* in tc_viewer.char_lights */
    float ambient[3];       /* its ambient lights' sum */
};

static inline bool held(const tc_viewer *v, tc_view_action a) { return (v->held >> a) & 1u; }

struct tc_viewer_tape {
    tc_tape tape;
    int32_t offset;        /* tape object = cast entry id + offset: the exe's list holds the persos in level order */
    int32_t ed, camera;    /* tape objects; camera -1: the follow camera */
    double clock;          /* ms played */
    bool done;
    int32_t clip_state;    /* the state ed_clip was looked up for, -1 none */
    int clip;
};

#define SKIP_MASK ((1u << TC_VA_JUMP) | (1u << TC_VA_ACTION) | (1u << TC_VA_SHOOT))

/* tc_viewer_play.c */
int tcv_ed_anim(const tc_level *l, const char *name);
tc_viewer_play *tcv_play_create(tc_viewer *v, const tc_viewer_desc *desc);
void tcv_play_destroy(tc_viewer_play *p);
void tcv_look(tc_viewer *v, float dt);
void tcv_fly(tc_viewer *v, float dt);
void tcv_set_inspecting(tc_viewer *v, bool on);
void tcv_toggle_play(tc_viewer *v);
bool tcv_hud_covered(const tc_viewer *v);
void tcv_update_play(tc_viewer *v, float dt, bool fresh);

/* tc_viewer_camera.c */
void tcv_mesh_spheres(tc_viewer *v);
void tcv_update_camera(tc_viewer *v, float dt, bool fresh);

/* tc_viewer_speech.c */
void tcv_update_speech(tc_viewer *v, float dt, bool fresh);
void tcv_update_scene(tc_viewer *v);

/* tc_viewer_portals.c */
int tcv_same_text(const char *a, const char *b);
void tcv_copy_text(char *dst, size_t n, const char *src);
bool tcv_collect_effects(tc_viewer *v, const tc_ai_model *m);
const tc_ai_model *tcv_ai_model(const tc_viewer *v);
void tcv_start_transition(tc_viewer *v, bool reveal);
void tcv_arrive(tc_viewer *v, const tc_viewer_carry *c);
void tcv_update_portals(tc_viewer *v, float dt);

/* tc_viewer_light.c */
void tcv_light_destroy(tc_viewer_light *l);
tc_viewer_light *tcv_light_create(tc_viewer *v, const tc_level *l, bool *oom);
void tcv_update_light(tc_viewer *v, float dt);
void tcv_measure_sky(tc_viewer *v);
void tcv_view_light(tc_view_light *o, const tc_light *L, float w);
uint16_t tcv_perso_light_sector(const tc_viewer *v, uint32_t i);
void tcv_update_lighting(tc_viewer *v, float dt);

/* tc_viewer_draw.c */
bool tcv_make_cells(tc_viewer *v);
bool tcv_split_transparent(tc_viewer *v, uint32_t **piece_ranges);
bool tcv_mesh_centres(tc_viewer *v);
bool tcv_mesh_ranges(tc_viewer *v, const uint32_t *piece_ranges);
const uint8_t *tcv_shown_sectors(tc_viewer *v);

/* tc_viewer_tape.c */
bool tcv_tape_actor_pose(const tc_viewer *v, const tc_cast_actor *c, tc_mat34 *world, const tc_cast_anim **anim, tc_sample *s);
void tcv_tape_update(tc_viewer *v, float dt, bool fresh);
void tcv_tape_free(tc_viewer *v);

/* tc_viewer_debug.c */
void tcv_debug_frame(tc_viewer *v, float dt);

#endif
