/* The main character (tc_main.h): Ed, his physics, his scripts and the game's tick around him. Moved out of the viewer
 * (tc_viewer_play.c) on 2026-11-13 as it was: the same steps in the same order. */
#include "tc_internal.h"
#include <stdio.h>
#include <string.h>
#include "tonic/tc_main.h"
#include "tonic/custom/tcx.h"
#include "tc_prof_hooks.h"

/* Ed's animation by .ttlv name with at least one sample, or -1. */
static int ed_anim(const tc_level *l, const char *name) {
    int i = tc_ed_find_anim(&l->ed, name);
    return i >= 0 && l->ed.anims[i].sample_count > 0 ? i : -1;
}

void tc_main_init(tc_main *m, const tc_level *l, tc_collision *world, const void *ai_data, size_t ai_size,
                  const char *level_name, tc_result *ai_result) {
    tc_mat34 start = l->ed.start;
    uint32_t i;
    int s;
    memset(m, 0, sizeof(*m));
    m->level = l;
    m->sectors = l->sectors.count ? &l->sectors : NULL;
    tc_player_init(&m->player, world, tc_v3(start.m[3], start.m[7], start.m[11]), tc_heading_of(&start));
    tc_player_apply_floors(&m->player);
    /* Lengths of the one-shot clips the simple controller plays (jump, land). */
    for (i = 0; i < l->ed.anim_count; i++) {
        const tc_ed_anim *a = &l->ed.anims[i];
        if (a->sample_count == 0 || !a->name || a->loop != 0) continue;
        for (s = 0; s < TC_ANIM_COUNT; s++)
            if (strcmp(tc_anim_state_name((tc_anim_state)s), a->name) == 0)
                m->player.clip_length[s] = (float)a->sample_count / fmaxf(a->sample_rate, 1e-3f);
    }
    if (level_name) {
        strncpy(m->level_name, level_name, sizeof(m->level_name) - 1);
        m->level_name[sizeof(m->level_name) - 1] = 0;
    }
    if (ai_data) {
        *ai_result = tc_ai_model_load(&m->model, (const uint8_t *)ai_data, ai_size);
        m->has_model = *ai_result == TC_OK;
        if (m->has_model) tc_ai_model_link(&m->model, l);
    }
    if (m->has_model && m->model.alt_data && m->model.mode_zone_count) {
        m->has_alt = tc_ai_model_load(&m->alt, m->model.alt_data, m->model.alt_size) == TC_OK;
        if (m->has_alt) tc_ai_model_link(&m->alt, l);
    }
    if (m->has_model) {
        tc_result r = tc_ed_ai_init(&m->driver, &m->model, &m->player, m->level_name);
        m->has_driver = r == TC_OK;
        if (r != TC_OK) *ai_result = r;
        if (m->has_driver) tc_ed_ai_set_sectors(&m->driver, m->sectors);
        if (m->has_driver && m->has_alt) m->driver.dyn.walk.firm_floors = true;
    }
}

void tc_main_free(tc_main *m) {
    if (m->has_driver) tc_ed_ai_free(&m->driver);
    if (m->has_model) tc_ai_model_free(&m->model);
    if (m->has_alt) tc_ai_model_free(&m->alt);
    m->has_driver = m->has_model = m->has_alt = false;
}

int tc_main_mode_zones(tc_main *m, tc_cast *cast) {
    uint32_t i;
    /* The SE's walk / ski switch. With a cast (SE Totalski's pickups and World since 2026-11-05) the cast is handed
     * the new driver: it holds links into the old one's AI (tc_cast_detach_main / tc_cast_set_main). */
    if (!m->has_alt || !m->has_driver) return 0;
    for (i = 0; i < m->model.mode_zone_count; i++) {
        const tc_ai_mode_zone *z = &m->model.mode_zones[i];
        tc_ai_model *next = z->mode == TC_AI_MODE_SKI ? &m->alt : &m->model;
        if (z->mode == m->mode || (i < 64 && z->once && m->zone_fired[i])) continue;
        if (z->require && z->require != m->mode + 1) continue;
        if (!tc_ai_zone_contains(z->inverse, z->prim, z->prim_count, m->player.position)) continue;
        if (z->teleport == 1) {
            m->player.position = z->vector;
            m->player.velocity = tc_v3(0, 0, 0);
            m->player.placed++;
        } else if (z->teleport == 2) {
            m->player.position = tc_v3_add(m->player.position, z->vector);
            m->player.placed++;
        }
        if (cast) tc_cast_detach_main(cast);
        tc_ed_ai_free(&m->driver);
        m->has_driver = tc_ed_ai_init(&m->driver, next, &m->player, m->level_name) == TC_OK;
        if (!m->has_driver) return -1;
        tc_ed_ai_set_sectors(&m->driver, m->sectors);
        if (cast) tc_cast_set_main(cast, &m->driver.ai);
        m->mode = z->mode;
        m->driver.dyn.walk.firm_floors = m->mode == TC_AI_MODE_WALK;
        if (i < 64) m->zone_fired[i] = 1;
        return 1;
    }
    return 0;
}

