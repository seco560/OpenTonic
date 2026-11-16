/* The main menu: tc_menu.h, docs/ENGINE.md "The menus". */
#include "tonic/tc_menu.h"
#include "tc_reader.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The texts' camera, as the HUD's (tc_hud.c: inferred): 7 units high, 5 away. */
#define CAMERA_AWAY 5.0f
#define HALF_HEIGHT 3.5f
#define FRAME_S (1.0f / 60.0f) /* the exe's menus step their backgrounds once a frame (frame delta 1.0) */

enum { FONT_TITLE = 0, FONT_CHOICE = 1 };

/* ---- options ---- */

static const float SPEEDS[] = {0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 0.75f, 1, 1.5f, 2}; /* tc_viewer_step_speed's */
#define SPEED_COUNT ((int)(sizeof(SPEEDS) / sizeof(SPEEDS[0])))

void tc_options_defaults(tc_options *o) {
    memset(o, 0, sizeof(*o));
    o->gamma = 1;
    o->animation_speed = 1;
    o->lighting = 0;
    o->fog = true;
    o->look_sensitivity = 1;
    o->music_volume = 1;
    o->sound_volume = 1;
    o->fov = 86;
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static void sane(tc_options *o) {
    int k, best = 5;
    if (!(o->gamma == o->gamma)) o->gamma = 1;
    o->gamma = clampf(o->gamma, 0.5f, 2.5f);
    for (k = 0; k < SPEED_COUNT; k++)
        if (fabsf(SPEEDS[k] - o->animation_speed) < fabsf(SPEEDS[best] - o->animation_speed)) best = k;
    o->animation_speed = SPEEDS[best];
    if (o->lighting > 1) o->lighting = 0;
    if (!(o->look_sensitivity == o->look_sensitivity)) o->look_sensitivity = 1;
    o->look_sensitivity = clampf(o->look_sensitivity, 0.25f, 4);
    if (!(o->music_volume == o->music_volume)) o->music_volume = 1;
    if (!(o->sound_volume == o->sound_volume)) o->sound_volume = 1;
    o->music_volume = clampf(o->music_volume, 0, 1);
    o->sound_volume = clampf(o->sound_volume, 0, 1);
    if (!(o->fov == o->fov)) o->fov = 86;
    o->fov = clampf(o->fov, 50, 120); /* tc_viewer_step_fov's range */
}

size_t tc_options_write(const tc_options *o, char *out, size_t size) {
    int n = snprintf(out, size,
                     "gamma=%.4f\nanimation_speed=%.4f\nlighting=%u\nfog=%d\ndolby=%d\nhud_auto_hide=%d\ninvert_y=%d\n"
                     "look_sensitivity=%.4f\nmusic_volume=%.2f\nsound_volume=%.2f\nfov=%.0f\nskin=%s\n",
                     (double)o->gamma, (double)o->animation_speed, (unsigned)o->lighting, o->fog, o->dolby,
                     o->hud_auto_hide, o->invert_y, (double)o->look_sensitivity, (double)o->music_volume,
                     (double)o->sound_volume, (double)o->fov, o->skin);
    return n < 0 ? 0 : (size_t)n;
}

void tc_options_parse(tc_options *o, const char *text, size_t size) {
    size_t i = 0;
    tc_options_defaults(o);
    while (text && i < size) {
        char line[96], *eq, *end;
        size_t n = 0;
        double x;
        while (i < size && text[i] != '\n' && n + 1 < sizeof(line)) line[n++] = text[i++];
        while (i < size && text[i] != '\n') i++;
        i++;
        line[n] = 0;
        if (!(eq = strchr(line, '='))) continue;
        *eq = 0;
        if (!strcmp(line, "skin")) { /* a name, not a number */
            tc_options_set_skin(o, eq + 1);
            continue;
        }
        x = strtod(eq + 1, &end);
        if (end == eq + 1) continue;
        if (!strcmp(line, "gamma")) o->gamma = (float)x;
        else if (!strcmp(line, "animation_speed")) o->animation_speed = (float)x;
        else if (!strcmp(line, "lighting")) o->lighting = (uint8_t)(x > 0);
        else if (!strcmp(line, "fog")) o->fog = x != 0;
        else if (!strcmp(line, "dolby")) o->dolby = x != 0;
        else if (!strcmp(line, "hud_auto_hide")) o->hud_auto_hide = x != 0;
        else if (!strcmp(line, "invert_y")) o->invert_y = x != 0;
        else if (!strcmp(line, "look_sensitivity")) o->look_sensitivity = (float)x;
        else if (!strcmp(line, "music_volume")) o->music_volume = (float)x;
        else if (!strcmp(line, "sound_volume")) o->sound_volume = (float)x;
        else if (!strcmp(line, "fov")) o->fov = (float)x;
    }
    sane(o);
}

const char *tc_options_skin(const tc_options *o) { return o->skin; }

void tc_options_set_skin(tc_options *o, const char *skin) {
    size_t k, n = skin ? strlen(skin) : 0;
    memset(o->skin, 0, sizeof(o->skin));
    while (n && (skin[n - 1] == '\r' || skin[n - 1] == ' ')) n--; /* a file edited elsewhere */
    if (n >= sizeof(o->skin)) return;
    for (k = 0; k < n; k++) {
        char c = skin[k];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return;
    }
    memcpy(o->skin, skin, n);
}

size_t tc_skin_texture_path(const char *path, const char *skin, char *out, size_t size) {
    static const char FOLDER[] = "Textures/", SKINS[] = "Skins/";
    const char *at = NULL, *p;
    size_t head, need;
    if (!size) return 0;
    out[0] = 0;
    if (!path || !skin || !skin[0]) return 0;
    for (p = path; (p = strstr(p, FOLDER)) != NULL; p++) /* the last: ".../Textures/<rest>" */
        if (p == path || p[-1] == '/') at = p;
    if (!at) return 0;
    head = (size_t)(at - path);
    need = head + strlen(SKINS) + strlen(skin) + 1 + strlen(at + strlen(FOLDER));
    if (need + 1 > size) return 0;
    snprintf(out, size, "%.*s%s%s/%s", (int)head, path, SKINS, skin, at + strlen(FOLDER));
    return need;
}

/* ---- data ---- */

static void data_free(tc_menu_data *d) {
    uint32_t i;
    for (i = 0; d->fonts && i < d->font_count; i++) tc_free(d->fonts[i].glyphs);
    for (i = 0; d->texts && i < d->text_count; i++) tc_free(d->texts[i].text);
    for (i = 0; d->images && i < d->image_count; i++) tc_free(d->images[i].rgba);
    tc_free(d->fonts);
    tc_free(d->texts);
    tc_free(d->images);
    tc_free(d->bgs);
    tc_free(d->skins);
    tc_free(d->atlas);
    memset(d, 0, sizeof(*d));
}

void tc_menu_init(tc_menu *m) {
    memset(m, 0, sizeof(*m));
    tc_options_defaults(&m->options);
    m->selected[TC_MS_NEW_ERASE] = m->selected[TC_MS_DELETE_ERASE] = 1; /* NO: erasing is never the default */
}

void tc_menu_free(tc_menu *m) {
    data_free(&m->data);
    m->loaded = false;
}

static void rd_name(reader *r, char *out, size_t size) {
    uint8_t n = rd_u8(r);
    if (!have(r, n)) return;
    memcpy(out, r->data + r->offset, n < size - 1 ? n : size - 1);
    out[n < size - 1 ? n : size - 1] = 0;
    r->offset += n;
}

static void rd_pixels(reader *r, uint8_t **out, size_t n) {
    if (!have(r, n) || !n) return;
    *out = (uint8_t *)tc_alloc(n);
    if (*out) memcpy(*out, r->data + r->offset, n);
    else r->error = TC_ERR_OUT_OF_MEMORY;
    r->offset += n;
}

tc_result tc_menu_load(tc_menu *m, const void *bytes, size_t size) {
    chunks c;
    reader r;
    tc_menu_data d;
    uint32_t i, k;
    tc_result res = chunks_open(&c, (const uint8_t *)bytes, size, "TTMN");
    if (res != TC_OK) return res;
    memset(&d, 0, sizeof(d));

    chunk_need(&c, "ATLS", &r);
    d.atlas_width = rd_u16(&r);
    d.atlas_height = rd_u16(&r);
    rd_pixels(&r, &d.atlas, (size_t)d.atlas_width * d.atlas_height * 4);
    if ((res = chunk_end(&r)) != TC_OK) goto fail;

    chunk_need(&c, "FONT", &r); /* hud.tthd's layout */
    d.font_count = rd_u16(&r);
    d.fonts = (tc_hud_font *)rd_array(&r, d.font_count, sizeof(*d.fonts), 11);
    for (i = 0; d.fonts && i < d.font_count; i++) {
        tc_hud_font *f = &d.fonts[i];
        rd_name(&r, f->name, sizeof(f->name));
        f->spacing = rd_f32(&r);
        f->line_step = rd_f32(&r);
        f->glyph_count = rd_u16(&r);
        f->glyphs = (tc_hud_glyph *)rd_array(&r, f->glyph_count, sizeof(*f->glyphs), 48);
        if (!f->glyphs) f->glyph_count = 0;
        for (k = 0; k < f->glyph_count; k++) {
            tc_hud_glyph *g = &f->glyphs[k];
            int j;
            g->code = rd_u16(&r);
            (void)rd_u16(&r);
            g->step = rd_f32(&r);
            g->advance = rd_f32(&r);
            g->shift = rd_f32(&r);
            for (j = 0; j < 4; j++) g->cell[j] = rd_f32(&r);
            for (j = 0; j < 4; j++) g->uv[j] = rd_f32(&r);
        }
    }
    if (!d.fonts) d.font_count = 0;
    if ((res = chunk_end(&r)) != TC_OK) goto fail;

    chunk_need(&c, "NTXT", &r);
    d.text_count = rd_u16(&r);
    d.texts = (tc_menu_named_text *)rd_array(&r, d.text_count, sizeof(*d.texts), 4);
    for (i = 0; d.texts && i < d.text_count; i++) {
        tc_menu_named_text *t = &d.texts[i];
        uint16_t n;
        rd_name(&r, t->name, sizeof(t->name));
        t->font = rd_u8(&r);
        n = rd_u16(&r);
        if (!have(&r, n)) break;
        t->text = (char *)tc_alloc((size_t)n + 1);
        if (!t->text) { r.error = TC_ERR_OUT_OF_MEMORY; break; }
        memcpy(t->text, r.data + r.offset, n);
        t->text[n] = 0;
        r.offset += n;
    }
    if (!d.texts) d.text_count = 0;
    if ((res = chunk_end(&r)) != TC_OK) goto fail;

    chunk_need(&c, "IMGS", &r);
    d.image_count = rd_u16(&r);
    d.images = (tc_menu_image *)rd_array(&r, d.image_count, sizeof(*d.images), 5);
    for (i = 0; d.images && i < d.image_count; i++) {
        tc_menu_image *im = &d.images[i];
        rd_name(&r, im->name, sizeof(im->name));
        im->width = rd_u16(&r);
        im->height = rd_u16(&r);
        rd_pixels(&r, &im->rgba, (size_t)im->width * im->height * 4);
    }
    if (!d.images) d.image_count = 0;
    if ((res = chunk_end(&r)) != TC_OK) goto fail;

    chunk_need(&c, "LAYR", &r);
    d.bg_count = rd_u16(&r);
    d.bgs = (tc_menu_bg *)rd_array(&r, d.bg_count, sizeof(*d.bgs), 88);
    for (i = 0; d.bgs && i < d.bg_count; i++) {
        tc_menu_bg *b = &d.bgs[i];
        b->image = rd_u8(&r);
        b->flags = rd_u8(&r);
        b->b20 = rd_u8(&r);
        b->b21 = rd_u8(&r);
        for (k = 0; k < 4; k++) b->rect[k] = rd_f32(&r);
        b->f1c = rd_f32(&r);
        for (k = 0; k < 16; k++) b->w[k] = rd_f32(&r);
        if (b->image >= d.image_count) r.error = TC_ERR_STALE;
    }
    if (!d.bgs) d.bg_count = 0;
    if ((res = chunk_end(&r)) != TC_OK) goto fail;

    chunk_need(&c, "EFCT", &r);
    if (rd_u16(&r) != 4) r.error = r.error ? r.error : TC_ERR_STALE;
    if (have(&r, sizeof(d.effects))) {
        memcpy(d.effects, r.data + r.offset, sizeof(d.effects));
        r.offset += sizeof(d.effects);
    }
    if ((res = chunk_end(&r)) != TC_OK) goto fail;
    if (d.font_count < 2) { res = TC_ERR_STALE; goto fail; }
    /* Ed's skins (tools/ttre/skins.py through menu.py): optional, a file without them offers Ed's own only. SKIN: u16 n,
     * u8 turns, f32 aspect, n x { u8 length, id; u8 length, name; turns x f32 uv[4] }. */
    if (chunk_need(&c, "SKIN", &r)) {
        uint16_t n = rd_u16(&r);
        uint8_t turns = rd_u8(&r);
        float aspect = rd_f32(&r);
        tc_menu_skin *skins = turns && turns <= TC_MENU_SKIN_TURNS
                                  ? (tc_menu_skin *)rd_array(&r, n, sizeof(*skins), 2 + 16 * turns) : NULL;
        for (i = 0; skins && i < n; i++) {
            rd_name(&r, skins[i].id, sizeof(skins[i].id));
            rd_name(&r, skins[i].name, sizeof(skins[i].name));
            for (k = 0; k < (uint32_t)turns * 4; k++) skins[i].uv[k / 4][k % 4] = rd_f32(&r);
        }
        if (skins && chunk_end(&r) == TC_OK && n && aspect > 0) {
            d.skins = skins;
            d.skin_count = n;
            d.skin_turns = turns;
            d.skin_aspect = aspect;
        } else tc_free(skins); /* another layout: as if there were none */
    }
    /* port: FontPC's space steps less than its capitals stand (the menus' two-line question overlapped): a line is at
     * least as tall as the font's tallest capital */
    for (i = 0; i < d.font_count; i++) {
        float tall = 0;
        for (k = 0; k < d.fonts[i].glyph_count; k++) {
            const tc_hud_glyph *g = &d.fonts[i].glyphs[k];
            if (g->code >= 'A' && g->code <= 'Z' && g->cell[3] - g->cell[1] > tall) tall = g->cell[3] - g->cell[1];
        }
        if (d.fonts[i].line_step > -tall) d.fonts[i].line_step = -tall;
    }

    data_free(&m->data);
    m->data = d;
    m->loaded = true;
    for (i = 0; i < d.bg_count && i < TC_MENU_MAX_LAYERS; i++) m->bgs[i] = d.bgs[i];
    return TC_OK;
fail:
    data_free(&d);
    return res;
}

/* game.dsb's text of that name, else the fallback (the port's own words, or a file without it). */
static const char *text(const tc_menu *m, const char *name, const char *fallback) {
    uint32_t i;
    for (i = 0; i < m->data.text_count; i++)
        if (strcmp(m->data.texts[i].name, name) == 0 && m->data.texts[i].text) return m->data.texts[i].text;
    return fallback;
}

/* ---- the screens' contents ---- */

enum { MAIN_START, MAIN_OPTIONS, MAIN_EXTRAS, MAIN_EXIT, MAIN_COUNT };
enum { EXTRAS_CUSTOMIZE, EXTRAS_VIEWER, EXTRAS_REPLAYS, EXTRAS_COUNT }; /* port (the user, 2026-11-16) */
enum { CUSTOM_SKIN, CUSTOM_BACK, CUSTOM_COUNT }; /* port: CUSTOMIZATION (the user, 2026-11-16) */
enum { START_NEW, START_LOAD, START_DELETE, START_COUNT };
enum { GAME_RESUME, GAME_OPTIONS, GAME_PHOTO, GAME_MAIN, GAME_COUNT }; /* quitting: the main menu's EXIT GAME (the user) */
#define LIST_ROWS 6 /* LOAD / DELETE GAME: games shown at once */
/* OPTIONS (port): the sound, then the picture, then the look, then the viewer's speed; BACK last, always in view. */
enum {
    OPT_MUSIC, OPT_SOUND, OPT_DOLBY, OPT_FOV, OPT_GAMMA, OPT_LIGHTING, OPT_FOG, OPT_HUD, OPT_LOOK, OPT_INVERT, OPT_SPEED,
    OPT_BACK, OPT_COUNT
};
#define OPT_ROWS 8 /* options shown at once */

int tc_menu_item_count(const tc_menu *m, tc_menu_screen s) {
    switch (s) {
    case TC_MS_MAIN: return MAIN_COUNT;
    case TC_MS_START: return START_COUNT;
    case TC_MS_NEW_GAME: return 1;
    case TC_MS_NEW_ERASE: case TC_MS_DELETE_ERASE: return 2;
    case TC_MS_LOAD: case TC_MS_DELETE: return m->game_count;
    case TC_MS_OPTIONS: return OPT_COUNT;
    case TC_MS_GAME: return GAME_COUNT;
    case TC_MS_CUSTOMIZE: return CUSTOM_COUNT;
    case TC_MS_EXTRAS: return EXTRAS_COUNT;
    default: return 0;
    }
}

static const char *on_off(bool on) { return on ? "ON" : "OFF"; }

static const char *speed_text(float s) {
    static const char *names[] = {"X0.25", "X0.33", "X0.5", "X0.67", "X0.75", "X1", "X1.5", "X2"}; /* FontPC's '/' is mirrored */
    int k;
    for (k = 0; k < SPEED_COUNT; k++)
        if (fabsf(SPEEDS[k] - s) < 0.01f) return names[k];
    return "1";
}

/* An option's name and its value. */
static void option_text(const tc_menu *m, int item, const char **name, char *value, size_t size) {
    const tc_options *o = &m->options;
    value[0] = 0;
    switch (item) {
    case OPT_MUSIC: *name = "MUSIC VOLUME"; snprintf(value, size, "%d", (int)(o->music_volume * 100 + 0.5f)); break;
    case OPT_SOUND: *name = "SOUND VOLUME"; snprintf(value, size, "%d", (int)(o->sound_volume * 100 + 0.5f)); break;
    case OPT_FOV: *name = "FIELD OF VIEW"; snprintf(value, size, "%d", (int)(o->fov + 0.5f)); break;
    case OPT_GAMMA: *name = "GAMMA"; snprintf(value, size, "%.2f", (double)o->gamma); break;
    case OPT_SPEED: *name = "ANIMATION SPEED"; snprintf(value, size, "%s", speed_text(o->animation_speed)); break;
    case OPT_LIGHTING: *name = "LIGHTING"; snprintf(value, size, "%s", o->lighting ? "PORT" : "ORIGINAL"); break;
    case OPT_FOG: *name = "FOG"; snprintf(value, size, "%s", on_off(o->fog)); break;
    case OPT_DOLBY: *name = "DOLBY SURROUND"; snprintf(value, size, "%s", on_off(o->dolby)); break;
    case OPT_HUD: *name = "HUD AUTO-HIDE"; snprintf(value, size, "%s", on_off(o->hud_auto_hide)); break;
    case OPT_INVERT: *name = "INVERT LOOK"; snprintf(value, size, "%s", on_off(o->invert_y)); break;
    case OPT_LOOK: *name = "LOOK SPEED"; snprintf(value, size, "%.2f", (double)o->look_sensitivity); break;
    default: *name = "BACK"; break;
    }
}

/* CUSTOMIZATION: the skin chosen, as an index into the menu's list (Ed's own, the first, when it is not there). */
static int skin_index(const tc_menu *m) {
    int k;
    for (k = 0; k < m->data.skin_count; k++)
        if (!strcmp(m->data.skins[k].id, m->options.skin)) return k;
    return 0;
}

static const char *skin_name(const tc_menu *m, int k) {
    return k < m->data.skin_count && m->data.skins[k].name[0] ? m->data.skins[k].name : "ED";
}

/* The next (dir > 0) or the previous skin, round the list: TC_MC_OPTIONS (the frontends keep it). */
static void step_skin(tc_menu *m, int dir) {
    int n = m->data.skin_count, k;
    if (n < 2) return;
    k = (skin_index(m) + (dir > 0 ? 1 : n - 1)) % n;
    tc_options_set_skin(&m->options, m->data.skins[k].id);
    m->command = TC_MC_OPTIONS;
}

const char *tc_menu_label(const tc_menu *m, tc_menu_screen s, int item, char *buf, size_t size) {
    const char *name = "";
    char value[32];
    if (!size) return buf;
    buf[0] = 0;
    if (item < 0 || item >= tc_menu_item_count(m, s)) return buf;
    switch (s) {
    case TC_MS_MAIN: {
        static const char *fallback[] = {"START GAME", "OPTIONS", "EXTRAS", "EXIT GAME"};
        static const char *names[] = {"MNU_STARTGAME", "MNU_OPTIONS", NULL, NULL};
        snprintf(buf, size, "%s", names[item] ? text(m, names[item], fallback[item]) : fallback[item]);
        break;
    }
    case TC_MS_EXTRAS: {
        static const char *names[] = {"CUSTOMIZATION", "TONIC VIEWER", "REPLAYS"};
        snprintf(buf, size, "%s", names[item]);
        break;
    }
    case TC_MS_START: {
        static const char *fallback[] = {"NEW GAME", "LOAD GAME", "DELETE GAME"};
        static const char *names[] = {"MNU_NEWGAME", "MNU_LOADGAME", NULL};
        snprintf(buf, size, "%s", names[item] ? text(m, names[item], fallback[item]) : fallback[item]);
        break;
    }
    case TC_MS_GAME: { /* InGame's RESUME GAME, OPTIONS, MAIN MENU (game.dsb); PHOTO MODE ours */
        static const char *fallback[] = {"RESUME GAME", "OPTIONS", "PHOTO MODE", "MAIN MENU"};
        static const char *names[] = {"MNU_RESUMEGAME", "MNU_OPTIONS", NULL, "MNU_MAINMENU"};
        snprintf(buf, size, "%s", names[item] ? text(m, names[item], fallback[item]) : fallback[item]);
        break;
    }
    case TC_MS_NEW_GAME: snprintf(buf, size, "%s", m->name); break;
    case TC_MS_NEW_ERASE: case TC_MS_DELETE_ERASE:
        snprintf(buf, size, "%s", item == 0 ? text(m, "MNU_YES", "YES") : text(m, "MNU_NO", "NO"));
        break;
    case TC_MS_LOAD: case TC_MS_DELETE: snprintf(buf, size, "%s", m->games[item].name); break;
    case TC_MS_OPTIONS:
        option_text(m, item, &name, value, sizeof(value));
        snprintf(buf, size, value[0] ? "%s %s" : "%s", name, value);
        break;
    case TC_MS_CUSTOMIZE:
        if (item == CUSTOM_SKIN) snprintf(buf, size, "SKIN %s", skin_name(m, skin_index(m)));
        else snprintf(buf, size, "BACK");
        break;
    default: break;
    }
    return buf;
}

/* game.dsb's names for the levels (the inventory's map: MNU_INV_*NAME). */
const char *tc_menu_level_title(const tc_menu *m, const char *level, char *buf, size_t size) {
    static const char *const map[][2] = {
        {"totalski", "MNU_INV_SKINAME"}, {"sud0", "MNU_INV_SOUTHNAME"}, {"cavedoc", "MNU_INV_CAVENAME"},
        {"carota", "MNU_INV_CAROTANAME"}, {"north", "MNU_INV_NORTHNAME"}, {"lecanyon", "MNU_INV_CANYONNAME"},
        {"cock01", "MNU_INV_COCKTAILNAME"}, {"pyramide", "MNU_INV_PYRAMIDNAME"}, {"marmite", "MNU_INV_POTNAME"},
        {"land", "MNU_INV_LANDNAME"}};
    const char *name = level ? strchr(level, ':') : NULL;
    char low[64];
    size_t k;
    name = name ? name + 1 : level ? level : "";
    for (k = 0; name[k] && k + 1 < sizeof(low); k++) low[k] = (char)tolower((unsigned char)name[k]);
    low[k] = 0;
    for (k = 0; k < sizeof(map) / sizeof(map[0]); k++)
        if (strcmp(low, map[k][0]) == 0) {
            snprintf(buf, size, "%s", text(m, map[k][1], name));
            return buf;
        }
    snprintf(buf, size, "%s", name);
    return buf;
}

/* ---- input ---- */

void tc_menu_set_games(tc_menu *m, const tc_menu_game *games, int count) {
    if (count < 0) count = 0;
    if (count > TC_MENU_MAX_GAMES) count = TC_MENU_MAX_GAMES;
    if (count) memcpy(m->games, games, (size_t)count * sizeof(*games));
    m->game_count = count;
    if (m->selected[TC_MS_LOAD] >= count) m->selected[TC_MS_LOAD] = count ? count - 1 : 0;
    if (m->selected[TC_MS_DELETE] >= count) m->selected[TC_MS_DELETE] = count ? count - 1 : 0;
}

void tc_menu_set_options(tc_menu *m, const tc_options *o) {
    m->options = *o;
    sane(&m->options);
}

void tc_menu_show(tc_menu *m, tc_menu_screen s) {
    if (s >= TC_MS_COUNT) s = TC_MS_MAIN;
    if (s == TC_MS_OPTIONS && m->screen != TC_MS_OPTIONS)
        m->options_from = m->screen == TC_MS_GAME ? TC_MS_GAME : TC_MS_MAIN;
    m->drag_item = -1;
    if (s == TC_MS_GAME && m->screen != TC_MS_OPTIONS) m->selected[TC_MS_GAME] = GAME_RESUME; /* opened afresh */
    m->screen = s;
    if (s == TC_MS_NEW_GAME) m->name[0] = 0;
    if (s == TC_MS_NEW_ERASE || s == TC_MS_DELETE_ERASE) m->selected[s] = 1;
}

const tc_menu_layer *tc_menu_layer_list(const tc_menu *m, uint32_t *count) {
    *count = m->layer_count;
    return m->layers;
}

const tc_menu_quad *tc_menu_quad_list(const tc_menu *m, uint32_t *count) {
    *count = m->quad_count;
    return m->quads;
}

const char *tc_menu_name(const tc_menu *m) { return m->name; }

tc_menu_command tc_menu_take_command(tc_menu *m) {
    tc_menu_command c = m->command;
    m->command = TC_MC_NONE;
    return c;
}

static bool same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    return *a == *b;
}

