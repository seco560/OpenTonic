#include "picker.h"
#include <tonic/tonic.h>
#include "util.h"

static const char *const section_titles[3] = {"Levels", "Cutscenes", "Imported"};

/* By section, then by name. */
static int compare_entries(const void *a, const void *b) {
    const picker_entry *x = (const picker_entry *)a, *y = (const picker_entry *)b;
    return x->section != y->section ? x->section - y->section : SDL_strcasecmp(x->name, y->name);
}

static int section_of(const char *name, const char *imported) {
    char *dir = imported ? path_join(imported, name) : NULL;
    bool is_imported = dir && path_is_dir(dir);
    SDL_free(dir);
    return is_imported ? 2 : tc_level_is_cutscene(name) ? 1 : 0;
}

void picker_open(picker *p, gpu *g, const char *folder, const char *thumbnails, float scale) {
    SDL_zerop(p);
    p->g = g;
    p->scale = scale > 0 ? scale : 1;
    p->hover = p->selected = -1;
    int n = 0;
    char **levels = SDL_GlobDirectory(folder, "*.ttlv", SDL_GLOB_CASEINSENSITIVE, &n);
    if (!levels || n == 0) { SDL_free(levels); return; }
    p->entries = SDL_calloc((size_t)n, sizeof *p->entries);
    if (!p->entries) { SDL_free(levels); return; }
    for (int i = 0; i < n; i++) {
        p->entries[i].path = path_join(folder, levels[i]);
        p->entries[i].name = path_stem(levels[i]);
        if (p->entries[i].path && p->entries[i].name) p->count++;
        else { SDL_free(p->entries[i].path); SDL_free(p->entries[i].name); SDL_zero(p->entries[i]); }
    }
    SDL_free(levels);
    /* Compact out failed entries, then sort by section and name. Imported levels have out/Imported/<name>. */
    char *out = path_dir(folder), *imported = out ? path_join(out, "Imported") : NULL;
    int k = 0;
    for (int i = 0; i < n; i++)
        if (p->entries[i].path) {
            p->entries[i].section = section_of(p->entries[i].name, imported);
            p->entries[k++] = p->entries[i];
        }
    SDL_free(out);
    SDL_free(imported);
    SDL_qsort(p->entries, (size_t)p->count, sizeof *p->entries, compare_entries);
    for (int s = 0, i = 0; s < 4; s++) {
        while (i < p->count && p->entries[i].section < s) i++;
        p->first[s] = s == 3 ? p->count : i;
    }

    int thumb_n = 0;
    char **thumbs = SDL_GlobDirectory(thumbnails, "*.png", SDL_GLOB_CASEINSENSITIVE, &thumb_n);
    gpu_upload u;
    bool uploading = gpu_upload_begin(g, &u);
    int text_scale = SDL_max(1, (int)SDL_roundf(2 * p->scale));
    for (int i = 0; i < p->count; i++) {
        picker_entry *e = &p->entries[i];
        for (int t = 0; uploading && thumbs && t < thumb_n; t++) {
            char *stem = path_stem(thumbs[t]);
            bool match = stem && SDL_strcasecmp(stem, e->name) == 0;
            SDL_free(stem);
            if (!match) continue;
            char *path = path_join(thumbnails, thumbs[t]);
            e->thumb = gpu_upload_png(g, &u, path, true, &e->thumb_w, &e->thumb_h);
            if (!e->thumb) SDL_Log("thumbnail %s: %s", path, SDL_GetError());
            SDL_free(path);
            break;
        }
        e->caption = gpu_text(g, e->name, text_scale, &e->caption_w, &e->caption_h);
    }
    if (uploading) gpu_upload_end(g, &u);
    SDL_free(thumbs);
    for (int s = 0; s < 3; s++)
        if (p->first[s] < p->first[s + 1])
            p->heading[s] = gpu_text(g, section_titles[s], text_scale, &p->heading_w[s], &p->heading_h[s]);
    if (p->first[2] < p->count) p->unknown = gpu_text(g, "?", 1, &p->unknown_w, &p->unknown_h);
    p->intro_text = gpu_text(g, "Play Intro Video", text_scale + 1, &p->intro_w, &p->intro_h);
    p->controls_text = gpu_text(g, "Controls (F1)", text_scale, &p->controls_w, &p->controls_h);
}

void picker_close(picker *p) {
    for (int i = 0; i < p->count; i++) {
        picker_entry *e = &p->entries[i];
        if (e->thumb) SDL_ReleaseGPUTexture(p->g->device, e->thumb);
        if (e->caption) SDL_ReleaseGPUTexture(p->g->device, e->caption);
        SDL_free(e->name);
        SDL_free(e->path);
    }
    for (int k = 0; k < 3; k++)
        if (p->heading[k]) SDL_ReleaseGPUTexture(p->g->device, p->heading[k]);
    if (p->unknown) SDL_ReleaseGPUTexture(p->g->device, p->unknown);
    if (p->intro_text) SDL_ReleaseGPUTexture(p->g->device, p->intro_text);
    if (p->controls_text) SDL_ReleaseGPUTexture(p->g->device, p->controls_text);
    SDL_free(p->entries);
    SDL_zerop(p);
}

