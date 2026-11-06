/* Speech: the text window (docs/ENGINE.md "Speech"). Read from the exe; the port's choices are marked "port". */
#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_alloc.h"
#include "tonic/tc_speech.h"

#define WRAP_WIDTH 8.2f        /* DAT_004d20c0 */
#define FIRST_LINE 0.8f        /* GAM_00417e90 */
#define LONG_START -0.75f      /* DAT_004d20c4: more than five lines start below the window and rise */
#define LONG_END -0.7f         /* DAT_004d20c8 */
#define LONG_SPEED 0.007f      /* 0x3BE56042 */
#define SCROLL_SPEED 0.01f     /* the player's: 0x3C23D70A */
#define MAX_LINES 5
#define UNITS_HIGH 2.0f        /* port: the window's height in font units: five lines of 0.3 from 0.8 fill it */

/* FON_004cd540 / 004cd5a0: a code's glyph; unknown codes take the space's. */
static const tc_glyph *glyph_of(const tc_speech_data *d, uint8_t code) {
    const tc_glyph *space = NULL;
    uint32_t i;
    for (i = 0; i < d->glyph_count; i++) {
        if (d->glyphs[i].code == code) return &d->glyphs[i];
        if (d->glyphs[i].code == ' ') space = &d->glyphs[i];
    }
    return space;
}

float tc_speech_line_width(const tc_speech_data *d, const char *text, uint32_t from) {
    const char *t = text + from;
    float w = 0;
    uint32_t i;
    for (i = 0; t[i] && t[i] != '^'; i++) {
        const tc_glyph *g = glyph_of(d, (uint8_t)t[i]);
        bool last = !t[i + 1] || t[i + 1] == '^';
        float adv = g ? g->advance : 0;
        if (i == 0 && last) w = 0;
        else if (last) w += adv * 0.5f;
        else if (i == 0) w += d->spacing + adv * 0.5f;
        else w += adv + d->spacing;
    }
    return w;
}

void tc_speech_init(tc_speech *s, const tc_speech_data *data) {
    memset(s, 0, sizeof(*s));
    s->data = data;
}

void tc_speech_free(tc_speech *s) {
    tc_free(s->pixels);
    s->pixels = NULL;
    s->dialogue = NULL;
}

/* GAM_00417e90: every '^' to a space, then the longest lines that fit, broken at spaces. */
static void wrap(tc_speech *s) {
    uint32_t n = (uint32_t)strlen(s->text), i, start = 0;
    s->lines = 1;
    for (i = 0; i < n; i++)
        if (s->text[i] == '^') s->text[i] = ' ';
    while (tc_speech_line_width(s->data, s->text, start) > WRAP_WIDTH) {
        uint32_t k = n;
        bool broke = false;
        while (k > start + 1) {
            k--;
            if (s->text[k] != ' ') continue;
            s->text[k] = '^';
            if (tc_speech_line_width(s->data, s->text, start) <= WRAP_WIDTH) { broke = true; break; }
            s->text[k] = ' ';
        }
        if (!broke) break; /* one word wider than the window */
        s->lines++;
        start = k + 1;
    }
}

void tc_speech_start(tc_speech *s, const tc_dialogue *d) {
    float step = 0;
    uint32_t i;
    for (i = 0; i < s->data->glyph_count; i++)
        if (s->data->glyphs[i].code == ' ') step = s->data->glyphs[i].step;
    s->dialogue = d;
    s->time = 0;
    s->scrolled_out = false;
    s->text[0] = 0;
    if (d->text) {
        size_t n = strlen(d->text);
        if (n >= sizeof(s->text)) n = sizeof(s->text) - 1;
        memcpy(s->text, d->text, n);
        s->text[n] = 0;
        wrap(s);
    }
    s->y = s->y_min = s->y_max = FIRST_LINE;
    s->speed = 0;
    if (s->lines > MAX_LINES) {
        s->y = LONG_START;
        s->y_max = LONG_END - step * (float)s->lines;
        s->speed = LONG_SPEED;
    }
    s->dirty = true;
}

void tc_speech_end(tc_speech *s) {
    s->dialogue = NULL;
    s->dirty = false;
}

void tc_speech_step(tc_speech *s, float dt, int scroll) {
    float frames = dt * 30, y = s->y; /* the exe moves it per frame */
    if (!s->dialogue) return;
    s->time += dt;
    if (!s->scrolled_out) { /* GAM_004178c0 */
        s->y += s->speed * frames;
        if (s->y > s->y_max) {
            s->y = s->y_max;
            s->scrolled_out = true;
        }
    } else if (scroll) {
        s->y += (float)scroll * SCROLL_SPEED * frames;
        if (s->y < s->y_min) s->y = s->y_min;
        if (s->y > s->y_max) s->y = s->y_max;
    }
    if (s->y != y) s->dirty = true;
}

