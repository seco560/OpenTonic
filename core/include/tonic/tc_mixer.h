/* The sound effects' mixer: the voices a frontend has started, summed into one stereo signal (docs/ENGINE.md "3D sound",
 * "The mixer"). The original's software mixer (WAVMWSVR.DLL) as far as the port follows it: 64 voices (all busy: a new
 * sound is not played), each at the gains tc_sound_gains made for it (tc_sound.h), stepped at its pitch with no
 * interpolation (the sample the step lands on), the sum saturated; with Dolby Surround on, the voices' rear signals
 * summed and put through the encoder's filter (tc_sound_dolby_filter) into the two channels.
 *
 * A voice's life is the mixer's own business: a one-shot ends when its last sample has been rendered, a loop when
 * something stops it. Nothing about it is read back from the audio system (the SDL build's per-voice streams were: a
 * resampling stream never reports itself empty, so every one-shot kept its voice and the 65th sound of a level was never
 * heard; BUGS.md, 2026-11-15).
 *
 * Threads: tc_mixer_render runs on the audio thread, everything else on the game's. The mixer takes no lock: the
 * frontend holds one (its audio stream's, an unfair lock) around every call here, render included. Nothing here
 * allocates, waits or reads the samples outside render. The samples are the frontend's, mono floats at the mixer's rate,
 * and must outlive their voices: stop them (tc_mixer_stop_all, _stop_speech) before freeing. */
#ifndef TONIC_TC_MIXER_H
#define TONIC_TC_MIXER_H
#include <stdbool.h>
#include <stdint.h>
#include "tc_sound.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_MIXER_VOICES 64       /* WAVMWSVR.DLL: 0x40 */
#define TC_MIXER_MAX_TAPS 256    /* the Dolby encoder's filter: 32 taps at 22050 Hz, as long a span at the mixer's rate */
#define TC_MIXER_BLOCK 1024      /* frames rendered at a time (the rear signal's buffer) */

typedef struct tc_mixer_voice {
    const float *samples;        /* NULL: a free voice. Mono, at the mixer's rate; the frontend's */
    uint32_t count;
    uint32_t at;                 /* the next sample */
    double frac;                 /* between samples: the pitch's steps */
    double pitch;                /* samples per frame (tc_sound_pitch; 1: as recorded). <= 0 is taken as 1 */
    float left, right, rear;     /* its gains now (tc_sound_mix) */
    float volume;                /* its event's (events.tsv): what its gains are made from again */
    int32_t loop;                /* the loop event it plays, -1: a one-shot */
    bool speech;                 /* a character's line: tc_mixer_stop_speech ends it */
    bool follow;                 /* placed again every frame from its emitter (events.tsv's follow column) */
    tc_sound_source source;      /* where it sounds from */
    uint32_t serial;             /* which start this voice holds (set by tc_mixer_start, never 0) */
} tc_mixer_voice;

typedef struct tc_mixer {
    tc_mixer_voice voices[TC_MIXER_VOICES];
    uint32_t serials;
    bool muted, dolby;
    float gain;                  /* the sound effects' volume (the menus' SOUND VOLUME), 0..1; 1 after tc_mixer_init */
    uint32_t dropped;            /* sounds not played since tc_mixer_stop_all: every voice was busy */
    int taps;                    /* the encoder's filter for this rate */
    float coef[TC_MIXER_MAX_TAPS];
    float rear[TC_MIXER_MAX_TAPS + TC_MIXER_BLOCK]; /* the rear signal: the last taps samples, then the block's */
} tc_mixer;

/* A silent mixer for sounds at `rate` Hz (the frontend's choice: every sound is brought to it when read). */
void tc_mixer_init(tc_mixer *m, float rate);

/* Starts `v` (its samples, count, pitch, gains, volume, loop, speech, follow and source; the rest is set here).
 * Returns its voice, or TC_MIXER_BUSY (all 64 are playing: not played, counted in dropped), TC_MIXER_PLAYING (a loop
 * already playing that event from that emitter goes on: the animations restart flight and sliding every cycle) or
 * TC_MIXER_NOTHING (no samples). */
#define TC_MIXER_BUSY (-1)
#define TC_MIXER_PLAYING (-2)
#define TC_MIXER_NOTHING (-3)
int tc_mixer_start(tc_mixer *m, const tc_mixer_voice *v);

/* A stop event: the loops of `event` from that emitter (from any, when the stop is a flat one: a global stop). */
void tc_mixer_stop_loop(tc_mixer *m, int32_t event, const tc_sound_source *from);
void tc_mixer_stop_speech(tc_mixer *m);
/* One voice (its perso was taken out). */
void tc_mixer_stop(tc_mixer *m, int voice);
/* Every voice, and the dropped count back to 0 (a level change). */
void tc_mixer_stop_all(tc_mixer *m);
/* How many voices are playing. */
int tc_mixer_playing(const tc_mixer *m);

/* The audio thread: the next `frames` of the mix, as interleaved stereo floats (1 = full scale), clamped to -1..1.
 * False: nothing is playing (out is all zeros). */
bool tc_mixer_render(tc_mixer *m, float *out, int frames);

#ifdef __cplusplus
}
#endif
#endif
