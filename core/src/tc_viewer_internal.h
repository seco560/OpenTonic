/* Private to tc_viewer*.c: the viewer module, split by subsystem (2026-11-13; one file until then). The public side is
 * include/tonic/tc_viewer.h. Here: the session's private structs, the helpers every part uses, and the functions one
 * part lends another (tcv_*: not static, so not bare names in the library's symbol table).
 *   tc_viewer.c          init / free, the frame (tc_viewer_update), settings, the title readout
 *   tc_viewer_play.c     play mode: input, the game's frame (tc_game.h) and Ed's drawing between its ticks, the simple
 *                        controller, custom gameplay's rider
 *   tc_viewer_camera.c   the play camera, its sight test against the drawn persos, the director's shot
 *   tc_viewer_speech.c   the text window and the prompts (the lines and hint scenes are the game's: tc_game_speech.c)
 *   tc_viewer_portals.c  the transitions' overlays, the game's camera requests, the gunsight (the level changes are the
 *                        game's: tc_game_portals.c)
 *   tc_viewer_light.c    baked light sets and the light persos, the game's lighting (sectors' lights and fog)
 *   tc_viewer_draw.c     the mesh table, transparent pieces, sector ranges, the draw list, the frame's shading values
 *   tc_viewer_occlusion.c  occlusion culling: what is in view but behind something solid (levels without sectors)
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
#include "tonic/tc_game.h"
#include "tonic/custom/tcx.h"
#include "tc_rec_hooks.h"
#include "tc_prof_hooks.h"

/* A settings door, for the recording: only the public functions, so one that calls another isn't written twice. */
#define REC_DOOR(door, a, x) do { if (v->rec) tc_rec_door(v->rec, door, a, x, 0); } while (0)

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
    float box[6];           /* their vertices' bounds: lo xyz, hi xyz (occlusion: tc_viewer_occlusion.c) */
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

#define SKIP_MASK TC_GAME_SKIP_MASK

/* tc_viewer_play.c */
int tcv_ed_anim(const tc_level *l, const char *name);
void tcv_look(tc_viewer *v, float dt);
int16_t tcv_mesh_surface(const tc_viewer *v, const tc_mesh *m); /* tc_viewer_draw.c: the water surface a mesh lies on */
/* Ends a camera transition in progress: the view as shown becomes the camera's own (before setting it outright). */
void tcv_camera_settle(tc_viewer *v);
/* Sounds (tc_sound.h): the camera's micro now. */
tc_sound_listener tcv_listener(const tc_viewer *v);
void tcv_fly(tc_viewer *v, float dt);
void tcv_set_inspecting(tc_viewer *v, bool on);
void tcv_toggle_play(tc_viewer *v);
/* The main character (tc_game_main: Ed, a chameleon while one holds the player, Ed riding the car) as drawn this frame
 * (between the ticks), its heading as drawn; where it is after the last tick: tc_game_main. */
tc_vec3 tcv_main_drawn(const tc_viewer *v);
float tcv_main_drawn_heading(const tc_viewer *v);
void tcv_update_play(tc_viewer *v, float dt, bool fresh);

/* tc_viewer_camera.c */
void tcv_mesh_spheres(tc_viewer *v);
void tcv_update_camera(tc_viewer *v, float dt, bool fresh);

/* tc_viewer_portals.c */
bool tcv_collect_effects(tc_viewer *v, const tc_ai_model *m);
const tc_ai_model *tcv_ai_model(const tc_viewer *v);
/* The host camera's settings the game keeps in a carry (tc_game_view). */
tc_game_view tcv_game_view(const tc_viewer *v);
/* The camera moves the game asked for (tc_game_camera_request), in order. */
void tcv_take_camera_requests(tc_viewer *v);
void tcv_update_portals(tc_viewer *v, float dt);

/* tc_viewer_light.c */
void tcv_light_destroy(tc_viewer_light *l);
tc_viewer_light *tcv_light_create(tc_viewer *v, const tc_level *l, bool *oom);
void tcv_update_light(tc_viewer *v, float dt);
void tcv_measure_sky(tc_viewer *v);
void tcv_view_light(tc_view_light *o, const tc_light *L, float w);
uint16_t tcv_perso_light_sector(const tc_viewer *v, uint32_t i);
void tcv_update_lighting(tc_viewer *v, float dt);

/* tc_viewer_occlusion.c */
bool tcv_occ_init(tc_viewer *v);
void tcv_occ_free(tc_viewer *v);
bool tcv_occ_begin(tc_viewer *v);
bool tcv_occ_hidden(const tc_viewer *v, const tc_mat4 *model, const float *box);

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

/* tc_viewer_particles.c: the particles' ticks (after the frame's update), and their memory. */
void tcv_update_particles(tc_viewer *v);
void tcv_free_particles(tc_viewer *v);

#endif
