/* Recording and replaying play runs (docs/RECORDING.md). A recorder, handed to each viewer through
 * tc_viewer_desc.recorder, writes everything the game takes in: per frame its dt and inputs, the settings doors in
 * order, when the frame was drawn; and what came of it, so a later build can say where and how far it left the run: a
 * trail of Ed, the camera and the game's state about 30 times a second, the cast now and then, sounds, story values,
 * hashes of what the core hands a frontend. The player's marks (tc_recorder_mark) and the frontend's notes ride along.
 *
 * A run is one file and one recorder: from a level opened from a menu, through the portals it goes on by, back to a menu.
 * The frontend makes a new recorder for each. A file is a header, then records, one segment per viewer (a portal builds a
 * new viewer). The
 * records are only ever appended and each frame's input goes out before the frame runs, so a crash leaves a file that
 * replays up to and into the crash. The core does no file I/O: the frontend's sink stores the bytes. The file holds
 * inputs and numbers, nothing of the game's data. Little-endian (every target is). */
#ifndef TONIC_TC_REC_H
#define TONIC_TC_REC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_viewer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_REC_VERSION 1u
#define TC_REC_EXTENSION "ttrec"
#define TC_REC_SAMPLE_SECONDS (1.0f / 30) /* the trail, as the game's ticks */
#define TC_REC_CAST_SECONDS 2.0f          /* the cast's states */
#define TC_REC_HASH_DRAWS 30              /* every so many draws, the hash of what the frontend got */

/* ---- recording ---- */

/* Stores bytes in order (append them to the file, and flush: this is what survives a crash). First called when the
 * first viewer starts, so a recorder that opens no level writes nothing. */
typedef void (*tc_rec_sink)(void *user, const void *data, size_t size);
tc_recorder *tc_recorder_create(tc_rec_sink sink, void *user);
void tc_recorder_free(tc_recorder *r);
/* A fact about the run, free text: frontend, os, build, window ... (docs/RECORDING.md). Any time. */
void tc_recorder_note(tc_recorder *r, const char *key, const char *value);
/* The player's "look at this moment": kept with the frame and the state right now. text may be NULL. Returns its
 * number in the run, from 1; 0 while no level is open. */
uint32_t tc_recorder_mark(tc_recorder *r, const char *text);

/* ---- what a recording holds ---- */

/* tc_rec_sample.flags */
enum {
    TC_RS_PLAYING = 1 << 0, TC_RS_PAUSED = 1 << 1, /* 1 << 2: the autopilot, removed */ TC_RS_AI = 1 << 3, TC_RS_GROUNDED = 1 << 4,
    TC_RS_DIRECTOR = 1 << 5, TC_RS_FIRST_PERSON = 1 << 6, TC_RS_CLOSEUP = 1 << 7, TC_RS_REFLEX = 1 << 8,
    TC_RS_PICTURE = 1 << 9
};

/* One point of the trail. Fields only ever get added at the end: a reader takes what the file has. */
typedef struct tc_rec_sample {
    uint32_t frame;              /* frames of this segment run */
    uint32_t flags;              /* TC_RS_* */
    float position[3], velocity[3], heading; /* Ed, tc_player */
    float eye[3], yaw, pitch, vertical_fov, orbit_distance; /* the camera */
    float animation_time, ed_phase;
    int32_t ed_clip;             /* tc_level.ed.anims index, -1 none */
    int32_t behaviour, state, wanted_state, card; /* his AI; -1 without one */
    int32_t hit_points, lives;
    uint32_t sector, camera_sector; /* TC_SECTOR_NONE: none */
    uint32_t ai_time_ms;         /* the game's clock */
    uint32_t placed, restarts;   /* tc_player.placed; his respawns */
    uint32_t viewer_phase, light_set;
    uint32_t awake, thinking, spawned; /* the cast */
    uint32_t rng, cast_rng;
    uint32_t ed_hash;            /* his dsgvars and random stream */
    uint32_t cast_hash;          /* every actor's place, behaviour, state, hit points, activity, dsgvars */
    uint32_t globals_hash;       /* tc_ai_globals */
    float picture_left, loading_time; /* seconds: the holds that stop the game's clock */
} tc_rec_sample;

/* A sample's fields by name, for the tools: kind 'u' uint32, 'i' int32, 'f' float, 'x' a hash. NULL past the last. */
typedef struct tc_rec_field { const char *name; uint32_t offset; char kind; } tc_rec_field;
const tc_rec_field *tc_rec_sample_field(uint32_t i);
const char *tc_rec_door_name(uint32_t door);

/* One actor of the cast at a moment. */
typedef struct tc_rec_actor {
    uint32_t id;
    float position[3];
    int32_t behaviour, state, hit_points;
    uint32_t flags;              /* 1 active, 2 dead, 4 awake, 8 thought */
} tc_rec_actor;

typedef enum tc_rec_type {
    TC_RT_NOTE = 1, TC_RT_SEGMENT, TC_RT_SEGMENT_END, TC_RT_DOOR, TC_RT_FRAME, TC_RT_SAMPLE, TC_RT_DRAW, TC_RT_SOUNDS,
    TC_RT_CAST, TC_RT_STORY, TC_RT_MARK
} tc_rec_type;

