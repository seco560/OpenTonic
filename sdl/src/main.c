/* TonicViewer (SDL3): level viewer and barebones collision tester over TonicCore. Port of
 * Sources/TonicViewer/main.swift: the level select screen, the level view and its keys, the title bar readout.
 * Usage: tonicviewer [levels folder | level.ttlv] */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "intro.h"
#include "music.h"
#include "picker.h"
#include "util.h"
#include "view.h"

#ifndef TONIC_REPO_DIR
#define TONIC_REPO_DIR "."
#endif

static const char *controls_text =
    "TonicViewer controls\n"
    "\n"
    "Move:  W A S D  (W/S fly where the camera looks)\n"
    "Up / down:  R / F\n"
    "Look:  arrow keys, or click to capture the mouse (Esc releases)\n"
    "Faster:  hold Shift  -  mouse wheel: fly speed (free-fly) / camera zoom (play mode)\n"
    "Field of view:  , narrower  -  . wider\n"
    "\n"
    "Play as Ed:  the default; Tab toggles free-fly, fly somewhere and press Tab again to drop Ed there\n"
    "  Move:  W A S D, relative to the camera  -  Jump:  Space\n"
    "  With Ed's AI (levels with a .ttai, the default; I switches to the simple controller):\n"
    "    Ed runs by default, hold Shift to walk  -  E stick (Action)  -  Q Shoot  -  C Lateral\n"
    "  Simple controller:  hold Shift to run\n"
    "  Camera:  arrow keys or mouse orbit around Ed\n"
    "  (simple collision and physics, not the original game's)\n"
    "\n"
    "Animation:  P pause / resume  -  [ slower  -  ] faster\n"
    "Lighting variant:  1, 2, 3 ...  (levels with day/night lighting, e.g. South Plain)\n"
    "Gamma:  - darker  -  = brighter  -  Fog:  G on / off (levels with fog, e.g. north, Totalski)\n"
    "Antialiasing:  F2  off / 2x / 4x / 8x  (TONIC_MSAA=n sets the start value, default 4)\n"
    "(speed, gamma, field of view, lighting variant, antialiasing and play mode are shown in the title bar)\n"
    "\n"
    "Music:  M mute / unmute  (level -> track table: out/audio/music.tsv)\n"
    "Level select:  arrow keys + Enter, or click  -  Ctrl+L from a level\n"
    "Controls:  F1  -  Full screen:  F11  -  Quit:  Ctrl+Q\n"
    "\n"
    "F1 or Esc closes this";

typedef struct app {
    SDL_Window *window;
    gpu g;
    view *view;                  /* the level screen when set, else the level select screen */
    picker picker;
    bool picker_open;
    intro *intro;                /* the intro movie, over the (still open) level select screen */
    char *level_folder;          /* out/ttlv */
    char *data_dir;              /* out: audio/, Vignette/, icon/ next to ttlv/ */
    bool captured;               /* mouse-look capture (viewer convenience, not part of the original controls) */
    bool help;
    SDL_GPUTexture *help_text;
    int help_w, help_h;
    char title_base[512];
    char title[1024];
    Uint64 last_title_ns;
    bool quit;
} app;

/* ---- title bar ---- */

/* Live viewer settings appended to the level's title. */
static void update_title(app *a) {
    view *v = a->view;
    if (!v) return;
    char s[1024];
    char ai[96] = "";
    SDL_snprintf(s, sizeof s, "%s  \xc2\xb7  view: anim %.2f\xc3\x97, gamma %.2f, FOV %.0f\xc2\xb0", a->title_base,
                 v->animation_speed, v->gamma, v->cam.vertical_fov * 180 / TC_PI);
    size_t n = SDL_strlen(s);
    if (a->g.msaa > 1) n += SDL_snprintf(s + n, sizeof s - n, ", AA %dx", a->g.msaa);
    else n += SDL_snprintf(s + n, sizeof s - n, ", AA off");
    if (v->light_set_count > 1)
        n += SDL_snprintf(s + n, sizeof s - n, ", lighting %u/%u", v->light_set + 1, v->light_set_count);
    if (n < sizeof s && v->has_fog) n += SDL_snprintf(s + n, sizeof s - n, v->fog_enabled ? ", fog on (G)" : ", fog off (G)");
    if (n < sizeof s && v->playing) n += SDL_snprintf(s + n, sizeof s - n, "  \xc2\xb7  PLAYING as Ed (Tab: free-fly)");
    if (n < sizeof s && v->playing && view_has_ai(v)) {
        if (v->player->use_ai) {
            play_ai_status(v->player, ai, sizeof ai);
            n += SDL_snprintf(s + n, sizeof s - n, "  \xc2\xb7  AI %s (I: simple controller)", ai);
        } else {
            n += SDL_snprintf(s + n, sizeof s - n, "  \xc2\xb7  simple controller (I: Ed's AI)");
        }
    }
    if (SDL_strcmp(s, a->title) != 0) {
        SDL_strlcpy(a->title, s, sizeof a->title);
        SDL_SetWindowTitle(a->window, a->title);
    }
}

