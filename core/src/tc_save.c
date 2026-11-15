/* Saved games: tc_save.h, docs/ENGINE.md "Saved games". */
#include "tonic/tc_save.h"
#include "tonic/tc_cast.h"
#include "tc_reader.h"
#include "tc_viewer_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ---- writing: the chunk container (tc_reader.h), "TTSV" ---- */

typedef struct out { uint8_t *data; size_t size, room; bool failed; } out;

static void put(out *o, const void *p, size_t n) {
    if (o->failed) return;
    if (o->size + n > o->room) {
        size_t room = o->room ? o->room * 2 : 4096;
        uint8_t *more;
        while (room < o->size + n) room *= 2;
        more = (uint8_t *)tc_alloc(room);
        if (!more) { o->failed = true; return; }
        if (o->data) memcpy(more, o->data, o->size);
        tc_free(o->data);
        o->data = more;
        o->room = room;
    }
    memcpy(o->data + o->size, p, n);
    o->size += n;
}
static void put_u8(out *o, uint8_t v) { put(o, &v, 1); }
static void put_u16(out *o, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; put(o, b, 2); }
static void put_u32(out *o, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(o, b, 4);
}
static void put_f32(out *o, float f) { uint32_t u; memcpy(&u, &f, 4); put_u32(o, u); }
static void put_name(out *o, const char *s) {
    size_t n = strlen(s);
    if (n > 255) n = 255;
    put_u8(o, (uint8_t)n);
    put(o, s, n);
}
/* A chunk: its header now, its size when it ends. */
static size_t chunk_begin(out *o, const char tag[4]) {
    put(o, tag, 4);
    put_u32(o, 0);
    return o->size;
}
static void chunk_finish(out *o, size_t start) {
    uint32_t n = (uint32_t)(o->size - start);
    static const uint8_t zero[4] = {0};
    if (o->failed) return;
    o->data[start - 4] = (uint8_t)n, o->data[start - 3] = (uint8_t)(n >> 8);
    o->data[start - 2] = (uint8_t)(n >> 16), o->data[start - 1] = (uint8_t)(n >> 24);
    put(o, zero, (4 - n % 4) % 4);
}

static void split_level(const char *level, uint8_t edition, char *name, size_t size, uint8_t *out_edition) {
    const char *colon = strchr(level, ':');
    *out_edition = edition;
    if (colon) { /* the custom layer's arches name their edition (tools/ttre/custom.py) */
        *out_edition = strncmp(level, "se:", 3) == 0 ? 1 : 0;
        level = colon + 1;
    }
    tcv_copy_text(name, size, level);
}

