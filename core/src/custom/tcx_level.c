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
    if (memcmp(tag, "VEHI", 4) != 0) return TC_OK;
    if (!x && !(x = level->custom = (tcx_level *)tc_calloc(1, sizeof(*x)))) return TC_ERR_OUT_OF_MEMORY;
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
