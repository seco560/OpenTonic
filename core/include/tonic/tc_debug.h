/* The debug menu (port; docs/ENGINE.md "The debug menu"): the game's progress flags at a glance, what uses each, and
 * switches for them. Two pages: the powers (the masks the scripts use) and the 31 save values. Next to the chosen flag,
 * the characters whose scripts test, give or take it: the converter's survey (tools/ttre/progress.py, progress.ttpg).
 *
 * All of it lives here: the state, the moves, and the picture, drawn on the CPU in a built-in 8x8 font. A frontend only
 * hands over the survey's bytes, opens and closes the menu, and shows the picture (tc_viewer_debug_panel). The rows do
 * not depend on the survey, so a recording replays the same with or without it. */
#ifndef TONIC_TC_DEBUG_H
#define TONIC_TC_DEBUG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_ai.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_DEBUG_PANEL_W 800u
#define TC_DEBUG_PANEL_H 480u

/* What the player presses while the menu is open (tc_debug_input): held buttons; the menu finds the fresh ones and
 * repeats a held direction. */
enum { TC_DEBUG_UP = 1, TC_DEBUG_DOWN = 2, TC_DEBUG_LEFT = 4, TC_DEBUG_RIGHT = 8, TC_DEBUG_OK = 16, TC_DEBUG_FAST = 32 };

enum { TC_DEBUG_POWERS, TC_DEBUG_VALUES, TC_DEBUG_PAGES };
enum { TC_DEBUG_POWER_ROWS = 6 };
extern const uint16_t tc_debug_power_masks[TC_DEBUG_POWER_ROWS]; /* 7, 8, 0x70, 0x80, 0x100, 0x200 */

/* The rows of a page, top to bottom: the page switch, the flags, then the actions. */
enum { TC_DEBUG_ROW_PAGE = 0, TC_DEBUG_ROW_FLAGS = 1 };
uint32_t tc_debug_row_count(uint32_t page);
/* Rows after a page's flags, in order. */
enum { TC_DEBUG_ACT_ALL_ON, TC_DEBUG_ACT_STORY_START, TC_DEBUG_ACT_SCOPE, TC_DEBUG_ACT_RELOAD, TC_DEBUG_POWER_ACTIONS };
enum { TC_DEBUG_ACT_ZERO, TC_DEBUG_ACT_VALUE_SCOPE, TC_DEBUG_ACT_VALUE_RELOAD, TC_DEBUG_VALUE_ACTIONS };

/* One row of the survey: a model's scripts in a level ("*": a perso of the fix, in every level) hold `calls` calls of
 * this use with this key, over `instances` persos. */
enum { TC_DEBUG_USE_TESTS, TC_DEBUG_USE_GIVES, TC_DEBUG_USE_TAKES, TC_DEBUG_USE_SETS, TC_DEBUG_USE_READS,
       TC_DEBUG_USE_ADDS, TC_DEBUG_USES };
typedef struct tc_debug_use {
    uint8_t kind;      /* 0 power, 1 save value */
    uint8_t use;
    int16_t key;       /* the mask or the slot; -1: computed by the script */
    uint16_t calls, instances;
    char *level, *model, *instance;
} tc_debug_use;

typedef struct tc_debug {
    bool open;
    uint8_t page;
    uint8_t row[TC_DEBUG_PAGES];
    bool all_levels;          /* the uses shown: every level's, not this level's alone */
    bool reload;              /* "reload the level" was chosen: the host takes it (and clears it) */
    uint32_t prev;            /* last input's buttons */
    float hold;               /* seconds a direction has been held, for the repeat */
    tc_debug_use *uses;
    uint32_t use_count;
    uint8_t *pixels;          /* TC_DEBUG_PANEL_W x _H x 4, straight alpha, made on first use */
    uint32_t version;         /* counts the picture's changes */
    uint64_t drawn;           /* what the picture shows (a hash of the state) */
} tc_debug;

void tc_debug_free(tc_debug *d);
/* The survey (progress.ttpg), copied. Replaces an earlier one. */
tc_result tc_debug_load_survey(tc_debug *d, const void *data, size_t size);
/* Opens (the buttons held now do not count as presses) or closes. */
void tc_debug_set_open(tc_debug *d, bool open, uint32_t buttons);
/* One frame of the open menu. g: the game's state, changed at once by the switches; NULL: the level has none. */
void tc_debug_input(tc_debug *d, uint32_t buttons, float dt, tc_ai_globals *g);
/* The picture for the state now; false when out of memory. level: the level's name, for "this level's" uses. */
bool tc_debug_render(tc_debug *d, const tc_ai_globals *g, const char *level);
/* The text of the picture's lines, for headless checks: line by line into out ('\n' between), as drawn. */
size_t tc_debug_text(const tc_debug *d, const tc_ai_globals *g, const char *level, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