uint8_t *tc_save_make(tc_viewer *v, const char *name, uint64_t time, size_t *size) {
    tc_viewer_play *p = v->play;
    tc_save_game s;
    out o = {0};
    size_t c;
    uint32_t i, n;
    *size = 0;
    if (!p || !p->main.has_driver || v->tape || !name || !name[0]) return NULL;
    memset(&s, 0, sizeof(s));
    tcv_copy_text(s.info.name, sizeof(s.info.name), name);
    s.info.time = time;
    if ((v->phase == TC_VP_LOADING || v->phase == TC_VP_PENDING) && v->carry) {
        /* on the way to the next level: its arrival, as the portal made it (the level's saved values went to the table
         * on the way out) */
        s.carry = *v->carry;
        split_level(v->carry->level, v->level->edition, s.info.level, sizeof(s.info.level), &s.info.edition);
    } else {
        const tc_ed_ai *e = &p->main.driver;
        const tc_ai_model *m = e->ai.model;
        tcv_copy_text(s.info.level, sizeof(s.info.level), v->level_name[0] ? v->level_name : p->main.level_name);
        s.info.edition = (uint8_t)v->level->edition;
        tcv_copy_text(s.carry.place, sizeof(s.carry.place), v->arrival_place);
        tcv_copy_text(s.carry.camera_place, sizeof(s.carry.camera_place), v->arrival_camera_place);
        /* Ed's variables: the player-saved ones as they are now, the rest as a level starts them (the exe's player
         * save holds those alone: dsgvar flag 0x200) */
        if (m && e->ai.dsg && m->dsg.size <= TC_VIEWER_CARRY_DSG) {
            s.carry.dsg_size = m->dsg.size;
            if (m->dsg.initial) memcpy(s.carry.dsg, m->dsg.initial, m->dsg.size);
            for (i = 0; m->dsg.vars && i < m->dsg.count; i++) {
                const tc_ai_dsgvar *d = &m->dsg.vars[i];
                int w = tc_ai_dsg_width(d->type);
                if ((d->save & TC_AI_DSG_SAVE_PLAYER) && w > 0 && d->offset + (uint32_t)w <= m->dsg.size)
                    memcpy(s.carry.dsg + d->offset, e->ai.dsg + d->offset, (size_t)w);
            }
        }
        if (p->has_cast) {
            const tc_ai_globals *g = &p->cast.globals;
            s.carry.has_progress = 1;
            s.carry.lives = g->lives;
            s.carry.powers = g->powers;
            memcpy(s.carry.values, g->values, sizeof(s.carry.values));
            if (g->has_checkpoint) {
                s.carry.has_checkpoint = 1;
                s.carry.checkpoint[0] = g->checkpoint.x;
                s.carry.checkpoint[1] = g->checkpoint.y;
                s.carry.checkpoint[2] = g->checkpoint.z;
                s.carry.checkpoint_heading = g->checkpoint_heading;
            }
            tc_cast_level_save(&p->cast); /* this level's saved values, as they are now */
        } else if (v->carry && v->carry->has_progress) { /* a level without a cast: what it arrived with */
            s.carry.has_progress = 1;
            s.carry.lives = v->carry->lives;
            s.carry.powers = v->carry->powers;
            memcpy(s.carry.values, v->carry->values, sizeof(s.carry.values));
        }
        s.carry.hit_points = e->ai.hit_points;
        s.carry.hit_points_max = e->ai.hit_points_max;
        s.carry.vertical_fov = v->camera.vertical_fov;
        s.carry.orbit_distance = v->orbit_distance;
        s.carry.closeup = v->cam.closeup;
    }
    tcv_copy_text(s.carry.level, sizeof(s.carry.level), s.info.level);
    s.info.lives = s.carry.lives;
    s.info.powers = s.carry.powers;

    put(&o, "TTSV", 4);
    put_u32(&o, TC_CONTAINER);
    c = chunk_begin(&o, "INFO");
    put_name(&o, s.info.name);
    put_name(&o, s.info.level);
    put_u8(&o, s.info.edition);
    put_u32(&o, (uint32_t)s.info.time);
    put_u32(&o, (uint32_t)(s.info.time >> 32));
    put_u8(&o, s.info.lives);
    put_u16(&o, s.info.powers);
    chunk_finish(&o, c);

    c = chunk_begin(&o, "CARY");
    put_name(&o, s.carry.place);
    put_name(&o, s.carry.camera_place);
    put_u32(&o, s.carry.dsg_size);
    put(&o, s.carry.dsg, s.carry.dsg_size);
    put_u8(&o, s.carry.has_progress);
    put_u8(&o, s.carry.lives);
    put_u16(&o, s.carry.powers);
    for (i = 0; i < 31; i++) put_u16(&o, (uint16_t)s.carry.values[i]);
    put_u8(&o, s.carry.has_checkpoint);
    for (i = 0; i < 3; i++) put_f32(&o, s.carry.checkpoint[i]);
    put_f32(&o, s.carry.checkpoint_heading);
    put_u32(&o, (uint32_t)s.carry.hit_points);
    put_u32(&o, (uint32_t)s.carry.hit_points_max);
    put_f32(&o, s.carry.vertical_fov);
    put_f32(&o, s.carry.orbit_distance);
    put_u8(&o, s.carry.closeup);
    chunk_finish(&o, c);

    c = chunk_begin(&o, "LSAV");
    n = tc_cast_level_saves_count();
    put_u32(&o, n);
    for (i = 0; i < n; i++) {
        tc_cast_level_value lv;
        if (!tc_cast_level_saves_get(i, &lv)) lv.size = 0;
        put_u32(&o, lv.level);
        put_u16(&o, lv.perso);
        put_u16(&o, lv.var);
        put_u8(&o, lv.size);
        put(&o, lv.bytes, sizeof(lv.bytes));
    }
    chunk_finish(&o, c);

    if (o.failed) {
        tc_free(o.data);
        return NULL;
    }
    *size = o.size;
    return o.data;
}

/* ---- reading ---- */

static void rd_name(reader *r, char *out, size_t size) {
    uint8_t n = rd_u8(r);
    out[0] = 0;
    if (!have(r, n)) return;
    memcpy(out, r->data + r->offset, n < size - 1 ? n : size - 1);
    out[n < size - 1 ? n : size - 1] = 0;
    r->offset += n;
}

static tc_result read_info(const chunks *c, tc_save_info *out) {
    reader r;
    uint32_t lo, hi;
    memset(out, 0, sizeof(*out));
    chunk_need(c, "INFO", &r);
    rd_name(&r, out->name, sizeof(out->name));
    rd_name(&r, out->level, sizeof(out->level));
    out->edition = rd_u8(&r);
    lo = rd_u32(&r);
    hi = rd_u32(&r);
    out->time = (uint64_t)hi << 32 | lo;
    out->lives = rd_u8(&r);
    out->powers = rd_u16(&r);
    return chunk_end(&r);
}