/* Layout, as LevelPicker.relayout: 256x192 tiles with a caption line, centred columns; each section starts on a new
 * row under a heading line. */
typedef struct layout { float tile_w, tile_h, caption, pad, cell_w, cell_h, left, heading, intro; int cols; } layout;

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
    l.intro = 64 * s;  /* the "Play Intro Video" button's band, above the first heading */
    l.cols = SDL_max(1, (int)((width - l.pad) / l.cell_w));
    l.left = SDL_max(l.pad, (width - (float)l.cols * l.cell_w + l.pad) / 2);
    return l;
}

static int rows_of(int n, int cols) { return (n + cols - 1) / cols; }

/* Top of section k's heading (content coordinates, before scrolling); k = 3: the end. Empty sections take no space. */
static float heading_y(const picker *p, const layout *l, int k) {
    float y = l->pad + l->intro;
    for (int s = 0; s < k; s++)
        if (p->first[s + 1] > p->first[s]) y += l->heading + (float)rows_of(p->first[s + 1] - p->first[s], l->cols) * l->cell_h;
    return y;
}

static SDL_FRect tile_rect(const picker *p, const layout *l, int i) {
    int s = p->entries[i].section;
    float top = heading_y(p, l, s) + l->heading;
    i -= p->first[s];
    SDL_FRect r = {l->left + (float)(i % l->cols) * l->cell_w, top + (float)(i / l->cols) * l->cell_h - p->scroll,
                   l->tile_w, l->tile_h + l->caption};
    return r;
}

static float max_scroll(const picker *p, const layout *l) {
    return SDL_max(0.0f, heading_y(p, l, 3) - p->height);
}

/* The two buttons, in target pixels: the intro one centred at the top (scrolls with the grid), Controls top right. */
static SDL_FRect intro_rect(const picker *p, const layout *l) {
    float w = SDL_max((float)p->intro_w + 60 * p->scale, 320 * p->scale), h = (float)p->intro_h + 24 * p->scale;
    SDL_FRect r = {SDL_roundf((p->width - w) / 2), l->pad - p->scroll, w, h};
    return r;
}

static SDL_FRect controls_rect(const picker *p) {
    float w = (float)p->controls_w + 24 * p->scale, h = (float)p->controls_h + 16 * p->scale;
    SDL_FRect r = {p->width - w - 16 * p->scale, 12 * p->scale, w, h};
    return r;
}