/* The options shown as sliders: where the value stands on its bar, 0..1 (gamma and the look speed by their ratio, as
 * their steps are), or -1 for the others. */
static float option_fraction(const tc_options *o, int item) {
    switch (item) {
    case OPT_MUSIC: return o->music_volume;
    case OPT_SOUND: return o->sound_volume;
    case OPT_FOV: return (o->fov - 50) / 70;
    case OPT_GAMMA: return logf(o->gamma / 0.5f) / logf(5);
    case OPT_LOOK: return logf(o->look_sensitivity / 0.25f) / logf(16);
    default: return -1;
    }
}

/* A slider put at `f` of its bar (the pointer's): the volumes by 5 %, the field of view by a degree, the two ratios
 * by 0.01 (1 held a little). True: the value changed. */
static bool set_option_fraction(tc_menu *m, int item, float f) {
    tc_options *o = &m->options, was = *o;
    float x;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    switch (item) {
    case OPT_MUSIC: o->music_volume = roundf(f * 20) / 20; break;
    case OPT_SOUND: o->sound_volume = roundf(f * 20) / 20; break;
    case OPT_FOV: o->fov = roundf(50 + f * 70); break;
    case OPT_GAMMA:
        x = roundf(0.5f * powf(5, f) * 100) / 100;
        o->gamma = fabsf(x - 1) < 0.04f ? 1 : x;
        break;
    case OPT_LOOK:
        x = roundf(0.25f * powf(16, f) * 100) / 100;
        o->look_sensitivity = fabsf(x - 1) < 0.05f ? 1 : x;
        break;
    default: return false;
    }
    sane(o);
    if (!memcmp(&was, o, sizeof(was))) return false;
    m->command = TC_MC_OPTIONS;
    return true;
}

