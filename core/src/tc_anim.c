#include "tc_internal.h"
#include "tonic/tc_anim.h"

/* Port choice: the largest step (units) of a channel across a part swap that is still blended. Ed's walk swaps at
 * 0.06-0.16; the original draws whole frames only, so blending at all is the port's. */
#define TC_SWAP_BLEND_MAX 0.5f

tc_sample tc_sample_at_time(float time, float sample_rate, uint32_t sample_count) {
    tc_sample s = {0, 0, 0};
    float f = time * sample_rate, fl;
    if (sample_count == 0 || !(f >= 0)) return s;
    fl = floorf(f);
    s.s0 = (uint32_t)fmodf(fl, (float)sample_count); /* exact, = Int(f) % n */
    s.s1 = (s.s0 + 1) % sample_count;
    s.blend = f - fl;
    return s;
}

tc_sample tc_sample_at_phase(float phase, uint32_t sample_count, bool loop) {
    tc_sample s = {0, 0, 0};
    if (sample_count == 0) return s;
    s.s0 = phase > 0 ? (phase < (float)(sample_count - 1) ? (uint32_t)phase : sample_count - 1) : 0;
    s.s1 = loop ? (s.s0 + 1) % sample_count : (s.s0 + 1 < sample_count ? s.s0 + 1 : sample_count - 1);
    s.blend = phase - (float)s.s0;
    return s;
}

float tc_phase_advance(float phase, float samples, uint32_t sample_count, bool loop) {
    phase += samples;
    if (loop) return fmodf(phase, (float)sample_count);
    return fminf(phase, (float)sample_count - 1);
}

tc_mat4 tc_channel_matrix(const tc_mat34 *transforms, const uint16_t *shown, uint32_t channel_count,
                          tc_sample s, uint32_t channel) {
    uint32_t i0 = s.s0 * channel_count + channel, i1 = s.s1 * channel_count + channel;
    tc_mat4 m0 = tc_mat4_from34(&transforms[i0]), m1;
    if (shown[i1] != shown[i0]) {
        /* A part swap (Ed's walk changes a foot's mesh 7 times a cycle, 3 of them round the loop): the channel's
         * transform usually goes on smoothly, and holding it for the whole step made that foot jerk at 30 Hz while
         * the rest moved smoothly. Blend unless a side is hidden or the channel jumps (a part moved elsewhere). */
        const float *a = transforms[i0].m, *b = transforms[i1].m;
        float dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
        if (shown[i0] == 0xFFFFu || shown[i1] == 0xFFFFu || dx * dx + dy * dy + dz * dz > TC_SWAP_BLEND_MAX * TC_SWAP_BLEND_MAX)
            return m0;
    }
    m1 = tc_mat4_from34(&transforms[i1]);
    return tc_mat4_lerp(&m0, &m1, s.blend);
}
