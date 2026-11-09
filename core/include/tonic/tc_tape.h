/* The original game's demo tapes: Demos/<level>/DemoNNN.sav on the retail disc, made by the exe's own demo recorder
 * (Demos.c; writer GAM_0042d2f0, player GAM_00434680 / GAM_00434440). A tape holds what happened, not the inputs: each
 * frame, every perso on show with its place, rotation, state and animation frame. So it plays on top of a level with no
 * AI and no mechanics, and an engine change can't make it drift. docs/RECORDING.md "The original demos".
 *
 * File (little-endian): u32 frames, u32 start sector mask, u32 size of the rest; then per frame u16 its size in bytes
 * with this header, u16 its length in ms, then records until the frame's end. A record starts with a u16 mask:
 *   bit 15 set, or 0x1000 / 0x0800: events (sounds, spawns, ...), read past;
 *   else u16 object, then by mask bit: 0x10 position 3 f32, 0x40 rotation 4 i16 (w, x, y, z of the inverse, 1 = 32767), 0x02 state u16,
 *   0x04 animation frame f32, 0x08 u8 (its transparency), 0x2000 u16 (its sector), 0x100 u32 (its custom bits).
 * Only what changed is written, and a perso is in a frame only while it is on show. */
#ifndef TONIC_TC_TAPE_H
#define TONIC_TC_TAPE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One tape object after the last frame read (`..._prev`: after the one before, to draw between the two). */
typedef struct tc_tape_object {
    tc_vec3 position, position_prev;
    float rotation[4], rotation_prev[4]; /* quaternion x, y, z, w */
    float frame, frame_prev;             /* frame_prev: as frame after a new state or a loop, nothing to draw between */
    float frame_before;                  /* the frame before this one's, whatever happened: what a sound has to have passed */
    uint16_t state, state_prev;
    bool known;                          /* seen at least once */
    bool shown;                          /* in the last frame read */
} tc_tape_object;

typedef struct tc_tape {
    uint8_t *data;                       /* the file */
    size_t size;
    uint32_t frame_count, frames_read;
    uint32_t bad_frames;                 /* frames with a record this reader doesn't know: the rest of those is skipped */
    size_t cursor;                       /* the next frame */
    double time_prev, time;              /* ms at which the last two frames read are due */
    double seconds;                      /* the tape's length */
    uint32_t object_count;
    tc_tape_object *objects;
} tc_tape;

/* Copies the file, checks it, and reads frame 0. false (t left empty): not a tape, or out of memory. */
bool tc_tape_open(tc_tape *t, const void *data, size_t size);
void tc_tape_close(tc_tape *t);
/* Reads the next frame; false at the end. */
bool tc_tape_next(tc_tape *t);
/* The quaternion's rotation, rows of a 3x3 (perso axes -> world). */
void tc_tape_rotation(const float q[4], float r[9]);

#ifdef __cplusplus
}
#endif
#endif
