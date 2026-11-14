/* The game's HUD: tc_hud.h, docs/ENGINE.md "The HUD". */
#include "tonic/tc_hud.h"
#include "tc_reader.h"
#include <stdio.h>
#include <string.h>

/* The exe's layout, in its font units. The camera the texts are drawn through is not read: three icon and number pairs
 * of the standard HUD (the spade at depth 12, the tings at 5, Doc's object at 6, each with its number at depth 0) line
 * up at x = 4.0 when it stands 5 away, and a string at y = -2.5 then falls in the middle of its box (75 % to 95 % of
 * the screen) with half the screen 3.5 high. Both inferred. */
#define CAMERA_AWAY 5.0f
#define HALF_HEIGHT 3.5f
#define WRAP_WIDTH 8.2f        /* _DAT_004d20e8: a slot's line is no wider (GAM_00418d70) */
#define SLOT_ANCHOR_X 1.5f     /* port: a slot's text beyond this, left or right of the middle, follows that edge */
#define TICK_MS (1000.0f / 30.0f)
#define SHOWN_MS 5000.0f       /* a changed element stays this long (GAM_0041a720: 5000 < now - DAT_008bd07c) */
#define GONE 2.0f              /* _DAT_004d2108: slid this far out, an element is no longer drawn */

/* The life gauge's steps (0x4da350): the height of each life point on the thermometer. */
static const float LIFE_STEPS[17] = {-0.12f, -0.05f, 0.02f, 0.09f, 0.17f, 0.25f, 0.32f, 0.40f, 0.48f,
                                     0.55f, 0.62f, 0.69f, 0.76f, 0.83f, 0.90f, 0.97f, 1.04f};

void tc_hud_init(tc_hud *h) {
    memset(h, 0, sizeof(*h));
    h->shown_hp = h->shown_lives = h->shown_tings = h->shown_spade = -2; /* the first tick shows them (the exe's start) */
    h->out_life = h->out_lives = h->out_spade = h->out_tings = h->out_doc = h->out_bees = GONE; /* GAM_0041bf90 */
}

void tc_hud_away(tc_hud *h) {
    h->out_life = h->out_lives = h->out_spade = h->out_tings = h->out_doc = h->out_bees = GONE;
}

static void data_free(tc_hud_data *d) {
    uint32_t i;
    for (i = 0; d->fonts && i < d->font_count; i++) tc_free(d->fonts[i].glyphs);
    for (i = 0; d->texts && i < d->text_count; i++) tc_free(d->texts[i].bytes);
    tc_free(d->fonts);
    tc_free(d->texts);
    tc_free(d->atlas);
    memset(d, 0, sizeof(*d));
}

void tc_hud_free(tc_hud *h) {
    data_free(&h->data);
    h->loaded = false;
}