static void set_title(app *a, const char *t) {
    SDL_strlcpy(a->title, t, sizeof a->title);
    SDL_SetWindowTitle(a->window, t);
}

/* ---- screens ---- */

static void set_captured(app *a, bool on) {
    if (on == a->captured) return;
    a->captured = on;
    SDL_SetWindowRelativeMouseMode(a->window, on);
}

static char *audio_dir(const app *a) { return path_join(a->data_dir, "audio"); }

static void close_level(app *a) {
    if (!a->view) return;
    SDL_WaitForGPUIdle(a->g.device);
    view_free(a->view);
    SDL_free(a->view);
    a->view = NULL;
}

static void close_picker(app *a) {
    if (!a->picker_open) return;
    picker_close(&a->picker);
    a->picker_open = false;
}

static void show_picker(app *a) {
    char *audio = audio_dir(a);
    if (audio) music_play("menu", audio);
    SDL_free(audio);
    set_captured(a, false);
    close_level(a);
    if (!a->picker_open) {
        /* Loading screens live next to the ttlv folder: out/ttlv -> out/Vignette/Level. */
        char *vignette = path_join(a->data_dir, "Vignette");
        char *thumbs = vignette ? path_join(vignette, "Level") : NULL;
        picker_open(&a->picker, &a->g, a->level_folder, thumbs ? thumbs : "", SDL_GetWindowDisplayScale(a->window));
        SDL_free(vignette);
        SDL_free(thumbs);
        a->picker_open = true;
        if (a->picker.count == 0) {
            char msg[1024];
            SDL_snprintf(msg, sizeof msg, "No .ttlv levels in %s. Please run your genuine installed Tonic Trouble "
                                          "files through the conversion step (docs/BUILD.md).", a->level_folder);
            SDL_Log("%s", msg);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TonicViewer", msg, a->window);
            a->quit = true;
            return;
        }
    }
    set_title(a, "TonicViewer  \xc2\xb7  pick a level (click, or arrow keys + Enter)  \xc2\xb7  F1: controls");
}

