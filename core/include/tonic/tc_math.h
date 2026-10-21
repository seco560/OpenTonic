/* Minimal vector/matrix math. Engine space, Z up.
 * tc_mat34: row-major 3x4 as stored in .ttlv (m[0..3] = first row, m[3]/m[7]/m[11] = translation).
 * tc_mat4:  column-major 4x4 (m[col * 4 + row]), the layout of simd_float4x4 / GL / Metal. */
#ifndef TONIC_TC_MATH_H
#define TONIC_TC_MATH_H
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TC_PI 3.14159265358979323846f

typedef struct tc_vec2 { float x, y; } tc_vec2;
typedef struct tc_vec3 { float x, y, z; } tc_vec3;
typedef struct tc_mat34 { float m[12]; } tc_mat34;
typedef struct tc_mat4 { float m[16]; } tc_mat4;

static inline tc_vec2 tc_v2(float x, float y) { tc_vec2 r; r.x = x; r.y = y; return r; }
static inline tc_vec3 tc_v3(float x, float y, float z) { tc_vec3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline tc_vec3 tc_v3_add(tc_vec3 a, tc_vec3 b) { return tc_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline tc_vec3 tc_v3_sub(tc_vec3 a, tc_vec3 b) { return tc_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline tc_vec3 tc_v3_scale(tc_vec3 a, float s) { return tc_v3(a.x * s, a.y * s, a.z * s); }
static inline tc_vec3 tc_v3_div(tc_vec3 a, float s) { return tc_v3(a.x / s, a.y / s, a.z / s); }
static inline tc_vec3 tc_v3_adds(tc_vec3 a, float s) { return tc_v3(a.x + s, a.y + s, a.z + s); }
static inline float tc_v3_dot(tc_vec3 a, tc_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
/* Cross product with one fused multiply-add per component, exactly as Apple's simd_cross is
 * compiled (clang contracts its a*b - c*d). The original Swift code used that, and near-degenerate
 * sliver triangles are kept or dropped depending on it (see tc_collision_init). fmaf is correctly
 * rounded by the C standard, so the result is the same on every platform. */
static inline tc_vec3 tc_v3_cross(tc_vec3 a, tc_vec3 b) {
    return tc_v3(fmaf(a.y, b.z, -(a.z * b.y)), fmaf(a.z, b.x, -(a.x * b.z)), fmaf(a.x, b.y, -(a.y * b.x)));
}
static inline float tc_v3_length_sq(tc_vec3 a) { return tc_v3_dot(a, a); }
static inline float tc_v3_length(tc_vec3 a) { return sqrtf(tc_v3_dot(a, a)); }
static inline float tc_v3_distance(tc_vec3 a, tc_vec3 b) { return tc_v3_length(tc_v3_sub(a, b)); }
static inline tc_vec3 tc_v3_min(tc_vec3 a, tc_vec3 b) { return tc_v3(fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)); }
static inline tc_vec3 tc_v3_max(tc_vec3 a, tc_vec3 b) { return tc_v3(fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)); }
static inline float tc_v2_length(tc_vec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }

static inline tc_mat4 tc_mat4_identity(void) {
    tc_mat4 r = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    return r;
}
/* Row-major 3x4 -> column-major 4x4 (bottom row 0 0 0 1). */
static inline tc_mat4 tc_mat4_from34(const tc_mat34 *a) {
    const float *m = a->m;
    tc_mat4 r = {{m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, m[3], m[7], m[11], 1}};
    return r;
}
static inline tc_mat4 tc_mat4_mul(const tc_mat4 *a, const tc_mat4 *b) {
    tc_mat4 r;
    int c, k;
    for (c = 0; c < 4; c++)
        for (k = 0; k < 4; k++)
            r.m[c * 4 + k] = a->m[k] * b->m[c * 4] + a->m[4 + k] * b->m[c * 4 + 1] +
                             a->m[8 + k] * b->m[c * 4 + 2] + a->m[12 + k] * b->m[c * 4 + 3];
    return r;
}
/* a * (1 - t) + b * t, element-wise. */
static inline tc_mat4 tc_mat4_lerp(const tc_mat4 *a, const tc_mat4 *b, float t) {
    tc_mat4 r;
    int i;
    for (i = 0; i < 16; i++) r.m[i] = a->m[i] * (1 - t) + b->m[i] * t;
    return r;
}

#ifdef __cplusplus
}
#endif
#endif
