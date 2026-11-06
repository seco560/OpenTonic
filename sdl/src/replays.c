#include "replays.h"
#include <tonic/tonic.h>
#include "picker.h"
#include "util.h"

static const SDL_FColor TEXT = {0.95f, 0.96f, 1, 1}, DIM = {0.68f, 0.71f, 0.8f, 1}, WARN = {1, 0.72f, 0.55f, 1};

/* Fills the row's text and state from the file. */
static void read_entry(replays *r, replays_entry *e, const char *name) {
    size_t size = 0;
    void *data = SDL_LoadFile(e->path, &size);
    tc_replay rp;
    tc_replay_summary s;
    char title[256], details[256];
    if (data && tc_replay_open(&rp, data, size) == TC_OK) {
        tc_replay_summarize(&rp, &s);
        e->playable = s.playable;
        tc_replay_close(&rp);
        SDL_snprintf(title, sizeof title, "%s", s.levels);
        SDL_snprintf(details, sizeof details, "%s%s%s", s.when, s.when[0] ? ", " : "", s.details);
    } else {
        SDL_snprintf(title, sizeof title, "%s", name);
        SDL_snprintf(details, sizeof details, "not a recording this build can read");
    }
    SDL_free(data);
    e->title = gpu_text(r->g, title, r->text_scale, &e->title_w, &e->title_h);
    e->details = gpu_text(r->g, details, r->text_scale, &e->details_w, &e->details_h);
}

/* The names start with their date: newest first. */
static int newest_first(const void *a, const void *b) {
    return SDL_strcmp(((const replays_entry *)b)->path, ((const replays_entry *)a)->path);
}

void replays_open(replays *r, gpu *g, float scale, const char *folder, bool recording) {
    int n = 0;
    char **names = folder ? SDL_GlobDirectory(folder, "*." TC_REC_EXTENSION, 0, &n) : NULL;
    SDL_zerop(r);
    r->g = g;
    r->scale = scale > 0 ? scale : 1;
    r->selected = r->confirm = -1;
    r->can_record = folder != NULL;
    r->recording = recording;
    r->text_scale = SDL_max(1, (int)SDL_roundf(2 * r->scale));
    r->entries = n > 0 ? SDL_calloc((size_t)n, sizeof *r->entries) : NULL;
    for (int i = 0; r->entries && i < n; i++)
        if ((r->entries[r->count].path = path_join(folder, names[i]))) r->count++;
    if (r->count) SDL_qsort(r->entries, (size_t)r->count, sizeof *r->entries, newest_first);
    for (int i = 0; i < r->count; i++) {
        char *name = path_stem(r->entries[i].path);
        read_entry(r, &r->entries[i], name ? name : "?");
        SDL_free(name);
    }
    SDL_free(names);
    r->heading = gpu_text(g, "Replays", r->text_scale + 1, &r->heading_w, &r->heading_h);
    r->back = gpu_text(g, "Back (Esc)", r->text_scale, &r->back_w, &r->back_h);
    r->play = gpu_text(g, "Play", r->text_scale, &r->play_w, &r->play_h);
    r->del = gpu_text(g, "Delete", r->text_scale, &r->del_w, &r->del_h);
    r->yes = gpu_text(g, "Yes, delete", r->text_scale, &r->yes_w, &r->yes_h);
    r->keep = gpu_text(g, "Keep", r->text_scale, &r->keep_w, &r->keep_h);
    r->ask = gpu_text(g, "Delete this recording for good?", r->text_scale, &r->ask_w, &r->ask_h);
    r->record_on = gpu_text(g, "Record runs: On (R)", r->text_scale, &r->record_on_w, &r->record_on_h);
    r->record_off = gpu_text(g, "Record runs: Off (R)", r->text_scale, &r->record_off_w, &r->record_off_h);
    r->empty = gpu_text(g, folder ? "No recordings yet: turn on Record runs, then play a level."
                                  : "Recording is off (TONIC_RECORDINGS).", r->text_scale, &r->empty_w, &r->empty_h);
}

static void free_entry(replays *r, replays_entry *e) {
    if (e->title) SDL_ReleaseGPUTexture(r->g->device, e->title);
    if (e->details) SDL_ReleaseGPUTexture(r->g->device, e->details);
    SDL_free(e->path);
}

void replays_close(replays *r) {
    SDL_GPUTexture *labels[] = {r->heading, r->back, r->play, r->del, r->yes, r->keep, r->ask, r->empty, r->record_on, r->record_off};
    for (int i = 0; i < r->count; i++) free_entry(r, &r->entries[i]);
    for (size_t k = 0; k < SDL_arraysize(labels); k++)
        if (labels[k]) SDL_ReleaseGPUTexture(r->g->device, labels[k]);
    SDL_free(r->entries);
    SDL_zerop(r);
}

