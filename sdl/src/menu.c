/* The main menu, saved games and options: menu.h. */
#include "menu.h"
#include "util.h"

/* ---- files ---- */

static char *pref_file(const char *name) {
    char *pref = SDL_GetPrefPath("", "OpenTonic");
    char *p = pref ? path_join(pref, name) : NULL;
    SDL_free(pref);
    return p;
}

char *saves_folder(void) {
    const char *env = SDL_getenv("TONIC_SAVES");
    return env && *env ? SDL_strdup(env) : pref_file("Saves");
}

static char *save_path_named(const char *name) {
    char file[64], *folder = saves_folder(), *path;
    tc_save_file_name(name, file, sizeof file - 8);
    SDL_strlcat(file, "." TC_SAVE_EXTENSION, sizeof file);
    path = folder ? path_join(folder, file) : NULL;
    SDL_free(folder);
    return path;
}

bool saves_write(tc_viewer *v, const char *name) {
    size_t size = 0;
    SDL_Time now = 0;
    char *folder = saves_folder(), *path = save_path_named(name);
    uint8_t *bytes;
    if (!SDL_GetCurrentTime(&now) || now < 0) now = 0;
    bytes = tc_save_make(v, name, (uint64_t)(now / SDL_NS_PER_SECOND), &size);
    bool ok = bytes && folder && path && SDL_CreateDirectory(folder) && SDL_SaveFile(path, bytes, size);
    if (bytes && !ok) SDL_Log("Can't save %s: %s", path ? path : name, SDL_GetError());
    tc_free(bytes);
    SDL_free(folder);
    SDL_free(path);
    return ok;
}

bool saves_read(const char *path, tc_save_game *out) {
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    tc_result r = data ? tc_save_read(data, size, out) : TC_ERR_TRUNCATED;
    if (r != TC_OK) SDL_Log("%s: %s", path, tc_result_string(r));
    SDL_free(data);
    return r == TC_OK;
}

void saves_delete_named(const char *name) {
    char *path = save_path_named(name);
    if (path) SDL_RemovePath(path);
    SDL_free(path);
}

/* The list LOAD and DELETE show, newest first (the menu's indices are into it). */
typedef struct save_entry { char *path; tc_save_info info; } save_entry;
static save_entry *g_saves;
static int g_save_count;

static int newest_first(const void *a, const void *b) {
    const save_entry *x = (const save_entry *)a, *y = (const save_entry *)b;
    return x->info.time < y->info.time ? 1 : x->info.time > y->info.time ? -1 : 0;
}

static void saves_scan(void) {
    char *folder = saves_folder();
    int n = 0;
    char **files = folder ? SDL_GlobDirectory(folder, "*." TC_SAVE_EXTENSION, 0, &n) : NULL;
    for (int i = 0; i < g_save_count; i++) SDL_free(g_saves[i].path);
    SDL_free(g_saves);
    g_saves = n > 0 ? SDL_calloc((size_t)n, sizeof *g_saves) : NULL;
    g_save_count = 0;
    for (int i = 0; g_saves && i < n; i++) {
        size_t size = 0;
        char *path = path_join(folder, files[i]);
        void *data = path ? SDL_LoadFile(path, &size) : NULL;
        save_entry *e = &g_saves[g_save_count];
        if (data && tc_save_read_info(data, size, &e->info)) {
            e->path = path;
            path = NULL;
            g_save_count++;
        }
        SDL_free(data);
        SDL_free(path);
    }
    if (g_save_count > 1) SDL_qsort(g_saves, (size_t)g_save_count, sizeof *g_saves, newest_first);
    SDL_free(files);
    SDL_free(folder);
}

char *menu_save_path(int i) { return i >= 0 && i < g_save_count ? SDL_strdup(g_saves[i].path) : NULL; }

void menu_refresh_games(menu_screen *s) {
    saves_scan();
    tc_save_info *infos = g_save_count ? SDL_calloc((size_t)g_save_count, sizeof *infos) : NULL;
    for (int i = 0; infos && i < g_save_count; i++) infos[i] = g_saves[i].info;
    if (s->m) tc_save_menu_games(s->m, infos, infos ? g_save_count : 0);
    SDL_free(infos);
}

void options_load(tc_options *o) {
    size_t size = 0;
    char *path = pref_file("options.cfg");
    void *text = path ? SDL_LoadFile(path, &size) : NULL;
    tc_options_parse(o, (const char *)text, text ? size : 0);
    SDL_free(text);
    SDL_free(path);
}

void options_store(const tc_options *o) {
    char text[512], *path = pref_file("options.cfg");
    size_t n = tc_options_write(o, text, sizeof text);
    if (path && !SDL_SaveFile(path, text, n)) SDL_Log("Can't write %s: %s", path, SDL_GetError());
    SDL_free(path);
}

/* ---- the screen ---- */