static void step_option(tc_menu *m, int item, int dir) {
    tc_options *o = &m->options;
    int k;
    switch (item) {
    case OPT_MUSIC: o->music_volume = roundf(o->music_volume * 10 + (dir > 0 ? 1.0f : -1.0f)) / 10; break;
    case OPT_SOUND: o->sound_volume = roundf(o->sound_volume * 10 + (dir > 0 ? 1.0f : -1.0f)) / 10; break;
    case OPT_FOV: o->fov = roundf(o->fov) + (dir > 0 ? 5.0f : -5.0f); break; /* tc_viewer_step_fov's steps */
    case OPT_GAMMA: o->gamma *= dir > 0 ? 1.1f : 1 / 1.1f; break; /* tc_viewer_step_gamma's steps */
    case OPT_SPEED:
        for (k = 0; k < SPEED_COUNT && SPEEDS[k] < o->animation_speed - 0.001f; k++) {}
        k += dir > 0 ? 1 : -1;
        o->animation_speed = SPEEDS[k < 0 ? 0 : k >= SPEED_COUNT ? SPEED_COUNT - 1 : k];
        break;
    case OPT_LIGHTING: o->lighting = (uint8_t)!o->lighting; break;
    case OPT_FOG: o->fog = !o->fog; break;
    case OPT_DOLBY: o->dolby = !o->dolby; break;
    case OPT_HUD: o->hud_auto_hide = !o->hud_auto_hide; break;
    case OPT_INVERT: o->invert_y = !o->invert_y; break;
    case OPT_LOOK: /* tc_viewer_step_look's steps */
        o->look_sensitivity *= dir > 0 ? 1.25f : 0.8f;
        if (fabsf(o->look_sensitivity - 1) < 0.01f) o->look_sensitivity = 1;
        break;
    default: return;
    }
    sane(o);
    m->command = TC_MC_OPTIONS;
}

