/* The game's HUD (docs/ENGINE.md "The HUD"): the scripts' text slots and the standard elements (life, lives, the
 * counters), laid out as the exe does and handed to the frontends as flat quads over one atlas.
 *
 * The exe draws every element as a "text" in one of its fonts, through a camera, for a 4:3 screen (FON_004cdd10,
 * GAM_00418d70, GAM_0041a720). The fonts' glyphs are small objects, some of them 3D models; the converter renders each
 * once into the atlas (tools/ttre/hud.py -> hud.tthd). Port: the layout is kept in the exe's units, 7 high whatever
 * the screen, and an element near the left or right edge of the 4:3 frame keeps its distance to that edge of the real
 * screen; what sits in the middle stays in the middle. */
#ifndef TONIC_TC_HUD_H
#define TONIC_TC_HUD_H
#include "tc_ai.h"

#define TC_HUD_STANDARD 17
enum { /* the standard HUD's texts, in the order of the exe's block (DAT_008bc868 ...) */
    TC_HUD_LIFE_1, TC_HUD_LIFE_2, TC_HUD_SPADE, TC_HUD_SPADE_NUMBER, TC_HUD_BEE, TC_HUD_BEE_NUMBER, TC_HUD_TINGS,
    TC_HUD_TINGS_NUMBER, TC_HUD_LIVES, TC_HUD_DOC, TC_HUD_DOC_NUMBER, TC_HUD_THERMOMETER, TC_HUD_GRADUATION,
    TC_HUD_MERCURY, TC_HUD_CHEAT, TC_HUD_DEMO, TC_HUD_LOGO
};

typedef struct tc_hud_glyph {
    uint16_t code;
    float step;                  /* minus its half height: it stands on the line's base */
    float advance, shift;        /* its width in the line; its baseline's shift */
    float cell[4];               /* x0, z0, x1, z1 about its centre, font units; all 0: nothing to draw */
    float uv[4];                 /* u0, v0 (top-left), u1, v1 */
} tc_hud_glyph;

typedef struct tc_hud_font {
    char name[16];
    float spacing, line_step;    /* between letters; a line's height, negative (the space's step) */
    uint16_t glyph_count;
    tc_hud_glyph *glyphs;
} tc_hud_font;

typedef struct tc_hud_text { uint8_t font; uint16_t capacity, length; char *bytes; } tc_hud_text;

typedef struct tc_hud_data {
    uint16_t atlas_width, atlas_height;
    uint8_t *atlas;              /* rgba, straight alpha, top row first */
    float panel_uv[4];           /* a block of the text window's colour */
    uint16_t font_count, text_count;
    tc_hud_font *fonts;
    tc_hud_text *texts;
    uint16_t standard[TC_HUD_STANDARD];
    bool has_head;               /* Ed's head beside the lives (hud.tthd HEAD), as a glyph: its cell and uv */
    float head_cell[4], head_uv[4];
} tc_hud_data;

/* A quad over the frame: rect x0, y0, x1, y1 in viewport units (y down), the atlas's u0, v0, u1, v1. */
typedef struct tc_hud_quad { float rect[4], uv[4]; } tc_hud_quad;
#define TC_HUD_MAX_QUADS 768

/* What the standard HUD shows, fed by the host each tick. A count of -1: that element is not there. */
typedef struct tc_hud_values {
    int32_t hit_points, hit_points_max;
    int32_t lives;
    int32_t bees;                /* save value 0 */
    int32_t tings;               /* the World perso's save value (the level's bonuses): FUN_0043bc00 */
    int32_t spade;               /* the one after it (the level's other pickups): GAM_00419490 */
    int32_t doc_slot, doc_count; /* the latest of Doc's objects (tc_ai_globals.hud_doc) and how many */
    uint32_t doc_serial;
    bool show_all;               /* every element in (the exe's DAT_008bd07a) */
} tc_hud_values;

typedef struct tc_hud {
    tc_hud_data data;
    bool loaded;
    /* The standard HUD stays on screen (the game's default). Set: an element shows for five seconds when its value
     * changes, then slides out (the game's other mode, on one of its F keys). tc_viewer_set_hud_auto_hide (H). */
    bool auto_hide;
    /* Port (the user, 2026-11-14): the levels outside the retail game (the Special Edition's, the imported ones: the SSX
     * courses, LibertyCity2001), extensions of it, show the standard HUD's left side only: Ed's life (the thermometer),
     * his head and lives; nothing on the right. Set by the viewer (tc_viewer_set_hud). */
    bool left_only;
    /* the standard HUD: what was last shown (a change brings its element in for five seconds), which elements are
     * asked for (the exe's DAT_008bd07b: 1 life and lives, 4 the spade, 8 the bees, 0x10 the tings, 0x20 Doc's
     * object), since when, and how far out each has slid (2: gone) */
    int32_t shown_hp, shown_lives, shown_bees, shown_tings, shown_spade;
    uint32_t doc_serial;
    uint8_t asked;
    float since_ms;
    float out_life, out_lives, out_spade, out_tings, out_doc, out_bees;
    tc_hud_values now;
    tc_hud_quad quads[TC_HUD_MAX_QUADS];
    uint32_t quad_count;
} tc_hud;

void tc_hud_init(tc_hud *h);
void tc_hud_free(tc_hud *h);
/* The bytes of hud.tthd (copied). */
tc_result tc_hud_load(tc_hud *h, const void *data, size_t size);
/* One game tick (1/30 s): the standard HUD's timers and slides, and the text slots' frame counts. */
void tc_hud_tick(tc_hud *h, tc_ai_globals *g, const tc_hud_values *now);
/* Port: the standard elements off the screen at once (behind a covered screen or a loading screen: tc_viewer); the
 * next ticks slide them in again. */
void tc_hud_away(tc_hud *h);
/* The quads of this frame for a viewport of this aspect (width / height): the text slots (boss_points /
 * boss_max: per slot, a boss bar's hit points, from the host), then the standard HUD when `standard`. */
uint32_t tc_hud_build(tc_hud *h, const tc_ai_globals *g, const uint8_t *boss_points, const uint8_t *boss_max,
                      bool standard, float aspect);
/* A text slot's letters as the HUD prints them (for checks): false when the slot shows nothing. */
bool tc_hud_slot_text(const tc_hud *h, const tc_ai_hud_slot *s, uint8_t points, uint8_t max, char *out, size_t size);

#endif