static SDL_GPUTexture *upload(gpu *g, const uint8_t *rgba, int w, int h) {
    SDL_GPUTexture *t = NULL;
    SDL_Surface *s = rgba && w > 0 && h > 0 ? SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, (void *)rgba, w * 4) : NULL;
    gpu_upload u;
    if (s && gpu_upload_begin(g, &u)) {
        t = gpu_upload_surface(g, &u, s, false);
        gpu_upload_end(g, &u);
    }
    SDL_DestroySurface(s);
    return t;
}

/* The swirls' picture with its alpha x TC_MENU_SWIRL_ALPHA: the overlay pipeline draws a picture as it is. */
static SDL_GPUTexture *upload_swirl(gpu *g, const char *path) {
    SDL_Surface *raw = path ? SDL_LoadPNG(path) : NULL, *s = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : NULL;
    SDL_GPUTexture *t = NULL;
    gpu_upload u;
    SDL_DestroySurface(raw);
    if (!s) return NULL;
    for (int y = 0; y < s->h; y++) {
        Uint8 *row = (Uint8 *)s->pixels + y * s->pitch;
        for (int x = 0; x < s->w; x++) row[4 * x + 3] = (Uint8)(row[4 * x + 3] * TC_MENU_SWIRL_ALPHA + 0.5f);
    }
    if (gpu_upload_begin(g, &u)) {
        t = gpu_upload_surface(g, &u, s, false);
        gpu_upload_end(g, &u);
    }
    SDL_DestroySurface(s);
    return t;
}

bool menu_open(menu_screen *s, gpu *g, SDL_Window *window, const char *level_folder, const tc_options *options,
               const char *swirl) {
    size_t size = 0;
    char *path = path_join(level_folder, "menu.ttmn");
    void *data = path ? SDL_LoadFile(path, &size) : NULL;
    tc_result r;
    SDL_zerop(s);
    s->g = g;
    s->m = SDL_malloc(sizeof *s->m);
    if (!s->m || !data) {
        SDL_Log("No %s: the levels grid instead (run the conversion)", path ? path : "menu.ttmn");
        SDL_free(s->m);
        s->m = NULL;
        SDL_free(data);
        SDL_free(path);
        return false;
    }
    tc_menu_init(s->m);
    r = tc_menu_load(s->m, data, size);
    SDL_free(data);
    if (r != TC_OK) {
        SDL_Log("%s: %s", path, tc_result_string(r));
        SDL_free(path);
        menu_close(s, window);
        return false;
    }
    SDL_free(path);
    s->atlas = upload(g, s->m->data.atlas, s->m->data.atlas_width, s->m->data.atlas_height);
    for (int i = 0; i < s->m->data.image_count && i < TC_MENU_MAX_LAYERS; i++)
        s->images[i] = upload(g, s->m->data.images[i].rgba, s->m->data.images[i].width, s->m->data.images[i].height);
    s->swirl = swirl ? upload_swirl(g, swirl) : NULL;
    tc_menu_set_options(s->m, options);
    menu_refresh_games(s);
    return true;
}

void menu_close(menu_screen *s, SDL_Window *window) {
    if (s->typing) SDL_StopTextInput(window);
    if (s->g) {
        SDL_WaitForGPUIdle(s->g->device);
        if (s->atlas) SDL_ReleaseGPUTexture(s->g->device, s->atlas);
        for (int i = 0; i < TC_MENU_MAX_LAYERS; i++)
            if (s->images[i]) SDL_ReleaseGPUTexture(s->g->device, s->images[i]);
        if (s->swirl) SDL_ReleaseGPUTexture(s->g->device, s->swirl);
    }
    if (s->m) tc_menu_free(s->m);
    SDL_free(s->m);
    SDL_zerop(s);
}

static tc_menu_command press(menu_screen *s, tc_menu_input in) {
    tc_menu_input_press(s->m, in);
    return tc_menu_take_command(s->m);
}

tc_menu_command menu_key(menu_screen *s, const SDL_KeyboardEvent *k) {
    bool naming = s->m->screen == TC_MS_NEW_GAME;
    switch (k->scancode) {
    case SDL_SCANCODE_UP: return press(s, TC_MI_UP);
    case SDL_SCANCODE_DOWN: return press(s, TC_MI_DOWN);
    case SDL_SCANCODE_LEFT: return press(s, TC_MI_LEFT);
    case SDL_SCANCODE_RIGHT: return press(s, TC_MI_RIGHT);
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return k->repeat ? TC_MC_NONE : press(s, TC_MI_CONFIRM);
    case SDL_SCANCODE_ESCAPE: return k->repeat ? TC_MC_NONE : press(s, TC_MI_BACK);
    case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_DELETE: return press(s, TC_MI_ERASE);
    default: break;
    }
    if (naming) return TC_MC_NONE; /* letters come as text (menu_text) */
    switch (k->scancode) {
    case SDL_SCANCODE_SPACE: return k->repeat ? TC_MC_NONE : press(s, TC_MI_CONFIRM);
    case SDL_SCANCODE_W: return press(s, TC_MI_UP);
    case SDL_SCANCODE_S: return press(s, TC_MI_DOWN);
    case SDL_SCANCODE_A: return press(s, TC_MI_LEFT);
    case SDL_SCANCODE_D: return press(s, TC_MI_RIGHT);
    default: return TC_MC_NONE;
    }
}

