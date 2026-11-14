/* 3D sound: where a sound is heard from, as the original's sound engine places it (docs/ENGINE.md "3D sound").
 *
 * The exe (sound3d.c, SND_sound3d_00404db0 and the per-frame SND_sound3d_00404c40 from the engine's frame at 0x4289d3)
 * asks the game for an object's position through the object type's callbacks (0x4225a0..: the perso's world matrix
 * translation) and for the listener, a "micro" the standard camera perso carries (its MS_Micro, perso +0x28: its world
 * matrix times the micro's own matrix, so the micro looks along the camera's sight with its pan axis to the camera's
 * right). The sound driver (WAVMWSVR.DLL _SND_fn_vParam3Dto2D, the software mixer; the DirectSound3D driver hands the
 * same to DirectSound) turns them into a volume and a pan with the exe's own exported helpers, in 16.16 fixed point:
 *   distance  _SND_fn_rPseudoNormeVectorSnd: f(f(|x|, |y|), |z|), f(a, b) = the larger + half the smaller
 *   volume    _SND_fn_ucDistanceToVolume (the default rolloff: 127 nearer than 8, falling straight to 0 at 64), or for a
 *             perso with a rolloff of its own (perso +0x40 {min, max}; .ttlv SND3) _SND_fn_ucDistanceToVolumeEx: 127
 *             nearer than min, straight to 0 at 8 min, and beyond max no quieter than at max (max < min: no such floor)
 *   pan       _SND_fn_ucPositionToPan: 64 + 127 (offset . right) / (2 distance), clamped to 0..127 (0 left, 127 right)
 *   dolby     _SND_fn_ucPositionToDolby: 63 - 127 (offset . front) / (2 distance), clamped (0 ahead, 127 behind)
 * The mixer then plays the voice at its resource's volume times this one (/128), and leaves voices under 3 unmixed
 * (still running: a loop comes back when its emitter does). Doppler is off (GAM_fn_vLoadFix: SND_sound3d_00404b70(0)).
 * The pitch: the event's random spread (SND_004048e0), fixed for the voice's life.
 * Whether a listener hears an object at all (callback 0x422860): the main actor always; an object without a sector
 * never; one in the listener's sector always; else unless its sector is on the listener sector's sound list (sector
 * +0x70) with value 0 (sud0, marmite and pyramide have such lists). */
#ifndef TONIC_TC_SOUND_H
#define TONIC_TC_SOUND_H
#include <stdbool.h>
#include <stdint.h>
#include "tc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The kinds of sound the game starts (PROC_PlaySound3DEtc_0048af90, GAM_00436910). */
#define TC_SOUND_FLAT 0  /* PlaySoundGlobal / Global2 (PROC 147 / 148): no object (-1), at its own volume, centred; also the
                            port's speech and menu sounds */
#define TC_SOUND_OWN 1   /* object type A: PlaySound3D (PROC 145) and the animations' sound events: on the perso, with its
                            own rolloff when it has one; DeactivatePerso / RemovePerso stop them (GAM_Objinit_00419a10 /
                            00419b60 -> SND_sound3d_004050e0(perso, type A)) */
#define TC_SOUND_OTHER 2 /* object type B: PlaySound3DOther (PROC 146): on the perso, the default rolloff, not stopped so */
#define TC_SOUND_EMITTER_MAIN (-1) /* tc_sound_source.emitter: the main actor (Ed, the skier) */
#define TC_SOUND_EMITTER_POINT (-2) /* tc_sound_source.emitter: a place (tc_sound_source.at; a recorded demo's persos) */

/* A started sound's emitter (tc_viewer_sound_sources, parallel to tc_viewer_sounds' events). */
typedef struct tc_sound_source {
    int32_t emitter;             /* the perso it sounds from: its id (the cast's), or TC_SOUND_EMITTER_MAIN */
    uint8_t kind;                /* TC_SOUND_* */
    uint8_t pad;
    uint16_t serial;             /* the emitter's tc_ai_perso.sound_serial when it started (TC_SOUND_OWN: a change stops it) */
    tc_vec3 at;                  /* TC_SOUND_EMITTER_POINT: where */
} tc_sound_source;

