/* Grid of converted levels (*.ttlv in one folder), each shown with its loading-screen image from
 * out/Vignette/Level/<Name>.png when available (matched case-insensitively). Port of LevelPicker.swift, drawn with
 * the 2D quad pipeline: click a tile, or move with the arrow keys and press Enter. All sizes in pixels.
 * Three sections under headings: "Levels", the game's cutscene-only levels (tc_level_is_cutscene), and "Imported":
 * models brought in with tools/ttre/import_gltf.py (they have a folder out/Imported/<name>), drawn with a question mark
 * when they have no picture. */
#ifndef TV_PICKER_H
#define TV_PICKER_H
#include "gpu.h"

typedef struct picker_entry {
    char *name, *path;
    SDL_GPUTexture *thumb;    /* NULL: no loading screen */
    int thumb_w, thumb_h;
    int section;              /* 0 levels, 1 cutscenes, 2 imported */
    SDL_GPUTexture *caption;
    int caption_w, caption_h;
} picker_entry;

typedef struct picker {
    gpu *g;
    picker_entry *entries;
    int count;
    int first[4];             /* section k = entries [first[k], first[k + 1]) */
    SDL_GPUTexture *heading[3];  /* "Levels", "Cutscenes", "Imported" (NULL: section empty) */
    int heading_w[3], heading_h[3];
    SDL_GPUTexture *unknown;  /* "?": the picture of an imported level */
    int unknown_w, unknown_h;
    SDL_GPUTexture *intro_text, *controls_text;  /* the two buttons' labels */
    int intro_w, intro_h, controls_w, controls_h;
    float scale;              /* UI scale (display scale x pixel density) */
    float scroll;             /* pixels */
    int hover, selected;      /* -1 = none */
    int columns;              /* from the last layout */
    float width, height;      /* last target size */
} picker;

/* Lists the folder's levels and loads their thumbnails. count == 0 when there are none. */
void picker_open(picker *p, gpu *g, const char *folder, const char *thumbnails, float scale);
void picker_close(picker *p);
/* Tile under the point (target pixels), PICKER_INTRO / PICKER_CONTROLS for the buttons, or -1. */
#define PICKER_INTRO (-2)     /* the big "Play Intro Video" button at the top */
#define PICKER_CONTROLS (-3)  /* "Controls (F1)", top right */
int picker_hit(picker *p, float x, float y);
void picker_set_hover(picker *p, float x, float y);
void picker_scroll(picker *p, float pixels);
/* Arrow keys: move the selection by columns/rows (the first press selects the first tile). */
void picker_move(picker *p, int dx, int dy);
/* Draws into a cleared 2D pass. */
void picker_draw(picker *p, gpu_frame *f, SDL_GPURenderPass *pass);

#endif
