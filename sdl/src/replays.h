/* The Replays menu: the recordings (record.h) in a list, newest first, each to play or delete; Sources/OpenTonic/
 * ReplaysScreen.swift is the Swift build's. Over the level select, which stays open underneath. Row text is the core's
 * (tc_replay_summarize). Deleting asks first, in the row; an autopilot run can't be played. A run ends on the way to a
 * menu, so nothing here is being written. All sizes in pixels. */
#ifndef TV_REPLAYS_H
#define TV_REPLAYS_H
#include "gpu.h"

typedef struct replays_entry {
    char *path;
    bool playable;
    SDL_GPUTexture *title, *details;
    int title_w, title_h, details_w, details_h;
} replays_entry;

/* What is under a point. */
typedef enum replays_action {
    REPLAYS_NOTHING, REPLAYS_BACK, REPLAYS_ROW, REPLAYS_PLAY, REPLAYS_DELETE, REPLAYS_YES, REPLAYS_KEEP, REPLAYS_RECORD
} replays_action;
typedef struct replays_hit { replays_action action; int row; } replays_hit;

typedef struct replays {
    gpu *g;
    replays_entry *entries;
    int count;
    float scale, scroll, width, height;
    int selected;                /* -1 none */
    int confirm;                 /* the row asking "delete?", -1 none */
    replays_hit hover;
    int text_scale;
    bool can_record, recording;  /* the Record runs toggle: shown, and its state (record_enabled) */
    SDL_GPUTexture *record_on, *record_off;
    int record_on_w, record_on_h, record_off_w, record_off_h;
    SDL_GPUTexture *heading, *back, *play, *del, *yes, *keep, *ask, *empty;
    int heading_w, heading_h, back_w, back_h, play_w, play_h, del_w, del_h, yes_w, yes_h, keep_w, keep_h, ask_w, ask_h,
        empty_w, empty_h;
} replays;

/* Lists folder's recordings (NULL: recording is off, no toggle); recording: the toggle's state. */
void replays_open(replays *r, gpu *g, float scale, const char *folder, bool recording);
void replays_close(replays *r);
replays_hit replays_at(const replays *r, float x, float y);
void replays_set_hover(replays *r, float x, float y);
void replays_scroll(replays *r, float pixels);
/* Up and down the list; the first press selects the first row. Any move drops a pending question. */
void replays_move(replays *r, int dy);
/* Row i asks to be deleted. */
void replays_ask(replays *r, int i);
/* Deletes row i's file and row; false (the row kept) when the file can't be removed. */
bool replays_delete(replays *r, int i);
void replays_draw(replays *r, gpu_frame *f, SDL_GPURenderPass *pass);
#endif
