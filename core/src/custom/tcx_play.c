/* Custom gameplay in a play session (tcx.h): the cars of the level and Ed's ride in one. The host (tc_viewer) calls in at
 * the seams tcx.h lists; nothing here is the game's. */
#include "../tc_internal.h"
#include <stdio.h>
#include <string.h>
#include "tonic/custom/tcx.h"
#include "tonic/tc_alloc.h"
#include "tonic/tc_anim.h"
#include "tonic/tc_obstacle.h"
#include "tonic/tc_viewer.h"

struct tcx_play {
    const tc_level *level;
    tc_collision *world;
    tc_obstacles *obstacles;
    uint32_t vehicle_count;
    tcx_vehicle *vehicles;
    uint32_t *vehicle_mesh;      /* per car: the first of its parts' tc_viewer_mesh numbers and their count */
    tcx_ride ride;
    bool action_held;            /* the Action that got him in or out: kept from his scripts until it is let go */
};

#define RIDE_ACTION (1u << TC_VA_ACTION)

static bool down(uint32_t held, tc_view_action a) { return (held >> a) & 1u; }

static float axis(uint32_t held, tc_view_action pos, tc_view_action neg) {
    return (down(held, pos) ? 1.0f : 0.0f) - (down(held, neg) ? 1.0f : 0.0f);
}

/* Ed's animation by .ttlv name with at least one sample, or -1. */
static int ed_anim(const tc_level *l, const char *name) {
    int i = tc_ed_find_anim(&l->ed, name);
    return i >= 0 && l->ed.anims[i].sample_count > 0 ? i : -1;
}

static bool riding(const tcx_play *x) { return x && x->ride.phase != TCX_RIDE_NONE && x->ride.vehicle >= 0; }

static const tcx_vehicle *ride_car(const tcx_play *x) { return riding(x) ? &x->vehicles[x->ride.vehicle] : NULL; }

tcx_play *tcx_play_create(const tc_level *level, tc_collision *world, struct tc_obstacles *obstacles, uint32_t perso_first) {
    const tcx_level *data = level->custom;
    tcx_play *x;
    uint32_t i, k;
    if (!data || !data->vehicle_count) return NULL;
    if (!(x = (tcx_play *)tc_calloc(1, sizeof(*x)))) return NULL;
    x->level = level;
    x->world = world;
    x->obstacles = obstacles;
    x->ride.vehicle = -1;
    x->vehicles = (tcx_vehicle *)tc_calloc(data->vehicle_count, sizeof(*x->vehicles));
    x->vehicle_mesh = (uint32_t *)tc_calloc(2 * data->vehicle_count, sizeof(*x->vehicle_mesh));
    if (!x->vehicles || !x->vehicle_mesh) { tcx_play_free(x); return NULL; }
    for (i = 0; i < data->vehicle_count; i++) {
        const tcx_vehicle_data *d = &data->vehicles[i];
        uint32_t first = perso_first;
        tcx_vehicle_init(&x->vehicles[i], d, &level->persos[d->prso].world, world);
        for (k = 0; k < d->prso; k++) first += level->persos[k].part_count;
        x->vehicle_mesh[2 * i] = first;
        x->vehicle_mesh[2 * i + 1] = level->persos[d->prso].part_count;
    }
    x->vehicle_count = data->vehicle_count;
    return x;
}

void tcx_play_free(tcx_play *x) {
    if (!x) return;
    tc_free(x->vehicles);
    tc_free(x->vehicle_mesh);
    tc_free(x);
}

/* The move keys and the left stick as the car's: forward / back the throttle (back brakes, then reverses), left / right
 * the steering, Jump (Space) the handbrake, Fast (Shift) a hop (swapped 2026-11-12, user). */
static tcx_vehicle_input drive_input(uint32_t held, float move_x, float move_y) {
    tcx_vehicle_input in;
    in.throttle = fmaxf(-1, fminf(1, axis(held, TC_VA_FORWARD, TC_VA_BACK) + move_y));
    in.steer = fmaxf(-1, fminf(1, axis(held, TC_VA_RIGHT, TC_VA_LEFT) + move_x));
    in.handbrake = down(held, TC_VA_JUMP);
    in.hop = down(held, TC_VA_FAST);
    return in;
}

/* Ed back on his feet at the ride's landing place (the host starts his own control over from there). */
static void land(tcx_play *x, tc_player *player) {
    tc_player_teleport(player, x->ride.to, x->ride.heading);
    x->ride.phase = TCX_RIDE_NONE;
    x->ride.vehicle = -1;
}

