/* 3D sound (tc_sound.h): the exe's 16.16 helpers, as WAVMWSVR.DLL's _SND_fn_vParam3Dto2D calls them. */
#include <math.h>
#include "tonic/tc_sound.h"

#define FIX_ONE 65536
#define DEFAULT_MIN (8 * FIX_ONE)  /* _SND_fn_vGetDefaultRollOff: 0x80000 (Ubi's SND_CPA.INI SXD_DIST_SATUR=8 says the same) */

/* The game's callbacks: float x 65536, then ftol (truncation). */
static int32_t fix(float f) {
    double d = (double)f * FIX_ONE;
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return (int32_t)-2147483647 - 1;
    return (int32_t)d;
}

static int32_t iabs(int32_t a) { return a < 0 ? -a : a; }

/* SND_0040c5d0: the larger plus half the smaller. */
static int32_t pseudo2(int32_t a, int32_t b) {
    a = iabs(a), b = iabs(b);
    return a < b ? (a >> 1) + b : (b >> 1) + a;
}

static int32_t pseudo_norm(const int32_t d[3]) { return pseudo2(pseudo2(d[0], d[1]), d[2]); }

/* _SND_fn_rDivRealRealSnd: trunc(a / b x 65536) on the x87 (b = 0 does not come here: the rolloffs' callers avoid it). */
static int32_t fix_div(int32_t a, int32_t b) { return (int32_t)((double)a / (double)b * FIX_ONE); }

/* _SND_fn_rMulRealRealSnd(0x7f, ratio) for 0 <= ratio <= 1.0: (ratio x 127) >> 16. */
static int scale127(int32_t ratio) {
    if (ratio <= 0) return 0;
    if (ratio >= FIX_ONE) return 127;
    return (int)(((int64_t)ratio * 127) >> 16);
}

static void offset_fix(tc_vec3 v, int32_t out[3]) {
    out[0] = fix(v.x), out[1] = fix(v.y), out[2] = fix(v.z);
}

int32_t tc_sound_distance(tc_vec3 offset) {
    int32_t d[3];
    offset_fix(offset, d);
    return pseudo_norm(d);
}

int tc_sound_volume(int32_t distance, const tc_sound_rolloff *own) {
    int32_t lo, hi, far;
    if (!own) { /* _SND_fn_ucDistanceToVolume with the default rolloff (min 8, its far end 8 min, no floor) */
        far = 8 * DEFAULT_MIN;
        if (distance < DEFAULT_MIN) return 127;
        if (distance > far) return 0;
        return scale127(fix_div(far - distance, far - DEFAULT_MIN));
    }
    /* _SND_fn_ucDistanceToVolumeEx */
    lo = own->min, hi = own->max;
    if (hi < lo) hi = 0x7FFFFFFF;
    if (distance < lo) return 127;
    if (lo <= 0) return 0; /* min 0 (sud0's family 39): 0 / 0 in the exe, whose byte comes out 0 */
    far = lo * 8;
    if (distance <= hi) return distance > far ? 0 : scale127(fix_div(far - distance, lo * 7));
    return far < hi ? 0 : scale127(fix_div(far - hi, lo * 7)); /* beyond max: held at max's */
}

int tc_sound_pan(tc_vec3 offset, tc_vec3 right) {
    int32_t d[3], a[3], n, dot, half, p;
    offset_fix(offset, d);
    offset_fix(right, a);
    n = pseudo_norm(d);
    /* _SND_fn_rPseudoScalaireNormeVectorSnd: each product of the 8.8 halves (_SND_fn_rMulRealRealQuickSnd) */
    dot = (d[0] >> 8) * (a[0] >> 8) + (d[1] >> 8) * (a[1] >> 8) + (d[2] >> 8) * (a[2] >> 8);
    half = (int32_t)(((int64_t)n * 2) >> 8);
    if (half == 0) return 64;
    if (dot <= -n) return 0;
    if (n <= dot) return 127;
    p = (dot / 256) * 127 / half + 64; /* C division truncates toward zero, as the exe's rounding before its shifts */
    return p < 0 ? 0 : p > 127 ? 127 : p;
}

