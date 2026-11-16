/* Custom gameplay: a course's end (tcx_course.h). */
#include "../tc_internal.h"
#include <string.h>
#include "../tc_reader.h"
#include "tcx_course.h"

static void read_name(reader *r, char *out) {
    uint16_t n = rd_u16(r), k;
    out[0] = 0;
    if (!have(r, n)) return;
    for (k = 0; k < n; k++) {
        uint8_t ch = rd_u8(r);
        if (k + 1 < TCX_COURSE_NAME) out[k] = (char)ch, out[k + 1] = 0;
    }
}

static tc_vec3 rd_vec3(reader *r) {
    tc_vec3 v;
    v.x = rd_f32(r), v.y = rd_f32(r), v.z = rd_f32(r);
    return v;
}

static void pad4(reader *r) {
    if (r->offset & 3) r->offset += 4 - (r->offset & 3);
}

/* CRSE: u16 n + bytes, three times (the level, Ed's place, the camera's place), padded to 4; f32 end[3];
 *       the field: f32 x0, y0, cell, z_lo, z_hi, u16 nx, ny, (nx ny + 7) / 8 bytes of bits, padded to 4;
 *       the boost: u32 n, f32 radius, speed, accel, n x f32[3];
 *       the walls: u32 n, n x f32[9]. */
tcx_course *tcx_course_read(const void *ai_data, size_t ai_size) {
    chunks cs;
    reader rr, *r = &rr;
    tcx_course *c;
    uint32_t i, n;
    size_t bytes;
    if (!ai_data || chunks_open(&cs, (const uint8_t *)ai_data, ai_size, "TTAI") != TC_OK || !chunk_need(&cs, "CRSE", r))
        return NULL;
    if (!(c = (tcx_course *)tc_calloc(1, sizeof(*c)))) return NULL;
    read_name(r, c->level);
    read_name(r, c->place);
    read_name(r, c->camera_place);
    pad4(r);
    c->end = rd_vec3(r);
    c->x0 = rd_f32(r), c->y0 = rd_f32(r), c->cell = rd_f32(r), c->z_lo = rd_f32(r), c->z_hi = rd_f32(r);
    c->nx = rd_u16(r), c->ny = rd_u16(r);
    bytes = ((size_t)c->nx * c->ny + 7) / 8;
    c->mask = (uint8_t *)rd_array(r, bytes, 1, 1);
    for (i = 0; c->mask && i < bytes; i++) c->mask[i] = rd_u8(r);
    pad4(r);
    n = rd_u32(r);
    c->boost_radius = rd_f32(r), c->boost_speed = rd_f32(r), c->boost_accel = rd_f32(r);
    c->boost = (tc_vec3 *)rd_array(r, n, sizeof(tc_vec3), 12);
    if (c->boost) c->boost_count = n;
    for (i = 0; i < c->boost_count; i++) c->boost[i] = rd_vec3(r);
    n = rd_u32(r);
    c->walls = (tc_vec3 *)rd_array(r, 3 * (size_t)n, sizeof(tc_vec3), 12);
    if (c->walls) c->wall_count = n;
    for (i = 0; i < 3 * c->wall_count; i++) c->walls[i] = rd_vec3(r);
    if (chunk_end(r) != TC_OK || !c->level[0] || !(c->cell > 0) || !c->mask) {
        tcx_course_free(c);
        return NULL;
    }
    return c;
}

void tcx_course_free(tcx_course *c) {
    if (!c) return;
    tc_free(c->mask);
    tc_free(c->boost);
    tc_free(c->walls);
    tc_free(c);
}

bool tcx_course_in_finish(const tcx_course *c, tc_vec3 p) {
    float fi, fj;
    uint32_t k;
    if (!c || !c->mask || p.z < c->z_lo || p.z > c->z_hi) return false;
    fi = floorf((p.x - c->x0) / c->cell + 0.5f);
    fj = floorf((p.y - c->y0) / c->cell + 0.5f);
    if (!(fi >= 0 && fj >= 0 && fi < (float)c->nx && fj < (float)c->ny)) return false;
    k = (uint32_t)fj * c->nx + (uint32_t)fi;
    return (c->mask[k >> 3] >> (k & 7)) & 1u;
}

void tcx_course_boost(const tcx_course *c, tc_mec_ski *ski, tc_vec3 position, tc_vec3 velocity, float mass) {
    uint32_t i;
    float best = -1, along, k;
    tc_vec3 dir = tc_v3(0, 0, 0);
    if (!c || c->boost_count < 2 || !(c->boost_speed > 0)) return;
    for (i = 0; i + 1 < c->boost_count; i++) { /* the nearest leg within the radius */
        tc_vec3 a = c->boost[i], d = tc_v3_sub(c->boost[i + 1], a);
        float len2 = tc_v3_dot(d, d), t, dist;
        if (!(len2 > 1e-6f)) continue;
        t = fminf(1, fmaxf(0, tc_v3_dot(tc_v3_sub(position, a), d) / len2));
        dist = tc_v3_distance(position, tc_v3_add(a, tc_v3_scale(d, t)));
        if (dist <= c->boost_radius && (best < 0 || dist < best)) {
            best = dist;
            dir = tc_v3_div(d, sqrtf(len2));
        }
    }
    if (best < 0) return;
    along = tc_v3_dot(velocity, dir);
    if (along >= c->boost_speed) return;
    k = fminf(1, (c->boost_speed - along) / (0.2f * c->boost_speed)); /* easing off over the last fifth */
    ski->force = tc_v3_add(ski->force, tc_v3_scale(dir, mass * c->boost_accel * k));
}
