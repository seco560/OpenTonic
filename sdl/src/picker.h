/* Grid of converted levels (*.ttlv in one folder), each shown with its loading-screen image from
 * out/Vignette/Level/<Name>.png when available (matched case-insensitively). Port of LevelPicker.swift, drawn with
 * the 2D quad pipeline: click a tile, or move with the arrow keys and press Enter. All sizes in pixels.
 * Two sections under headings: "Levels", then the game's cutscene-only levels (tc_level_is_cutscene). */
#ifndef TV_PICKER_H
#define TV_PICKER_H
#include "gpu.h"

typedef struct picker_entry {
    char *name, *path;
    SDL_GPUTexture *thumb;    /* NULL: no loading screen */
    int thumb_w, thumb_h;
    SDL_GPUTexture *caption;
    int caption_w, caption_h;
} picker_entry;

typedef struct picker {
    gpu *g;
    picker_entry *entries;
    int count;
    int first_cutscene;       /* entries [first_cutscene, count) are cutscene levels */
    SDL_GPUTexture *heading[2];  /* "Levels", "Cutscenes" (NULL: section empty) */
    int heading_w[2], heading_h[2];
    float scale;              /* UI scale (display scale x pixel density) */
    float scroll;             /* pixels */
    int hover, selected;      /* -1 = none */
    int columns;              /* from the last layout */
    float width, height;      /* last target size */
} picker;

/* Lists the folder's levels and loads their thumbnails. count == 0 when there are none. */
void picker_open(picker *p, gpu *g, const char *folder, const char *thumbnails, float scale);
void picker_close(picker *p);
/* Tile under the point (target pixels), or -1. */
int picker_hit(picker *p, float x, float y);
void picker_set_hover(picker *p, float x, float y);
void picker_scroll(picker *p, float pixels);
/* Arrow keys: move the selection by columns/rows (the first press selects the first tile). */
void picker_move(picker *p, int dx, int dy);
/* Draws into a cleared 2D pass. */
void picker_draw(picker *p, gpu_frame *f, SDL_GPURenderPass *pass);

#endif