tc_result tc_hud_load(tc_hud *h, const void *bytes, size_t size) {
    chunks c;
    reader r;
    tc_hud_data d;
    uint32_t i, k, n;
    tc_result res = chunks_open(&c, (const uint8_t *)bytes, size, "TTHD");
    if (res != TC_OK) return res;
    memset(&d, 0, sizeof(d));

    chunk_need(&c, "ATLS", &r);
    d.atlas_width = rd_u16(&r);
    d.atlas_height = rd_u16(&r);
    for (i = 0; i < 4; i++) d.panel_uv[i] = rd_f32(&r);
    n = (uint32_t)d.atlas_width * d.atlas_height * 4;
    if (have(&r, n) && n) {
        d.atlas = (uint8_t *)tc_alloc(n);
        if (d.atlas) memcpy(d.atlas, r.data + r.offset, n);
        else r.error = TC_ERR_OUT_OF_MEMORY;
        r.offset += n;
    }
    if ((res = chunk_end(&r)) != TC_OK) goto fail;

    chunk_need(&c, "FONT", &r);
    d.font_count = rd_u16(&r);
    d.fonts = (tc_hud_font *)rd_array(&r, d.font_count, sizeof(*d.fonts), 11);
    for (i = 0; d.fonts && i < d.font_count; i++) {
        tc_hud_font *f = &d.fonts[i];
        uint8_t len = rd_u8(&r);
        if (have(&r, len)) {
            memcpy(f->name, r.data + r.offset, len < sizeof(f->name) - 1 ? len : sizeof(f->name) - 1);
            r.offset += len;
        }
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

    chunk_need(&c, "TEXT", &r);
    d.text_count = rd_u16(&r);
    d.texts = (tc_hud_text *)rd_array(&r, d.text_count, sizeof(*d.texts), 6);
    for (i = 0; d.texts && i < d.text_count; i++) {
        tc_hud_text *t = &d.texts[i];
        t->font = rd_u8(&r);
        (void)rd_u8(&r);
        t->capacity = rd_u16(&r);
        t->length = rd_u16(&r);
        if (have(&r, t->length)) {
            t->bytes = (char *)tc_alloc((size_t)t->length + 1);
            if (!t->bytes) { r.error = TC_ERR_OUT_OF_MEMORY; break; }
            memcpy(t->bytes, r.data + r.offset, t->length);
            t->bytes[t->length] = 0;
            r.offset += t->length;
        }
    }
    if (!d.texts) d.text_count = 0;
    if ((res = chunk_end(&r)) != TC_OK) goto fail;

    chunk_need(&c, "STDH", &r);
    if (rd_u16(&r) != TC_HUD_STANDARD) r.error = r.error ? r.error : TC_ERR_STALE;
    for (i = 0; i < TC_HUD_STANDARD; i++) d.standard[i] = rd_u16(&r);
    if ((res = chunk_end(&r)) != TC_OK) goto fail;

    if (chunk_need(&c, "HEAD", &r)) { /* optional: a file from before it has no head */
        for (i = 0; i < 4; i++) d.head_cell[i] = rd_f32(&r);
        for (i = 0; i < 4; i++) d.head_uv[i] = rd_f32(&r);
        if ((res = chunk_end(&r)) != TC_OK) goto fail;
        d.has_head = true;
    }

    data_free(&h->data);
    h->data = d;
    h->loaded = true;
    return TC_OK;
fail:
    data_free(&d);
    return res;
}

/* ---- the fonts: FON_004cd540, FON_004ce110, FON_004cdd10 ---- */

static const tc_hud_font *font_of(const tc_hud *h, uint32_t k) {
    return k < h->data.font_count ? &h->data.fonts[k] : NULL;
}

static const tc_hud_text *text_of(const tc_hud *h, uint32_t k) {
    return k < h->data.text_count ? &h->data.texts[k] : NULL;
}

/* A code's glyph, else the space's (FON_004cd540); NULL: the font has neither. */
static const tc_hud_glyph *glyph_of(const tc_hud_font *f, uint8_t code) {
    const tc_hud_glyph *space = NULL;
    uint32_t k;
    for (k = 0; k < f->glyph_count; k++) {
        if (f->glyphs[k].code == code) return &f->glyphs[k];
        if (f->glyphs[k].code == ' ' && !space) space = &f->glyphs[k];
    }
    return space;
}

/* A line's width, first letter's middle to last letter's middle (FON_004ce110): n letters from s, or up to '^'. */
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

typedef struct draw { tc_hud *h; float aspect; } draw;

static void put_quad(draw *d, float x0, float z0, float x1, float z1, float depth, float anchor, const float uv[4]) {
    tc_hud *h = d->h;
    float s = CAMERA_AWAY / (CAMERA_AWAY + depth);
    float half_w = HALF_HEIGHT * d->aspect;
    float shift = anchor * (half_w - HALF_HEIGHT * 4.0f / 3.0f);
    tc_hud_quad *q;
    if (h->quad_count >= TC_HUD_MAX_QUADS) return;
    q = &h->quads[h->quad_count++];
    q->rect[0] = 0.5f + (x0 * s + shift) / (2 * half_w);
    q->rect[2] = 0.5f + (x1 * s + shift) / (2 * half_w);
    q->rect[1] = 0.5f - z1 * s / (2 * HALF_HEIGHT);
    q->rect[3] = 0.5f - z0 * s / (2 * HALF_HEIGHT);
    memcpy(q->uv, uv, sizeof(q->uv));
}

/* A text, its middle at (x, y) (FON_004cdd10 as the HUD calls it: centred both ways), `depth` away from the
 * screen's plane. Lines part at '^'. */
static void put_text(draw *d, const tc_hud_font *f, const char *s, float x, float y, float depth, float anchor) {
    int lines = 1;
    const char *p;
    float z;
    if (!f || !s) return;
    for (p = s; *p; p++) lines += *p == '^';
    z = y - 0.5f * f->line_step * (float)lines; /* FON_004cdb60: the line step is negative, so this is the top */
    while (*s) {
        size_t n = strcspn(s, "^"), k;
        float pen = x - 0.5f * line_width(f, s, n);
        for (k = 0; k < n; k++) {
            const tc_hud_glyph *g = glyph_of(f, (uint8_t)s[k]);
            if (!g) continue;
            if (g->cell[0] != g->cell[2]) {
                float zc = z + f->line_step - g->step + g->shift;
                put_quad(d, pen + g->cell[0], zc + g->cell[1], pen + g->cell[2], zc + g->cell[3], depth, anchor, g->uv);
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
}

/* GAM_00418d70's wrap: while a line is wider than 8.2, its last space that fits becomes a line break. */
static void wrap(const tc_hud_font *f, char *s) {
    size_t start = 0, i;
    long space = -1;
    for (i = 0; s[i]; i++) {
        if (s[i] == '^') { start = i + 1; space = -1; continue; }
        if (s[i] == ' ') space = (long)i;
        if (space >= 0 && line_width(f, s + start, i - start + 1) > WRAP_WIDTH) {
            s[space] = '^';
            start = (size_t)space + 1;
            space = -1;
        }
    }
}

static const tc_hud_font *font_named(const tc_hud *h, const char *name) {
    uint32_t k;
    for (k = 0; k < h->data.font_count; k++)
        if (strcmp(h->data.fonts[k].name, name) == 0) return &h->data.fonts[k];
    return NULL;
}

/* ---- the text slots: GAM_00418d70 ---- */

static const tc_hud_font *slot_font(const tc_hud *h, const tc_ai_hud_slot *s) {
    const tc_hud_text *t = text_of(h, s->text);
    if (s->kind == TC_AI_HUD_BOSS) return font_named(h, "FixHitPoints"); /* GAM_00418c00 */
    return font_of(h, s->font != 0xFF ? s->font : t ? t->font : 0xFF);
}

bool tc_hud_slot_text(const tc_hud *h, const tc_ai_hud_slot *s, uint8_t points, uint8_t max, char *out, size_t size) {
    const tc_hud_text *t;
    if (!h->loaded || !s->active || size < 2) return false;
    out[0] = 0;
    switch (s->kind) {
    case TC_AI_HUD_STRING:
        t = text_of(h, s->text);
        if (!t || !t->bytes) return false;
        snprintf(out, size, "%s", t->bytes);
        break;
    case TC_AI_HUD_NUMBER: snprintf(out, size, "%d", (int)s->value); break; /* GAM_00418a80: _itoa */
    case TC_AI_HUD_CLOCK: { /* GAM_00418b00: "%02d:%02d:%02d" of minutes, seconds, hundredths */
        int ms = (int)s->value;
        snprintf(out, size, "%02d:%02d:%02d", ms / 60000, ms % 60000 / 1000, ms % 1000 / 10);
        break;
    }
    case TC_AI_HUD_BOSS: { /* the head's letter, then a 'P' per life point left and a 'V' per one lost */
        size_t n = 0, k;
        t = text_of(h, s->head);
        out[n++] = t && t->bytes && t->bytes[0] ? t->bytes[0] : ' ';
        for (k = 0; k < points && n + 1 < size; k++) out[n++] = 'P';
        for (k = points; k < max && n + 1 < size; k++) out[n++] = 'V';
        out[n] = 0;
        break;
    }
    default: return false;
    }
    return out[0] != 0;
}

/* ---- the standard HUD: GAM_0041a720 ---- */

static float slide(float out, bool on, float in_rate, float out_rate) {
    if (on) return out >= 0.01f ? out * in_rate : 0;
    out = out == 0 ? 0.01f : out * out_rate;
    return out > GONE ? GONE : out;
}

void tc_hud_tick(tc_hud *h, tc_ai_globals *g, const tc_hud_values *now) {
    uint32_t k;
    bool all = now->show_all;
    for (k = 0; g && k < TC_AI_HUD_SLOTS; k++) { /* GAM_00418d70: out of frames, a slot goes */
        tc_ai_hud_slot *s = &g->hud[k];
        if (!s->active) continue;
        if (s->frames == 0) s->active = 0;
        else if (s->frames > 0) s->frames--;
    }
    h->now = *now;
    if (now->hit_points != h->shown_hp || now->lives != h->shown_lives) {
        h->shown_hp = now->hit_points;
        h->shown_lives = now->lives;
        h->asked |= 1;
        h->since_ms = 0;
    }
    if (now->spade != h->shown_spade) {
        h->shown_spade = now->spade;
        h->asked = (uint8_t)((h->asked & ~0x20u) | 4);
        h->since_ms = 0;
    }
    if (now->tings != h->shown_tings) {
        h->shown_tings = now->tings;
        h->asked |= 0x10;
        h->since_ms = 0;
    }
    if (now->bees != h->shown_bees) {
        h->shown_bees = now->bees;
        h->asked |= 8;
        h->since_ms = 0;
    }
    if (now->doc_serial != h->doc_serial) { /* 0x41c010: Doc's object alone */
        h->doc_serial = now->doc_serial;
        h->asked = 0x20;
        h->since_ms = 0;
    }
    h->since_ms += TICK_MS;
    h->out_life = slide(h->out_life, all || (h->asked & 1), 0.5f, 2.0f);
    h->out_lives = slide(h->out_lives, all || (h->asked & 1), 0.4f, 1.5f);
    h->out_spade = slide(h->out_spade, (all || (h->asked & 4)) && !(h->asked & 0x20), 0.5f, 2.0f);
    h->out_tings = slide(h->out_tings, all || (h->asked & 0x10), 0.5f, 2.0f);
    h->out_doc = slide(h->out_doc, (h->asked & 0x20) != 0, 0.5f, 2.0f);
    h->out_bees = slide(h->out_bees, all || (h->asked & 8), 0.5f, 2.0f);
    if (h->since_ms > SHOWN_MS) h->asked = 0;
}

static void put_standard_text(draw *d, int which, const char *s, float x, float y, float depth, float anchor) {
    const tc_hud_text *t = text_of(d->h, d->h->data.standard[which]);
    if (t) put_text(d, font_of(d->h, t->font), s ? s : t->bytes, x, y, depth, anchor);
}

static void put_standard(draw *d) {
    tc_hud *h = d->h;
    const tc_hud_values *v = &h->now;
    char num[16];
    if (h->out_life < GONE) {
        float x = -h->out_life, z = -0.14f;
        int k, hp = v->hit_points < 0 ? 0 : v->hit_points > 17 ? 17 : v->hit_points;
        int max = v->hit_points_max < 0 ? 0 : v->hit_points_max > 17 ? 17 : v->hit_points_max;
        put_standard_text(d, TC_HUD_THERMOMETER, NULL, -2.47f + x, 0.5f, -1.99f, -1);
        for (k = 0; k < hp; k++) /* GAM_0041a360: the mercury, a drop every 0.04 up to the point's step */
            for (; z < LIFE_STEPS[k]; z += 0.04f) put_standard_text(d, TC_HUD_MERCURY, NULL, -2.48f + x, z, -2.0f, -1);
        for (k = 0; k < max; k++) put_standard_text(d, TC_HUD_GRADUATION, NULL, -2.39f + x, LIFE_STEPS[k], -2.0f, -1);
    }
    if (h->out_life < GONE && h->data.has_head) { /* Ed's head, at (-4.33, 2.62) (_DAT_004d2144, VStack_70.z) */
        const float *c = h->data.head_cell;
        float x = -4.33f - h->out_life, z = 2.62f;
        put_quad(d, x + c[0], z + c[1], x + c[2], z + c[3], 0, -1, h->data.head_uv);
    }
    if (h->out_lives < GONE) {
        snprintf(num, sizeof(num), "x%d", (int)v->lives);
        put_standard_text(d, TC_HUD_LIVES, num, -3.5f - h->out_lives, 2.87f, 0, -1);
    }
    if (h->left_only) return; /* an extension's (tc_hud.left_only): the right side clean */
    if (h->out_spade < GONE && v->spade >= 0) {
        snprintf(num, sizeof(num), "%d", (int)v->spade);
        put_standard_text(d, TC_HUD_SPADE, NULL, 13.8f, h->out_spade + 9.5f, 12.0f, 1);
        put_standard_text(d, TC_HUD_SPADE_NUMBER, num, 4.0f, h->out_spade + 2.3f, 0, 1);
    }
    if (h->out_tings < GONE && v->tings >= 0) {
        snprintf(num, sizeof(num), "%d", (int)v->tings);
        put_standard_text(d, TC_HUD_TINGS, NULL, h->out_tings + 8.0f, 2.6f, 5.0f, 1);
        put_standard_text(d, TC_HUD_TINGS_NUMBER, num, h->out_tings + 4.0f, 0.5f, 0, 1);
    }
    if (h->out_doc < GONE && v->doc_slot >= 1 && v->doc_slot <= 6) {
        static const char LETTER[7] = {0, 'I', 'K', 'G', 'H', 'N', 'O'}; /* 0x41c010: domino, helicon, rock, pig, feather, spring */
        char icon[2] = {LETTER[v->doc_slot], 0};
        snprintf(num, sizeof(num), "%02d", (int)v->doc_count);
        put_standard_text(d, TC_HUD_DOC, icon, 9.0f, h->out_doc + 6.5f, 6.0f, 1);
        put_standard_text(d, TC_HUD_DOC_NUMBER, num, 4.0f, h->out_doc + 2.3f, 0, 1);
    }
    if (h->out_bees < GONE && v->bees >= 0) {
        snprintf(num, sizeof(num), "%d", (int)v->bees);
        put_standard_text(d, TC_HUD_BEE, NULL, h->out_bees + 2.3f, -0.1f, -2.0f, 1);
        put_standard_text(d, TC_HUD_BEE_NUMBER, num, h->out_bees + 4.0f, -1.0f, 0, 1);
    }
}

uint32_t tc_hud_build(tc_hud *h, const tc_ai_globals *g, const uint8_t *boss_points, const uint8_t *boss_max,
                      bool standard, float aspect) {
    draw d;
    uint32_t k;
    h->quad_count = 0;
    if (!h->loaded || aspect <= 0) return 0;
    d.h = h;
    d.aspect = aspect;
    for (k = 0; g && k < TC_AI_HUD_SLOTS; k++) {
        const tc_ai_hud_slot *s = &g->hud[k];
        const tc_hud_font *f = slot_font(h, s);
        char text[256];
        if (!f || !tc_hud_slot_text(h, s, boss_points ? boss_points[k] : 0, boss_max ? boss_max[k] : 0, text, sizeof(text)))
            continue;
        if (s->kind != TC_AI_HUD_BOSS) wrap(f, text);
        if (s->backdrop && h->quad_count < TC_HUD_MAX_QUADS) { /* the text window's box: 5-95 % by 75-95 % of the 4:3 frame */
            tc_hud_quad *q = &h->quads[h->quad_count++];
            float half = 0.45f * (4.0f / 3.0f) / aspect;
            q->rect[0] = 0.5f - half;
            q->rect[2] = 0.5f + half;
            q->rect[1] = 0.75f;
            q->rect[3] = 0.95f;
            memcpy(q->uv, h->data.panel_uv, sizeof(q->uv));
        }
        put_text(&d, f, text, s->x, s->y, 0, s->x < -SLOT_ANCHOR_X ? -1.0f : s->x > SLOT_ANCHOR_X ? 1.0f : 0.0f);
    }
    if (standard) put_standard(&d);
    return h->quad_count;
}