/* false (with a message box) when the level can't be loaded; the current screen stays. */
static bool open_level(app *a, const char *path) {
    view *v = SDL_calloc(1, sizeof *v);
    if (!v) return false;
    char err[512];
    if (!view_load(v, &a->g, path, err, sizeof err)) {
        char msg[1024];
        SDL_snprintf(msg, sizeof msg, "Could not load %s: %s", path, err);
        SDL_Log("%s", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TonicViewer", msg, a->window);
        SDL_free(v);
        return false;
    }
    close_level(a);
    close_picker(a);
    a->view = v;
    /* Level music from the user-edited table out/audio/music.tsv (next to out/ttlv). */
    char *audio = audio_dir(a);
    if (audio) music_play(v->level.name, audio);
    SDL_free(audio);
    const view_stats *s = &v->stats;
    SDL_snprintf(a->title_base, sizeof a->title_base,
                 "TonicViewer \xe2\x80\x94 %s  \xc2\xb7  %u meshes, %u animated objects, %u tris, %u textures (%u missing)"
                 "  \xc2\xb7  P pause  \xc2\xb7  Ctrl+L level select%s",
                 v->level.name, s->meshes, s->persos, s->triangles, s->textures, s->missing_textures,
                 v->player ? "  \xc2\xb7  Tab: play as Ed" : "");
    a->title[0] = 0;
    update_title(a);  /* play mode is on from the start when the level has Ed */
    SDL_Log("Loaded %s: %u meshes, %u persos (%u parts), %u triangles, %u textures, %u missing", v->level.name, s->meshes,
            s->persos, s->parts, s->triangles, s->textures, s->missing_textures);
    return true;
}

/* The intro movie, from the level select screen (V): out/video/intro.mpg next to the ttlv folder. */
static void play_intro(app *a) {
    char *video = path_join(a->data_dir, "video");
    char *path = video ? path_join(video, "intro.mpg") : NULL;
    SDL_free(video);
    if (!path) return;
    char msg[1024];
    if (!path_is_file(path)) {
        SDL_snprintf(msg, sizeof msg, "%s is missing. Build it from the disc's Videos/EDPROL.AVI with "
                                      "tools/make_intro.sh mpg (docs/SDL3.md).", path);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "No intro movie", msg, a->window);
        SDL_free(path);
        return;
    }
    music_stop();
    char err[512];
    a->intro = intro_open(&a->g, path, music_muted(), err, sizeof err);
    SDL_free(path);
    if (!a->intro) {
        SDL_snprintf(msg, sizeof msg, "Could not play the intro: %s", err);
        SDL_Log("%s", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TonicViewer", msg, a->window);
        show_picker(a);  /* the menu music again */
        return;
    }
    a->help = false;
    set_title(a, "TonicViewer \xe2\x80\x94 intro  \xc2\xb7  Esc skips");
}

/* At the end, or skipped: back to the level select screen. */
static void finish_intro(app *a) {
    if (!a->intro) return;
    intro_close(a->intro);
    a->intro = NULL;
    show_picker(a);
}

/* ---- input ---- */

static void level_key(app *a, const SDL_KeyboardEvent *k) {
    view *v = a->view;
    SDL_Scancode sc = k->scancode;
    if (sc == SDL_SCANCODE_ESCAPE) { set_captured(a, false); return; }
    if (sc == SDL_SCANCODE_LEFTBRACKET || sc == SDL_SCANCODE_RIGHTBRACKET) {  /* [ / ]: animation speed */
        if (k->repeat) return;
        static const float steps[] = {0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 0.75f, 1, 1.5f, 2};
        const int count = (int)SDL_arraysize(steps);
        int i = 5;
        for (int s = 0; s < count; s++) if (steps[s] >= v->animation_speed - 0.001f) { i = s; break; }
        i += sc == SDL_SCANCODE_RIGHTBRACKET ? 1 : -1;
        v->animation_speed = steps[SDL_clamp(i, 0, count - 1)];
        update_title(a);
        return;
    }
    /* 1-9: baked lighting set (day/night variants, only some levels); - / =: display gamma. */
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        uint32_t n = (uint32_t)(sc - SDL_SCANCODE_1);
        if (n < v->light_set_count) v->light_set = n;
        update_title(a);
        return;
    }
    if (sc == SDL_SCANCODE_MINUS || sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_MINUS || sc == SDL_SCANCODE_KP_PLUS) {
        bool up = sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_PLUS;
        v->gamma = SDL_clamp(v->gamma * (up ? 1.1f : 1 / 1.1f), 0.5f, 2.5f);
        update_title(a);
        return;
    }
    if (sc == SDL_SCANCODE_I) {  /* I: Ed's AI / simple controller */
        if (!k->repeat) { view_toggle_ai(v); update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_TAB) {  /* Tab: play as Ed / free-fly */
        if (!k->repeat) { view_toggle_play(v); update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_COMMA || sc == SDL_SCANCODE_PERIOD) {  /* , / .: field of view (vertical, 5 degree steps) */
        float deg = SDL_roundf(v->cam.vertical_fov * 180 / TC_PI) + (sc == SDL_SCANCODE_PERIOD ? 5 : -5);
        v->cam.vertical_fov = SDL_clamp(deg, 30.0f, 100.0f) * TC_PI / 180;
        update_title(a);
        return;
    }
    if (sc == SDL_SCANCODE_G) {  /* G: level fog on/off */
        if (!k->repeat) { v->fog_enabled = !v->fog_enabled; update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_P) {  /* P: pause/resume animation */
        if (!k->repeat) v->paused = !v->paused;
        return;
    }
    input_key(&v->in, sc, true);
}

static void picker_key(app *a, const SDL_KeyboardEvent *k) {
    picker *p = &a->picker;
    switch (k->scancode) {
    case SDL_SCANCODE_LEFT: picker_move(p, -1, 0); break;
    case SDL_SCANCODE_RIGHT: picker_move(p, 1, 0); break;
    case SDL_SCANCODE_UP: picker_move(p, 0, -1); break;
    case SDL_SCANCODE_DOWN: picker_move(p, 0, 1); break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
        if (!k->repeat && p->selected >= 0 && p->selected < p->count) {
            char *path = SDL_strdup(p->entries[p->selected].path);  /* opening closes the picker */
            if (path) open_level(a, path);
            SDL_free(path);
        }
        break;
    case SDL_SCANCODE_V:  /* V: the intro movie */
        if (!k->repeat && !(k->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) play_intro(a);
        break;
    default: break;
    }
}

static void key_down(app *a, const SDL_KeyboardEvent *k) {
    SDL_Scancode sc = k->scancode;
    bool ctrl = (k->mod & SDL_KMOD_CTRL) != 0, alt = (k->mod & SDL_KMOD_ALT) != 0;
    if (ctrl && sc == SDL_SCANCODE_Q) { a->quit = true; return; }
    if (ctrl && sc == SDL_SCANCODE_L) { if (!k->repeat && a->view) show_picker(a); return; }
    if (a->intro) {  /* Esc, Space, Return, keypad Enter skip; M, F11 and Ctrl+Q as everywhere */
        if (k->repeat) return;
        bool enter = sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER;
        if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_SPACE || (enter && !alt)) {
            finish_intro(a);
            return;
        }
        if (sc == SDL_SCANCODE_M) { music_set_muted(!music_muted()); intro_set_muted(a->intro, music_muted()); return; }
        if (sc != SDL_SCANCODE_F11 && !(alt && enter)) return;
    }
    if (sc == SDL_SCANCODE_F2) {  /* F2: antialiasing off / 2x / 4x / 8x (what the GPU supports) */
        if (!k->repeat) {
            int samples = gpu_next_msaa(&a->g);
            if (gpu_set_msaa(&a->g, samples)) SDL_Log("Antialiasing: %dx MSAA", samples);
            update_title(a);
        }
        return;
    }
    if (sc == SDL_SCANCODE_F1 || (ctrl && sc == SDL_SCANCODE_SLASH)) {
        if (!k->repeat) { a->help = !a->help; if (a->help) set_captured(a, false); }
        return;
    }
    if (sc == SDL_SCANCODE_F11 || (alt && (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER))) {
        if (!k->repeat) SDL_SetWindowFullscreen(a->window, (SDL_GetWindowFlags(a->window) & SDL_WINDOW_FULLSCREEN) == 0);
        return;
    }
    if (sc == SDL_SCANCODE_ESCAPE && a->help) { a->help = false; return; }
    if (sc == SDL_SCANCODE_M) {  /* M: mute/unmute music */
        if (!k->repeat) music_set_muted(!music_muted());
        return;
    }
    if (ctrl) return;
    if (a->view) level_key(a, k);
    else if (a->picker_open) picker_key(a, k);
}

/* Window coordinates -> swapchain pixels. */
static float to_pixels(const app *a, float v) { return v * SDL_GetWindowPixelDensity(a->window); }

static void handle(app *a, const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_QUIT: a->quit = true; break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        set_captured(a, false);
        if (a->view) a->view->in.held = 0;
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if (a->picker_open) a->picker.scale = SDL_GetWindowDisplayScale(a->window);
        break;
    case SDL_EVENT_KEY_DOWN: key_down(a, &e->key); break;
    case SDL_EVENT_KEY_UP:
        if (a->view) input_key(&a->view->in, e->key.scancode, false);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button != SDL_BUTTON_LEFT || a->help || a->intro) break;
        if (a->view) set_captured(a, true);
        else if (a->picker_open) {
            int i = picker_hit(&a->picker, to_pixels(a, e->button.x), to_pixels(a, e->button.y));
            if (i >= 0) {
                char *path = SDL_strdup(a->picker.entries[i].path);  /* opening closes the picker */
                if (path) open_level(a, path);
                SDL_free(path);
            }
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (a->view && a->captured) {
            a->view->in.mouse_dx += e->motion.xrel;
            a->view->in.mouse_dy += e->motion.yrel;
        } else if (a->picker_open && !a->intro) {
            picker_set_hover(&a->picker, to_pixels(a, e->motion.x), to_pixels(a, e->motion.y));
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float y = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        if (a->view) {
            view *v = a->view;
            float step = SDL_powf(1.1f, y);
            if (v->playing) v->orbit_distance = SDL_clamp(v->orbit_distance / step, 1.5f, 25.0f);  /* camera zoom */
            else v->in.base_speed = SDL_clamp(v->in.base_speed * step, 0.5f, 500.0f);           /* fly speed */
        } else if (a->picker_open && !a->intro) {
            picker_scroll(&a->picker, -y * 80 * a->picker.scale);
        }
        break;
    }
    default: break;
    }
}

/* ---- drawing ---- */

static void draw_help(app *a, gpu_frame *f, SDL_GPURenderPass *pass) {
    if (!a->help_text) a->help_text = gpu_text(&a->g, controls_text, 1, &a->help_w, &a->help_h);
    gpu_quad(&a->g, f, pass, NULL, (SDL_FRect){0, 0, (float)f->width, (float)f->height}, NULL, (SDL_FColor){0, 0, 0, 0.55f});
    if (!a->help_text) return;
    /* The 8x8 font at the largest whole multiple that fits, up to twice the display scale. */
    int k = (int)SDL_max(1.0f, SDL_roundf(2 * SDL_GetWindowDisplayScale(a->window)));
    while (k > 1 && ((float)(a->help_w * k) > 0.92f * (float)f->width || (float)(a->help_h * k) > 0.9f * (float)f->height)) k--;
    float w = (float)(a->help_w * k), h = (float)(a->help_h * k), pad = 16.0f * (float)k;
    float x = SDL_floorf(((float)f->width - w) / 2), y = SDL_floorf(((float)f->height - h) / 2);
    gpu_quad(&a->g, f, pass, NULL, (SDL_FRect){x - pad, y - pad, w + 2 * pad, h + 2 * pad}, NULL,
             (SDL_FColor){0.1f, 0.11f, 0.17f, 0.95f});
    gpu_quad_sampled(&a->g, f, pass, a->help_text, a->g.nearest_sampler, (SDL_FRect){x, y, w, h}, NULL,
                     (SDL_FColor){0.92f, 0.93f, 0.96f, 1});
}

static void draw(app *a) {
    gpu_frame f;
    if (!gpu_frame_begin(&a->g, &f)) { SDL_Delay(16); return; }  /* minimised */
    if (a->intro) {
        intro_upload(a->intro, &f);
        SDL_GPURenderPass *pass = gpu_pass_2d(&a->g, &f, true, (SDL_FColor){0, 0, 0, 1});
        if (pass) { intro_draw(a->intro, &f, pass); SDL_EndGPURenderPass(pass); }
    } else if (a->view) {
        view_draw_frame(a->view, &f);
        if (a->help) {
            SDL_GPURenderPass *pass = gpu_pass_2d(&a->g, &f, false, (SDL_FColor){0, 0, 0, 1});
            if (pass) { draw_help(a, &f, pass); SDL_EndGPURenderPass(pass); }
        }
    } else {
        SDL_GPURenderPass *pass = gpu_pass_2d(&a->g, &f, true, (SDL_FColor){0.08f, 0.09f, 0.14f, 1});
        if (pass) {
            if (a->picker_open) picker_draw(&a->picker, &f, pass);
            if (a->help) draw_help(a, &f, pass);
            SDL_EndGPURenderPass(pass);
        }
    }
    gpu_frame_end(&a->g, &f);
}

/* ---- startup ---- */

static char *strip_slashes(char *p) {
    size_t n = p ? SDL_strlen(p) : 0;
    while (n > 1 && p[n - 1] == '/') p[--n] = 0;
    return p;
}

/* Default data, first that exists: files/out/ttlv in the working directory; next to the binary in the checkout
 * (sdl/build/tonicviewer -> files/out/ttlv); in the checkout this binary was built from; out/ttlv in the working
 * directory (older layout). */
static char *default_level_folder(void) {
    const char *base = SDL_GetBasePath();
    char *candidates[4] = {SDL_strdup("files/out/ttlv"), base ? path_join(base, "../../files/out/ttlv") : NULL,
                           SDL_strdup(TONIC_REPO_DIR "/files/out/ttlv"), SDL_strdup("out/ttlv")};
    char *found = NULL;
    for (int i = 0; i < 4; i++) {
        if (!found && candidates[i] && path_is_dir(candidates[i])) found = candidates[i];
        else SDL_free(candidates[i]);
    }
    return found ? found : SDL_strdup("files/out/ttlv");
}

/* TONIC_MSAA=1/2/4/8: the level's starting antialiasing (F2 cycles it). */
static int msaa_setting(void) {
    const char *e = SDL_getenv("TONIC_MSAA");
    int n = e ? SDL_atoi(e) : 4;
    return n >= 8 ? 8 : n >= 4 ? 4 : n >= 2 ? 2 : 1;
}

int main(int argc, char **argv) {
    SDL_SetAppMetadata("TonicViewer", "0.1", "org.tonicviewer.sdl");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    app a;
    SDL_zero(a);
    a.window = SDL_CreateWindow("TonicViewer", 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!a.window || !gpu_init(&a.g, a.window, msaa_setting())) {
        char msg[512];
        SDL_snprintf(msg, sizeof msg, "Could not set up the GPU: %s", SDL_GetError());
        SDL_Log("%s", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TonicViewer", msg, a.window);
        gpu_quit(&a.g);
        if (a.window) SDL_DestroyWindow(a.window);
        SDL_Quit();
        return 1;
    }

    const char *level_file = NULL;
    if (argc > 1 && path_is_file(argv[1])) {
        level_file = argv[1];
        a.level_folder = path_dir(argv[1]);
    } else if (argc > 1) {
        a.level_folder = SDL_strdup(argv[1]);
    } else {
        a.level_folder = default_level_folder();
    }
    strip_slashes(a.level_folder);
    a.data_dir = path_dir(a.level_folder);

    /* Window icon: Ed's original icon on the sud0 water swirl, generated into out/icon by tools/make_icon.sh. */
    char *icon_dir = path_join(a.data_dir, "icon");
    char *icon_path = icon_dir ? path_join(icon_dir, "TonicViewer.png") : NULL;
    SDL_Surface *icon = icon_path && path_is_file(icon_path) ? SDL_LoadPNG(icon_path) : NULL;
    if (icon) { SDL_SetWindowIcon(a.window, icon); SDL_DestroySurface(icon); }
    SDL_free(icon_dir);
    SDL_free(icon_path);

    SDL_Log("Controls: WASD move, arrow keys look, R/F up/down, Shift fast; click to capture mouse-look, Esc to release; "
            "wheel = fly speed / play-mode zoom; , / . field of view; P pause animation; F2 antialiasing; starts as Ed when the level has "
            "him, Tab toggles free-fly (Space jump); Ctrl+L level select; F1 controls; F11 fullscreen");
    if (!level_file || !open_level(&a, level_file)) show_picker(&a);

    Uint64 last = SDL_GetTicksNS();
    while (!a.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(&a, &e);
        if (a.quit) break;
        Uint64 now = SDL_GetTicksNS();
        float dt = SDL_min((float)(now - last) / 1e9f, 0.1f);
        last = now;
        if (a.intro && !intro_update(a.intro, dt)) finish_intro(&a);
        if (a.view) {
            view_update(a.view, dt);
            /* Refresh the AI's behaviour/state in the title while playing. */
            if (a.view->playing && view_has_ai(a.view) && a.view->player->use_ai && now - a.last_title_ns > 250000000) {
                a.last_title_ns = now;
                update_title(&a);
            }
        }
        draw(&a);
    }

    music_stop();
    intro_close(a.intro);
    close_level(&a);
    close_picker(&a);
    if (a.help_text) SDL_ReleaseGPUTexture(a.g.device, a.help_text);
    gpu_quit(&a.g);
    SDL_DestroyWindow(a.window);
    SDL_free(a.level_folder);
    SDL_free(a.data_dir);
    SDL_Quit();
    return 0;
}
