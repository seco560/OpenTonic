/* The sound effects' mixer (tc_mixer.h). */
#include <string.h>
#include "tonic/tc_mixer.h"

void tc_mixer_init(tc_mixer *m, float rate) {
    int16_t c[TC_MIXER_MAX_TAPS];
    int taps = (int)(32.0 * rate / 22050 + 1) & ~1, k; /* the original's 32 at 22050 Hz: as long a span, even */
    memset(m, 0, sizeof(*m));
    if (taps < 2) taps = 2;
    if (taps > TC_MIXER_MAX_TAPS) taps = TC_MIXER_MAX_TAPS;
    memset(c, 0, sizeof(c));
    tc_sound_dolby_filter(c, taps, rate);
    m->taps = taps;
    m->gain = 1;
    for (k = 0; k < taps; k++) m->coef[k] = (float)c[k] / 8192; /* its sum >> 13 */
}

int tc_mixer_start(tc_mixer *m, const tc_mixer_voice *v) {
    int i, free_voice = -1;
    if (!v->samples || !v->count) return TC_MIXER_NOTHING;
    for (i = 0; i < TC_MIXER_VOICES; i++) {
        const tc_mixer_voice *x = &m->voices[i];
        if (!x->samples) {
            if (free_voice < 0) free_voice = i;
        } else if (v->loop >= 0 && x->loop == v->loop && x->source.emitter == v->source.emitter)
            return TC_MIXER_PLAYING;
    }
    if (free_voice < 0) {
        m->dropped++;
        return TC_MIXER_BUSY;
    }
    m->voices[free_voice] = *v;
    m->voices[free_voice].at = 0;
    m->voices[free_voice].frac = 0;
    if (!(v->pitch > 0)) m->voices[free_voice].pitch = 1;
    if (++m->serials == 0) m->serials = 1;
    m->voices[free_voice].serial = m->serials;
    return free_voice;
}

void tc_mixer_stop_loop(tc_mixer *m, int32_t event, const tc_sound_source *from) {
    int i;
    if (event < 0) return;
    for (i = 0; i < TC_MIXER_VOICES; i++) {
        tc_mixer_voice *x = &m->voices[i];
        if (x->samples && x->loop == event && (from->kind == TC_SOUND_FLAT || x->source.emitter == from->emitter))
            x->samples = NULL;
    }
}

void tc_mixer_stop_speech(tc_mixer *m) {
    int i;
    for (i = 0; i < TC_MIXER_VOICES; i++)
        if (m->voices[i].speech) m->voices[i].samples = NULL, m->voices[i].speech = false;
}

void tc_mixer_stop(tc_mixer *m, int voice) {
    if (voice >= 0 && voice < TC_MIXER_VOICES) m->voices[voice].samples = NULL;
}

void tc_mixer_stop_all(tc_mixer *m) {
    int i;
    for (i = 0; i < TC_MIXER_VOICES; i++) m->voices[i].samples = NULL, m->voices[i].speech = false;
    m->dropped = 0;
}

int tc_mixer_playing(const tc_mixer *m) {
    int i, n = 0;
    for (i = 0; i < TC_MIXER_VOICES; i++) n += m->voices[i].samples != NULL;
    return n;
}

static float clamp1(float x) { return x < -1 ? -1 : x > 1 ? 1 : x; }

/* frames <= TC_MIXER_BLOCK of the voices, summed. */
static bool mix_block(tc_mixer *m, float *out, int frames) {
    int t = m->taps, i, j, k;
    float *back = m->rear + t; /* back[-t .. -1]: the last block's end */
    bool sounding = false;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    memset(back, 0, (size_t)frames * sizeof(float));
    for (i = 0; i < TC_MIXER_VOICES; i++) {
        tc_mixer_voice *v = &m->voices[i];
        const float *samples = v->samples;
        float gl, gr, gb;
        uint32_t at, count;
        double frac, step;
        bool heard;
        if (!samples) continue;
        sounding = true;
        gl = m->muted ? 0 : v->left * m->gain, gr = m->muted ? 0 : v->right * m->gain;
        gb = m->muted || !m->dolby ? 0 : v->rear * m->gain;
        heard = gl != 0 || gr != 0 || gb != 0; /* an unheard voice runs on silently (the original's: under 3, not mixed) */
        at = v->at, count = v->count, frac = v->frac, step = v->pitch;
        for (j = 0; j < frames; j++) {
            if (at >= count) {
                if (v->loop < 0) { /* a one-shot, played out: its voice is free again */
                    v->samples = NULL;
                    v->speech = false;
                    break;
                }
                at %= count;
            }
            if (heard) {
                float x = samples[at];
                out[2 * j] += x * gl;
                out[2 * j + 1] += x * gr;
                back[j] += x * gb;
            }
            if (step == 1) at++;
            else {
                uint32_t whole;
                frac += step;
                whole = (uint32_t)frac;
                at += whole;
                frac -= whole;
            }
        }
        v->at = at;
        v->frac = frac;
    }
    if (m->dolby && !m->muted) { /* the encoder (WAVMWSVR.DLL 0x1000d823): y[n] = sum c[k] rear[n - taps + k], L + y, R - y */
        for (j = 0; j < frames; j++) {
            float y = 0;
            for (k = 0; k < t; k++) y += m->coef[k] * back[j - t + k];
            out[2 * j] += y;
            out[2 * j + 1] -= y;
        }
        memmove(m->rear, back + frames - t, (size_t)t * sizeof(float)); /* the next block's history */
    } else
        memset(m->rear, 0, (size_t)t * sizeof(float));
    for (j = 0; j < frames * 2; j++) out[j] = clamp1(out[j]); /* the original's sum saturates (at 16 bits) */
    return sounding;
}

bool tc_mixer_render(tc_mixer *m, float *out, int frames) {
    bool sounding = false;
    while (frames > 0) {
        int n = frames < TC_MIXER_BLOCK ? frames : TC_MIXER_BLOCK;
        sounding |= mix_block(m, out, n);
        out += n * 2;
        frames -= n;
    }
    return sounding;
}