static void car_abort(void *self, tc_player *player) {
    tcx_play *x = (tcx_play *)self;
    const tcx_vehicle *c = ride_car(x);
    if (!c) return;
    if (x->ride.phase != TCX_RIDE_LEAVING && !tcx_ride_leave(&x->ride, c, x->world)) { /* nowhere free: on its roof */
        const tcx_vehicle_data *d = c->data;
        x->ride.to = tcx_vehicle_point(c, tc_v3(0.5f * (d->box_lo.x + d->box_hi.x), 0.5f * (d->box_lo.y + d->box_hi.y),
                                                d->box_hi.z + 0.05f));
        x->ride.heading = tcx_vehicle_drawn_heading(c);
    }
    land(x, player);
}

bool tcx_holds_action(const tcx_play *x) { return x && x->action_held; }

/* The cars move (the one Ed drives by the player's keys, the others coast), their collision posed where they are; then
 * Action within reach of one gets Ed in (the car takes the main character's slot: car_frame from now). */
bool tcx_update(tcx_play *x, const tcx_host *h, float dt) {
    tcx_ride *r;
    float step;
    bool press;
    uint32_t k;
    if (!x) return false;
    r = &x->ride;
    step = h->paused ? 0 : dt;
    if (!(h->held & RIDE_ACTION)) x->action_held = false;
    press = (h->held & RIDE_ACTION) && !x->action_held && !h->held_back && !h->paused;
    for (k = 0; k < x->vehicle_count; k++) {
        tcx_vehicle *c = &x->vehicles[k];
        tcx_vehicle_input in = drive_input(h->held, h->move_x, h->move_y);
        bool driven = r->phase == TCX_RIDE_DRIVING && (int32_t)k == r->vehicle && !h->held_back;
        tc_obstacle *ob;
        tc_mat34 w;
        tcx_vehicle_update(c, x->world, driven ? &in : NULL, step);
        w = tcx_vehicle_world(c);
        if (c->data->collision_id != 0xFFFF && (ob = tc_obstacles_find(x->obstacles, c->data->collision_id)) != NULL)
            tc_obstacle_pose_static(ob, &w);
    }
    if (r->phase == TCX_RIDE_NONE && press && h->may_start) {
        for (k = 0; k < x->vehicle_count; k++)
            if (tcx_ride_in_reach(&x->vehicles[k], h->player->position)) {
                tcx_ride_board(r, (int32_t)k, &x->vehicles[k], h->player->position, x->world);
                x->action_held = true;
                return true;
            }
    }
    return false;
}

/* The car as the main character (tc_main.h), from the frame Ed got in: Action hops him out (from the seat), the ride's
 * hop runs, and he is drawn hopping (his jump clip, stretched over it, his size going to the seat's or back) or in the
 * seat. Gives the slot back when he lands. */
static bool car_frame(void *self, const tc_character_input *h, float dt, tc_character_pose *ed) {
    tcx_play *x = (tcx_play *)self;
    tcx_ride *r = &x->ride;
    const tc_level *l = x->level;
    float step = h->paused ? 0 : dt;
    bool press = (h->held & RIDE_ACTION) && !x->action_held && !h->held_back && !h->paused;
    if (r->phase == TCX_RIDE_DRIVING && press) {
        if (tcx_ride_leave(r, ride_car(x), x->world)) x->action_held = true;
    }
    if (!riding(x)) return false;
    if (tcx_ride_step(r, step) && r->phase == TCX_RIDE_NONE) {
        land(x, h->ed);
        return false; /* his own control from this frame on */
    }
    {
        const tcx_vehicle *c = ride_car(x);
        tc_vec3 feet;
        float heading, scale, done;
        int jump = ed_anim(l, "jump"), idle = ed_anim(l, "idle");
        tcx_ride_pose(r, c, &feet, &heading, &scale, &done);
        /* the player stays with him (his sector follows the car through the portals; the recording reads it) */
        h->ed->position = r->phase == TCX_RIDE_DRIVING ? tcx_vehicle_drawn_position(c) : feet;
        h->ed->heading = heading;
        h->ed->velocity = r->phase == TCX_RIDE_DRIVING ? c->velocity : tc_v3(0, 0, 0);
        if (r->phase == TCX_RIDE_DRIVING) { /* in the seat, shrunk, riding the body; his idle */
            tc_mat34 m = tcx_ride_seat_matrix(c);
            ed->model = tc_mat4_from34(&m);
            ed->clip = idle;
            ed->loop = true;
            ed->phase = h->ed_clip == idle ? h->ed_phase : 0;
            if (idle >= 0) {
                const tc_ed_anim *a = &l->ed.anims[idle];
                ed->phase = tc_phase_advance(ed->phase, step * (a->sample_rate / 4) * h->animation_speed, a->sample_count, true);
            }
        } else { /* the hop: his take-off, stretched over it, his size going to the seat's or back */
            tc_player at = *h->ed;
            int i;
            at.position = feet;
            at.heading = heading;
            ed->model = tc_player_model_matrix(&at);
            for (i = 0; i < 12; i++) if (i % 4 != 3) ed->model.m[i] *= scale;
            ed->clip = jump >= 0 ? jump : idle;
            ed->loop = false;
            ed->phase = ed->clip >= 0 ? done * (float)(l->ed.anims[ed->clip].sample_count - 1) : 0;
        }
    }
    return true;
}