int tc_sound_dolby(tc_vec3 offset, tc_vec3 front) {
    int32_t d[3], a[3], n, dot, half, p;
    offset_fix(offset, d);
    offset_fix(front, a);
    n = pseudo_norm(d);
    dot = (d[0] >> 8) * (a[0] >> 8) + (d[1] >> 8) * (a[1] >> 8) + (d[2] >> 8) * (a[2] >> 8);
    half = (int32_t)(((int64_t)n * 2) >> 8);
    if (half == 0) return 63;
    if (dot <= -n) return 127;
    if (n <= dot) return 0;
    p = 63 - (dot / 256) * 127 / half;
    return p < 0 ? 0 : p > 127 ? 127 : p;
}

void tc_sound_gains(int volume, int pan, int dolby, float event_volume, bool dolby_on, tc_sound_mix *out) {
    int res = (int)(event_volume * 127 + 0.5f), v;
    if (res < 0) res = 0;
    if (res > 127) res = 127;
    v = (res * volume) >> 7; /* FUN_10008e90: resource volume x voice volume >> 7 */
    out->left = out->right = out->rear = 0;
    if (v < 3 || pan < 0 || pan > 127) return; /* not mixed (voice +0x40 = 0) */
    if (!dolby_on) { /* 0x1000c91b: the gains as 16-bit words for pmulhw (x >> 16) */
        out->left = (float)(int16_t)(v * (128 - pan) * 2) / 65536;
        out->right = (float)(int16_t)(v * pan * 2) / 65536;
        return;
    }
    if (dolby < 0) dolby = 0;
    if (dolby > 127) dolby = 127;
    out->left = (float)(int16_t)((uint32_t)(v * (128 - pan) * (128 - dolby)) >> 5) / 65536;
    out->right = (float)(int16_t)((uint32_t)(v * pan * (128 - dolby)) >> 5) / 65536;
    out->rear = (float)(int16_t)(v * dolby * 2) / 65536;
}

float tc_sound_pitch(float spread, int rand15) {
    int32_t s = (int32_t)(spread * FIX_ONE), r, p;
    if (s == 0) return 1;
    r = fix_div(rand15, 0x7fff);                 /* SND_00409150: rand() / 0x7fff in 16.16 */
    p = (r >> 8) * ((s * 2) >> 8);               /* x 2 spread, _SND_fn_rMulRealRealQuickSnd */
    return (float)(p + FIX_ONE - s) / FIX_ONE;   /* + 1 - spread */
}

void tc_sound_dolby_filter(int16_t *coef, int taps, float rate) {
    double h[256], sum = 0;
    float lo = 100.0f / (rate * 0.5f), hi = 7000.0f / (rate * 0.5f); /* fVar1, fVar2 (single precision) */
    const double pi = 3.1415;                     /* the exe's constant */
    int half = taps / 2, i, k;
    if (taps <= 0 || taps > 256) return;
    for (i = 0; i < taps; i++) {                   /* type 5: iVar10 = i - half; iVar7 = -iVar10 */
        int j = i - half;
        double f = 0;
        for (k = -128; k < 128; k++) {
            int u = k - j;
            if (!(u & 1)) continue;                /* odd taps of the Hilbert transform only */
            if (k == 0) f += ((double)hi - lo) * (-4.0 / ((double)(-j) * pi));
            else f += (-4.0 / ((double)u * pi)) * ((sin(hi * k * pi) - sin(lo * k * pi)) / (k * pi));
        }
        h[i] = f;
        sum += fabs(f);
    }
    for (i = 0; i < taps; i++) coef[i] = (int16_t)(sum > 0 ? h[i] * 32768.0 / sum : 0); /* ftol: truncation */
}