/* OPTIONS' list scrolled so that the selection shows (BACK: the list's end). */
static void options_in_view(tc_menu *m) {
    int sel = m->selected[TC_MS_OPTIONS], last = OPT_BACK - OPT_ROWS;
    if (last < 0) last = 0;
    if (sel >= OPT_BACK) m->options_first = last;
    else if (sel < m->options_first) m->options_first = sel;
    else if (sel >= m->options_first + OPT_ROWS) m->options_first = sel - OPT_ROWS + 1;
    if (m->options_first > last) m->options_first = last;
    if (m->options_first < 0) m->options_first = 0;
}

void tc_menu_scroll(tc_menu *m, int rows) {
    int *sel = &m->selected[m->screen], n = tc_menu_item_count(m, m->screen);
    if (m->screen == TC_MS_OPTIONS) {
        int last = OPT_BACK - OPT_ROWS;
        m->options_first += rows;
        if (m->options_first > last) m->options_first = last;
        if (m->options_first < 0) m->options_first = 0;
        if (*sel < OPT_BACK) { /* the selection comes along when it would leave the view */
            if (*sel < m->options_first) *sel = m->options_first;
            if (*sel >= m->options_first + OPT_ROWS) *sel = m->options_first + OPT_ROWS - 1;
        }
    } else if ((m->screen == TC_MS_LOAD || m->screen == TC_MS_DELETE) && n > 0) {
        *sel += rows;
        if (*sel >= n) *sel = n - 1;
        if (*sel < 0) *sel = 0;
    }
}

