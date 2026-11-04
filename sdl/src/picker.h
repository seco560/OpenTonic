/* Grid of converted levels (*.ttlv in the levels folder, and in the Special Edition's out/se/ttlv), each shown with
 * its loading-screen image from out/Vignette/Level/<Name>.png (the Special Edition's: out/se/Vignette/Level) when
 * available (matched case-insensitively). Port of LevelPicker.swift, drawn with the 2D quad pipeline: click a tile, or
 * move with the arrow keys and press Enter. All sizes in pixels.
 * Four sections under headings: "Retail Edition", "Special Edition" (the 1998 disc's levels, tools/ttre/se.py), the
 * game's cutscene-only levels (tc_level_is_cutscene), and "Imported": models brought in with tools/ttre/import_gltf.py
 * (they have a folder out/Imported/<name>), drawn with a question mark when they have no picture. */
#ifndef TV_PICKER_H
#define TV_PICKER_H
#include "gpu.h"

#define PICKER_SECTIONS 4

typedef struct picker_entry {
    char *name, *path;
    SDL_GPUTexture *thumb;    /* NULL: no loading screen */
    int thumb_w, thumb_h;
    int section;              /* 0 retail, 1 special edition, 2 cutscenes, 3 imported */
    SDL_GPUTexture *caption;
    int caption_w, caption_h;
} picker_entry;

typedef struct picker {
    gpu *g;
    picker_entry *entries;
    int count;
    int first[PICKER_SECTIONS + 1];  /* section k = entries [first[k], first[k + 1]) */
    SDL_GPUTexture *heading[PICKER_SECTIONS];  /* the section titles (NULL: section empty) */
    int heading_w[PICKER_SECTIONS], heading_h[PICKER_SECTIONS];
    SDL_GPUTexture *unknown;  /* "?": the picture of an imported level */
    int unknown_w, unknown_h;
    SDL_GPUTexture *intro_text, *demo_text, *controls_text;  /* the buttons' labels */
    int intro_w, intro_h, demo_w, demo_h, controls_w, controls_h;
    float scale;              /* UI scale (display scale x pixel density) */
    float scroll;             /* pixels */
    int hover, selected;      /* -1 = none */
    int columns;              /* from the last layout */
    float width, height;      /* last target size */
} picker;

/* Lists the folders' levels and loads their thumbnails (se_folder / se_thumbnails: the Special Edition's, NULL or
 * missing: none). count == 0 when there are none. */
void picker_open(picker *p, gpu *g, const char *folder, const char *thumbnails, const char *se_folder,
                 const char *se_thumbnails, float scale);
void picker_close(picker *p);
/* Tile under the point (target pixels), PICKER_INTRO / PICKER_DEMO / PICKER_CONTROLS for the buttons, or -1. */
#define PICKER_INTRO (-2)     /* the big "Play Intro Video" button at the top */
#define PICKER_CONTROLS (-3)  /* "Controls (F1)", top right */
#define PICKER_DEMO (-4)      /* "Demo", beside the intro's: Ed wanders around random main levels */
int picker_hit(picker *p, float x, float y);
void picker_set_hover(picker *p, float x, float y);
void picker_scroll(picker *p, float pixels);
/* Arrow keys: move the selection by columns/rows (the first press selects the first tile). */
void picker_move(picker *p, int dx, int dy);
/* Draws into a cleared 2D pass. */
void picker_draw(picker *p, gpu_frame *f, SDL_GPURenderPass *pass);

#endif
