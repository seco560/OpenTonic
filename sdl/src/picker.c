#include "picker.h"
#include <tonic/tonic.h>
#include "util.h"

static const char *const section_titles[PICKER_SECTIONS] = {"Retail Edition", "Special Edition", "Cutscenes",
                                                             "Imported"};
#define SECTION_SE 1
#define SECTION_CUTSCENES 2
#define SECTION_IMPORTED 3

/* By section, then by name. */
static int compare_entries(const void *a, const void *b) {
    const picker_entry *x = (const picker_entry *)a, *y = (const picker_entry *)b;
    return x->section != y->section ? x->section - y->section : SDL_strcasecmp(x->name, y->name);
}

static int section_of(const char *name, const char *imported) {
    char *dir = imported ? path_join(imported, name) : NULL;
    bool is_imported = dir && path_is_dir(dir);
    SDL_free(dir);
    return is_imported ? SECTION_IMPORTED : tc_level_is_cutscene(name) ? SECTION_CUTSCENES : 0;
}

static bool has_levels(const picker *p, int s) { return p->first[s + 1] > p->first[s]; }

/* The ring of panes: those with levels, in section order. */
static int pane_count(const picker *p) {
    int n = 0;
    for (int s = 0; s < PICKER_PANES; s++) n += has_levels(p, s);
    return n;
}

/* Where section s's pane stands in the ring. */
static int pane_slot(const picker *p, int s) {
    int j = 0;
    for (int t = 0; t < s; t++) j += has_levels(p, t);
    return j;
}

/* The pane next to section s's that way round the ring (s itself when it is the only one). */
static int pane_next(const picker *p, int s, int dir) {
    for (int k = 1; k <= PICKER_PANES; k++) {
        int t = ((s + dir * k) % PICKER_PANES + PICKER_PANES) % PICKER_PANES;
        if (has_levels(p, t)) return t;
    }
    return s;
}

/* The arrows (and the arrow keys past a pane's side) go round the ring: with two panes or more. */
static bool can_slide(const picker *p) { return pane_count(p) > 1; }

/* The levels of one folder appended to p->entries (grown); se: all of them in the Special Edition's section. */
static void add_folder(picker *p, const char *folder, bool se) {
    int n = 0;
    char **levels = folder ? SDL_GlobDirectory(folder, "*.ttlv", SDL_GLOB_CASEINSENSITIVE, &n) : NULL;
    picker_entry *grown = levels && n > 0 ? SDL_realloc(p->entries, (size_t)(p->count + n) * sizeof *p->entries) : NULL;
    if (!grown) { SDL_free(levels); return; }
    p->entries = grown;
    /* Imported levels have out/Imported/<name>. */
    char *out = path_dir(folder), *imported = out && !se ? path_join(out, "Imported") : NULL;
    for (int i = 0; i < n; i++) {
        picker_entry *e = &p->entries[p->count];
        SDL_zerop(e);
        e->path = path_join(folder, levels[i]);
        e->name = path_stem(levels[i]);
        if (!e->path || !e->name) { SDL_free(e->path); SDL_free(e->name); SDL_zerop(e); continue; }
        e->section = se ? SECTION_SE : section_of(e->name, imported);
        p->count++;
    }
    SDL_free(out);
    SDL_free(imported);
    SDL_free(levels);
}

/* The entry's thumbnail from `thumbnails` (<name>.png, any case), if any. */
static void load_thumb(gpu *g, gpu_upload *u, picker_entry *e, const char *thumbnails, char **thumbs, int thumb_n) {
    for (int t = 0; thumbs && t < thumb_n; t++) {
        char *stem = path_stem(thumbs[t]);
        bool match = stem && SDL_strcasecmp(stem, e->name) == 0;
        SDL_free(stem);
        if (!match) continue;
        char *path = path_join(thumbnails, thumbs[t]);
        e->thumb = gpu_upload_png(g, u, path, true, &e->thumb_w, &e->thumb_h);
        if (!e->thumb) SDL_Log("thumbnail %s: %s", path, SDL_GetError());
        SDL_free(path);
        return;
    }
}

