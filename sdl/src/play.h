/* Wrappers over the portable C core. Port of Sources/TonicViewer/Core.swift: the core owns level data, collision
 * and Ed's controller; these only manage files and lifetimes. */
#ifndef TV_PLAY_H
#define TV_PLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <tonic/tonic.h>

/* A .ttlv level loaded by tc_level_load (file read here, parsed in C). */
typedef struct tv_level {
    tc_level raw;
    char *path;
    char *folder;   /* texture paths are relative to it */
    char *name;     /* file name without extension ("bar") */
} tv_level;

bool tv_level_load(tv_level *l, const char *path, char *err, size_t err_size);
void tv_level_free(tv_level *l);
bool tv_level_has_ed(const tv_level *l);
/* Ed's animation by .ttlv name with at least one sample, or -1. */
int tv_level_ed_anim(const tv_level *l, const char *name);

/* Play mode: the level's collision grid plus Ed's controller, heap-allocated at a stable address because the
 * tc_player keeps a pointer to its world and the AI driver to the player. With Ed's AI (.ttai next to the .ttlv,
 * written by `tools.ttre level-ttai`), Ed is driven by his own scripts; use_ai switches back to the simple
 * controller. */
typedef struct play_session {
    tc_collision world;
    tc_player player;
    tc_ai_model model;
    bool has_model;
    tc_ed_ai driver;
    bool has_driver;
    bool use_ai;
    char level_name[128];
} play_session;

/* NULL when the level has no Ed or no collision. */
play_session *play_create(const tv_level *l);
void play_destroy(play_session *p);

/* One frame of Ed's AI and physics. inputs: 1 << TC_AI_IN_* bits; camera: the eye position. */
void play_step_ai(play_session *p, float dt, uint32_t inputs, tc_vec3 camera);
/* The simple controller. move: world-space XY direction, length <= 1. */
void play_step(play_session *p, float dt, tc_vec2 move, bool run, bool jump);
/* The AI's animation: .ttlv clip name ("s<key>"), frame into it, and whether it loops. false without AI. */
bool play_ai_animation(const play_session *p, char *name, size_t name_size, float *frame, bool *loops);
/* Behaviour / reflex / state, for the title bar. */
void play_ai_status(const play_session *p, char *out, size_t out_size);
/* Ed -> world. */
tc_mat4 play_model_matrix(const play_session *p);
/* Length of a one-shot clip, by .ttlv animation name (ignored for names the controller doesn't use). */
void play_set_clip_length(play_session *p, const char *name, float seconds);
void play_teleport(play_session *p, tc_vec3 position, float heading);
/* Distance to the nearest hit along the segment, or -1. */
float play_raycast(play_session *p, tc_vec3 origin, tc_vec3 dir, float max_distance);

#endif