/* Layout: a header band that stays put, then the rows, which scroll under it. */
static float header_height(const replays *r) { return 76 * r->scale; }
static float row_height(const replays *r) { return 78 * r->scale; }
static float row_gap(const replays *r) { return 8 * r->scale; }

static SDL_FRect row_rect(const replays *r, int i) {
    float w = SDL_min(r->width - 40 * r->scale, 1100 * r->scale);
    SDL_FRect q = {SDL_roundf((r->width - w) / 2), header_height(r) + (float)i * (row_height(r) + row_gap(r)) - r->scroll, w, row_height(r)};
    return q;
}

static SDL_FRect back_rect(const replays *r) {
    float w = (float)r->back_w + 24 * r->scale, h = (float)r->back_h + 16 * r->scale;
    SDL_FRect q = {16 * r->scale, 16 * r->scale, w, h};
    return q;
}

/* The Record runs toggle, the header's right end; as wide for either state, so it stays put under the pointer. */
static SDL_FRect record_rect(const replays *r) {
    float w = (float)SDL_max(r->record_on_w, r->record_off_w) + 24 * r->scale, h = (float)r->record_off_h + 16 * r->scale;
    SDL_FRect q = {r->width - w - 16 * r->scale, 16 * r->scale, w, h};
    return q;
}

/* The row's two buttons, right to left: k 0 the far right (Delete / Keep), 1 beside it (Play / Yes). */
static SDL_FRect button_rect(const replays *r, SDL_FRect row, int k) {
    float w = (float)SDL_max(r->yes_w, r->del_w) + 28 * r->scale, h = (float)r->play_h + 18 * r->scale, gap = 10 * r->scale;
    SDL_FRect q = {row.x + row.w - 14 * r->scale - w - (float)k * (w + gap), SDL_roundf(row.y + (row.h - h) / 2), w, h};
    return q;
}

static bool inside(SDL_FRect q, float x, float y) { return x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h; }

static float max_scroll(const replays *r) {
    float all = header_height(r) + (float)r->count * (row_height(r) + row_gap(r));
    return SDL_max(0.0f, all - r->height);
}

replays_hit replays_at(const replays *r, float x, float y) {
    replays_hit h = {REPLAYS_NOTHING, -1};
    if (inside(back_rect(r), x, y)) { h.action = REPLAYS_BACK; return h; }
    if (r->can_record && inside(record_rect(r), x, y)) { h.action = REPLAYS_RECORD; return h; }
    if (y < header_height(r)) return h;  /* the rows scroll under the header */
    for (int i = 0; i < r->count; i++) {
        const replays_entry *e = &r->entries[i];
        SDL_FRect q = row_rect(r, i);
        if (!inside(q, x, y)) continue;
        h.row = i;
        h.action = REPLAYS_ROW;
        if (inside(button_rect(r, q, 0), x, y)) {
            if (r->confirm == i) h.action = REPLAYS_KEEP;
            else h.action = REPLAYS_DELETE;
        } else if (inside(button_rect(r, q, 1), x, y)) {
            if (r->confirm == i) h.action = REPLAYS_YES;
            else if (e->playable) h.action = REPLAYS_PLAY;
        }
        return h;
    }
    return h;
}

void replays_set_hover(replays *r, float x, float y) { r->hover = replays_at(r, x, y); }

void replays_scroll(replays *r, float pixels) { r->scroll = SDL_clamp(r->scroll + pixels, 0.0f, max_scroll(r)); }

void replays_move(replays *r, int dy) {
    if (r->count == 0) return;
    r->confirm = -1;
    r->selected = r->selected < 0 ? 0 : SDL_clamp(r->selected + dy, 0, r->count - 1);
    SDL_FRect q = row_rect(r, r->selected);
    if (q.y < header_height(r)) replays_scroll(r, q.y - header_height(r));
    else if (q.y + q.h > r->height) replays_scroll(r, q.y + q.h - r->height);
}

void replays_ask(replays *r, int i) {
    if (i >= 0 && i < r->count) r->confirm = i;
}