/* A level of the run. */
typedef struct tc_replay_segment {
    char level[64];              /* as the frontend named it ("sud0") */
    uint32_t edition;            /* tc_level.edition */
    uint32_t seed;               /* tc_viewer_desc.seed */
    bool has_carry;              /* arrived through a portal */
    tc_viewer_carry carry;
    char level_stamp[16], ai_stamp[16]; /* the converter's stamps: any exporter edit changes them */
    uint64_t level_hash, ai_hash;       /* the data itself, without the stamp and paths (tc_level.data_hash) */
    uint32_t frames, draws, marks;
    double seconds;              /* the sum of its dts */
    bool closed;                 /* the viewer was freed; false: the file ends inside it (a crash, a kill) */
    size_t first, end;           /* its records in the file */
} tc_replay_segment;

/* One record, as tc_replay_walk reads it; pointers into the file. */
typedef struct tc_replay_record {
    tc_rec_type type;
    uint32_t segment;            /* index, TC_REPLAY_NO_SEGMENT before the first */
    uint32_t frame;              /* frames of the segment run so far */
    double time;                 /* seconds of the segment so far */
    char key[128], text[512];    /* NOTE: key and value; MARK: text ("" none) */
    uint32_t mark;               /* MARK: its number */
    uint32_t door;               /* DOOR: tc_rec_door_name */
    int32_t door_a;
    float door_x, door_y;
    float dt;                    /* FRAME; */
    uint32_t held;               /* ... the inputs it ran with (tc_viewer.held ...) */
    float mouse[2], move[2], look[2];
    tc_rec_sample sample;        /* SAMPLE */
    uint32_t draw_count;         /* DRAW with hashes: */
    bool has_hash;
    uint64_t draw_hash, frame_hash;
    const uint8_t *sounds;       /* SOUNDS: sound_count little-endian uint16 */
    uint32_t sound_count;
    const uint8_t *actors;       /* CAST: actor_count tc_rec_actor */
    uint32_t actor_count;
    int32_t lives;               /* STORY */
    int16_t values[31];
} tc_replay_record;
#define TC_REPLAY_NO_SEGMENT 0xFFFFFFFFu

/* Where a replay left the recorded run, for one segment. Some drift is an intended fix: it says where and how far. */
typedef struct tc_replay_drift {
    uint32_t samples, hashes, sounds, casts;      /* compared */
    uint32_t bad_samples, bad_hashes, bad_sounds, bad_casts; /* of those, different */
    bool drifted;
    uint32_t first_frame;        /* the first difference */
    double first_time;
    char first_what[96];         /* "position[1]: recorded 12.5, replayed 12.75", "draws hash", "actor 17 state ..." */
    float max_distance;          /* Ed's farthest from the recorded trail, units */
    uint32_t max_frame;
    float end_distance;          /* at the last sample */
    bool carry_checked, carry_differs; /* the level change the run ended with, against the next segment's */
} tc_replay_drift;

typedef struct tc_replay {
    const uint8_t *data;         /* not owned: outlives the replay */
    size_t size;
    uint32_t version;
    tc_replay_segment *segments;
    uint32_t segment_count;
    bool truncated;              /* the last record is cut short (the file ends mid-write) */
    bool autopilot;              /* made with Ed's autopilot, since removed: its inputs came from code that is gone, so
                                    it can't be replayed */
    bool closed;                 /* the last segment was closed */
    /* Running one segment (tc_replay_begin, tc_replay_step). */
    tc_viewer *viewer;
    bool verify;
    uint32_t segment;
    size_t at;
    uint32_t frame;
    double time;
    uint32_t held;
    float move[2], look[2];
    bool drawn;                  /* the last frame was drawn in the recording */
    bool paused;                 /* tc_replay_pause */
    bool was_inspecting;         /* the viewer's own, before it: a recorded pause */
    tc_camera saved_camera;      /* what a resume puts back */
    tc_cam saved_cam;
    float saved_orbit;
    tc_replay_drift drift;
} tc_replay;

/* What a list of recordings shows, the same in every frontend. */
typedef struct tc_replay_summary {
    char when[24];               /* "2026-11-06 00:30", "" when the file doesn't say */
    char levels[112];            /* "sud0, cavedoc, bar (SE) and 4 more" */
    char details[112];           /* "12:34, 3 marks, did not end cleanly" */
    double seconds;
    uint32_t marks;
    bool playable;               /* it has a level to run and nothing that can't be run */
} tc_replay_summary;
void tc_replay_summarize(const tc_replay *r, tc_replay_summary *out);

/* Reads the file's outline. TC_ERR_BAD_MAGIC: not a recording; TC_ERR_STALE: a version this build can't read. */
tc_result tc_replay_open(tc_replay *r, const void *data, size_t size);
void tc_replay_close(tc_replay *r);
/* Every record in file order: *at from 0 until false. */
bool tc_replay_walk(const tc_replay *r, size_t *at, tc_replay_record *out);

/* Runs segment `segment` on v, a viewer just made from its level, tc_replay_segment.seed and .carry, with no recorder.
 * verify: compare what comes of it with what was recorded (tc_replay.drift). The frontend's own tc_viewer_draws in
 * between don't matter: the replay draws where the run drew, because the camera's sight reads the last draw list. */
void tc_replay_begin(tc_replay *r, uint32_t segment, tc_viewer *v, bool verify);
/* One recorded frame: its doors, its inputs, tc_viewer_update, its draw. false at the segment's end. *dt: the frame's;
 * tc_viewer_sounds holds its sounds. */
bool tc_replay_step(tc_replay *r, float *dt);
/* Pauses or resumes a running replay. Paused, don't step: call tc_viewer_paused_look each frame, with the player's look
 * inputs let through to the viewer. Resuming puts the camera back, so the run goes on exact. */
void tc_replay_pause(tc_replay *r, bool on);

#ifdef __cplusplus
}
#endif
#endif