void picker_open(picker *p, gpu *g, const char *folder, const char *thumbnails, const char *se_folder,
                 const char *se_thumbnails, float scale, int pane) {
    SDL_zerop(p);
    p->g = g;
    p->scale = scale > 0 ? scale : 1;
    p->hover = p->selected = -1;
    add_folder(p, folder, false);
    if (se_folder && path_is_dir(se_folder)) add_folder(p, se_folder, true);
    if (p->count == 0) return;
    SDL_qsort(p->entries, (size_t)p->count, sizeof *p->entries, compare_entries);
    for (int s = 0, i = 0; s <= PICKER_SECTIONS; s++) {
        while (i < p->count && p->entries[i].section < s) i++;
        p->first[s] = s == PICKER_SECTIONS ? p->count : i;
    }
    p->pane = pane >= 0 && pane < PICKER_PANES ? pane : 0;
    if (!has_levels(p, p->pane)) p->pane = pane_next(p, p->pane, 1);  /* none at all: no panes */
    p->slide = p->slide_to = (float)pane_slot(p, p->pane);

    int thumb_n = 0, se_thumb_n = 0;
    char **thumbs = SDL_GlobDirectory(thumbnails, "*.png", SDL_GLOB_CASEINSENSITIVE, &thumb_n);
    char **se_thumbs = se_thumbnails ? SDL_GlobDirectory(se_thumbnails, "*.png", SDL_GLOB_CASEINSENSITIVE, &se_thumb_n) : NULL;
    gpu_upload u;
    bool uploading = gpu_upload_begin(g, &u);
    int text_scale = SDL_max(1, (int)SDL_roundf(2 * p->scale));
    for (int i = 0; i < p->count; i++) {
        picker_entry *e = &p->entries[i];
        if (uploading) {
            if (e->section == SECTION_SE) load_thumb(g, &u, e, se_thumbnails, se_thumbs, se_thumb_n);
            else load_thumb(g, &u, e, thumbnails, thumbs, thumb_n);
        }
        e->caption = gpu_text(g, e->name, text_scale, &e->caption_w, &e->caption_h);
    }
    if (uploading) gpu_upload_end(g, &u);
    SDL_free(thumbs);
    SDL_free(se_thumbs);
    for (int s = 0; s < PICKER_SECTIONS; s++)
        if (p->first[s] < p->first[s + 1])
            p->heading[s] = gpu_text(g, section_titles[s], text_scale, &p->heading_w[s], &p->heading_h[s]);
    if (p->first[SECTION_IMPORTED] < p->count) p->unknown = gpu_text(g, "?", 1, &p->unknown_w, &p->unknown_h);
    p->intro_text = gpu_text(g, "Play Intro Video", text_scale, &p->intro_w, &p->intro_h);
    p->random_text = gpu_text(g, "Random Stage", text_scale, &p->random_w, &p->random_h);
    p->replays_text = gpu_text(g, "Replays", text_scale, &p->replays_w, &p->replays_h);
    p->controls_text = gpu_text(g, "Controls (F1)", text_scale, &p->controls_w, &p->controls_h);
    p->left_text = gpu_text(g, "<", text_scale + 1, &p->left_w, &p->left_h);
    p->right_text = gpu_text(g, ">", text_scale + 1, &p->right_w, &p->right_h);
}

void picker_close(picker *p) {
    for (int i = 0; i < p->count; i++) {
        picker_entry *e = &p->entries[i];
        if (e->thumb) SDL_ReleaseGPUTexture(p->g->device, e->thumb);
        if (e->caption) SDL_ReleaseGPUTexture(p->g->device, e->caption);
        SDL_free(e->name);
        SDL_free(e->path);
    }
    for (int k = 0; k < PICKER_SECTIONS; k++)
        if (p->heading[k]) SDL_ReleaseGPUTexture(p->g->device, p->heading[k]);
    if (p->unknown) SDL_ReleaseGPUTexture(p->g->device, p->unknown);
    if (p->intro_text) SDL_ReleaseGPUTexture(p->g->device, p->intro_text);
    if (p->random_text) SDL_ReleaseGPUTexture(p->g->device, p->random_text);
    if (p->replays_text) SDL_ReleaseGPUTexture(p->g->device, p->replays_text);
    if (p->controls_text) SDL_ReleaseGPUTexture(p->g->device, p->controls_text);
    if (p->left_text) SDL_ReleaseGPUTexture(p->g->device, p->left_text);
    if (p->right_text) SDL_ReleaseGPUTexture(p->g->device, p->right_text);
    SDL_free(p->entries);
    SDL_zerop(p);
}