bool replays_delete(replays *r, int i) {
    if (i < 0 || i >= r->count) return false;
    r->confirm = -1;
    if (!SDL_RemovePath(r->entries[i].path)) {
        SDL_Log("Can't delete %s: %s", r->entries[i].path, SDL_GetError());
        return false;
    }
    SDL_Log("Deleted %s", r->entries[i].path);
    free_entry(r, &r->entries[i]);
    SDL_memmove(&r->entries[i], &r->entries[i + 1], (size_t)(r->count - i - 1) * sizeof *r->entries);
    r->count--;
    r->selected = SDL_min(r->selected, r->count - 1);
    r->hover = (replays_hit){REPLAYS_NOTHING, -1};
    r->scroll = SDL_clamp(r->scroll, 0.0f, max_scroll(r));
    return true;
}

/* A label cut at a width, for long level lists. */
static void clipped(replays *r, gpu_frame *f, SDL_GPURenderPass *pass, SDL_GPUTexture *t, int w, int h, float x, float y, float room, SDL_FColor c) {
    if (!t || w <= 0) return;
    float shown = SDL_min((float)w, room);
    SDL_FRect uv = {0, 0, shown / (float)w, 1};
    picker_label(r->g, f, pass, t, (SDL_FRect){x, y, shown, (float)h}, &uv, r->scale, c);
}

static void row_button(replays *r, gpu_frame *f, SDL_GPURenderPass *pass, SDL_FRect q, SDL_GPUTexture *label, int w, int h, int i, replays_action a) {
    picker_button(r->g, f, pass, q, label, w, h, r->hover.row == i && r->hover.action == a, r->scale);
}

void replays_draw(replays *r, gpu_frame *f, SDL_GPURenderPass *pass) {
    gpu *g = r->g;
    float s = r->scale;
    r->width = (float)f->width;
    r->height = (float)f->height;
    r->scroll = SDL_clamp(r->scroll, 0.0f, max_scroll(r));
    for (int i = 0; i < r->count; i++) {
        const replays_entry *e = &r->entries[i];
        SDL_FRect q = row_rect(r, i);
        if (q.y + q.h < header_height(r) || q.y > r->height) continue;
        bool hot = i == r->selected || (r->hover.row == i && r->hover.action != REPLAYS_NOTHING);
        SDL_FRect left = button_rect(r, q, 1);
        float tx = q.x + 16 * s, room = left.x - tx - 12 * s;
        gpu_quad(g, f, pass, NULL, q, NULL, hot ? (SDL_FColor){0.2f, 0.23f, 0.34f, 1} : (SDL_FColor){0.14f, 0.15f, 0.22f, 1});
        if (r->confirm == i) {
            clipped(r, f, pass, r->ask, r->ask_w, r->ask_h, tx, SDL_roundf(q.y + (q.h - (float)r->ask_h) / 2), room, WARN);
            row_button(r, f, pass, left, r->yes, r->yes_w, r->yes_h, i, REPLAYS_YES);
            row_button(r, f, pass, button_rect(r, q, 0), r->keep, r->keep_w, r->keep_h, i, REPLAYS_KEEP);
            continue;
        }
        clipped(r, f, pass, e->title, e->title_w, e->title_h, tx, q.y + 14 * s, room, TEXT);
        clipped(r, f, pass, e->details, e->details_w, e->details_h, tx, q.y + q.h - 14 * s - (float)e->details_h, room, DIM);
        if (e->playable) row_button(r, f, pass, left, r->play, r->play_w, r->play_h, i, REPLAYS_PLAY);
        row_button(r, f, pass, button_rect(r, q, 0), r->del, r->del_w, r->del_h, i, REPLAYS_DELETE);
    }
    gpu_quad(g, f, pass, NULL, (SDL_FRect){0, 0, r->width, header_height(r)}, NULL, (SDL_FColor){0.08f, 0.09f, 0.14f, 1});
    picker_button(g, f, pass, back_rect(r), r->back, r->back_w, r->back_h, r->hover.action == REPLAYS_BACK, s);
    if (r->can_record)
        picker_button(g, f, pass, record_rect(r), r->recording ? r->record_on : r->record_off, r->recording ? r->record_on_w : r->record_off_w,
                      r->recording ? r->record_on_h : r->record_off_h, r->hover.action == REPLAYS_RECORD, s);
    if (r->heading) picker_label(g, f, pass, r->heading, (SDL_FRect){SDL_roundf((r->width - (float)r->heading_w) / 2),
                                 SDL_roundf((header_height(r) - (float)r->heading_h) / 2), (float)r->heading_w, (float)r->heading_h}, NULL, s, TEXT);
    if (r->count == 0 && r->empty)
        picker_label(g, f, pass, r->empty, (SDL_FRect){SDL_roundf((r->width - (float)r->empty_w) / 2), SDL_roundf(r->height / 2),
                     (float)r->empty_w, (float)r->empty_h}, NULL, s, DIM);
}