void tc_main_capture(tc_main *m) {
    char name[16];
    int i;
    snprintf(name, sizeof(name), "s%u", (unsigned)tc_ed_ai_anim(&m->driver));
    i = ed_anim(m->level, name);
    m->pose_cur = tc_ed_ai_model_matrix(&m->driver);
    m->clip_cur = i >= 0 ? i : ed_anim(m->level, "idle");
    m->phase_cur = tc_ed_ai_frame(&m->driver);
    m->loop_cur = tc_ed_ai_loops(&m->driver);
    m->rate_cur = (float)m->driver.ai.frame_rate;
    if (!m->have_pose) {
        m->pose_prev = m->pose_cur, m->clip_prev = m->clip_cur, m->phase_prev = m->phase_cur;
        m->have_pose = true;
    }
}

/* Ed's own tick, between the cast's passes (tc_cast_tick) or alone. */
typedef struct ed_tick {
    tc_main *m;
    tc_cast *cast;
    const tc_main_input *in;
    uint16_t *sounds;
    uint32_t *count, cap;
} ed_tick;

static void ed_step(void *self, float dt) {
    ed_tick *x = (ed_tick *)self;
    tc_main *m = x->m;
    const tc_main_input *in = x->in;
    uint32_t n;
    double prof;
    /* port: in first person Ed faces where the player looks (the original's stick mode turns him with the stick, B29),
     * so what he shoots goes where the gunsight is */
    if (in->face) m->player.heading = in->heading;
    if (m->driver.dyn.card_type == 2 && m->level->custom) { /* custom gameplay: the skier's cruise push (tcx.h) */
        const tc_ai_state *ski_state = &m->driver.ai.model->states[m->driver.ai.state];
        if (ski_state->card != TC_AI_NONE16)
            tcx_ski_tick(m->level->custom, &m->driver.dyn.ski, m->player.velocity, m->driver.dyn.contacts,
                         tc_ai_card_f32(&m->driver.ai.model->cards[ski_state->card], 4));
    }
    /* the start text shows: the player presses Jump. Not where the level itself asks for the automatic start
     * (.ttai AUTO: the Special Edition's Totalski, imported courses), which have no such text */
    m->driver.wait_start = in->hud_waits && !m->driver.ai.model->auto_start;
    /* (Waiting there the skier sits still by his own state: 119, a card of type 6, set by the start's B4.) */
    prof = TC_PROF_BEGIN();
    tc_ed_ai_step(&m->driver, dt, in->inputs, in->stick_x, in->stick_y, in->eye);
    TC_PROF_END(TC_PZ_ED, prof);
    if (m->driver.events.restarted) {
        m->driver.events.restarted = false;
        if (x->cast) tc_cast_respawn(x->cast);
        memset(m->zone_fired, 0, sizeof(m->zone_fired)); /* the SE reloads the level: its triggers fire again */
    }
    for (n = 0; n < m->driver.ai.sound_count && *x->count < x->cap; n++) x->sounds[(*x->count)++] = m->driver.ai.sounds[n];
}

static uint16_t ed_sector(const void *self) { return ((const ed_tick *)self)->m->driver.sector.sector; }

static const tc_cast_main_ops ed_ops = {ed_step, ed_sector};

/* His mechanics: the cast's, as any actor's (tc_cast_main_mechanics). */
static void cast_mechanics(void *user, const tc_mec_perso *x, float dt) { tc_cast_main_mechanics((tc_cast *)user, x, dt); }

/* Ed riding the character in the slot: his scripts and mechanics sleep, but the cast's scripts still see him as the main
 * actor where it carries him, and the game's clock (his: the cast runs on it) goes on as his own step would move it. */
static void ride_step(void *self, float dt) {
    tc_main *m = ((ed_tick *)self)->m;
    m->driver.ai.position = m->player.position;
    m->driver.ai.velocity = m->player.velocity;
    m->driver.ai.heading = m->player.heading;
    m->driver.ai.time_ms += (uint32_t)(dt * 1000 + 0.5f); /* as tc_ai_step */
}