/* Where the sounds are heard from: the camera's micro (its position, its right axis, unit length, and its sector). */
typedef struct tc_sound_listener {
    tc_vec3 position, right, front; /* front: the camera's sight (the Dolby rear value's axis) */
    uint16_t sector;             /* 0xFFFF: unknown (no sector muting) */
    bool valid;                  /* false: not known yet (every sound counts as heard at full volume) */
} tc_sound_listener;
#define TC_SOUND_AUDIBLE 3       /* the mixer leaves quieter voices unmixed */

/* A rolloff in 16.16 units (perso +0x40). */
typedef struct tc_sound_rolloff { int32_t min, max; } tc_sound_rolloff;

/* The pseudo-distance of an offset, 16.16 (positions are taken to 16.16 as the exe does: x 65536, truncated). */
int32_t tc_sound_distance(tc_vec3 offset);
/* 0..127 at that 16.16 distance: the perso's own rolloff, or NULL for the default one. */
int tc_sound_volume(int32_t distance, const tc_sound_rolloff *own);
/* 0..127 (0 left, 64 centred, 127 right): offset = emitter - listener, right = the listener's unit right axis. */
int tc_sound_pan(tc_vec3 offset, tc_vec3 right);
/* 0..127 (0 ahead, 63 to the side, 127 behind): front = the listener's unit sight. */
int tc_sound_dolby(tc_vec3 offset, tc_vec3 front);

/* A voice's gains, applied to its samples (floats, 1 = 16-bit full scale) as the original's software mixer does
 * (WAVMWSVR.DLL): v = resource volume x volume >> 7 (FUN_10008e90; under 3 not mixed: all 0). Stereo (0x1000c910):
 * left = v (128 - pan) 2 / 65536, right = v pan 2 / 65536: a centred sound at full volume is 0.246 on each side, one
 * hard to a side 0.49 there (the headroom of its 64 voices, saturated at 16 bits). Dolby Surround, the sound menu's
 * option (MnuSNDmw.dll ChangeDolby -> _SND_fn_vSetDolbySample; off by default) (0x1000d4f7): the front gains are
 * v (128 - pan) (128 - dolby) >> 5 / 65536 (twice the stereo ones ahead, as much at the side, none behind) and
 * rear = v dolby 2 / 65536 goes to the mono rear signal, which tc_sound_dolby_encode puts into the two channels. (That
 * path in the exe hands the pan's lanes over the other way round, left and right swapped against the stereo path:
 * taken as a slip, not ported.) */
typedef struct tc_sound_mix { float left, right, rear; } tc_sound_mix;
void tc_sound_gains(int volume, int pan, int dolby, float event_volume, bool dolby_on, tc_sound_mix *out);

/* The pitch of a voice of an event with this spread (events.tsv, the event's +0x0C / 65536): SND_004048e0 with
 * rand15 = the C library's rand() (0..32767): 1 + rand15 / 32767 x 2 spread - spread, in its 16.16 steps. */
float tc_sound_pitch(float spread, int rand15);

/* The Dolby Surround encoder's filter (FUN_10007b90 type 5, made for the mixer's rate: 32 taps at 22050 Hz): a band-pass
 * of 100 to 7000 Hz with a Hilbert transform (the rear 90 degrees out of phase), coefficients h x 32768 / sum |h|,
 * truncated. taps even, <= 256. The mixer (0x1000d823) then adds y[n] = sum_k c[k] rear[n - taps + k] >> 13 to the left
 * and takes it from the right. */
void tc_sound_dolby_filter(int16_t *coef, int taps, float rate);

#ifdef __cplusplus
}
#endif
#endif