/* Layout (LevelPicker.relayout fits the window instead): 256x192 tiles with a caption line, centred columns; each section starts on a new
 * row under a heading line, the panes' in their fixed height. */
typedef struct layout { float tile_w, tile_h, caption, pad, cell_w, cell_h, left, heading, intro, pane_h, inset; int cols; } layout;

static layout layout_for(const picker *p, float width) {
    layout l;
    float s = p->scale;
    l.tile_w = 256 * s;
    l.tile_h = 192 * s;
    l.caption = 26 * s;
    l.pad = 20 * s;
    l.cell_w = l.tile_w + l.pad;
    l.cell_h = l.tile_h + l.caption + l.pad;
    l.heading = 40 * s;
    l.intro = 68 * s;  /* the band of the big buttons, above the first heading */
    l.inset = 8 * s;  /* above a pane's first row: room for the selection's glow */
    /* what the window has left under the heading, so a pane's own scrolling reaches all of it; at least a row */
    l.pane_h = SDL_max(l.cell_h + l.inset, p->height - (l.pad + l.intro + l.heading) - l.pad);
    l.cols = SDL_max(1, (int)((width - l.pad) / l.cell_w));
    l.left = SDL_max(l.pad, (width - (float)l.cols * l.cell_w + l.pad) / 2);
    return l;
}

static int rows_of(int n, int cols) { return (n + cols - 1) / cols; }

/* Top of section k's heading (content coordinates, before scrolling); k = PICKER_SECTIONS: the end. The panes share
 * theirs, over them; empty sections take no space. */
static float heading_y(const picker *p, const layout *l, int k) {
    float y = l->pad + l->intro;
    if (k < PICKER_PANES) return y;
    if (p->first[PICKER_PANES] > 0) y += l->heading + l->pane_h;
    return y;
}

/* The window onto the panes, in target pixels: the full width, so a pane leaves by the window's side. */
static SDL_FRect pane_rect(const picker *p, const layout *l) {
    SDL_FRect r = {0, heading_y(p, l, 0) + l->heading - p->scroll, p->width, l->pane_h};
    return r;
}

/* Where section s's pane stands now: side by side with the others, the nearer way round the ring (so the pane slid
 * to comes in from the side of the arrow, the cutscenes' right of retail's after them). */
static float pane_x(const picker *p, int s) {
    float n = (float)SDL_max(1, pane_count(p)), d = (float)pane_slot(p, s) - p->slide;
    d -= n * SDL_floorf((d + n / 2) / n);
    return d * p->width;
}

static float pane_max_scroll(const picker *p, const layout *l, int s) {
    return SDL_max(0.0f, (float)rows_of(p->first[s + 1] - p->first[s], l->cols) * l->cell_h + l->inset - l->pane_h);
}

static SDL_FRect tile_rect(const picker *p, const layout *l, int i) {
    int s = p->entries[i].section;
    i -= p->first[s];
    float x = l->left + (float)(i % l->cols) * l->cell_w, y = (float)(i / l->cols) * l->cell_h;
    if (s < PICKER_PANES) {
        x += pane_x(p, s);
        y += pane_rect(p, l).y + l->inset - p->pane_scroll[s];
    } else {
        y += heading_y(p, l, s) + l->heading - p->scroll;
    }
    SDL_FRect r = {x, y, l->tile_w, l->tile_h + l->caption};
    return r;
}

