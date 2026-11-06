/* Speech (docs/ENGINE.md "Speech"; the exe's GAM_LipsSync.c): one line at a time, in a window over the frame. The text is
 * wrapped and laid out as GAM_00417e90 / FON_004cdd10 do, in the font's units, and drawn here on the CPU into an RGBA
 * panel in the game's own font: the frontends only show the picture. */
#ifndef TONIC_TC_SPEECH_H
#define TONIC_TC_SPEECH_H
#include <stdbool.h>
#include <stdint.h>
#include "tc_level.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_SPEECH_PANEL_W 1152u /* the window at 4:3 is 6 to 1 (90 % by 20 %), 2 font units high */
#define TC_SPEECH_PANEL_H 192u
#define TC_SPEECH_TEXT_MAX 1024u

typedef struct tc_speech {
    const tc_speech_data *data;
    const tc_dialogue *dialogue; /* the line on show, NULL none */
    char text[TC_SPEECH_TEXT_MAX]; /* wrapped: '^' ends a line */
    uint32_t lines;
    float y, y_min, y_max;       /* the top line's height (line +0x14, +0x1C, +0x20) */
    float speed;                 /* per 1/30 s (+0x48): 0 when it fits the window */
    bool scrolled_out;           /* DAT_004ec6c0: the text reached its end; the player may scroll it then */
    float time;                  /* seconds into the line */
    uint32_t version;            /* counts the panel's changes */
    bool dirty;
    bool prompting;              /* the panel holds tc_speech_render_prompt's picture */
    uint8_t *pixels;             /* TC_SPEECH_PANEL_W x TC_SPEECH_PANEL_H x 4, made on first use */
} tc_speech;

void tc_speech_init(tc_speech *s, const tc_speech_data *data);
void tc_speech_free(tc_speech *s);
/* The width in font units of the line of `text` that starts at `from` (FON_004ce110). */
float tc_speech_line_width(const tc_speech_data *data, const char *text, uint32_t from);
void tc_speech_start(tc_speech *s, const tc_dialogue *d);
void tc_speech_end(tc_speech *s);
/* dt seconds on: the text moves on by itself until it is out; then `scroll` (-1, 0, 1) moves it. */
void tc_speech_step(tc_speech *s, float dt, int scroll);
/* The panel for the line now: false when there is nothing to show (no line, or a line without text). */
bool tc_speech_render(tc_speech *s);
/* Port: one line in a small box in the panel's middle, the rest clear (a hint scene's "talk" prompt). */
bool tc_speech_render_prompt(tc_speech *s, const char *text);

#ifdef __cplusplus
}
#endif
#endif