/* One glyph, the atlas sampled between its texels, alpha over what is there. */
static void draw_glyph(tc_speech *s, const tc_glyph *g, float cx, float cy, float scale) {
    const tc_speech_data *d = s->data;
    int x0 = (int)floorf(cx - g->half_w * scale), x1 = (int)ceilf(cx + g->half_w * scale);
    int y0 = (int)floorf(cy - g->half_h * scale), y1 = (int)ceilf(cy + g->half_h * scale), x, y;
    float du = (g->u1 - g->u0) / (2 * g->half_w * scale), dv = (g->v1 - g->v0) / (2 * g->half_h * scale);
    if (!(g->half_w > 0) || !(g->half_h > 0)) return;
    for (y = y0 < 0 ? 0 : y0; y < y1 && y < (int)TC_SPEECH_PANEL_H; y++) {
        /* v runs up the atlas, as the font's height does (the data's rows are top first) */
        float v = g->v1 - ((float)y + 0.5f - (cy - g->half_h * scale)) * dv;
        float ty = (1 - v) * (float)d->atlas_h - 0.5f;
        int r0 = (int)floorf(ty);
        float fy = ty - (float)r0;
        for (x = x0 < 0 ? 0 : x0; x < x1 && x < (int)TC_SPEECH_PANEL_W; x++) {
            float u = g->u0 + ((float)x + 0.5f - (cx - g->half_w * scale)) * du;
            float tx = u * (float)d->atlas_w - 0.5f, c[4] = {0, 0, 0, 0}, fx, a;
            int c0 = (int)floorf(tx), k, i, j;
            uint8_t *o = &s->pixels[((size_t)y * TC_SPEECH_PANEL_W + (size_t)x) * 4];
            if (u < g->u0 || u > g->u1 || v < g->v0 || v > g->v1) continue;
            fx = tx - (float)c0;
            for (j = 0; j < 2; j++)
                for (i = 0; i < 2; i++) {
                    int px = c0 + i, py = r0 + j;
                    const uint8_t *t;
                    float w = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                    if (px < 0) px = 0;
                    if (py < 0) py = 0;
                    if (px >= d->atlas_w) px = d->atlas_w - 1;
                    if (py >= d->atlas_h) py = d->atlas_h - 1;
                    t = &d->atlas[((size_t)py * d->atlas_w + (size_t)px) * 4];
                    for (k = 0; k < 3; k++) c[k] += w * (float)t[k] * (float)t[3] / 255;
                    c[3] += w * (float)t[3];
                }
            a = c[3] / 255;
            if (a <= 0) continue;
            for (k = 0; k < 3; k++) o[k] = (uint8_t)(c[k] + (float)o[k] * (1 - a) + 0.5f);
            o[3] = (uint8_t)(c[3] + (float)o[3] * (1 - a) + 0.5f);
        }
    }
}

/* FON_004cdd10, one line: centred; a glyph's height from its line by the space's and its own step and its shift. Returns
 * where the line ended. */
static uint32_t draw_line(tc_speech *s, const char *text, uint32_t from, float line, float step, float scale) {
    const tc_speech_data *d = s->data;
    float x = -0.5f * tc_speech_line_width(d, text, from);
    uint32_t k = from;
    while (text[k] && text[k] != '^') {
        const tc_glyph *g = glyph_of(d, (uint8_t)text[k]);
        const tc_glyph *next = text[k + 1] && text[k + 1] != '^' ? glyph_of(d, (uint8_t)text[k + 1]) : NULL;
        if (g) {
            float z = step + line - g->step + g->shift;
            draw_glyph(s, g, (float)TC_SPEECH_PANEL_W / 2 + x * scale, (float)TC_SPEECH_PANEL_H / 2 - z * scale, scale);
            x += d->spacing + g->advance * 0.5f;
        }
        if (next) x += next->advance * 0.5f;
        k++;
    }
    return k;
}

bool tc_speech_render(tc_speech *s) {
    const tc_speech_data *d = s->data;
    float scale = (float)TC_SPEECH_PANEL_H / UNITS_HIGH, line = s->y, step = 0;
    uint32_t i, n = TC_SPEECH_PANEL_W * TC_SPEECH_PANEL_H;
    if (!s->dialogue || !s->text[0] || !d->atlas) return false;
    if (!s->dirty && s->pixels && !s->prompting) return true;
    if (!s->pixels && !(s->pixels = (uint8_t *)tc_alloc((size_t)n * 4))) return false;
    s->prompting = false;
    for (i = 0; i < n; i++) memcpy(&s->pixels[(size_t)i * 4], d->panel, 4); /* the backdrop: LipSync.tga, one colour */
    for (i = 0; i < d->glyph_count; i++)
        if (d->glyphs[i].code == ' ') step = d->glyphs[i].step;
    {
        uint32_t from = 0;
        for (;;) {
            uint32_t k = draw_line(s, s->text, from, line, step, scale);
            if (!s->text[k]) break;
            from = k + 1;
            line += step;
        }
    }
    s->dirty = false;
    s->version++;
    return true;
}

bool tc_speech_render_prompt(tc_speech *s, const char *text) {
    const tc_speech_data *d = s->data;
    float scale = (float)TC_SPEECH_PANEL_H / UNITS_HIGH, step = 0, half;
    int x0, x1, y0, y1, x, y;
    uint32_t i;
    if (!d->atlas) return false;
    if (s->prompting && s->pixels) return true;
    if (!s->pixels && !(s->pixels = (uint8_t *)tc_alloc((size_t)TC_SPEECH_PANEL_W * TC_SPEECH_PANEL_H * 4))) return false;
    memset(s->pixels, 0, (size_t)TC_SPEECH_PANEL_W * TC_SPEECH_PANEL_H * 4);
    for (i = 0; i < d->glyph_count; i++)
        if (d->glyphs[i].code == ' ') step = d->glyphs[i].step;
    half = (0.5f * tc_speech_line_width(d, text, 0) + 0.35f) * scale;
    x0 = (int)((float)TC_SPEECH_PANEL_W / 2 - half), x1 = (int)((float)TC_SPEECH_PANEL_W / 2 + half);
    y0 = (int)((float)TC_SPEECH_PANEL_H / 2 - 0.3f * scale), y1 = (int)((float)TC_SPEECH_PANEL_H / 2 + 0.3f * scale);
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) memcpy(&s->pixels[((size_t)y * TC_SPEECH_PANEL_W + (size_t)x) * 4], d->panel, 4);
    draw_line(s, text, 0, -0.5f * step, step, scale); /* the line's middle on the panel's */
    s->prompting = true;
    s->version++;
    return true;
}