void tc_menu_input_press(tc_menu *m, tc_menu_input in) {
    int *sel = &m->selected[m->screen];
    int n = tc_menu_item_count(m, m->screen);
    bool vertical = in == TC_MI_UP || in == TC_MI_DOWN;
    switch (m->screen) {
    case TC_MS_MAIN:
        if (vertical) *sel = (*sel + (in == TC_MI_DOWN ? 1 : n - 1)) % n;
        else if (in == TC_MI_CONFIRM) {
            switch (*sel) {
            case MAIN_START: tc_menu_show(m, TC_MS_START); break;
            case MAIN_OPTIONS: tc_menu_show(m, TC_MS_OPTIONS); break;
            case MAIN_EXTRAS: tc_menu_show(m, TC_MS_EXTRAS); break;
            default: m->command = TC_MC_EXIT; break;
            }
        } else if (in == TC_MI_BACK) *sel = MAIN_EXIT;
        break;
    case TC_MS_EXTRAS:
        if (vertical) *sel = (*sel + (in == TC_MI_DOWN ? 1 : n - 1)) % n;
        else if (in == TC_MI_BACK) tc_menu_show(m, TC_MS_MAIN);
        else if (in == TC_MI_CONFIRM) {
            switch (*sel) {
            case EXTRAS_CUSTOMIZE: tc_menu_show(m, TC_MS_CUSTOMIZE); break;
            case EXTRAS_VIEWER: m->command = TC_MC_TONIC_VIEWER; break;
            default: m->command = TC_MC_REPLAYS; break;
            }
        }
        break;
    case TC_MS_START:
        if (vertical) *sel = (*sel + (in == TC_MI_DOWN ? 1 : n - 1)) % n;
        else if (in == TC_MI_BACK) tc_menu_show(m, TC_MS_MAIN);
        else if (in == TC_MI_CONFIRM)
            tc_menu_show(m, *sel == START_NEW ? TC_MS_NEW_GAME : *sel == START_LOAD ? TC_MS_LOAD : TC_MS_DELETE);
        break;
    case TC_MS_NEW_GAME:
        if (in == TC_MI_BACK) tc_menu_show(m, TC_MS_START);
        else if (in == TC_MI_ERASE) {
            size_t len = strlen(m->name);
            if (len) m->name[len - 1] = 0;
        } else if (in == TC_MI_CONFIRM) {
            int k;
            size_t len = strlen(m->name);
            while (len && m->name[len - 1] == ' ') m->name[--len] = 0; /* no trailing spaces in a game's name */
            if (!len) snprintf(m->name, sizeof(m->name), "ED"); /* port: a pad can't type */
            for (k = 0; k < m->game_count; k++)
                if (same_name(m->games[k].name, m->name)) break;
            if (k < m->game_count) tc_menu_show(m, TC_MS_NEW_ERASE);
            else m->command = TC_MC_NEW_GAME;
        }
        break;
    case TC_MS_NEW_ERASE: case TC_MS_DELETE_ERASE: {
        tc_menu_screen back = m->screen == TC_MS_NEW_ERASE ? TC_MS_NEW_GAME : m->erase_from;
        if (in == TC_MI_LEFT || in == TC_MI_RIGHT || vertical) *sel = !*sel;
        else if (in == TC_MI_BACK) m->screen = back;
        else if (in == TC_MI_CONFIRM) {
            if (*sel == 0 && m->screen == TC_MS_NEW_ERASE) m->command = TC_MC_NEW_GAME;
            else if (*sel == 0) {
                m->command = TC_MC_DELETE_GAME;
                m->command_game = m->selected[m->erase_from];
            }
            m->screen = back;
        }
        break;
    }
    case TC_MS_LOAD: case TC_MS_DELETE: /* the exe's LOAD GAME shows one game at a time, its arrows going through them
                                         * (item type 0x204); port (the user): a list, DELETE GAME's too */
        if (in == TC_MI_BACK) tc_menu_show(m, TC_MS_START);
        else if (!n) break;
        else if (in == TC_MI_UP) *sel = (*sel + n - 1) % n;
        else if (in == TC_MI_DOWN) *sel = (*sel + 1) % n;
        else if (in == TC_MI_LEFT || in == TC_MI_RIGHT) { /* a page */
            *sel += in == TC_MI_RIGHT ? LIST_ROWS : -LIST_ROWS;
            *sel = *sel < 0 ? 0 : *sel >= n ? n - 1 : *sel;
        }
        else if (in == TC_MI_CONFIRM && m->screen == TC_MS_LOAD) {
            m->command = TC_MC_LOAD_GAME;
            m->command_game = *sel;
        } else if (in == TC_MI_ERASE || in == TC_MI_CONFIRM) {
            m->erase_from = m->screen;
            m->selected[TC_MS_DELETE] = m->selected[TC_MS_LOAD] = *sel;
            tc_menu_show(m, TC_MS_DELETE_ERASE);
        }
        break;
    case TC_MS_OPTIONS:
        if (vertical) {
            *sel = (*sel + (in == TC_MI_DOWN ? 1 : n - 1)) % n;
            options_in_view(m);
        } else if (in == TC_MI_BACK || (in == TC_MI_CONFIRM && *sel == OPT_BACK)) tc_menu_show(m, m->options_from);
        else if (in == TC_MI_LEFT || in == TC_MI_RIGHT) step_option(m, *sel, in == TC_MI_RIGHT ? 1 : -1);
        else if (in == TC_MI_CONFIRM) step_option(m, *sel, 1);
        break;
    case TC_MS_CUSTOMIZE:
        if (vertical) *sel = (*sel + (in == TC_MI_DOWN ? 1 : n - 1)) % n;
        else if (in == TC_MI_BACK || (in == TC_MI_CONFIRM && *sel == CUSTOM_BACK)) tc_menu_show(m, TC_MS_EXTRAS);
        else if (in == TC_MI_LEFT || in == TC_MI_RIGHT) step_skin(m, in == TC_MI_RIGHT ? 1 : -1);
        else if (in == TC_MI_CONFIRM) step_skin(m, 1);
        break;
    case TC_MS_GAME:
        if (vertical) *sel = (*sel + (in == TC_MI_DOWN ? 1 : n - 1)) % n;
        else if (in == TC_MI_BACK) m->command = TC_MC_RESUME;
        else if (in == TC_MI_CONFIRM) {
            switch (*sel) {
            case GAME_RESUME: m->command = TC_MC_RESUME; break;
            case GAME_OPTIONS: tc_menu_show(m, TC_MS_OPTIONS); break;
            case GAME_PHOTO: m->command = TC_MC_PHOTO_MODE; break;
            default: m->command = TC_MC_MAIN_MENU; break;
            }
        }
        break;
    default: break;
    }
}

bool tc_menu_in_game(const tc_menu *m) {
    return m->screen == TC_MS_GAME || (m->screen == TC_MS_OPTIONS && m->options_from == TC_MS_GAME);
}

static int hit_at(const tc_menu *m, float x, float y) {
    uint32_t k;
    for (k = 0; k < m->hit_count; k++)
        if (x >= m->hits[k].rect[0] && x <= m->hits[k].rect[2] && y >= m->hits[k].rect[1] && y <= m->hits[k].rect[3])
            return (int)k;
    return -1;
}

/* A slider's hit box is its bar and BAR_PAD of its length past each end (so the ends can be reached): where x is on
 * the bar, 0..1. */
#define BAR_PAD 0.08f
static float bar_fraction(const float rect[4], float x) {
    float w = rect[2] - rect[0], f;
    if (w <= 0) return 0;
    f = ((x - rect[0]) / w * (1 + 2 * BAR_PAD)) - BAR_PAD;
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

bool tc_menu_pointer(tc_menu *m, float x, float y) {
    int h = hit_at(m, x, y);
    if (h < 0) return false;
    m->selected[m->screen] = m->hits[h].item;
    return true;
}

bool tc_menu_click(tc_menu *m, float x, float y) {
    int h = hit_at(m, x, y);
    if (h < 0) return false;
    m->selected[m->screen] = m->hits[h].item;
    m->drag_item = -1;
    if (m->screen == TC_MS_OPTIONS && m->hits[h].item != OPT_BACK) {
        int part = m->hits[h].part;
        if (part == 3 || part == -3) tc_menu_scroll(m, part);
        else if (part == 2) { /* a slider's bar: the value goes there, and follows the pointer while it is held */
            m->drag_item = m->hits[h].item;
            set_option_fraction(m, m->hits[h].item, bar_fraction(m->hits[h].rect, x));
        } else if (part) step_option(m, m->hits[h].item, part < 0 ? -1 : 1);
    } else if (m->screen == TC_MS_CUSTOMIZE && m->hits[h].part) step_skin(m, m->hits[h].part);
    else tc_menu_input_press(m, TC_MI_CONFIRM);
    return true;
}

bool tc_menu_drag(tc_menu *m, float x, float y) {
    uint32_t k;
    (void)y; /* held: it follows sideways wherever the pointer strays */
    if (m->screen != TC_MS_OPTIONS || m->drag_item < 0) return false;
    for (k = 0; k < m->hit_count; k++)
        if (m->hits[k].item == m->drag_item && m->hits[k].part == 2)
            return set_option_fraction(m, m->drag_item, bar_fraction(m->hits[k].rect, x));
    return false;
}

void tc_menu_type(tc_menu *m, uint32_t c) {
    size_t len = strlen(m->name);
    if (m->screen != TC_MS_NEW_GAME || len + 1 >= TC_MENU_NAME) return;
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == ' ' && len > 0))) return;
    m->name[len] = (char)c;
    m->name[len + 1] = 0;
}

/* ---- time ---- */

/* MNU_0044df60: a background's corners. The uv centre drifts (+0x24 .. +0x40), its half size breathes (+0x44 .. +0x50)
 * and it turns (+0x54 .. +0x60); the corners stand at 135, 45, 225 and 315 degrees round it (0x4d2720 ... 0x4d2734). */
static void bg_step(tc_menu_bg *b) {
    float *w = b->w;
    w[10] += w[11];
    w[4] += w[6];
    w[5] += w[7];
    w[13] += sinf(w[14]) * w[12];
    w[14] += w[15];
}

void tc_menu_update(tc_menu *m, float dt) {
    uint32_t i;
    if (dt < 0 || dt > 0.5f) dt = 0;
    m->time += dt;
    m->frame_carry += dt;
    while (m->frame_carry >= FRAME_S) {
        m->frame_carry -= FRAME_S;
        for (i = 0; i < m->data.bg_count && i < TC_MENU_MAX_LAYERS; i++)
            if (m->bgs[i].flags & 1) bg_step(&m->bgs[i]);
    }
}

/* ---- layout ---- */

typedef struct draw { tc_menu *m; float aspect; } draw;

static const tc_hud_glyph *glyph_of(const tc_hud_font *f, uint8_t code) {
    const tc_hud_glyph *space = NULL;
    uint32_t k;
    for (k = 0; k < f->glyph_count; k++) {
        if (f->glyphs[k].code == code) return &f->glyphs[k];
        if (f->glyphs[k].code == ' ' && !space) space = &f->glyphs[k];
    }
    return space;
}

