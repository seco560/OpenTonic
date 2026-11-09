/* Grid of converted levels (*.ttlv in the levels folder, and in the Special Edition's out/se/ttlv), each shown with
 * its loading-screen image from out/Vignette/Level/<Name>.png (the Special Edition's: out/se/Vignette/Level) when
 * available (matched case-insensitively). Port of LevelPicker.swift, drawn with the 2D quad pipeline: click a tile, or
 * move with the arrow keys and press Enter. All sizes in pixels.
 * Four sections, four panes in a ring, one showing at a time: "Retail Edition", "Special Edition" (the 1998 disc's levels,
 * tools/ttre/se.py), "Cutscenes" (the retail game's cutscene-only levels, tc_level_is_cutscene) and "Imported" (models
 * brought in with tools/ttre/import_gltf.py: they have a folder out/Imported/<name>, and a question mark when they have no
 * picture). Each fills the window's height, scrolls inside itself and slides left and right with the arrows at its top. */
#ifndef TV_PICKER_H
#define TV_PICKER_H
#include "gpu.h"

#define PICKER_SECTIONS 4
#define PICKER_PANES 4  /* all the sections are panes, Imported the last */

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
    SDL_GPUTexture *intro_text, *random_text, *replays_text, *controls_text, *left_text, *right_text;  /* the buttons' labels */
    int intro_w, intro_h, random_w, random_h, replays_w, replays_h, controls_w, controls_h, left_w, left_h, right_w, right_h;
    float scale;              /* UI scale (display scale x pixel density) */
    float scroll;             /* the page's, pixels */
    int pane;                 /* the section of the pane showing: a selected tile of a pane is always in it */
    float pane_scroll[PICKER_PANES];  /* each pane's own, pixels */
    float slide, slide_to;    /* where the ring of panes stands, in panes (the n with levels: n is the first again);
                               * eases to slide_to, one step per slide whichever way round */
    Uint64 slide_ns;          /* the last frame's time */
    int hover, selected;      /* -1 = none */
    int columns;              /* from the last layout */
    float width, height;      /* last target size */
} picker;

/* Lists the folders' levels and loads their thumbnails (se_folder / se_thumbnails: the Special Edition's, NULL or
 * missing: none). count == 0 when there are none. pane: the section to show first (the first pane with levels if it
 * has none). */
void picker_open(picker *p, gpu *g, const char *folder, const char *thumbnails, const char *se_folder,
                 const char *se_thumbnails, float scale, int pane);
void picker_close(picker *p);
/* Tile under the point (target pixels), one of the PICKER_ button values, or -1. */
#define PICKER_INTRO (-2)     /* the big "Play Intro Video" button at the top */
#define PICKER_CONTROLS (-3)  /* "Controls (F1)", top right */
#define PICKER_REPLAYS (-4)   /* "Replays", in the same row: the recordings (replays.h) */
#define PICKER_PANE_LEFT (-5) /* the panes' arrows (when there is more than one pane: they go round) */
#define PICKER_PANE_RIGHT (-6)
#define PICKER_RANDOM (-7)    /* "Random Stage", between the intro's and the replays' */
int picker_hit(picker *p, float x, float y);
void picker_set_hover(picker *p, float x, float y);
/* A stage of either edition at random, -1 if none. Not the cutscenes (the Special Edition's pane has one too: its
 * outro), nor the imported models. */
int picker_random(const picker *p);
void picker_scroll(picker *p, float pixels);
/* The pane showing, and the page with what it can't take (the end of the pane). */
void picker_scroll_pane(picker *p, float pixels);
/* The wheel, the pointer at (x, y): over the panes as picker_scroll_pane, elsewhere the page. */
void picker_wheel(picker *p, float x, float y, float pixels);
/* Slide to the pane on that side (-1 left, 1 right), round the ring: left of retail are the cutscenes. */
void picker_pane(picker *p, int dir);
/* Arrow keys: move the selection by columns/rows (the first press selects the first tile); past the side of a pane, on
 * to the pane there (round the ring, as the arrows). */
void picker_move(picker *p, int dx, int dy);
/* Draws into a cleared 2D pass. */
void picker_draw(picker *p, gpu_frame *f, SDL_GPURenderPass *pass);
/* The menus' flat button, and a label with a drop shadow (it reads on any colour); uv NULL: all of it. */
void picker_button(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_FRect r, SDL_GPUTexture *label, int lw, int lh, bool hot,
                   float scale);
/* The big buttons' look (the level select's): colour c, a rim, a glow under the mouse. */
void picker_big_button(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_FRect r, SDL_GPUTexture *label, int lw, int lh,
                       bool hot, float scale, SDL_FColor c);
void picker_label(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_GPUTexture *label, SDL_FRect t, const SDL_FRect *uv,
                  float scale, SDL_FColor color);

#endif
