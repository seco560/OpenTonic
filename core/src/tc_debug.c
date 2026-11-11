/* The debug menu: tc_debug.h. A screen of text cells is laid out first (layout), then drawn (tc_debug_render) or handed
 * out as text (tc_debug_text). */
#include "tonic/tc_debug.h"
#include "tc_reader.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The glyphs of ' ' .. '~', 8 rows each, bit 0 the left pixel: font8x8_basic by Daniel Hepper, from Marcel Sondaar's
 * copy of the IBM VGA font. Public domain. */
static const uint8_t font[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*   */
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, /* ! */
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* " */
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, /* # */
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, /* $ */
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, /* % */
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, /* & */
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' */
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, /* ( */
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, /* ) */
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, /* * */
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, /* + */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, /* , */
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, /* . */
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, /* / */
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, /* 0 */
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, /* 1 */
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, /* 2 */
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, /* 3 */
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, /* 4 */
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, /* 5 */
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, /* 6 */
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, /* 7 */
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, /* 8 */
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, /* 9 */
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, /* : */
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, /* ; */
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, /* < */
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, /* = */
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, /* > */
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, /* ? */
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, /* @ */
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, /* A */
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, /* B */
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, /* C */
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, /* D */
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, /* E */
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, /* F */
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, /* G */
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, /* H */
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, /* I */
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, /* J */
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, /* K */
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, /* L */
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, /* M */
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, /* N */
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, /* O */
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, /* P */
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, /* Q */
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, /* R */
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, /* S */
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, /* T */
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, /* U */
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, /* V */
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, /* W */
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, /* X */
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, /* Y */
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, /* Z */
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, /* [ */
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, /* backslash */
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, /* ] */
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, /* ^ */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, /* _ */
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ` */
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, /* a */
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, /* b */
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, /* c */
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00}, /* d */
    {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, /* e */
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00}, /* f */
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, /* g */
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, /* h */
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, /* i */
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, /* j */
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, /* k */
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, /* l */
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, /* m */
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, /* n */
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, /* o */
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, /* p */
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, /* q */
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, /* r */
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, /* s */
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, /* t */
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, /* u */
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, /* v */
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, /* w */
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, /* x */
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, /* y */
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, /* z */
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, /* { */
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, /* | */
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, /* } */
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ~ */
};

#define COLS (TC_DEBUG_PANEL_W / 8u)   /* 100 */
#define TOP 5u                         /* pixels above the first line */
#define LINES ((TC_DEBUG_PANEL_H - 2u * TOP) / 10u) /* 47: a glyph and two pixels each */
#define LEFT_W 41u                     /* the rows; the uses start after it */
#define PANE_X 43u
#define PANE_W (COLS - PANE_X - 1u)
#define VALUE_COUNT 31u

enum { A_TEXT, A_DIM, A_TITLE, A_ON, A_OFF, A_SELECTED, A_COUNT };
static const uint8_t colours[A_COUNT][3] = {
    {235, 235, 235}, {150, 156, 176}, {255, 208, 96}, {120, 230, 130}, {240, 130, 120}, {255, 255, 255}};

typedef struct screen {
    char ch[LINES][COLS + 1];
    uint8_t attr[LINES][COLS];
    int bar; /* the chosen row's line, -1 none */
} screen;

const uint16_t tc_debug_power_masks[TC_DEBUG_POWER_ROWS] = {7, 8, 0x70, 0x80, 0x100, 0x200};
/* What each is: only flying is read (docs/ENGINE.md "Game progress"). */
static const char *const power_names[TC_DEBUG_POWER_ROWS] = {"", "", "", "flying", "", ""};
static const char *const use_names[TC_DEBUG_USES] = {"tests", "gives", "takes", "sets", "reads", "adds"};
static const uint8_t use_order[TC_DEBUG_USES] = {TC_DEBUG_USE_GIVES, TC_DEBUG_USE_TAKES, TC_DEBUG_USE_SETS,
                                                 TC_DEBUG_USE_ADDS, TC_DEBUG_USE_TESTS, TC_DEBUG_USE_READS};

uint32_t tc_debug_row_count(uint32_t page) {
    return page == TC_DEBUG_POWERS ? TC_DEBUG_ROW_FLAGS + TC_DEBUG_POWER_ROWS + TC_DEBUG_POWER_ACTIONS
                                   : TC_DEBUG_ROW_FLAGS + VALUE_COUNT + TC_DEBUG_VALUE_ACTIONS;
}

void tc_debug_free(tc_debug *d) {
    uint32_t i;
    for (i = 0; i < d->use_count; i++) {
        tc_free(d->uses[i].level);
        tc_free(d->uses[i].model);
        tc_free(d->uses[i].instance);
    }
    tc_free(d->uses);
    tc_free(d->pixels);
    memset(d, 0, sizeof(*d));
}

tc_result tc_debug_load_survey(tc_debug *d, const void *data, size_t size) {
    chunks c;
    reader r;
    tc_debug_use *uses;
    uint32_t n, i;
    tc_result res = chunks_open(&c, (const uint8_t *)data, size, "TTPG");
    if (res != TC_OK) return res;
    chunk_need(&c, "USES", &r);
    n = rd_u32(&r);
    uses = (tc_debug_use *)rd_array(&r, n, sizeof(*uses), 14);
    for (i = 0; uses && i < n; i++) {
        uses[i].kind = rd_u8(&r);
        uses[i].use = rd_u8(&r);
        uses[i].key = rd_i16(&r);
        uses[i].calls = rd_u16(&r);
        uses[i].instances = rd_u16(&r);
        uses[i].level = rd_string(&r);
        uses[i].model = rd_string(&r);
        uses[i].instance = rd_string(&r);
        if (uses[i].use >= TC_DEBUG_USES) r.error = TC_ERR_STALE;
    }
    res = chunk_end(&r);
    for (i = 0; uses && i < n; i++)
        if (res != TC_OK || !uses[i].level || !uses[i].model || !uses[i].instance) {
            res = res != TC_OK ? res : TC_ERR_OUT_OF_MEMORY;
            break;
        }
    if (res != TC_OK) { /* keep what there was */
        for (i = 0; uses && i < n; i++) {
            tc_free(uses[i].level);
            tc_free(uses[i].model);
            tc_free(uses[i].instance);
        }
        tc_free(uses);
        return res;
    }
    for (i = 0; i < d->use_count; i++) {
        tc_free(d->uses[i].level);
        tc_free(d->uses[i].model);
        tc_free(d->uses[i].instance);
    }
    tc_free(d->uses);
    d->uses = uses;
    d->use_count = uses ? n : 0;
    d->drawn = 0;
    return TC_OK;
}

void tc_debug_set_open(tc_debug *d, bool open, uint32_t buttons) {
    d->open = open;
    d->prev = buttons;
    d->hold = 0;
}

/* ---- the moves ---- */

void tc_debug_input(tc_debug *d, uint32_t buttons, float dt, tc_ai_globals *g) {
    const uint32_t dirs = buttons & 15u;
    uint32_t fire = buttons & ~d->prev, rows = tc_debug_row_count(d->page), row, flags, act;
    int step;
    if (dirs && dirs == (d->prev & 15u)) { /* a held direction repeats: after 0.4 s, 12 times a second */
        d->hold += dt;
        if (d->hold > 0.4f) {
            d->hold -= 1.0f / 12;
            fire |= dirs;
        }
    } else d->hold = 0;
    d->prev = buttons;
    if (!d->open || !fire) return;
    if (fire & TC_DEBUG_UP) d->row[d->page] = (uint8_t)((d->row[d->page] + rows - 1) % rows);
    if (fire & TC_DEBUG_DOWN) d->row[d->page] = (uint8_t)((d->row[d->page] + 1) % rows);
    row = d->row[d->page];
    step = (fire & TC_DEBUG_RIGHT ? 1 : 0) - (fire & TC_DEBUG_LEFT ? 1 : 0);
    if (row == TC_DEBUG_ROW_PAGE) {
        if (step || (fire & TC_DEBUG_OK)) d->page = (uint8_t)((d->page + 1) % TC_DEBUG_PAGES);
        return;
    }
    flags = d->page == TC_DEBUG_POWERS ? TC_DEBUG_POWER_ROWS : VALUE_COUNT;
    row -= TC_DEBUG_ROW_FLAGS;
    if (row < flags) {
        if (!g) return;
        if (d->page == TC_DEBUG_POWERS) {
            const uint16_t m = tc_debug_power_masks[row];
            const bool on = (g->powers & m) == m;
            if (step > 0 || ((fire & TC_DEBUG_OK) && !on)) g->powers |= m;
            else if (step < 0 || (fire & TC_DEBUG_OK)) g->powers = (uint16_t)(g->powers & ~m);
        } else {
            int v = g->values[row];
            if (fire & TC_DEBUG_OK) v = v ? 0 : 1;
            v += step * ((buttons & TC_DEBUG_FAST) ? 10 : 1);
            g->values[row] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
        return;
    }
    act = row - flags;
    if (d->page == TC_DEBUG_VALUES) /* the same actions, without "all on" */
        act = act == TC_DEBUG_ACT_ZERO ? 100 : act == TC_DEBUG_ACT_VALUE_SCOPE ? TC_DEBUG_ACT_SCOPE : TC_DEBUG_ACT_RELOAD;
    if (act == TC_DEBUG_ACT_SCOPE) {
        if (step || (fire & TC_DEBUG_OK)) d->all_levels = !d->all_levels;
        return;
    }
    if (!(fire & TC_DEBUG_OK)) return;
    if (act == TC_DEBUG_ACT_RELOAD) d->reload = true;
    if (!g) return;
    if (act == TC_DEBUG_ACT_ALL_ON) g->powers = TC_AI_POWERS_ALL;
    if (act == TC_DEBUG_ACT_STORY_START) g->powers = 0;
    if (act == TC_DEBUG_ACT_STORY_START || act == 100) memset(g->values, 0, sizeof(g->values));
}

/* ---- the screen ---- */

static void put(screen *s, uint32_t line, uint32_t col, uint32_t width, uint8_t attr, const char *fmt, ...) {
    char text[160];
    va_list ap;
    uint32_t i;
    if (line >= LINES || col >= COLS) return;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (width > COLS - col) width = COLS - col;
    for (i = 0; text[i] && i < width; i++) {
        const unsigned char c = (unsigned char)text[i];
        s->ch[line][col + i] = c >= 32 && c < 127 ? (char)c : '?';
        s->attr[line][col + i] = attr;
    }
}

static int same_level(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        const int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) return 0;
    }
    return !*a && !*b;
}

/* The pane's lines: appended to, wrapped at the pane's width with the continuation set in. */
typedef struct pane { char line[96][PANE_W + 1]; uint8_t attr[96]; uint32_t count, dropped; } pane;

static void pane_add(pane *p, uint8_t attr, uint32_t indent, const char *text) {
    size_t left = strlen(text);
    bool first = true;
    if (!left) text = " ", left = 1;
    while (left) {
        const uint32_t room = first ? PANE_W : PANE_W - indent;
        size_t n = left <= room ? left : room;
        if (n < left) { /* break at a space when there is one in the second half */
            size_t k = n;
            while (k > room / 2 && text[k] != ' ') k--;
            if (k > room / 2) n = k;
        }
        if (p->count >= 96) { p->dropped++; return; }
        memset(p->line[p->count], ' ', first ? 0 : indent);
        memcpy(p->line[p->count] + (first ? 0 : indent), text, n);
        p->line[p->count][(first ? 0 : indent) + n] = 0;
        p->attr[p->count++] = attr;
        text += n;
        left -= n;
        while (left && *text == ' ') text++, left--;
        first = false;
    }
}

static void append(char *out, size_t size, const char *fmt, ...) {
    const size_t n = strlen(out);
    va_list ap;
    if (n + 1 >= size) return;
    va_start(ap, fmt);
    vsnprintf(out + n, size - n, fmt, ap);
    va_end(ap);
}

/* The uses of one flag (kind, key) into the pane. This level's: a line per model. Every level's: a line per model with
 * its levels. Returns how many rows of the survey matched. */
static uint32_t pane_uses(pane *p, const tc_debug *d, uint8_t kind, int key, const char *level, bool all) {
    uint32_t u, i, j, found = 0;
    char text[512];
    for (u = 0; u < TC_DEBUG_USES; u++) {
        const uint8_t use = use_order[u];
        for (i = 0; i < d->use_count; i++) {
            const tc_debug_use *a = &d->uses[i];
            const bool fix = a->level[0] == '*';
            if (a->kind != kind || a->key != key || a->use != use) continue;
            if (!all) {
                if (!fix && !(level && same_level(a->level, level))) continue;
                found++;
                text[0] = 0;
                append(text, sizeof(text), "%-5s  %s", use_names[use], a->model);
                if (a->instances > 1) append(text, sizeof(text), " x%u", (unsigned)a->instances);
                if (strcmp(a->model, a->instance) != 0)
                    append(text, sizeof(text), " (%s%s)", a->instance, a->instances > 1 ? ", ..." : "");
                if (fix) append(text, sizeof(text), " [in every level]");
                pane_add(p, use == TC_DEBUG_USE_TESTS || use == TC_DEBUG_USE_READS ? A_TEXT : A_ON, 7, text);
                continue;
            }
            for (j = 0; j < i; j++) /* this model's line was written at its first row */
                if (d->uses[j].kind == kind && d->uses[j].key == key && d->uses[j].use == use &&
                    strcmp(d->uses[j].model, a->model) == 0) break;
            found++;
            if (j < i) continue;
            text[0] = 0;
            append(text, sizeof(text), "%-5s  %s:", use_names[use], a->model);
            for (j = i; j < d->use_count; j++) {
                const tc_debug_use *b = &d->uses[j];
                if (b->kind != kind || b->key != key || b->use != use || strcmp(b->model, a->model) != 0) continue;
                append(text, sizeof(text), "%s %s", j == i ? "" : ",", b->level[0] == '*' ? "every level" : b->level);
                if (b->instances > 1) append(text, sizeof(text), " x%u", (unsigned)b->instances);
            }
            pane_add(p, use == TC_DEBUG_USE_TESTS || use == TC_DEBUG_USE_READS ? A_TEXT : A_ON, 7, text);
        }
    }
    return found;
}

static void layout(const tc_debug *d, const tc_ai_globals *g, const char *level, screen *s) {
    const uint32_t page = d->page, rows = tc_debug_row_count(page), chosen = d->row[page];
    const uint32_t flags = page == TC_DEBUG_POWERS ? TC_DEBUG_POWER_ROWS : VALUE_COUNT;
    static pane p; /* big, and the menu is drawn from one thread */
    uint32_t i, line;
    memset(s->ch, ' ', sizeof(s->ch));
    memset(s->attr, A_TEXT, sizeof(s->attr));
    for (i = 0; i < LINES; i++) s->ch[i][COLS] = 0;
    memset(&p, 0, sizeof(p));
    s->bar = -1;

    put(s, 0, 1, 60, A_TITLE, "DEBUG MENU: game progress");
    if (g) put(s, 0, 44, 56, A_DIM, "level %s   powers 0x%04X   lives %u", level && level[0] ? level : "?",
               (unsigned)g->powers, (unsigned)g->lives);
    else put(s, 0, 44, 56, A_OFF, "this level runs no game state: nothing to set");

    for (i = 0; i < rows; i++) {
        const bool sel = i == chosen;
        const uint8_t a = sel ? A_SELECTED : A_TEXT;
        line = 2 + i + (i >= TC_DEBUG_ROW_FLAGS ? 1 : 0) + (i >= TC_DEBUG_ROW_FLAGS + flags ? 1 : 0);
        if (sel) s->bar = (int)line;
        put(s, line, 1, 1, a, sel ? ">" : " ");
        if (i == TC_DEBUG_ROW_PAGE) {
            put(s, line, 3, LEFT_W - 3, a, "Page:  < %s >", page == TC_DEBUG_POWERS ? "Powers" : "Save values");
        } else if (i - TC_DEBUG_ROW_FLAGS < flags && page == TC_DEBUG_POWERS) {
            const uint32_t k = i - TC_DEBUG_ROW_FLAGS;
            const uint16_t m = tc_debug_power_masks[k], have = g ? (uint16_t)(g->powers & m) : 0;
            put(s, line, 3, 14, a, "power 0x%04X", (unsigned)m);
            put(s, line, 17, 5, !g ? A_DIM : have == m ? A_ON : A_OFF, !g ? "-" : have == m ? "ON" : have ? "part" : "off");
            put(s, line, 23, LEFT_W - 23, sel ? A_SELECTED : A_DIM, "%s", power_names[k]);
        } else if (i - TC_DEBUG_ROW_FLAGS < flags) {
            const uint32_t k = i - TC_DEBUG_ROW_FLAGS;
            put(s, line, 3, 10, a, "value %2u", (unsigned)k);
            if (g) put(s, line, 13, 8, g->values[k] ? A_ON : A_DIM, "%6d", (int)g->values[k]);
            else put(s, line, 13, 8, A_DIM, "     -");
            put(s, line, 23, LEFT_W - 23, sel ? A_SELECTED : A_DIM, "%s", k == 0 ? "the bees" : "");
        } else {
            uint32_t act = i - TC_DEBUG_ROW_FLAGS - flags;
            if (page == TC_DEBUG_VALUES)
                act = act == TC_DEBUG_ACT_ZERO ? 100 : act == TC_DEBUG_ACT_VALUE_SCOPE ? TC_DEBUG_ACT_SCOPE : TC_DEBUG_ACT_RELOAD;
            put(s, line, 3, LEFT_W - 3, a,
                act == TC_DEBUG_ACT_ALL_ON ? "All powers on" :
                act == TC_DEBUG_ACT_STORY_START ? "Story start: no powers, values 0" :
                act == 100 ? "All values to 0" :
                act == TC_DEBUG_ACT_SCOPE ? (d->all_levels ? "Uses shown:  < every level >" : "Uses shown:  < this level >") :
                "Reload the level with these");
        }
    }

    /* the pane: what the chosen row is, and who uses it */
    if (chosen == TC_DEBUG_ROW_PAGE) {
        pane_add(&p, A_TEXT, 0, "Left / right changes the page.");
        pane_add(&p, A_DIM, 0, "");
        pane_add(&p, A_DIM, 0, "A switch changes the game's state at once. Most scripts test a flag once, when the "
                               "level starts: to see a story state, set it and reload the level.");
        pane_add(&p, A_DIM, 0, "");
        pane_add(&p, A_DIM, 0, "The lines here name the characters whose scripts hold a call with this flag. A script "
                               "holding a call does not mean it runs.");
    } else if (chosen - TC_DEBUG_ROW_FLAGS < flags) {
        const uint32_t k = chosen - TC_DEBUG_ROW_FLAGS;
        const uint8_t kind = page == TC_DEBUG_POWERS ? 0 : 1;
        const int key = page == TC_DEBUG_POWERS ? tc_debug_power_masks[k] : (int)k;
        char head[96];
        uint32_t found;
        if (page == TC_DEBUG_POWERS)
            snprintf(head, sizeof(head), "Power 0x%04X: %s", (unsigned)key,
                     power_names[k][0] ? power_names[k] : "what it is has not been read");
        else snprintf(head, sizeof(head), "Save value %u%s", (unsigned)k,
                      k == 0 ? ": the bees" : k <= 6 ? ": one of Doc's six things (inferred)" : "");
        pane_add(&p, A_TITLE, 0, head);
        pane_add(&p, A_DIM, 0, d->all_levels ? "Its uses in every level:" :
                               "Its uses in this level (and by Ed and the others in every level):");
        pane_add(&p, A_DIM, 0, "");
        if (!d->use_count) pane_add(&p, A_OFF, 0, "No survey beside these levels (progress.ttpg: the conversion makes it, for retail).");
        else {
            found = pane_uses(&p, d, kind, key, level, d->all_levels);
            if (!found) pane_add(&p, A_DIM, 0, "No script uses it with a constant.");
            if (kind == 1) { /* the scripts that work the slot out could mean any */
                pane_add(&p, A_DIM, 0, "");
                pane_add(&p, A_DIM, 0, "By a slot the script works out (any slot):");
                if (!pane_uses(&p, d, kind, -1, level, d->all_levels)) pane_add(&p, A_DIM, 0, "none");
            }
        }
    } else {
        uint32_t act = chosen - TC_DEBUG_ROW_FLAGS - flags;
        if (page == TC_DEBUG_VALUES)
            act = act == TC_DEBUG_ACT_ZERO ? 100 : act == TC_DEBUG_ACT_VALUE_SCOPE ? TC_DEBUG_ACT_SCOPE : TC_DEBUG_ACT_RELOAD;
        pane_add(&p, A_TEXT, 0,
                 act == TC_DEBUG_ACT_ALL_ON ? "Every power on (0xFFFF): how the port has started every level so far." :
                 act == TC_DEBUG_ACT_STORY_START ? "No powers and every save value 0. Then reload the level: expect every "
                                                   "gate shut and every missing-power bug at once." :
                 act == 100 ? "Every save value to 0. The powers stay." :
                 act == TC_DEBUG_ACT_SCOPE ? "Whose uses the lines here list: this level's characters, or every level's." :
                 "Starts this level again from Ed's start place, with the powers, save values and lives as they are "
                 "now. Its scripts then make their first tests against them.");
    }
    for (i = 0; i < p.count && 2 + i < LINES - 3; i++) put(s, 2 + i, PANE_X, PANE_W, p.attr[i], "%s", p.line[i]);
    if (i < p.count || p.dropped)
        put(s, LINES - 3, PANE_X, PANE_W, A_OFF, "... %u more lines not shown", (unsigned)(p.count - i + p.dropped));
    put(s, LINES - 1, 1, COLS - 2, A_DIM,
        "Up / down: choose    Left / right: change (Shift: by 10)    Jump or Action: switch    F4: close");
}

static uint64_t state_hash(const tc_debug *d, const tc_ai_globals *g, const char *level) {
    uint64_t h = 7;
    uint32_t s[4] = {d->page, d->row[0], d->row[1], (uint32_t)d->all_levels | (uint32_t)d->use_count << 1};
    h = tc_hash64(h, s, sizeof(s));
    if (g) {
        h = tc_hash64(h, &g->powers, sizeof(g->powers));
        h = tc_hash64(h, &g->lives, sizeof(g->lives));
        h = tc_hash64(h, g->values, sizeof(g->values));
    }
    if (level) h = tc_hash64(h, level, strlen(level));
    return h | 1;
}

bool tc_debug_render(tc_debug *d, const tc_ai_globals *g, const char *level) {
    static screen s;
    const uint64_t h = state_hash(d, g, level);
    uint32_t line, col, y, x;
    if (!d->pixels) {
        d->pixels = (uint8_t *)tc_alloc((size_t)TC_DEBUG_PANEL_W * TC_DEBUG_PANEL_H * 4);
        if (!d->pixels) return false;
        d->drawn = 0;
    }
    if (h == d->drawn) return true;
    d->drawn = h;
    d->version++;
    layout(d, g, level, &s);
    for (y = 0; y < TC_DEBUG_PANEL_H; y++) {
        const bool bar = s.bar >= 0 && y >= TOP + (uint32_t)s.bar * 10 && y < TOP + (uint32_t)s.bar * 10 + 10;
        uint8_t *px = d->pixels + (size_t)y * TC_DEBUG_PANEL_W * 4;
        for (x = 0; x < TC_DEBUG_PANEL_W; x++, px += 4) {
            const bool lit = bar && x < LEFT_W * 8;
            px[0] = lit ? 50 : 10, px[1] = lit ? 70 : 12, px[2] = lit ? 130 : 22, px[3] = 228;
        }
    }
    for (line = 0; line < LINES; line++)
        for (col = 0; col < COLS; col++) {
            const unsigned char c = (unsigned char)s.ch[line][col];
            const uint8_t *glyph, *rgb = colours[s.attr[line][col]];
            if (c <= 32 || c >= 127) continue;
            glyph = font[c - 32];
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++)
                    if (glyph[y] >> x & 1) {
                        uint8_t *px = d->pixels + ((size_t)(TOP + line * 10 + 1 + y) * TC_DEBUG_PANEL_W + col * 8 + x) * 4;
                        px[0] = rgb[0], px[1] = rgb[1], px[2] = rgb[2], px[3] = 255;
                    }
        }
    return true;
}

size_t tc_debug_text(const tc_debug *d, const tc_ai_globals *g, const char *level, char *out, size_t size) {
    static screen s;
    size_t n = 0;
    uint32_t line;
    layout(d, g, level, &s);
    if (!size) return 0;
    for (line = 0; line < LINES; line++) {
        int end = (int)COLS;
        while (end > 0 && s.ch[line][end - 1] == ' ') end--;
        if (n + (size_t)end + 2 > size) break;
        memcpy(out + n, s.ch[line], (size_t)end);
        n += (size_t)end;
        out[n++] = '\n';
    }
    out[n] = 0;
    return n;
}