static float max_scroll(const picker *p, const layout *l) {
    return SDL_max(0.0f, heading_y(p, l, PICKER_SECTIONS) - p->height);
}

/* The buttons, in target pixels: Controls top right, the big ones in a row at the top (they scroll with the grid). */
static SDL_FRect controls_rect(const picker *p) {
    float w = (float)p->controls_w + 24 * p->scale, h = (float)p->controls_h + 16 * p->scale;
    SDL_FRect r = {p->width - w - 16 * p->scale, 12 * p->scale, w, h};
    return r;
}

/* Big button k: each as wide as its label needs, the row centred, or as near as it gets without reaching Controls; a
 * window too narrow for that narrows them (draw_big_button fits the labels in). */
#define BIG_BUTTONS 3
static SDL_FRect big_rect(const picker *p, const layout *l, int k) {
    const int labels[BIG_BUTTONS] = {p->intro_w, p->random_w, p->replays_w};
    float s = p->scale, gap = 16 * s, stop = controls_rect(p).x - gap, w[BIG_BUTTONS], widths = 0;
    for (int i = 0; i < BIG_BUTTONS; i++) {
        w[i] = SDL_max((float)labels[i] + 40 * s, 200 * s);
        widths += w[i];
    }
    float gaps = (BIG_BUTTONS - 1) * gap, room = stop - l->pad - gaps;
    float fit = widths > room ? SDL_max(room, widths * 0.1f) / widths : 1, row = widths * fit + gaps;
    float x = SDL_max(l->pad, SDL_min(SDL_roundf((p->width - row) / 2), stop - row));
    for (int i = 0; i < k; i++) x += w[i] * fit + gap;
    SDL_FRect r = {SDL_roundf(x), l->pad + 4 * s - p->scroll, SDL_roundf(w[k] * fit), (float)p->intro_h + 28 * s};
    return r;
}
static SDL_FRect intro_rect(const picker *p, const layout *l) { return big_rect(p, l, 0); }
static SDL_FRect random_rect(const picker *p, const layout *l) { return big_rect(p, l, 1); }
static SDL_FRect replays_rect(const picker *p, const layout *l) { return big_rect(p, l, 2); }

/* The panes' arrows, at the ends of their heading line. */
static SDL_FRect arrow_rect(const picker *p, const layout *l, int dir) {
    float w = 56 * p->scale, h = 30 * p->scale, right = l->left + (float)l->cols * l->cell_w - l->pad;
    SDL_FRect r = {dir < 0 ? l->left : right - w, SDL_roundf(heading_y(p, l, 0) + (l->heading - h) / 2 - 6 * p->scale - p->scroll), w, h};
    return r;
}