/* As tc_hud.c's (FON_004ce110): first letter's middle to last letter's middle. */
static float line_width(const tc_hud_font *f, const char *s, size_t n) {
    float w = 0;
    size_t k;
    for (k = 0; k < n && s[k] && s[k] != '^'; k++) {
        const tc_hud_glyph *g = glyph_of(f, (uint8_t)s[k]);
        bool last = k + 1 >= n || !s[k + 1] || s[k + 1] == '^';
        float a = g ? g->advance : 0;
        if (k == 0 && last) w = 0;
        else if (last) w += a * 0.5f;
        else if (k == 0) w += f->spacing + a * 0.5f;
        else w += a + f->spacing;
    }
    return w;
}

static void put_quad(draw *d, float x0, float z0, float x1, float z1, const float uv[4], const float color[4]) {
    tc_menu *m = d->m;
    float half_w = HALF_HEIGHT * d->aspect;
    tc_menu_quad *q;
    if (m->quad_count >= TC_MENU_MAX_QUADS) return;
    q = &m->quads[m->quad_count++];
    q->rect[0] = 0.5f + x0 / (2 * half_w);
    q->rect[2] = 0.5f + x1 / (2 * half_w);
    q->rect[1] = 0.5f - z1 / (2 * HALF_HEIGHT);
    q->rect[3] = 0.5f - z0 / (2 * HALF_HEIGHT);
    memcpy(q->uv, uv, sizeof(q->uv));
    memcpy(q->color, color, sizeof(q->color));
}

/* A place the pointer can hit: the box x0..x1, y0..y1 in font units (y up). */
static void add_hit(draw *d, float x0, float y0, float x1, float y1, int item, int part) {
    tc_menu *m = d->m;
    float half_w = HALF_HEIGHT * d->aspect;
    if (m->hit_count >= sizeof(m->hits) / sizeof(m->hits[0])) return;
    m->hits[m->hit_count].rect[0] = 0.5f + x0 / (2 * half_w);
    m->hits[m->hit_count].rect[2] = 0.5f + x1 / (2 * half_w);
    m->hits[m->hit_count].rect[1] = 0.5f - y1 / (2 * HALF_HEIGHT);
    m->hits[m->hit_count].rect[3] = 0.5f - y0 / (2 * HALF_HEIGHT);
    m->hits[m->hit_count].item = (int16_t)item;
    m->hits[m->hit_count].part = (int16_t)part;
    m->hit_count++;
}

/* A text centred on (x, y) at `scale` (lines part at '^'). bob: the letters rise and fall in a wave (the selected
 * choice's). Returns the widest line's width, scaled. */
static float put_text(draw *d, int font, const char *s, float x, float y, float scale, const float color[4], bool bob) {
    const tc_hud_font *f = font < d->m->data.font_count ? &d->m->data.fonts[font] : NULL;
    int lines = 1, letter = 0;
    const char *p;
    float z, widest = 0;
    if (!f || !s) return 0;
    for (p = s; *p; p++) lines += *p == '^';
    z = -0.5f * f->line_step * (float)lines; /* the top, about y (the line step is negative) */
    while (*s) {
        size_t n = strcspn(s, "^"), k;
        float w = line_width(f, s, n), pen = -0.5f * w;
        if (w * scale > widest) widest = w * scale;
        for (k = 0; k < n; k++, letter++) {
            const tc_hud_glyph *g = glyph_of(f, (uint8_t)s[k]);
            if (!g) continue;
            if (g->cell[0] != g->cell[2]) {
                float zc = z + f->line_step - g->step + g->shift;
                float rise = bob ? 0.07f * sinf(d->m->time * 6.0f - (float)letter * 0.7f) : 0;
                put_quad(d, x + (pen + g->cell[0]) * scale, y + (zc + g->cell[1] + rise) * scale,
                         x + (pen + g->cell[2]) * scale, y + (zc + g->cell[3] + rise) * scale, g->uv, color);
            }
            pen += f->spacing + g->advance * 0.5f;
            if (k + 1 < n) {
                const tc_hud_glyph *next = glyph_of(f, (uint8_t)s[k + 1]);
                if (next) pen += next->advance * 0.5f;
            }
        }
        s += n;
        if (*s == '^') s++;
        z += f->line_step;
    }
    return widest;
}

static float text_width(const tc_menu *m, int font, const char *s, float scale) {
    const tc_hud_font *f = font < m->data.font_count ? &m->data.fonts[font] : NULL;
    return f && s ? line_width(f, s, strcspn(s, "^")) * scale : 0;
}

/* A scale no larger than `scale` at which the text's widest line fits `room` (font units). */
static float fit(const tc_menu *m, int font, const char *s, float scale, float room) {
    const tc_hud_font *f = font < m->data.font_count ? &m->data.fonts[font] : NULL;
    float widest = 0;
    while (f && s && *s) {
        size_t n = strcspn(s, "^");
        float w = line_width(f, s, n) + 0.6f; /* the outer letters' halves */
        if (w > widest) widest = w;
        s += n;
        if (*s == '^') s++;
    }
    return widest * scale > room ? room / widest : scale;
}

static const float WHITE[4] = {1, 1, 1, 1};

/* An arrow pointing left or right, centred on (x, y). FontPC's '<' (and '/') come out mirrored from their models, so
 * both arrows are its '>', the left one flipped. */
static void put_arrow(draw *d, bool left, float x, float y, float scale, const float color[4]) {
    uint32_t first = d->m->quad_count;
    put_text(d, FONT_CHOICE, ">", x, y, scale, color, false);
    if (left && d->m->quad_count > first) {
        float *uv = d->m->quads[first].uv, u = uv[0];
        uv[0] = uv[2];
        uv[2] = u;
    }
}
static const float DIM[4] = {0.78f, 0.78f, 0.78f, 1};   /* port: a choice not selected */
static const float TRACK[4] = {0.3f, 0.3f, 0.3f, 0.85f}; /* port: a slider's bar past its value, the scroll bar's rail */
static const float GOLD[4] = {1.3f, 1.0f, 0.35f, 1};     /* port: a slider's bar up to its value (the title's gold) */
static const float GOLD_DIM[4] = {0.95f, 0.72f, 0.25f, 1};

/* A plain bar x0..x1, y0..y1 (font units): the atlas has no blank texel to draw with, so it is the gold from the
 * middle of FontPC's 'I' (its stem), stretched. */
static void put_block(draw *d, float x0, float y0, float x1, float y1, const float color[4]) {
    const tc_hud_font *f = FONT_CHOICE < d->m->data.font_count ? &d->m->data.fonts[FONT_CHOICE] : NULL;
    const tc_hud_glyph *g = f ? glyph_of(f, 'I') : NULL;
    float uv[4], u, v;
    if (!g || g->cell[0] == g->cell[2] || x1 <= x0 || y1 <= y0) return;
    u = 0.5f * (g->uv[0] + g->uv[2]), v = 0.5f * (g->uv[1] + g->uv[3]);
    uv[0] = uv[2] = u, uv[1] = uv[3] = v;
    put_quad(d, x0, y0, x1, y1, uv, color);
}
#define SELECTED_SCALE 1.12f                            /* port: a selected choice grows a little */
#define CURSOR_GAP 0.55f

/* Ed's head (FontPC's '§': the exe's MNU_CURSOR, a 3D head turning at the end of the name being typed), centred on
 * (x, y). The atlas has it once, front on: port, it turns by narrowing the picture (mirrored past 90 degrees). */
static void put_head(draw *d, float x, float y, float scale) {
    uint32_t first = d->m->quad_count;
    float c = cosf(d->m->time * 2.5f);
    put_text(d, FONT_CHOICE, "\xA7", x, y, scale, WHITE, false);
    if (d->m->quad_count > first) {
        tc_menu_quad *q = &d->m->quads[first];
        float mid = 0.5f * (q->rect[0] + q->rect[2]), half = 0.5f * (q->rect[2] - q->rect[0]) * fabsf(c);
        q->rect[0] = mid - half;
        q->rect[2] = mid + half;
        if (c < 0) {
            float u = q->uv[0];
            q->uv[0] = q->uv[2];
            q->uv[2] = u;
        }
    }
}

/* The head beside the selected choice (port: the menu's selection marker), bobbing. x: the choice's left edge. */
static void put_cursor(draw *d, float x, float y, float scale) {
    put_head(d, x - CURSOR_GAP * scale, y + 0.08f * sinf(d->m->time * 5.0f), scale);
}

