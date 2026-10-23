#include "picker.h"
#include "util.h"

static int compare_entries(const void *a, const void *b) {
    return SDL_strcasecmp(((const picker_entry *)a)->name, ((const picker_entry *)b)->name);
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
    /* Compact out failed entries, then sort by name. */
    int k = 0;
    for (int i = 0; i < n; i++) if (p->entries[i].path) p->entries[k++] = p->entries[i];
    SDL_qsort(p->entries, (size_t)p->count, sizeof *p->entries, compare_entries);

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
}

void picker_close(picker *p) {
    for (int i = 0; i < p->count; i++) {
        picker_entry *e = &p->entries[i];
        if (e->thumb) SDL_ReleaseGPUTexture(p->g->device, e->thumb);
        if (e->caption) SDL_ReleaseGPUTexture(p->g->device, e->caption);
        SDL_free(e->name);
        SDL_free(e->path);
    }
    SDL_free(p->entries);
    SDL_zerop(p);
}

/* Layout, as LevelPicker.relayout: 256x192 tiles with a caption line, centred columns. */
typedef struct layout { float tile_w, tile_h, caption, pad, cell_w, cell_h, left; int cols; } layout;

static layout layout_for(const picker *p, float width) {
    layout l;
    float s = p->scale;
    l.tile_w = 256 * s;
    l.tile_h = 192 * s;
    l.caption = 26 * s;
    l.pad = 20 * s;
    l.cell_w = l.tile_w + l.pad;
    l.cell_h = l.tile_h + l.caption + l.pad;
    l.cols = SDL_max(1, (int)((width - l.pad) / l.cell_w));
    l.left = SDL_max(l.pad, (width - (float)l.cols * l.cell_w + l.pad) / 2);
    return l;
}

static SDL_FRect tile_rect(const picker *p, const layout *l, int i) {
    SDL_FRect r = {l->left + (float)(i % l->cols) * l->cell_w, l->pad + (float)(i / l->cols) * l->cell_h - p->scroll,
                   l->tile_w, l->tile_h + l->caption};
    return r;
}

static float max_scroll(const picker *p, const layout *l) {
    int rows = (p->count + l->cols - 1) / l->cols;
    return SDL_max(0.0f, l->pad + (float)rows * l->cell_h - p->height);
}

int picker_hit(picker *p, float x, float y) {
    layout l = layout_for(p, p->width);
    for (int i = 0; i < p->count; i++) {
        SDL_FRect r = tile_rect(p, &l, i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
    }
    return -1;
}

void picker_set_hover(picker *p, float x, float y) { p->hover = picker_hit(p, x, y); }

void picker_scroll(picker *p, float pixels) {
    layout l = layout_for(p, p->width);
    p->scroll = SDL_clamp(p->scroll + pixels, 0.0f, max_scroll(p, &l));
}

void picker_move(picker *p, int dx, int dy) {
    if (p->count == 0) return;
    if (p->selected < 0) { p->selected = 0; }
    else {
        int cols = SDL_max(1, p->columns);
        int i = p->selected + dx + dy * cols;
        if (i >= 0 && i < p->count) p->selected = i;
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
        }
        if (e->caption) {
            float cw = (float)e->caption_w, ch = (float)e->caption_h;
            if (cw > l.tile_w) cw = l.tile_w;  /* very long names: cut at the tile edge */
            SDL_FRect uv = {0, 0, cw / (float)e->caption_w, 1};
            SDL_FRect cr = {SDL_roundf(r.x + (l.tile_w - cw) / 2), SDL_roundf(r.y + l.tile_h + (l.caption - ch) / 2 + 2 * p->scale),
                            cw, ch};
            gpu_quad(g, f, pass, e->caption, cr, &uv, (SDL_FColor){0.92f, 0.93f, 0.96f, 1});
        }
    }
}