static bool inside(SDL_FRect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

int picker_hit(picker *p, float x, float y) {
    layout l = layout_for(p, p->width);
    if (inside(controls_rect(p), x, y)) return PICKER_CONTROLS;
    if (inside(intro_rect(p, &l), x, y)) return PICKER_INTRO;
    if (inside(random_rect(p, &l), x, y)) return PICKER_RANDOM;
    if (inside(replays_rect(p, &l), x, y)) return PICKER_REPLAYS;
    if (can_slide(p) && inside(arrow_rect(p, &l, -1), x, y)) return PICKER_PANE_LEFT;
    if (can_slide(p) && inside(arrow_rect(p, &l, 1), x, y)) return PICKER_PANE_RIGHT;
    bool in_panes = inside(pane_rect(p, &l), x, y);  /* a pane's tiles end at its edges */
    for (int i = 0; i < p->count; i++)
        if ((in_panes || p->entries[i].section >= PICKER_PANES) && inside(tile_rect(p, &l, i), x, y)) return i;
    return -1;
}

void picker_set_hover(picker *p, float x, float y) { p->hover = picker_hit(p, x, y); }

/* A stage of either edition or an imported level (SSX courses, LibertyCity2001 ...) at random; not the cutscenes. */
int picker_random(const picker *p) {
    int found = 0, pick = -1;
    for (int i = 0; i < p->count; i++)
        if (p->entries[i].section != SECTION_CUTSCENES && !tc_level_is_cutscene(p->entries[i].name) && SDL_rand(++found) == 0)
            pick = i;
    return pick;
}

void picker_button(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_FRect r, SDL_GPUTexture *label, int lw, int lh, bool hot,
                   float scale) {
    gpu_quad(g, f, pass, NULL, r, NULL, hot ? (SDL_FColor){0.32f, 0.38f, 0.56f, 1} : (SDL_FColor){0.22f, 0.25f, 0.36f, 1});
    if (!label) return;
    SDL_FRect t = {SDL_roundf(r.x + (r.w - (float)lw) / 2), SDL_roundf(r.y + (r.h - (float)lh) / 2 + 2 * scale), (float)lw, (float)lh};
    gpu_quad(g, f, pass, label, t, NULL, (SDL_FColor){0.95f, 0.96f, 1, 1});
}

void picker_label(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_GPUTexture *label, SDL_FRect t, const SDL_FRect *uv,
                  float scale, SDL_FColor color) {
    float o = SDL_max(1.0f, SDL_roundf(1.5f * scale));
    gpu_quad(g, f, pass, label, (SDL_FRect){t.x + o, t.y + o, t.w, t.h}, uv, (SDL_FColor){0, 0, 0, 0.75f});
    gpu_quad(g, f, pass, label, t, uv, color);
}

/* The big buttons (as LevelPicker.swift's PickerButton, in flat quads): a glow when under the mouse, a light rim, the
 * colour with a lighter top half, the label white with a shadow. */
void picker_big_button(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_FRect r, SDL_GPUTexture *label, int lw, int lh,
                            bool hot, float scale, SDL_FColor c) {
    float k = hot ? 1.2f : 1, e = SDL_max(1.0f, SDL_roundf(scale)), glow = 6 * scale;
    SDL_FColor body = {SDL_min(1.0f, c.r * k), SDL_min(1.0f, c.g * k), SDL_min(1.0f, c.b * k), 1};
    if (hot) gpu_quad(g, f, pass, NULL, (SDL_FRect){r.x - glow, r.y - glow, r.w + 2 * glow, r.h + 2 * glow}, NULL,
                      (SDL_FColor){c.r, c.g, c.b, 0.35f});
    gpu_quad(g, f, pass, NULL, r, NULL, (SDL_FColor){1, 1, 1, 0.45f});
    SDL_FRect in = {r.x + e, r.y + e, r.w - 2 * e, r.h - 2 * e};
    gpu_quad(g, f, pass, NULL, in, NULL, body);
    gpu_quad(g, f, pass, NULL, (SDL_FRect){in.x, in.y, in.w, SDL_roundf(in.h / 2)}, NULL, (SDL_FColor){1, 1, 1, 0.16f});
    if (!label) return;
    float fit = SDL_min(1.0f, (r.w - 12 * scale) / (float)lw), w = (float)lw * fit, h = (float)lh * fit;  /* a narrow window */
    SDL_FRect t = {SDL_roundf(r.x + (r.w - w) / 2), SDL_roundf(r.y + (r.h - h) / 2 + 2 * scale), w, h};
    picker_label(g, f, pass, label, t, NULL, scale, (SDL_FColor){1, 1, 1, 1});
}

void picker_scroll(picker *p, float pixels) {
    layout l = layout_for(p, p->width);
    p->scroll = SDL_clamp(p->scroll + pixels, 0.0f, max_scroll(p, &l));
}

void picker_scroll_pane(picker *p, float pixels) {
    layout l = layout_for(p, p->width);
    float *scroll = &p->pane_scroll[p->pane], was = *scroll;
    *scroll = SDL_clamp(was + pixels, 0.0f, pane_max_scroll(p, &l, p->pane));
    picker_scroll(p, pixels - (*scroll - was));
}

void picker_wheel(picker *p, float x, float y, float pixels) {
    layout l = layout_for(p, p->width);
    if (p->first[PICKER_PANES] > 0 && inside(pane_rect(p, &l), x, y)) picker_scroll_pane(p, pixels);
    else picker_scroll(p, pixels);
}

void picker_pane(picker *p, int dir) {
    if (!can_slide(p) || dir == 0) return;
    dir = dir < 0 ? -1 : 1;
    p->pane = pane_next(p, p->pane, dir);
    p->slide_to += (float)dir;
    if (p->selected >= 0 && p->entries[p->selected].section < PICKER_PANES) p->selected = -1;
}

void picker_move(picker *p, int dx, int dy) {
    if (p->count == 0) return;
    if (p->selected < 0) { p->selected = p->first[PICKER_PANES] > 0 ? p->first[p->pane] : 0; }
    else {
        int cols = SDL_max(1, p->columns), i = p->selected, s = p->entries[i].section;
        int first = p->first[s], n = p->first[s + 1] - first, col = (i - first) % cols;
        int j = i - first + dx + dy * cols;  /* position within the section */
        bool at_side = dx < 0 ? col == 0 : dx > 0 && (col == cols - 1 || i == first + n - 1);
        if (s < PICKER_PANES && at_side && can_slide(p)) {  /* on to the same row of the pane there, round the ring */
            int t = pane_next(p, s, dx), m = p->first[t + 1] - p->first[t];
            int row = SDL_min((i - first) / cols, (m - 1) / cols);
            i = p->first[t] + SDL_min(row * cols + (dx > 0 ? 0 : cols - 1), m - 1);
            p->pane = t;
            p->slide_to += (float)dx;
        } else if (j >= 0 && j < n) i = first + j;
        else if (dy > 0) {  /* down from the last row: the first row of the next section with levels */
            for (int t = SDL_max(s + 1, PICKER_PANES); t < PICKER_SECTIONS; t++)
                if (has_levels(p, t)) { i = p->first[t] + SDL_min(col, p->first[t + 1] - p->first[t] - 1); break; }
        } else if (dy < 0 && s >= PICKER_PANES) {  /* up from the first row: the last row of the previous one */
            for (int k = s - 1; k >= 0; k--) {
                int t = k < PICKER_PANES ? p->pane : k;  /* the panes: the one showing */
                if (has_levels(p, t)) {
                    int last_row = p->first[t] + (p->first[t + 1] - p->first[t] - 1) / cols * cols;
                    i = SDL_min(last_row + col, p->first[t + 1] - 1);
                    break;
                }
            }
        }
        p->selected = i;
    }
    /* Keep the selection on screen: first in its pane, then on the page. */
    layout l = layout_for(p, p->width);
    SDL_FRect r = tile_rect(p, &l, p->selected);
    int s = p->entries[p->selected].section;
    if (s < PICKER_PANES) {
        SDL_FRect v = pane_rect(p, &l);
        float by = r.y < v.y + l.inset ? r.y - v.y - l.inset : r.y + r.h > v.y + v.h ? r.y + r.h - v.y - v.h : 0;
        p->pane_scroll[s] = SDL_clamp(p->pane_scroll[s] + by, 0.0f, pane_max_scroll(p, &l, s));
        r = tile_rect(p, &l, p->selected);
    }
    if (r.y < l.pad) picker_scroll(p, r.y - l.pad);
    else if (r.y + r.h > p->height - l.pad) picker_scroll(p, r.y + r.h - p->height + l.pad);
}

/* One tile, a caption under its picture. */
static void draw_tile(picker *p, gpu_frame *f, SDL_GPURenderPass *pass, const layout *l, int i) {
    gpu *g = p->g;
    const picker_entry *e = &p->entries[i];
    SDL_FRect r = tile_rect(p, l, i);
    bool hot = i == p->hover || i == p->selected;
    if (hot) {
        float o = 6 * p->scale;
        SDL_FRect h = {r.x - o, r.y - o, r.w + 2 * o, r.h + 2 * o};
        gpu_quad(g, f, pass, NULL, h, NULL, (SDL_FColor){0.25f, 0.3f, 0.45f, 1});
    }
    /* The image keeps its aspect inside the tile (scaleProportionallyUpOrDown), centred. */
    SDL_FRect img = {r.x, r.y, l->tile_w, l->tile_h};
    if (e->thumb && e->thumb_w > 0 && e->thumb_h > 0) {
        float k = SDL_min(l->tile_w / (float)e->thumb_w, l->tile_h / (float)e->thumb_h);
        float w = (float)e->thumb_w * k, h = (float)e->thumb_h * k;
        SDL_FRect fit = {SDL_roundf(img.x + (l->tile_w - w) / 2), SDL_roundf(img.y + (l->tile_h - h) / 2), w, h};
        gpu_quad(g, f, pass, e->thumb, fit, NULL, (SDL_FColor){1, 1, 1, 1});
    } else {
        gpu_quad(g, f, pass, NULL, img, NULL, (SDL_FColor){0.14f, 0.15f, 0.22f, 1});
        if (e->section == SECTION_IMPORTED && p->unknown) {  /* imported: a big question mark (the 8x8 font, scaled up) */
            float h = l->tile_h * 0.6f, w = h * (float)p->unknown_w / (float)SDL_max(1, p->unknown_h);
            SDL_FRect q = {SDL_roundf(img.x + (l->tile_w - w) / 2), SDL_roundf(img.y + (l->tile_h - h) / 2), w, h};
            gpu_quad_sampled(g, f, pass, p->unknown, g->nearest_sampler, q, NULL, (SDL_FColor){0.55f, 0.6f, 0.75f, 1});
        }
    }
    if (e->caption) { /* (the Controls button is drawn after the tiles, over them) */
        float cw = (float)e->caption_w, ch = (float)e->caption_h;
        if (cw > l->tile_w) cw = l->tile_w;  /* very long names: cut at the tile edge */
        SDL_FRect uv = {0, 0, cw / (float)e->caption_w, 1};
        SDL_FRect cr = {SDL_roundf(r.x + (l->tile_w - cw) / 2), SDL_roundf(r.y + l->tile_h + (l->caption - ch) / 2 + 2 * p->scale),
                        cw, ch};
        picker_label(g, f, pass, e->caption, cr, &uv, p->scale, (SDL_FColor){1, 1, 1, 1});
    }
}

/* The panes' slide, by the time since the last frame; once there, both back into [0, n) (the same place in the ring). */
static void ease_slide(picker *p) {
    Uint64 now = SDL_GetTicksNS();
    float dt = p->slide_ns ? (float)(now - p->slide_ns) / 1e9f : 0, to = p->slide_to;
    p->slide_ns = now;
    p->slide += (to - p->slide) * (1 - SDL_expf(-14 * dt));
    if (SDL_fabsf(to - p->slide) < 0.001f) {
        float n = (float)SDL_max(1, pane_count(p)), k = SDL_floorf(to / n) * n;
        p->slide = p->slide_to = to - k;
    }
}

/* What the panes show now, cut at their top and bottom, with a scroll bar for those with more. */
static void draw_panes(picker *p, gpu_frame *f, SDL_GPURenderPass *pass, const layout *l) {
    SDL_FRect v = pane_rect(p, l);
    int top = (int)SDL_max(0.0f, SDL_roundf(v.y)), bottom = (int)SDL_min(p->height, SDL_roundf(v.y + v.h));
    if (bottom <= top) return;  /* scrolled off the page */
    SDL_SetGPUScissor(pass, &(SDL_Rect){0, top, (int)p->width, bottom - top});
    for (int s = 0; s < PICKER_PANES; s++) {
        if (!has_levels(p, s)) continue;
        float x = pane_x(p, s), most = pane_max_scroll(p, l, s);
        if (SDL_fabsf(x) >= p->width) continue;
        for (int i = p->first[s]; i < p->first[s + 1]; i++) {
            SDL_FRect r = tile_rect(p, l, i);
            if (r.y + r.h >= v.y && r.y <= v.y + v.h) draw_tile(p, f, pass, l, i);
        }
        if (most > 0) {
            float h = SDL_roundf(v.h * v.h / (v.h + most)), w = 4 * p->scale;
            SDL_FRect bar = {x + l->left + (float)l->cols * l->cell_w - l->pad + 8 * p->scale,
                             SDL_roundf(v.y + (v.h - h) * p->pane_scroll[s] / most), w, h};
            gpu_quad(p->g, f, pass, NULL, bar, NULL, (SDL_FColor){0.45f, 0.48f, 0.6f, 1});
        }
    }
    SDL_SetGPUScissor(pass, &(SDL_Rect){0, 0, (int)p->width, (int)p->height});
}

void picker_draw(picker *p, gpu_frame *f, SDL_GPURenderPass *pass) {
    gpu *g = p->g;
    p->width = (float)f->width;
    p->height = (float)f->height;
    layout l = layout_for(p, p->width);
    p->columns = l.cols;
    p->scroll = SDL_clamp(p->scroll, 0.0f, max_scroll(p, &l));
    for (int s = 0; s < PICKER_PANES; s++) p->pane_scroll[s] = SDL_clamp(p->pane_scroll[s], 0.0f, pane_max_scroll(p, &l, s));
    ease_slide(p);
    SDL_FRect ir = intro_rect(p, &l), nr = random_rect(p, &l), dr = replays_rect(p, &l);
    if (ir.y + ir.h > 0) {
        picker_big_button(g, f, pass, ir, p->intro_text, p->intro_w, p->intro_h, p->hover == PICKER_INTRO, p->scale,
                        (SDL_FColor){0.88f, 0.32f, 0.3f, 1});
        picker_big_button(g, f, pass, nr, p->random_text, p->random_w, p->random_h, p->hover == PICKER_RANDOM, p->scale,
                        (SDL_FColor){0.25f, 0.65f, 0.38f, 1});
        picker_big_button(g, f, pass, dr, p->replays_text, p->replays_w, p->replays_h, p->hover == PICKER_REPLAYS, p->scale,
                        (SDL_FColor){0.2f, 0.55f, 0.85f, 1});
    }
    for (int k = 0; k < PICKER_SECTIONS; k++) {
        if (!p->heading[k]) continue;
        /* The panes' headings ride with them, centred between the arrows. */
        bool in_pane = k < PICKER_PANES;
        float y = heading_y(p, &l, k) - p->scroll, x = in_pane ? pane_x(p, k) : 0;
        if (in_pane && SDL_fabsf(x) >= p->width) continue;
        SDL_FRect line = {l.left + x, SDL_roundf(y + l.heading - 8 * p->scale), (float)l.cols * l.cell_w - l.pad, SDL_max(1.0f, p->scale)};
        SDL_FRect hr = {in_pane ? SDL_roundf(x + (p->width - (float)p->heading_w[k]) / 2) : l.left,
                        SDL_roundf(y + (l.heading - (float)p->heading_h[k]) / 2 - 4 * p->scale),
                        (float)p->heading_w[k], (float)p->heading_h[k]};
        picker_label(g, f, pass, p->heading[k], hr, NULL, p->scale, (SDL_FColor){0.92f, 0.93f, 0.97f, 1});
        gpu_quad(g, f, pass, NULL, line, NULL, (SDL_FColor){0.3f, 0.32f, 0.42f, 1});
    }
    if (can_slide(p))
        picker_button(g, f, pass, arrow_rect(p, &l, -1), p->left_text, p->left_w, p->left_h, p->hover == PICKER_PANE_LEFT, p->scale);
    if (can_slide(p))
        picker_button(g, f, pass, arrow_rect(p, &l, 1), p->right_text, p->right_w, p->right_h, p->hover == PICKER_PANE_RIGHT, p->scale);
    draw_panes(p, f, pass, &l);
    for (int i = p->first[PICKER_PANES]; i < p->count; i++) {
        SDL_FRect r = tile_rect(p, &l, i);
        if (r.y + r.h >= 0 && r.y <= p->height) draw_tile(p, f, pass, &l, i);
    }
    picker_button(g, f, pass, controls_rect(p), p->controls_text, p->controls_w, p->controls_h, p->hover == PICKER_CONTROLS,
                p->scale);
}
