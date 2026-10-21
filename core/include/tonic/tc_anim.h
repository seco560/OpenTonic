/* Sampling of baked animations (persos and Ed): pick the two neighbouring samples and a blend
 * factor, then lerp the channel matrices (fine for samples this close together). */
#ifndef TONIC_TC_ANIM_H
#define TONIC_TC_ANIM_H
#include <stdbool.h>
#include <stdint.h>
#include "tc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_sample { uint32_t s0, s1; float blend; } tc_sample;

/* Looping clip at `time` seconds (persos): f = time * rate, s0 = f mod n, s1 = s0 + 1 mod n. */
tc_sample tc_sample_at_time(float time, float sample_rate, uint32_t sample_count);
/* Clip position in samples (Ed): looping wraps s1, one-shot clamps it to the last sample. */
tc_sample tc_sample_at_phase(float phase, uint32_t sample_count, bool loop);
/* Advance a phase (in samples) by `samples`; loop: wrap to 0..n, else clamp to n - 1. */
float tc_phase_advance(float phase, float samples, uint32_t sample_count, bool loop);

/* Channel -> object matrix at sample s. Blends toward s1 only while the channel shows the same
 * object there (no lerp across a part swap). Visibility follows s0: the caller checks shown[]. */
tc_mat4 tc_channel_matrix(const tc_mat34 *transforms, const uint16_t *shown, uint32_t channel_count,
                          tc_sample s, uint32_t channel);

#ifdef __cplusplus
}
#endif
#endif