bool tc_save_read_info(const void *data, size_t size, tc_save_info *out) {
    chunks c;
    return chunks_open(&c, (const uint8_t *)data, size, "TTSV") == TC_OK && read_info(&c, out) == TC_OK &&
           out->name[0] && out->level[0];
}

tc_result tc_save_read(const void *data, size_t size, tc_save_game *out) {
    chunks c;
    reader r;
    tc_result res;
    uint32_t i, n;
    tc_viewer_carry *k = &out->carry;
    memset(out, 0, sizeof(*out));
    if ((res = chunks_open(&c, (const uint8_t *)data, size, "TTSV")) != TC_OK) return res;
    if ((res = read_info(&c, &out->info)) != TC_OK) return res;
    if (!out->info.level[0]) return TC_ERR_STALE;
    tcv_copy_text(k->level, sizeof(k->level), out->info.level);

    chunk_need(&c, "CARY", &r);
    rd_name(&r, k->place, sizeof(k->place));
    rd_name(&r, k->camera_place, sizeof(k->camera_place));
    k->dsg_size = rd_u32(&r);
    if (k->dsg_size > TC_VIEWER_CARRY_DSG) r.error = TC_ERR_STALE;
    else if (have(&r, k->dsg_size)) {
        memcpy(k->dsg, r.data + r.offset, k->dsg_size);
        r.offset += k->dsg_size;
    }
    k->has_progress = rd_u8(&r);
    k->lives = rd_u8(&r);
    k->powers = rd_u16(&r);
    for (i = 0; i < 31; i++) k->values[i] = rd_i16(&r);
    k->has_checkpoint = rd_u8(&r);
    for (i = 0; i < 3; i++) k->checkpoint[i] = rd_f32(&r);
    k->checkpoint_heading = rd_f32(&r);
    k->hit_points = rd_i32(&r);
    k->hit_points_max = rd_i32(&r);
    k->vertical_fov = rd_f32(&r);
    k->orbit_distance = rd_f32(&r);
    k->closeup = rd_u8(&r);
    if ((res = chunk_end(&r)) != TC_OK) return res;

    chunk_need(&c, "LSAV", &r);
    n = rd_u32(&r);
    if (n > (r.size - r.offset) / 21) r.error = TC_ERR_TRUNCATED;
    if (!r.error) tc_cast_level_saves_clear();
    for (i = 0; !r.error && i < n; i++) {
        tc_cast_level_value lv;
        lv.level = rd_u32(&r);
        lv.perso = rd_u16(&r);
        lv.var = rd_u16(&r);
        lv.size = rd_u8(&r);
        if (have(&r, sizeof(lv.bytes))) {
            memcpy(lv.bytes, r.data + r.offset, sizeof(lv.bytes));
            r.offset += sizeof(lv.bytes);
        }
        if (!r.error && lv.size && !tc_cast_level_saves_put(&lv)) r.error = TC_ERR_OUT_OF_MEMORY;
    }
    return chunk_end(&r);
}

void tc_save_start_new_game(tc_viewer_carry *out, const char *level) {
    tc_ai_globals g;
    memset(out, 0, sizeof(*out));
    memset(&g, 0, sizeof(g));
    tc_ai_globals_new_game(&g);
    tcv_copy_text(out->level, sizeof(out->level), level);
    out->has_progress = 1;
    out->lives = 3; /* as a cast starts them (tc_cast.c: the user's, 2026-11-12) */
    out->powers = g.powers;
    memcpy(out->values, g.values, sizeof(out->values));
    tc_cast_level_saves_clear();
}

void tc_save_menu_games(tc_menu *m, const tc_save_info *infos, int count) {
    tc_menu_game games[TC_MENU_MAX_GAMES];
    int k;
    if (count > TC_MENU_MAX_GAMES) count = TC_MENU_MAX_GAMES;
    if (count < 0) count = 0;
    memset(games, 0, sizeof(games));
    for (k = 0; k < count; k++) {
        tcv_copy_text(games[k].name, sizeof(games[k].name), infos[k].name);
        tcv_copy_text(games[k].level, sizeof(games[k].level), infos[k].level);
        games[k].edition = infos[k].edition;
    }
    tc_menu_set_games(m, games, count);
}

void tc_save_file_name(const char *name, char *out, size_t size) {
    size_t k = 0;
    if (!size) return;
    for (; name && *name && k + 1 < size; name++)
        out[k++] = isalnum((unsigned char)*name) ? (char)toupper((unsigned char)*name) : '_';
    if (!k && size > 4) memcpy(out, "GAME", 4), k = 4;
    out[k] = 0;
}