/* A choice, centred on (x, y); the pointer hits it as `item`. */
static void put_choice(draw *d, const char *s, float x, float y, float scale, bool selected, int item) {
    float k = selected ? scale * SELECTED_SCALE : scale;
    float w = put_text(d, FONT_CHOICE, s, x, y, k, selected ? WHITE : DIM, selected), base = w < 1.2f * scale ? 1.2f * scale : w;
    if (selected) put_cursor(d, x - 0.5f * w, y, k);
    add_hit(d, x - 0.5f * base - 0.35f * scale, y - 0.38f * scale, x + 0.5f * base + 0.35f * scale, y + 0.38f * scale, item, 0);
}

static void put_title(draw *d, const char *s) { put_text(d, FONT_TITLE, s, 0, 3.0f, 1, WHITE, false); }

static void build_layers(tc_menu *m, float aspect) {
    uint32_t i, k;
    /* port: the exe's screen is 4:3; a wider one sees more of the picture across instead of a stretched one */
    float wide = aspect / (4.0f / 3.0f);
    static const float ANGLE[4] = {-2.3561945f, -0.7853982f, -3.9269907f, -5.497787f};
    m->layer_count = 0;
    if (tc_menu_in_game(m)) { /* port (the user): over the game, a dim and two slow translucent swirls turning apart */
        static const float CORNER[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f}};
        float t = m->time;
        tc_menu_layer *l = &m->layers[m->layer_count++];
        memset(l, 0, sizeof(*l));
        l->image = TC_MENU_IMAGE_DIM;
        l->alpha = 0.55f;
        for (i = 0; i < 2; i++) {
            float a = i ? 1.3f - 0.045f * t : 0.06f * t, s = i ? 2.4f + 0.3f * sinf(0.09f * t + 1) : 1.6f + 0.2f * sinf(0.13f * t);
            float cu = 0.5f + 0.3f * sinf(0.021f * t + (float)i), cv = 0.5f + (i ? -0.013f : 0.017f) * t;
            l = &m->layers[m->layer_count++];
            l->image = TC_MENU_IMAGE_SWIRL;
            l->alpha = TC_MENU_SWIRL_ALPHA;
            for (k = 0; k < 4; k++) {
                float x = CORNER[k][0] * aspect * s, y = CORNER[k][1] * s;
                l->uv[k][0] = cu + cosf(a) * x - sinf(a) * y;
                l->uv[k][1] = cv + sinf(a) * x + cosf(a) * y;
            }
        }
        return;
    }
    for (i = 0; i < m->data.bg_count && i < TC_MENU_MAX_LAYERS; i++) {
        const tc_menu_bg *b = &m->bgs[i];
        const float *w = b->w;
        tc_menu_layer *l = &m->layers[m->layer_count++];
        float r = sinf(w[10]) * w[9] + w[8] - 0.5f;
        float cu = sinf(w[2] + w[4]) * w[0] + 0.5f, cv = sinf(w[3] + w[5]) * w[1] + 0.5f;
        l->image = b->image;
        l->alpha = 1;
        for (k = 0; k < 4; k++) {
            float a = w[13] - ANGLE[k];
            float du = cosf(a) * r * 1.414f, dv = -sinf(a) * r * 1.414f;
            if (!(b->flags & 1)) { /* still: the rect's corners */
                l->uv[k][0] = k & 1 ? 1.0f : 0.0f;
                l->uv[k][1] = k & 2 ? 1.0f : 0.0f;
                continue;
            }
            l->uv[k][0] = 0.5f + (cu - 0.5f) + du * wide;
            l->uv[k][1] = cv + dv;
        }
    }
}