tc_menu_command menu_text(menu_screen *s, const char *utf8) {
    for (; utf8 && *utf8; utf8++)
        if ((unsigned char)*utf8 < 0x80) tc_menu_type(s->m, (uint32_t)(unsigned char)*utf8);
    return TC_MC_NONE;
}

tc_menu_command menu_pad_button(menu_screen *s, SDL_GamepadButton b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: return press(s, TC_MI_CONFIRM);
    case SDL_GAMEPAD_BUTTON_EAST: return press(s, TC_MI_BACK);
    case SDL_GAMEPAD_BUTTON_NORTH: return press(s, TC_MI_ERASE);
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return press(s, TC_MI_UP);
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return press(s, TC_MI_DOWN);
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return press(s, TC_MI_LEFT);
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return press(s, TC_MI_RIGHT);
    default: return TC_MC_NONE;
    }
}

tc_menu_command menu_update(menu_screen *s, SDL_Window *window, SDL_Gamepad *pad, float dt) {
    tc_menu_command c = TC_MC_NONE;
    bool naming = s->m->screen == TC_MS_NEW_GAME;
    if (naming != s->typing) { /* text input only while a name is typed (it may bring up an on-screen keyboard) */
        if (naming) SDL_StartTextInput(window);
        else SDL_StopTextInput(window);
        s->typing = naming;
    }
    if (pad) { /* the left stick moves, repeating while held */
        float x = (float)SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        float y = (float)SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f; /* SDL: down is + */
        int dir = y < -0.6f ? TC_MI_UP + 1 : y > 0.6f ? TC_MI_DOWN + 1 : x < -0.6f ? TC_MI_LEFT + 1 : x > 0.6f ? TC_MI_RIGHT + 1 : 0;
        if (dir != s->nav) {
            s->nav = dir;
            s->nav_repeat = 0.4f;
            if (dir) c = press(s, (tc_menu_input)(dir - 1));
        } else if (dir && (s->nav_repeat -= dt) <= 0) {
            s->nav_repeat = 0.12f;
            c = press(s, (tc_menu_input)(dir - 1));
        }
    }
    tc_menu_update(s->m, dt);
    return c;
}

void menu_draw(menu_screen *s, gpu_frame *f, SDL_GPURenderPass *pass) {
    float W = (float)f->width, H = (float)f->height;
    uint32_t n, k;
    const tc_menu_layer *layers;
    const tc_menu_quad *quads;
    tc_menu_build(s->m, H > 0 ? W / H : 1);
    layers = tc_menu_layer_list(s->m, &n);
    for (k = 0; k < n; k++) { /* mode 3: the picture itself, sampled repeating */
        const tc_menu_layer *l = &layers[k];
        if (l->image == TC_MENU_IMAGE_DIM)
            gpu_quad(s->g, f, pass, NULL, (SDL_FRect){0, 0, W, H}, NULL, (SDL_FColor){0, 0, 0, l->alpha});
        else if (l->image == TC_MENU_IMAGE_SWIRL) {
            if (s->swirl) gpu_overlay_sampled(s->g, f, pass, s->swirl, s->g->repeat_sampler, NULL, l->uv, 3, 0);
        } else if (l->image < TC_MENU_MAX_LAYERS && s->images[l->image])
            gpu_overlay_sampled(s->g, f, pass, s->images[l->image], s->g->repeat_sampler, NULL, l->uv, 3, 0);
    }
    quads = tc_menu_quad_list(s->m, &n);
    for (k = 0; s->atlas && k < n; k++) {
        const float *r = quads[k].rect, *t = quads[k].uv, *c = quads[k].color;
        SDL_FRect uv = {t[0], t[1], t[2] - t[0], t[3] - t[1]};
        gpu_quad(s->g, f, pass, s->atlas, (SDL_FRect){r[0] * W, r[1] * H, (r[2] - r[0]) * W, (r[3] - r[1]) * H}, &uv,
                 (SDL_FColor){c[0], c[1], c[2], c[3]});
    }
}

void menu_pointer(menu_screen *s, float x, float y, float w, float h) {
    if (s->m && w > 0 && h > 0) tc_menu_pointer(s->m, x / w, y / h);
}

tc_menu_command menu_click(menu_screen *s, float x, float y, float w, float h, bool right) {
    if (!s->m || w <= 0 || h <= 0) return TC_MC_NONE;
    if (right) return press(s, TC_MI_BACK);
    return tc_menu_click(s->m, x / w, y / h) ? tc_menu_take_command(s->m) : TC_MC_NONE;
}
