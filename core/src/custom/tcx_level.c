/* Custom gameplay's level data (tcx.h): the custom layer's chunks of a .ttlv. */
#include "../tc_internal.h"
#include <string.h>
#include "../tc_reader.h"
#include "tonic/custom/tcx.h"

/* .ttlv VEHI (tcx_vehicle.h; tools/ttre/vehicle.py). A record whose perso or wheel channels are not in PRSO is dropped. */
static void read_vehicles(tcx_level *x, const tc_level *level, reader *r) {
    uint32_t n = rd_u16(r), i, k, kept = 0;
    (void)rd_u16(r);
    x->vehicles = (tcx_vehicle_data *)rd_array(r, n, sizeof(tcx_vehicle_data), 32);
    if (!x->vehicles) return;
    for (i = 0; i < n && !r->error; i++) {
        tcx_vehicle_data *d = &x->vehicles[kept];
        uint16_t wheels, len;
        bool ok;
        memset(d, 0, sizeof(*d));
        d->prso = rd_u16(r);
        d->collision_id = rd_u16(r);
        d->box_lo.x = rd_f32(r), d->box_lo.y = rd_f32(r), d->box_lo.z = rd_f32(r);
        d->box_hi.x = rd_f32(r), d->box_hi.y = rd_f32(r), d->box_hi.z = rd_f32(r);
        wheels = rd_u16(r);
        (void)rd_u16(r);
        ok = d->prso < level->perso_count;
        for (k = 0; k < wheels && !r->error; k++) {
            tcx_vehicle_wheel w;
            w.channel = rd_u16(r);
            w.steers = rd_u8(r) != 0;
            (void)rd_u8(r);
            w.centre.x = rd_f32(r), w.centre.y = rd_f32(r), w.centre.z = rd_f32(r);
            w.radius = rd_f32(r);
            if (ok && w.channel >= level->persos[d->prso].channel_count) ok = false;
            if (d->wheel_count < TCX_VEHICLE_MAX_WHEELS) d->wheels[d->wheel_count++] = w;
        }
        d->seat.x = rd_f32(r), d->seat.y = rd_f32(r), d->seat.z = rd_f32(r);
        d->rider_scale = rd_f32(r);
        if (!(d->rider_scale > 0.01f && d->rider_scale < 10)) d->rider_scale = 1;
        len = rd_u16(r);
        for (k = 0; k < len && !r->error; k++) {
            uint8_t ch = rd_u8(r);
            if (k + 1 < sizeof(d->name)) d->name[k] = (char)ch;
        }
        if ((r->offset & 3) && len) r->offset += 4 - (r->offset & 3); /* records are padded to 4 */
        if (ok) kept++;
    }
    x->vehicle_count = kept;
}

tc_result tcx_level_read(tc_level *level, const char tag[4], const uint8_t *data, size_t size) {
    reader rr = {data, size, 0, TC_OK}, *r = &rr;
    tcx_level *x = level->custom;
    bool ski = memcmp(tag, "SKIA", 4) == 0;
    if (!ski && memcmp(tag, "VEHI", 4) != 0) return TC_OK;
    if (!x && !(x = level->custom = (tcx_level *)tc_calloc(1, sizeof(*x)))) return TC_ERR_OUT_OF_MEMORY;
    if (ski) { /* .ttlv SKIA (tools/ttre/ssx.py): f32 cruise speed, f32 push[, f32 air gravity scale (since 2026-11-14)] */
        x->ski_cruise = rd_f32(r);
        x->ski_push = rd_f32(r);
        x->ski_air_gravity = r->size >= 12 ? rd_f32(r) : 1;
        if (!(x->ski_cruise > 0 && x->ski_cruise < 1000 && x->ski_push > 0 && x->ski_push < 1000))
            x->ski_cruise = x->ski_push = 0;
        if (!(x->ski_air_gravity > 0 && x->ski_air_gravity <= 1)) x->ski_air_gravity = 1;
        return r->error || r->offset != r->size ? TC_ERR_STALE : TC_OK;
    }
    tc_free(x->vehicles);
    x->vehicles = NULL;
    x->vehicle_count = 0;
    read_vehicles(x, level, r);
    if (r->error == TC_ERR_OUT_OF_MEMORY) return r->error;
    return r->error || r->offset != r->size ? TC_ERR_STALE : TC_OK;
}

void tcx_level_free(tcx_level *x) {
    if (!x) return;
    tc_free(x->vehicles);
    tc_free(x);
}

bool tcx_mesh_model(const tc_mesh *mesh, tc_vec3 eye, tc_mat4 *model) {
    if (!(mesh->surface.flags & TCX_FLAG_ROUND_CAMERA)) return false;
    *model = tc_mat4_identity();
    model->m[12] = eye.x, model->m[13] = eye.y, model->m[14] = eye.z;
    return true;
}

void tcx_ski_tick(const tcx_level *l, tc_mec_ski *ski, tc_vec3 velocity, uint32_t contacts, const tc_ai_card *card) {
    tc_vec3 front;
    float along, k, mass = card ? tc_ai_card_f32(card, 4) : 0;
    if (!l || !(l->ski_cruise > 0) || !(mass > 0)) return;
    if (!(contacts & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE | TC_AI_CONTACT_WALL))) {
        /* off the snow: lift part of his weight (the card's: g x +0x34, x +0x40 when the probe finds no ground near;
         * every level's air has g 9.81); nothing to push against */
        float weight = mass * 9.81f * tc_ai_card_f32(card, 0x34) * (ski->near_ground ? 1 : tc_ai_card_f32(card, 0x40));
        if (l->ski_air_gravity < 1 && weight > 0)
            ski->force = tc_v3_add(ski->force, tc_v3(0, 0, weight * (1 - l->ski_air_gravity)));
        return;
    }
    if (!(contacts & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE))) return; /* against a wall only: no push either */
    front = tc_v3_scale(ski->y, -1);
    along = tc_v3_dot(velocity, front);
    if (along >= l->ski_cruise) return;
    k = fminf(1, (l->ski_cruise - along) / (0.5f * l->ski_cruise));
    ski->force = tc_v3_add(ski->force, tc_v3_scale(front, mass * l->ski_push * k));
}