void tc_menu_build(tc_menu *m, float aspect) {
    draw d = {m, aspect > 0.1f ? aspect : 4.0f / 3.0f};
    char buf[96], value[32];
    int k, n = tc_menu_item_count(m, m->screen), sel = m->selected[m->screen];
    m->quad_count = 0;
    m->hit_count = 0;
    build_layers(m, d.aspect);
    if (!m->loaded) return;
    switch (m->screen) {
    case TC_MS_MAIN: {
        /* StartUp's five choices stand at y 1.0, 0.2, -0.6, -1.4, -2.2 (read); the title is the user's: "Open Tonic" in
         * the title font, a size up from the other screens' */
        put_text(&d, FONT_TITLE, "Open Tonic", 0, 2.75f, 1.3f, WHITE, false);
        for (k = 0; k < n; k++)
            put_choice(&d, tc_menu_label(m, TC_MS_MAIN, k, buf, sizeof(buf)), 0, 1.0f - 0.8f * (float)k, 1, k == sel, k);
        break;
    }
    case TC_MS_EXTRAS: /* port: as START GAME's (its choices a step of 1 from 0.5) */
        put_title(&d, "EXTRAS");
        for (k = 0; k < n; k++)
            put_choice(&d, tc_menu_label(m, TC_MS_EXTRAS, k, buf, sizeof(buf)), 0, 0.5f - 1.0f * (float)k, 1, k == sel, k);
        break;
    case TC_MS_CUSTOMIZE: { /* port: Ed turning in the skin chosen, its name between arrows under him, BACK */
        const float scale = 0.85f, name_y = -1.75f, high = 3.6f, mid = 0.5f;
        int skin = skin_index(m);
        const char *name = skin_name(m, skin);
        float s = sel == CUSTOM_SKIN ? scale * SELECTED_SCALE : scale, half, w;
        put_text(&d, FONT_TITLE, "CUSTOMIZATION", 0, 3.0f, fit(m, FONT_TITLE, "CUSTOMIZATION", 1, 8.6f), WHITE, false);
        if (skin < m->data.skin_count && m->data.skin_turns) { /* he turns (anticlockwise), each picture fading
                                                                * into the next */
            const tc_menu_skin *sk = &m->data.skins[skin];
            int turns = m->data.skin_turns;
            float a = m->time * 0.11f * (float)turns, f = a - floorf(a);
            int k0 = (int)floorf(a) % turns, k1 = (k0 + 1) % turns;
            float fade[4] = {1, 1, 1, 1};
            w = high * m->data.skin_aspect;
            put_quad(&d, -0.5f * w, mid - 0.5f * high, 0.5f * w, mid + 0.5f * high, sk->uv[k0], WHITE);
            fade[3] = f;
            put_quad(&d, -0.5f * w, mid - 0.5f * high, 0.5f * w, mid + 0.5f * high, sk->uv[k1], fade);
            add_hit(&d, -0.5f * w, mid - 0.5f * high, 0.5f * w, mid + 0.5f * high, CUSTOM_SKIN, 1);
        } else put_text(&d, FONT_CHOICE, "NO SKINS^RE-RUN THE CONVERSION", 0, mid, 0.55f, DIM, false);
        s = fit(m, FONT_CHOICE, name, s, 5.0f);
        half = 0.5f * text_width(m, FONT_CHOICE, name, s) + 0.55f;
        put_text(&d, FONT_CHOICE, name, 0, name_y, s, sel == CUSTOM_SKIN ? WHITE : DIM, sel == CUSTOM_SKIN);
        if (m->data.skin_count > 1) {
            const float *c = sel == CUSTOM_SKIN ? WHITE : DIM;
            put_arrow(&d, true, -half, name_y, s, c);
            put_arrow(&d, false, half, name_y, s, c);
            add_hit(&d, -half - 0.6f, name_y - 0.35f, 0, name_y + 0.35f, CUSTOM_SKIN, -1);
            add_hit(&d, 0, name_y - 0.35f, half + 0.6f, name_y + 0.35f, CUSTOM_SKIN, 1);
            snprintf(buf, sizeof(buf), "%d OF %d", skin + 1, m->data.skin_count);
            put_text(&d, FONT_CHOICE, buf, 0, name_y - 0.58f, 0.42f, DIM, false);
        }
        if (sel == CUSTOM_SKIN) put_cursor(&d, -half - 0.35f, name_y, s);
        put_choice(&d, "BACK", 0, -2.95f, 0.72f, sel == CUSTOM_BACK, CUSTOM_BACK);
        break;
    }
    case TC_MS_GAME: /* InGame: its title at 3, its choices a step of 1 from 0.5 (read); PHOTO MODE makes four */
        put_title(&d, text(m, "MNU_TITLEGAMEMENU", "GAME MENU"));
        for (k = 0; k < n; k++)
            put_choice(&d, tc_menu_label(m, TC_MS_GAME, k, buf, sizeof(buf)), 0, 1.0f - 1.0f * (float)k, 1, k == sel, k);
        break;
    case TC_MS_START: /* StartGame: NEW GAME at 0.5, LOAD GAME at -0.5 (read); DELETE GAME, ours, a step below */
        put_title(&d, text(m, "MNU_TITLESTARTGAME", "START GAME"));
        for (k = 0; k < n; k++)
            put_choice(&d, tc_menu_label(m, TC_MS_START, k, buf, sizeof(buf)), 0, 0.5f - 1.0f * (float)k, 1, k == sel, k);
        break;
    case TC_MS_NEW_GAME: { /* NewGame: the prompt at 0.5, the name at -1.0 with its cursor after it */
        float w;
        put_title(&d, text(m, "MNU_TITLENEWGAME", "NEW GAME"));
        put_text(&d, FONT_CHOICE, text(m, "MNU_ENTERTHEGAMENAME", "ENTER THE GAME NAME"), 0, 0.5f, 1, DIM, false);
        {
            float s = fit(m, FONT_CHOICE, m->name, 1, 7.6f); /* room for the cursor after it on a 4:3 screen */
            w = put_text(&d, FONT_CHOICE, m->name, 0, -1.0f, s, WHITE, false);
            put_head(&d, 0.5f * w + (m->name[0] ? CURSOR_GAP * s : 0), -1.0f, s);
        }
        break;
    }
    case TC_MS_NEW_ERASE: case TC_MS_DELETE_ERASE: { /* NewGameDeleteSavedGame: the name as title (2.8), YES / NO at +-1 */
        int g = m->selected[m->erase_from];
        const char *name = m->screen == TC_MS_NEW_ERASE ? m->name : g < m->game_count ? m->games[g].name : "";
        put_text(&d, FONT_TITLE, name, 0, 2.8f, fit(m, FONT_TITLE, name, 1, 8.6f), WHITE, false);
        put_text(&d, FONT_CHOICE, text(m, "MNU_ERASEGAME", "DO YOU REALLY WANT^TO ERASE THIS GAME ?"), 0, 0.5f, 1, DIM,
                 false);
        put_choice(&d, tc_menu_label(m, m->screen, 0, buf, sizeof(buf)), -1.0f, -1.0f, 1, sel == 0, 0);
        put_choice(&d, tc_menu_label(m, m->screen, 1, buf, sizeof(buf)), 1.0f, -1.0f, 1, sel == 1, 1);
        break;
    }
    case TC_MS_LOAD: case TC_MS_DELETE: { /* LoadGame's prompt (game.dsb); port (the user): the games as a list, each
                                           * with the level it is in, the selection in view */
        const float top = 1.15f, step = 0.66f, left = -3.6f, right = 4.1f, scale = 0.72f;
        int first = sel - LIST_ROWS / 2;
        put_title(&d, m->screen == TC_MS_LOAD ? text(m, "MNU_TITLELOADGAME", "LOAD GAME") : "DELETE GAME");
        put_text(&d, FONT_CHOICE, text(m, "MNU_CHOOSEGAME", "CHOOSE YOUR GAME"), 0, 2.0f, 0.8f, DIM, false);
        if (!n) {
            put_text(&d, FONT_CHOICE, "NO SAVED GAME", 0, -0.3f, 0.8f, DIM, false);
            break;
        }
        if (first > n - LIST_ROWS) first = n - LIST_ROWS;
        if (first < 0) first = 0;
        for (k = first; k < n && k < first + LIST_ROWS; k++) {
            const tc_menu_game *g = &m->games[k];
            float y = top - step * (float)(k - first);
            float s = fit(m, FONT_CHOICE, g->name, k == sel ? scale * SELECTED_SCALE : scale, 4.4f), t;
            const float *c = k == sel ? WHITE : DIM;
            size_t j;
            put_text(&d, FONT_CHOICE, g->name, left + 0.5f * text_width(m, FONT_CHOICE, g->name, s), y, s, c, k == sel);
            if (k == sel) put_cursor(&d, left, y, s);
            add_hit(&d, left - 0.5f, y - 0.32f, right + 0.3f, y + 0.32f, k, 0);
            tc_menu_level_title(m, g->level, buf, sizeof(buf));
            for (j = 0; buf[j]; j++) buf[j] = (char)toupper((unsigned char)buf[j]);
            t = fit(m, FONT_CHOICE, buf, 0.5f, 3.2f);
            put_text(&d, FONT_CHOICE, buf, right - 0.5f * text_width(m, FONT_CHOICE, buf, t), y, t, DIM, false);
        }
        if (n > LIST_ROWS) {
            snprintf(buf, sizeof(buf), "%d OF %d", sel + 1, n);
            put_text(&d, FONT_CHOICE, buf, 0, top - step * (float)LIST_ROWS - 0.15f, 0.5f, DIM, false);
        }
        break;
    }
    case TC_MS_OPTIONS: { /* port: a name at the left, its value at the right, smaller than a menu's choices: OPT_ROWS
                           * of them in view, the list scrolling under the selection, BACK below; the numbers that
                           * run over a range (volumes, field of view, gamma, look speed) as sliders */
        const float scale = 0.62f, top = 2.0f, step = 0.58f, left = -3.6f, right = 2.9f;
        const float bar0 = 0.95f, bar1 = 2.8f, number = 3.55f, rail = 4.45f;
        int first, options = OPT_BACK;
        put_title(&d, text(m, "MNU_TITLEOPTIONS", "OPTIONS"));
        if (m->options_first > options - OPT_ROWS) m->options_first = options - OPT_ROWS;
        if (m->options_first < 0) m->options_first = 0;
        first = m->options_first;
        for (k = first; k < options && k < first + OPT_ROWS; k++) {
            const char *name;
            float y = top - step * (float)(k - first), s = k == sel ? scale * SELECTED_SCALE : scale;
            float f = option_fraction(&m->options, k);
            const float *c = k == sel ? WHITE : DIM;
            option_text(m, k, &name, value, sizeof(value));
            put_text(&d, FONT_CHOICE, name, left + 0.5f * text_width(m, FONT_CHOICE, name, s), y, s, c, k == sel);
            if (k == sel) put_cursor(&d, left, y, s);
            if (f >= 0) { /* a slider: its bar, filled up to the value, a knob there, the number after it */
                float pad = BAR_PAD * (bar1 - bar0), x = bar0 + (bar1 - bar0) * (f > 1 ? 1 : f);
                float thick = k == sel ? 0.075f : 0.06f, knob = k == sel ? 0.2f : 0.16f;
                put_block(&d, bar0, y - thick, bar1, y + thick, TRACK);
                put_block(&d, bar0, y - thick, x, y + thick, k == sel ? GOLD : GOLD_DIM);
                put_block(&d, x - 0.07f, y - knob, x + 0.07f, y + knob, c);
                put_text(&d, FONT_CHOICE, value, number, y, s * 0.85f, c, false);
                add_hit(&d, left - 0.4f, y - 0.29f, bar0 - pad, y + 0.29f, k, 0); /* the name: selects it */
                add_hit(&d, bar0 - pad, y - 0.29f, bar1 + pad, y + 0.29f, k, 2);
                continue;
            }
            put_text(&d, FONT_CHOICE, value, right, y, s, c, false);
            add_hit(&d, left - 0.4f, y - 0.29f, right - 1.6f, y + 0.29f, k, 1); /* the name: a step on (a switch flips) */
            add_hit(&d, right - 1.6f, y - 0.29f, right, y + 0.29f, k, -1); /* left of the value's middle: back */
            add_hit(&d, right, y - 0.29f, right + 1.4f, y + 0.29f, k, 1);
            if (k == sel) {
                float half = 0.5f * text_width(m, FONT_CHOICE, value, s) + 0.45f;
                put_arrow(&d, true, right - half, y, s, c);
                put_arrow(&d, false, right + half, y, s, c);
            }
        }
        if (options > OPT_ROWS) { /* the scroll bar: its thumb is the part of the list in view */
            float y0 = top + 0.24f, y1 = top - step * (float)(OPT_ROWS - 1) - 0.24f, len = y0 - y1;
            float t0 = y0 - len * (float)first / (float)options, t1 = y0 - len * (float)(first + OPT_ROWS) / (float)options;
            put_block(&d, rail - 0.035f, y1, rail + 0.035f, y0, TRACK);
            put_block(&d, rail - 0.06f, t1, rail + 0.06f, t0, GOLD_DIM);
            if (first > 0) add_hit(&d, rail - 0.2f, t0, rail + 0.2f, y0, sel, -3);
            if (first + OPT_ROWS < options) add_hit(&d, rail - 0.2f, y1, rail + 0.2f, t1, sel, 3);
        }
        put_choice(&d, "BACK", 0, top - step * (float)OPT_ROWS - 0.25f, scale, sel == OPT_BACK, OPT_BACK);
        break;
    }
    default: break;
    }
}