static const tc_cast_main_ops ride_ops = {ride_step, ed_sector};

/* A tick while another character holds the slot: the cast's passes round Ed riding it. */
static bool ride_tick(tc_main *m, tc_cast *cast, uint16_t *sounds, uint32_t *count, uint32_t cap) {
    ed_tick x;
    uint32_t n;
    memset(&x, 0, sizeof(x));
    x.m = m;
    if (!cast) {
        ride_step(&x, TC_ED_AI_TICK);
        return true;
    }
    cast->main_carrier = -1; /* he rides it, not a perso of theirs */
    cast->main_mass = 0;
    cast->talk_press = false; /* his Action is its own (getting out) */
    tc_cast_tick(cast, TC_ED_AI_TICK, m->sectors, &ride_ops, &x);
    for (n = 0; n < cast->sounds_out_count && *count < cap; n++) sounds[(*count)++] = cast->sounds_out[n];
    if (cast->talk_cover) {
        cast->talk_cover = false;
        m->driver.events.cover = true;
        m->driver.screen_covered = false;
    }
    if (cast->talk_level) {
        m->driver.events.level = cast->talk_level;
        cast->talk_level = NULL;
    }
    return true;
}

bool tc_main_tick(tc_main *m, tc_cast *cast, const tc_main_input *in, uint16_t *sounds, uint32_t *count, uint32_t cap,
                  int *mode_switch) {
    ed_tick x;
    uint32_t n;
    *mode_switch = 0;
    if (!tc_main_is_ed(m)) return m->has_driver && ride_tick(m, cast, sounds, count, cap);
    m->pose_prev = m->pose_cur, m->clip_prev = m->clip_cur, m->phase_prev = m->phase_cur;
    *mode_switch = tc_main_mode_zones(m, cast);
    if (!m->has_driver) return false;
    x.m = m, x.cast = cast, x.in = in, x.sounds = sounds, x.count = count, x.cap = cap;
    if (cast) {
        /* what the run-first persos need of him: what he rides and what he weighs on it */
        const tc_ai_state *st = &m->driver.ai.model->states[m->driver.ai.state];
        cast->main_carrier = m->driver.dyn.walk.carrier;
        cast->talk_press = in->talk_press;
        cast->main_mass = st->card != TC_AI_NONE16 ? tc_ai_card_f32(&m->driver.ai.model->cards[st->card], 4) : 0;
        /* he is the cast's actor: his record, and his mechanics through the cast's (again after a walk / ski switch) */
        tc_cast_attach_main(cast, &m->driver.ai, &m->driver.dyn, &m->player);
        m->driver.mechanics = cast_mechanics;
        m->driver.mechanics_user = cast;
        /* the exe's frame (docs/ENGINE.md): the run-first persos (platforms, lifts) move before the rest, so Ed rides
         * and collides with them where they are this tick; then Ed, then the other persos */
        tc_cast_tick(cast, TC_ED_AI_TICK, m->sectors, &ed_ops, &x);
        for (n = 0; n < cast->sounds_out_count && *count < cap; n++) sounds[(*count)++] = cast->sounds_out[n];
        if (cast->talk_cover) { /* a speaker's way out (docs/ENGINE.md "Walkable scenes") */
            cast->talk_cover = false;
            m->driver.events.cover = true;
            m->driver.screen_covered = false;
        }
        if (cast->talk_level) {
            m->driver.events.level = cast->talk_level;
            cast->talk_level = NULL;
        }
    } else
        ed_step(&x, TC_ED_AI_TICK);
    tc_main_capture(m);
    return true;
}

/* ---- the slot ---- */

void tc_main_take(tc_main *m, const tc_character_ops *ops, void *self) {
    m->ops = ops;
    m->self = self;
}

bool tc_main_frame(tc_main *m, const tc_character_input *in, float dt) {
    if (!m->ops) return false;
    if (m->ops->frame(m->self, in, dt, &m->rider)) return true;
    m->ops = NULL, m->self = NULL; /* Ed on his feet: main again */
    return false;
}

void tc_main_abort(tc_main *m) {
    if (!m->ops) return;
    m->ops->abort(m->self, &m->player);
    m->ops = NULL, m->self = NULL;
}

void tc_main_camera_target(const tc_main *m, struct tc_cam_target *t) {
    if (m->ops) m->ops->camera_target(m->self, t);
}

bool tc_main_status(const tc_main *m, char *out, size_t size) { return m->ops && m->ops->status(m->self, out, size); }