static bool inside(SDL_FRect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

int picker_hit(picker *p, float x, float y) {
    layout l = layout_for(p, p->width);
    if (inside(controls_rect(p), x, y)) return PICKER_CONTROLS;
    if (inside(intro_rect(p, &l), x, y)) return PICKER_INTRO;
    for (int i = 0; i < p->count; i++) {
        SDL_FRect r = tile_rect(p, &l, i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
    }
    return -1;
}

void picker_set_hover(picker *p, float x, float y) { p->hover = picker_hit(p, x, y); }

static void draw_button(gpu *g, gpu_frame *f, SDL_GPURenderPass *pass, SDL_FRect r, SDL_GPUTexture *label, int lw, int lh,
                        bool hot, float scale) {
    gpu_quad(g, f, pass, NULL, r, NULL, hot ? (SDL_FColor){0.32f, 0.38f, 0.56f, 1} : (SDL_FColor){0.22f, 0.25f, 0.36f, 1});
    if (!label) return;
    SDL_FRect t = {SDL_roundf(r.x + (r.w - (float)lw) / 2), SDL_roundf(r.y + (r.h - (float)lh) / 2 + 2 * scale), (float)lw, (float)lh};
    gpu_quad(g, f, pass, label, t, NULL, (SDL_FColor){0.95f, 0.96f, 1, 1});
}

void picker_scroll(picker *p, float pixels) {
    layout l = layout_for(p, p->width);
    p->scroll = SDL_clamp(p->scroll + pixels, 0.0f, max_scroll(p, &l));
}

void picker_move(picker *p, int dx, int dy) {
    if (p->count == 0) return;
    if (p->selected < 0) { p->selected = 0; }
    else {
        int cols = SDL_max(1, p->columns), i = p->selected, s = p->entries[i].section;
        int first = p->first[s], n = p->first[s + 1] - first, col = (i - first) % cols;
        int j = i - first + dx + dy * cols;  /* position within the section */
        if (j >= 0 && j < n) i = first + j;
        else if (dy > 0) {  /* down from the last row: the first row of the next section with levels */
            for (int t = s + 1; t < 3; t++)
                if (p->first[t + 1] > p->first[t]) { i = p->first[t] + SDL_min(col, p->first[t + 1] - p->first[t] - 1); break; }
        } else if (dy < 0) {  /* up from the first row: the last row of the previous one */
            for (int t = s - 1; t >= 0; t--)
                if (p->first[t + 1] > p->first[t]) {
                    int last_row = p->first[t] + (p->first[t + 1] - p->first[t] - 1) / cols * cols;
                    i = SDL_min(last_row + col, p->first[t + 1] - 1);
                    break;
                }
        }
        p->selected = i;
    }
    /* Keep the selection on screen. */
    layout l = layout_for(p, p->width);
    SDL_FRect r = tile_rect(p, &l, p->selected);
    if (r.y < l.pad) picker_scroll(p, r.y - l.pad);
    else if (r.y + r.h > p->height - l.pad) picker_scroll(p, r.y + r.h - p->height + l.pad);
}

void picker_draw(picker *p, gpu_frame *f, SDL_GPURenderPass *pass) {
    gpu *g = p->g;
    p->width = (float)f->width;
    p->height = (float)f->height;
    layout l = layout_for(p, p->width);
    p->columns = l.cols;
    p->scroll = SDL_clamp(p->scroll, 0.0f, max_scroll(p, &l));
    SDL_FRect ir = intro_rect(p, &l);
    if (ir.y + ir.h > 0)
        draw_button(g, f, pass, ir, p->intro_text, p->intro_w, p->intro_h, p->hover == PICKER_INTRO, p->scale);
    for (int k = 0; k < 3; k++) {
        if (!p->heading[k]) continue;
        float y = heading_y(p, &l, k) - p->scroll;
        SDL_FRect line = {l.left, SDL_roundf(y + l.heading - 8 * p->scale), (float)l.cols * l.cell_w - l.pad, SDL_max(1.0f, p->scale)};
        SDL_FRect hr = {l.left, SDL_roundf(y + (l.heading - (float)p->heading_h[k]) / 2 - 4 * p->scale),
                        (float)p->heading_w[k], (float)p->heading_h[k]};
        gpu_quad(g, f, pass, p->heading[k], hr, NULL, (SDL_FColor){0.6f, 0.63f, 0.72f, 1});
        gpu_quad(g, f, pass, NULL, line, NULL, (SDL_FColor){0.3f, 0.32f, 0.42f, 1});
    }
    for (int i = 0; i < p->count; i++) {
        const picker_entry *e = &p->entries[i];
        SDL_FRect r = tile_rect(p, &l, i);
        if (r.y + r.h < 0 || r.y > p->height) continue;
        bool hot = i == p->hover || i == p->selected;
        if (hot) {
            float o = 6 * p->scale;
            SDL_FRect h = {r.x - o, r.y - o, r.w + 2 * o, r.h + 2 * o};
            gpu_quad(g, f, pass, NULL, h, NULL, (SDL_FColor){0.25f, 0.3f, 0.45f, 1});
        }
        /* The image keeps its aspect inside the tile (scaleProportionallyUpOrDown), centred. */
        SDL_FRect img = {r.x, r.y, l.tile_w, l.tile_h};
        if (e->thumb && e->thumb_w > 0 && e->thumb_h > 0) {
            float k = SDL_min(l.tile_w / (float)e->thumb_w, l.tile_h / (float)e->thumb_h);
            float w = (float)e->thumb_w * k, h = (float)e->thumb_h * k;
            SDL_FRect fit = {SDL_roundf(img.x + (l.tile_w - w) / 2), SDL_roundf(img.y + (l.tile_h - h) / 2), w, h};
            gpu_quad(g, f, pass, e->thumb, fit, NULL, (SDL_FColor){1, 1, 1, 1});
        } else {
            gpu_quad(g, f, pass, NULL, img, NULL, (SDL_FColor){0.14f, 0.15f, 0.22f, 1});
            if (e->section == 2 && p->unknown) {  /* imported: a big question mark (the 8x8 font, scaled up) */
                float h = l.tile_h * 0.6f, w = h * (float)p->unknown_w / (float)SDL_max(1, p->unknown_h);
                SDL_FRect q = {SDL_roundf(img.x + (l.tile_w - w) / 2), SDL_roundf(img.y + (l.tile_h - h) / 2), w, h};
                gpu_quad_sampled(g, f, pass, p->unknown, g->nearest_sampler, q, NULL, (SDL_FColor){0.55f, 0.6f, 0.75f, 1});
            }
        }
        if (e->caption) { /* (the Controls button is drawn after the tiles, over them) */
            float cw = (float)e->caption_w, ch = (float)e->caption_h;
            if (cw > l.tile_w) cw = l.tile_w;  /* very long names: cut at the tile edge */
            SDL_FRect uv = {0, 0, cw / (float)e->caption_w, 1};
            SDL_FRect cr = {SDL_roundf(r.x + (l.tile_w - cw) / 2), SDL_roundf(r.y + l.tile_h + (l.caption - ch) / 2 + 2 * p->scale),
                            cw, ch};
            gpu_quad(g, f, pass, e->caption, cr, &uv, (SDL_FColor){0.92f, 0.93f, 0.96f, 1});
        }
    }
    draw_button(g, f, pass, controls_rect(p), p->controls_text, p->controls_w, p->controls_h, p->hover == PICKER_CONTROLS,
                p->scale);
}