static void car_camera_target(const void *self, struct tc_cam_target *t) {
    const tcx_play *x = (const tcx_play *)self;
    const tcx_vehicle *c = ride_car(x);
    float heading, scale, done;
    if (!c) return;
    /* the hop, then the car: the ski follow's preset (pulled back with speed, a slow rope) and the chase behind it */
    tcx_ride_pose(&x->ride, c, &t->position, &heading, &scale, &done);
    t->behind = TC_PI - heading;
    t->speed = 0;
    t->velocity = tc_v3(0, 0, 0);
    t->situation = 0;
    if (x->ride.phase == TCX_RIDE_DRIVING) {
        t->position = tcx_vehicle_drawn_position(c); /* the car's ground */
        t->velocity = c->velocity;
        t->speed = sqrtf(c->velocity.x * c->velocity.x + c->velocity.y * c->velocity.y);
        t->shot = TC_CAM_SHOT_SKI;
        t->chase = TCX_VEHICLE_CHASE_TIME;
    }
}

void tcx_camera_obstacles(tcx_play *x, bool on) {
    uint32_t k;
    for (k = 0; x && k < x->vehicle_count; k++) {
        tc_obstacle *ob = x->vehicles[k].data->collision_id != 0xFFFF
                              ? tc_obstacles_find(x->obstacles, x->vehicles[k].data->collision_id) : NULL;
        if (ob) ob->live = on;
    }
}

static const tcx_vehicle *vehicle_of(const tcx_play *x, uint32_t perso) {
    uint32_t k;
    for (k = 0; x && k < x->vehicle_count; k++)
        if (x->vehicles[k].data->prso == perso) return &x->vehicles[k];
    return NULL;
}

bool tcx_perso_pose(const tcx_play *x, uint32_t perso, tc_mat34 *world) {
    const tcx_vehicle *c = vehicle_of(x, perso);
    if (!c) return false;
    *world = tcx_vehicle_world(c);
    return true;
}

tc_mat34 tcx_perso_channel(const tcx_play *x, uint32_t perso, uint16_t channel) {
    const tcx_vehicle *c = vehicle_of(x, perso);
    tc_mat34 m = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    return c ? tcx_vehicle_channel(c, channel) : m;
}

/* A car's parts: the camera follows from inside one, and its ~100k triangles per ray would be the frame's biggest cost;
 * its PCOL box still stops Ed. */
bool tcx_sight_skips(const tcx_play *x, uint32_t mesh) {
    uint32_t k;
    for (k = 0; x && k < x->vehicle_count; k++)
        if (mesh >= x->vehicle_mesh[2 * k] && mesh < x->vehicle_mesh[2 * k] + x->vehicle_mesh[2 * k + 1]) return true;
    return false;
}

static bool car_status(const void *self, char *out, size_t size) {
    const tcx_play *x = (const tcx_play *)self;
    if (!riding(x) || x->ride.phase != TCX_RIDE_DRIVING) return false;
    snprintf(out, size, ", driving %.0f km/h", (double)(fabsf(tcx_vehicle_speed(ride_car(x))) * 3.6f)); /* units ~ metres */
    return true;
}

uint32_t tcx_play_vehicle_count(const tcx_play *x) { return x ? x->vehicle_count : 0; }

const tcx_vehicle *tcx_play_vehicle(const tcx_play *x, uint32_t i) { return x && i < x->vehicle_count ? &x->vehicles[i] : NULL; }

const tcx_ride *tcx_play_ride(const tcx_play *x) { return x ? &x->ride : NULL; }

static const tc_character_ops car_ops = {car_frame, car_camera_target, car_status, car_abort};

const tc_character_ops *tcx_character(void) { return &car_ops; }
