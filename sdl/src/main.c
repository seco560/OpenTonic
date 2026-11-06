/* Open Tonic (SDL3): the game over TonicCore (began as Tonic Viewer, a level viewer and collision tester). Port of
 * Sources/OpenTonic/main.swift: the level select screen, the level view and its keys, the title bar readout.
 * Usage: opentonic [levels folder | level.ttlv] */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "input.h"
#include "intro.h"
#include "music.h"
#include "picker.h"
#include "record.h"
#include "replays.h"
#include "sfx.h"
#include "util.h"
#include "view.h"

#ifndef TONIC_REPO_DIR
#define TONIC_REPO_DIR "."
#endif

static const char *controls_text =
    "Open Tonic controls\n"
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
    "  Camera:  the mouse, the right stick, the arrow keys or Numpad 4 / 6 turn it round Ed (up / down raise or lower\n"
    "    it); it stays while he stands and eases back behind him once he moves  -  X (middle mouse, L2) recentres it\n"
    "    5 close-up, 0 first person (number row or numpad; L3)  -  ; less / ' more look sensitivity  -  Y invert\n"
    "    up / down  -  the mouse looks from the start of a level (Esc releases it, a click takes it again)\n"
    "    (TONIC_LOOK=percent and TONIC_INVERT=x|y|xy set where they start)\n"
    "  (simple collision and physics, not the original game's)\n"
    "\n"
    "Animation:  P pause / resume  -  [ slower  -  ] faster\n"
    "Lighting variant:  Alt + 1, 2, 3 ...  (levels with day/night lighting, e.g. South Plain), Alt + 0 the level's own\n"
    "Gamma:  - darker  -  = brighter  -  Fog:  G on / off (levels with fog, e.g. north, Totalski)\n"
    "Sectors:  K  Ed's sector and what it sees, sky included (default) / all of them / his sector alone  (title bar: his sector)\n"
    "Lighting:  L  the original's (default: its formula, the lights and fog of Ed's sector) / the port's old one\n"
    "Antialiasing:  F2  off / 2x / 4x / 8x  (TONIC_MSAA=n sets the start value, default 8)\n"
    "Supersampling:  F3  off / 1.5x / 2x  (draws the level at that many times the window's pixels; TONIC_SCALE=150|200)\n"
    "(speed, gamma, field of view, lighting variant, antialiasing, supersampling and play mode are in the title bar)\n"
    "\n"
    "Music and sound effects:  M mute / unmute  (level -> track table: out/audio/music.tsv)\n"
    "Level select:  arrow keys + Enter, or click (past a pane's side: Retail / Special)  -  Ctrl+L from a level  -  V intro  -  R replays\n"
    "Controls:  F1  -  Full screen:  F11  -  Quit:  Ctrl+Q\n"
    "Recording:  always on  -  F9 or the touchpad marks this moment\n"
    "Replays:  Enter plays, Delete deletes, Esc back  -  Pad: Cross plays, Square deletes, Circle back\n"
    "Watching one:  P or Cross pauses  -  Esc or Start back to the list\n"
    "Paused:  mouse, arrows, right stick and wheel move the camera round Ed, back where it was on resuming\n"
    "\n"
    "Gamepad (positions as on a PlayStation pad; Xbox: A B X Y = Cross Circle Square Triangle):\n"
    "  Left stick:  move Ed (push further to run) / fly  -  Right stick:  camera  -  R3 (click):  free-fly / play\n"
    "  Cross:  Jump  -  Square:  Action  -  Circle:  Shoot  -  R1:  Lateral / fly up  -  L1:  fly down\n"
    "  R2:  fly faster  -  L2:  recentre the camera  -  L3 (click):  first person\n"
    "  D-pad up / down:  camera distance / fly speed  -  Start:  level select  -  Select:  controls\n"
    "  Level select:  D-pad or left stick, Cross opens, Triangle plays the intro, Square the replays  -  Intro:  Cross, Circle or Start skip\n"
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
    bool captured;               /* mouse-look capture (viewer convenience, not part of the original controls); on
                                    from the start of a level (not a playback), Esc releases it, a click takes it again */
    bool skip_motion;            /* the first motion after capturing: dropped (set_captured) */
    bool cursor_hidden;          /* no pointer over the intro, its loading screen and a playback (update_cursor) */
    SDL_JoystickID pad;          /* the gamepad last used, 0 = none (every gamepad is opened as it connects) */
    bool pad_fast;               /* its right trigger past half way: TC_VA_FAST */
    bool pad_recenter;           /* its left trigger past half way: recentre the camera (on the press) */
    float look_sensitivity;      /* the look settings (TONIC_LOOK, TONIC_INVERT; ; ' Y), given to every level's viewer */
    bool invert_x, invert_y;
    int nav_dx, nav_dy;          /* level select: the left stick's direction, and when it moves the selection again */
    float nav_repeat;
    bool help;
    SDL_GPUTexture *help_text;
    int help_w, help_h;
    /* A red portal's loading screen (docs/PORTALS.md): the coming level's vignette (no caption: the original has none,
     * the vignette shows where you are going). */
    char loading_level[64];
    SDL_GPUTexture *loading_vignette;
    int vignette_w, vignette_h;
    char picture_name[64];       /* the level scripts' picture shown (TC_VO_PICTURE), "" none */
    SDL_GPUTexture *picture;
    int picture_w, picture_h;
    Uint64 loading_start_ns;     /* when the level last changed: the bar's animation clock */
    SDL_GPUTexture *loading_bar; /* the bar's rainbow pool (Textures/Sud0/o_zarbi_auntxy.png), loaded once */
    bool loading_bar_tried;
    /* After the intro: Totalski opens after the loading screen, as a portal's (the level select stays underneath). */
    char *next_level;            /* its .ttlv, or NULL */
    char next_name[64];          /* its level name (for the vignette) */
    float next_time;             /* seconds into its loading screen */
    char *current_folder;        /* the shown level's folder (retail out/ttlv or out/se/ttlv): portals stay in it */
    /* The run being recorded (docs/RECORDING.md): from a level opened from a menu, through its portals, back to a menu;
     * and playing one back: the recorded frames at their own pace, level after level, the game's inputs locked. Esc or
     * Start go back to the Replays menu, as its end does; Ctrl+L to the level select. */
    tc_recorder *recorder;
    uint32_t marks;              /* F9, the touchpad: this run's */
    bool replaying;
    tc_replay replay;
    void *replay_data;
    uint32_t replay_segment;
    float replay_clock;          /* seconds of real time the recording is behind */
    bool replay_paused;          /* P, Cross: the frames wait, the camera turns round Ed (tc_replay_pause) */
    /* The Replays menu (R, Square), over the level select. */
    bool replays_on;
    replays rp;
    char title_base[512];
    char title[1024];
    Uint64 last_title_ns;
    bool quit;
} app;

/* ---- title bar ---- */

/* Live viewer settings appended to the level's title (the core's readout, plus the antialiasing). */
static void update_title(app *a) {
    view *v = a->view;
    if (!v) return;
    char aa[32], status[512], s[1024];
    if (a->g.msaa > 1) SDL_snprintf(aa, sizeof aa, ", AA %dx", a->g.msaa);
    else SDL_snprintf(aa, sizeof aa, ", AA off");
    if (a->marks) SDL_snprintf(aa + SDL_strlen(aa), sizeof aa - SDL_strlen(aa), ", %u marks", a->marks);
    if (a->replaying) SDL_strlcat(aa, a->replay_paused ? ", playback, paused" : ", playback", sizeof aa);
    if (a->g.scale > 100) SDL_snprintf(aa + SDL_strlen(aa), sizeof aa - SDL_strlen(aa), ", SS %s", a->g.scale >= 200 ? "2x" : "1.5x");
    tc_viewer_status(&v->viewer, aa, status, sizeof status);
    SDL_snprintf(s, sizeof s, "%s  \xc2\xb7  %s", a->title_base, status);
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
    if (on) {  /* the switch can report a jump of its own (the pointer moved to the window's middle): not a look */
        SDL_GetRelativeMouseState(NULL, NULL);
        a->skip_motion = true;
    }
}

/* Every frame: the pointer hides over the intro, the loading screen after it and a playback, and shows again on the
 * level select and in play (where mouse-look capture hides it as before; relative mode keeps it hidden either way). */
static void update_cursor(app *a) {
    bool hide = a->intro || a->next_level || (a->replaying && a->view);
    if (hide == a->cursor_hidden) return;
    a->cursor_hidden = hide;
    if (hide) SDL_HideCursor();
    else SDL_ShowCursor();
}

static char *audio_dir(const app *a) { return path_join(a->data_dir, "audio"); }

static void loading_free(app *a) {
    if (a->loading_vignette) SDL_ReleaseGPUTexture(a->g.device, a->loading_vignette);
    a->loading_vignette = NULL;
    a->loading_level[0] = 0;
    if (a->picture) SDL_ReleaseGPUTexture(a->g.device, a->picture);
    a->picture = NULL;
    a->picture_name[0] = 0;
}

static void close_level(app *a) {
    if (!a->view) return;
    SDL_WaitForGPUIdle(a->g.device);
    loading_free(a);
    view_free(a->view);
    SDL_free(a->view);
    a->view = NULL;
}

static void close_picker(app *a) {
    if (!a->picker_open) return;
    picker_close(&a->picker);
    a->picker_open = false;
}

static void close_replays(app *a) {
    if (!a->replays_on) return;
    replays_close(&a->rp);
    a->replays_on = false;
}

/* The run ends here: what follows from a menu is another. */
static void end_recording(app *a) {
    record_stop(a->recorder);
    a->recorder = NULL;
    a->marks = 0;
}

static void stop_playback(app *a, const char *why);

static void show_picker(app *a) {
    stop_playback(a, "the level select");
    end_recording(a);
    close_replays(a);
    sfx_stop_all();
    char *audio = audio_dir(a);
    if (audio) music_play("menu", audio);
    SDL_free(audio);
    set_captured(a, false);
    close_level(a);
    if (!a->picker_open) {
        /* Loading screens live next to the ttlv folder: out/ttlv -> out/Vignette/Level. The Special Edition's levels
         * (tools/ttre/se.py): out/se/ttlv, their pictures out/se/Vignette/Level. */
        char *vignette = path_join(a->data_dir, "Vignette");
        char *thumbs = vignette ? path_join(vignette, "Level") : NULL;
        char *se = path_join(a->data_dir, "se"), *se_levels = se ? path_join(se, "ttlv") : NULL;
        char *se_vignette = se ? path_join(se, "Vignette") : NULL, *se_thumbs = se_vignette ? path_join(se_vignette, "Level") : NULL;
        /* Back from a level: on the pane of its edition. */
        bool from_se = se_levels && a->current_folder && SDL_strcmp(a->current_folder, se_levels) == 0;
        picker_open(&a->picker, &a->g, a->level_folder, thumbs ? thumbs : "", se_levels, se_thumbs,
                    SDL_GetWindowDisplayScale(a->window), from_se ? 1 : 0);
        SDL_free(vignette);
        SDL_free(thumbs);
        SDL_free(se);
        SDL_free(se_levels);
        SDL_free(se_vignette);
        SDL_free(se_thumbs);
        a->picker_open = true;
        if (a->picker.count == 0) {
            char msg[1024];
            SDL_snprintf(msg, sizeof msg, "No .ttlv levels in %s. Please run your genuine installed Tonic Trouble "
                                          "files through the conversion step (docs/BUILD.md).", a->level_folder);
            SDL_Log("%s", msg);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open Tonic", msg, a->window);
            a->quit = true;
            return;
        }
    }
    set_title(a, "Open Tonic");
}

/* false (with a message box) when the level can't be loaded; the current screen stays. carry: arriving through a
 * portal, or NULL. Mouse-look capture stays as it was, so a portal's level change keeps playing. */
static bool open_level_seeded(app *a, const char *path, const tc_viewer_carry *carry, uint32_t seed) {
    view *v = SDL_calloc(1, sizeof *v);
    if (!v) return false;
    char err[512];
    /* No carry: from a menu, a new run; a portal's level goes on in this one. A playback isn't a run: the file is the record. */
    if (!carry && !a->replaying) {
        end_recording(a);
        a->recorder = record_start();
    }
    if (!view_load(v, &a->g, path, carry, a->replaying ? NULL : a->recorder, seed, err, sizeof err)) {
        char msg[1024];
        SDL_snprintf(msg, sizeof msg, "Could not load %s: %s", path, err);
        SDL_Log("%s", msg);
        set_captured(a, false);  /* a portal's level change may hold the mouse */
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open Tonic", msg, a->window);
        SDL_free(v);
        return false;
    }
    close_level(a);
    close_picker(a);
    close_replays(a);
    a->view = v;
    SDL_free(a->current_folder);
    a->current_folder = path_dir(path);
    a->pad_fast = false;  /* the new viewer holds nothing */
    tc_viewer_set_look(&v->viewer, a->look_sensitivity, a->invert_x, a->invert_y);
    /* Level music from the user-edited table out/audio/music.tsv (next to out/ttlv); sound effects from out/audio/sfx. */
    char *audio = audio_dir(a);
    sfx_stop_all();
    if (audio) {
        sfx_load(audio);
        music_play(v->level.name, audio);
    }
    SDL_free(audio);
    const tc_view_stats *s = &v->viewer.stats;
    uint32_t textures = v->texture_count - v->missing_textures;
    SDL_snprintf(a->title_base, sizeof a->title_base, "Open Tonic \xe2\x80\x94 %s%s", a->replaying ? "Playback \xe2\x80\x94 " : "",
                 v->level.name);
    if (!a->replaying && (SDL_GetWindowFlags(a->window) & SDL_WINDOW_INPUT_FOCUS)) set_captured(a, true);  /* mouse look from the start */
    a->title[0] = 0;
    update_title(a);  /* play mode is on from the start when the level has Ed */
    SDL_Log("Loaded %s: %u meshes, %u persos (%u parts), %u triangles, %u textures, %u missing", v->level.name, s->meshes,
            s->persos, s->parts, s->triangles, textures, v->missing_textures);
    return true;
}

static bool open_level(app *a, const char *path, const tc_viewer_carry *carry) {
    return open_level_seeded(a, path, carry, 0);
}

/* A red portal's level change (after its loading screen): the .ttlv of that name (any case) in the levels folder,
 * entered with the carry; the level select when it's missing or doesn't load. */
static void open_portal_level(app *a, const tc_viewer_carry *pending) {
    tc_viewer_carry carry = *pending;  /* freed with the current viewer */
    char pattern[80];
    /* The shown level's folder: the Special Edition's arches lead to its own levels (out/se/ttlv). */
    char *folder = SDL_strdup(a->current_folder ? a->current_folder : a->level_folder);
    SDL_snprintf(pattern, sizeof pattern, "%s.ttlv", carry.level);
    int n = 0;
    char **levels = folder ? SDL_GlobDirectory(folder, pattern, SDL_GLOB_CASEINSENSITIVE, &n) : NULL;
    char *path = levels && n > 0 ? path_join(folder, levels[0]) : NULL;
    SDL_free(levels);
    if (!path) {
        char msg[1024];
        SDL_snprintf(msg, sizeof msg, "The portal leads to %s, which is not in %s.", carry.level, folder ? folder : "?");
        SDL_free(folder);
        SDL_Log("%s", msg);
        set_captured(a, false);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "No level", msg, a->window);
        show_picker(a);
        return;
    }
    if (!open_level(a, path, &carry)) show_picker(a);
    SDL_free(path);
    SDL_free(folder);
}

/* ---- playing a recording back ---- */

static void stop_playback(app *a, const char *why) {
    if (!a->replaying) return;
    SDL_Log("Playback ended: %s", why);
    tc_replay_close(&a->replay);
    SDL_free(a->replay_data);
    a->replay_data = NULL;
    a->replaying = a->replay_paused = false;
    update_title(a);
}

/* The level of segment i, as the recording's run opened it; false (with the playback over) when it isn't here. */
static void show_replays(app *a) {
    if (a->replays_on) return;
    char *folder = record_folder();
    replays_open(&a->rp, &a->g, SDL_GetWindowDisplayScale(a->window), folder, record_enabled());
    SDL_free(folder);
    a->replays_on = true;
    set_title(a, "Open Tonic \xe2\x80\x94 Replays");
}

/* A playback's end, or Esc / Start in one: back to the list. */
static void back_to_replays(app *a, const char *why) {
    stop_playback(a, why);
    show_picker(a);
    if (!a->quit) show_replays(a);
}

static bool playback_level(app *a, uint32_t i) {
    const tc_replay_segment *s = &a->replay.segments[i];
    char *base = s->edition ? path_join(a->data_dir, "se") : NULL;
    char *folder = s->edition ? (base ? path_join(base, "ttlv") : NULL) : SDL_strdup(a->level_folder);
    char pattern[80];
    int n = 0;
    SDL_snprintf(pattern, sizeof pattern, "%s.ttlv", s->level);
    char **levels = folder ? SDL_GlobDirectory(folder, pattern, SDL_GLOB_CASEINSENSITIVE, &n) : NULL;
    char *path = levels && n > 0 ? path_join(folder, levels[0]) : NULL;
    bool ok = path && open_level_seeded(a, path, s->has_carry ? &s->carry : NULL, s->seed);
    SDL_free(levels);
    SDL_free(path);
    SDL_free(folder);
    SDL_free(base);
    if (!ok) {
        char msg[160];
        SDL_snprintf(msg, sizeof msg, "This recording goes to %s, which is not here.", s->level);
        stop_playback(a, msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Replay", msg, a->window);
        return false;
    }
    a->replay_segment = i;
    tc_replay_begin(&a->replay, i, &a->view->viewer, false);
    return true;
}

static bool start_playback(app *a, const char *file) {
    size_t size = 0;
    tc_result e = TC_ERR_TRUNCATED;
    a->replay_data = file ? SDL_LoadFile(file, &size) : NULL;
    if (a->replay_data) e = tc_replay_open(&a->replay, a->replay_data, size);
    if (e != TC_OK || !a->replay.segment_count) {
        SDL_Log("Can't play %s: %s", file ? file : "(no recording)", a->replay_data ? tc_result_string(e) : "no such file");
        if (e == TC_OK) tc_replay_close(&a->replay);
        SDL_free(a->replay_data);
        a->replay_data = NULL;
        return false;
    }
    SDL_Log("Playing back %s: %u levels", file, a->replay.segment_count);
    a->replaying = true;
    a->replay_clock = 0;
    return playback_level(a, 0);
}

static void toggle_playback_pause(app *a) {
    a->replay_paused = !a->replay_paused;
    tc_replay_pause(&a->replay, a->replay_paused);
    a->replay_clock = 0;
    set_captured(a, a->replay_paused && (SDL_GetWindowFlags(a->window) & SDL_WINDOW_INPUT_FOCUS));  /* the mouse looks */
    update_title(a);
}

/* The frames real time has reached; a frontend's frame may hold none or several of the recording's. */
static void playback_tick(app *a, float dt) {
    if (a->replay_paused) {
        tc_viewer_paused_look(&a->view->viewer, dt);
        return;
    }
    a->replay_clock += dt;
    while (a->replaying && a->replay_clock > 0) {
        float step = 0;
        const uint16_t *sounds;
        if (!tc_replay_step(&a->replay, &step)) {
            if (a->replay_segment + 1 >= a->replay.segment_count) back_to_replays(a, "the end of the recording");
            else if (!playback_level(a, a->replay_segment + 1)) back_to_replays(a, "its next level isn't here");
            return;
        }
        uint32_t n = tc_viewer_sounds(&a->view->viewer, &sounds);
        if (n > 0) sfx_play(sounds, n);
        a->replay_clock -= step;
    }
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
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open Tonic", msg, a->window);
        show_picker(a);  /* the menu music again */
        return;
    }
    a->help = false;
    set_title(a, "Open Tonic \xe2\x80\x94 intro");
}

/* The folder's ski slope, for the loading screen after the intro: its .ttlv, with next_name set, or NULL. */
static char *ski_slope(app *a, const char *folder) {
    int n = 0;
    char **levels = folder ? SDL_GlobDirectory(folder, "totalski.ttlv", SDL_GLOB_CASEINSENSITIVE, &n) : NULL;
    char *path = levels && n > 0 ? path_join(folder, levels[0]) : NULL;
    if (path) {
        SDL_strlcpy(a->next_name, levels[0], sizeof a->next_name);
        char *dot = SDL_strrchr(a->next_name, '.');
        if (dot) *dot = 0;
    }
    SDL_free(levels);
    return path;
}

/* At the end, or skipped: the ski slope, as the original game starts it, after its loading screen. Which one is a coin
 * toss, retail or Special Edition (the user's, not the original's); the other when that one isn't converted, the level
 * select when neither is. */
static void finish_intro(app *a) {
    if (!a->intro) return;
    intro_close(a->intro);
    a->intro = NULL;
    char *se = path_join(a->data_dir, "se"), *se_levels = se ? path_join(se, "ttlv") : NULL;
    const char *folders[2] = {a->level_folder, se_levels};
    int toss = SDL_rand(2);
    char *path = ski_slope(a, folders[toss]);
    if (!path) path = ski_slope(a, folders[1 - toss]);
    SDL_free(se);
    SDL_free(se_levels);
    if (!path) { show_picker(a); return; }
    SDL_free(a->current_folder);
    a->current_folder = path_dir(path);  /* the loading screen's vignette is the edition's */
    a->next_level = path;
    a->next_time = 0;
    set_title(a, "Open Tonic");
}

/* The loading screen after the intro is done (drawn at least once at full progress): open the level. */
static void finish_loading(app *a) {
    char *path = a->next_level;
    a->next_level = NULL;
    if (!open_level(a, path, NULL)) show_picker(a);
    SDL_free(path);
}

/* ---- input ---- */

static void level_key(app *a, const SDL_KeyboardEvent *k) {
    tc_viewer *v = &a->view->viewer;
    SDL_Scancode sc = k->scancode;
    if (sc == SDL_SCANCODE_ESCAPE) { set_captured(a, false); return; }
    if (sc == SDL_SCANCODE_LEFTBRACKET || sc == SDL_SCANCODE_RIGHTBRACKET) {  /* [ / ]: animation speed */
        if (!k->repeat) { tc_viewer_step_speed(v, sc == SDL_SCANCODE_RIGHTBRACKET ? 1 : -1); update_title(a); }
        return;
    }
    /* Alt + 1-9: hold a baked lighting set (sud0 and Land have three); Alt + 0: the level's own light again (sud0's day /
     * night cycle, Land's lightning); 5 and 0 alone: the camera; - / =: display gamma. */
    bool alt = (k->mod & SDL_KMOD_ALT) != 0;
    if (!alt && (sc == SDL_SCANCODE_5 || sc == SDL_SCANCODE_KP_5 || sc == SDL_SCANCODE_0 || sc == SDL_SCANCODE_KP_0)) {
        /* 5 and 0, number row or numpad: the original's camera keys (Numpad 5 close-up, Numpad 0 first person) */
        if (!k->repeat) {
            if (sc == SDL_SCANCODE_0 || sc == SDL_SCANCODE_KP_0) tc_viewer_toggle_first_person(v);
            else tc_viewer_toggle_closeup(v);
            update_title(a);
        }
        return;
    }
    if (alt && sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {  /* Alt + 1-9: a lighting set */
        tc_viewer_set_light_set(v, (uint32_t)(sc - SDL_SCANCODE_1));
        update_title(a);
        return;
    }
    if (alt && sc == SDL_SCANCODE_0) {  /* Alt + 0: the level's own light again */
        tc_viewer_follow_light(v);
        update_title(a);
        return;
    }
    if (sc == SDL_SCANCODE_MINUS || sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_MINUS || sc == SDL_SCANCODE_KP_PLUS) {
        tc_viewer_step_gamma(v, sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_PLUS);
        update_title(a);
        return;
    }
    if (sc == SDL_SCANCODE_I) {  /* I: Ed's AI / simple controller */
        if (!k->repeat) { tc_viewer_toggle_ai(v); update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_TAB) {  /* Tab: play as Ed / free-fly */
        if (!k->repeat) { tc_viewer_toggle_play(v); update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_COMMA || sc == SDL_SCANCODE_PERIOD) {  /* , / .: field of view (vertical, 5 degree steps) */
        tc_viewer_step_fov(v, sc == SDL_SCANCODE_PERIOD ? 1 : -1);
        update_title(a);
        return;
    }
    if (sc == SDL_SCANCODE_G) {  /* G: level fog on/off */
        if (!k->repeat) { tc_viewer_toggle_fog(v); update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_L) {  /* L: lighting: the original's / the port's earlier one (comparing) */
        if (!k->repeat) { tc_viewer_cycle_lighting(v); update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_K) {  /* K: sectors drawn: Ed's and its list (default) / all / his alone */
        if (!k->repeat) { tc_viewer_cycle_sectors(v); update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_X) {  /* X: recentre the camera behind Ed */
        if (!k->repeat) { tc_viewer_recenter_camera(v); update_title(a); }
        return;
    }
    if (sc == SDL_SCANCODE_SEMICOLON || sc == SDL_SCANCODE_APOSTROPHE || sc == SDL_SCANCODE_Y) {  /* ; ': look sensitivity, Y: invert */
        if (!k->repeat) {
            if (sc == SDL_SCANCODE_Y) tc_viewer_toggle_invert_y(v);
            else tc_viewer_step_look(v, sc == SDL_SCANCODE_APOSTROPHE ? 1 : -1);
            a->look_sensitivity = v->look_sensitivity;
            a->invert_y = v->invert_y;
            update_title(a);
        }
        return;
    }
    if (sc == SDL_SCANCODE_P) {  /* P: pause/resume animation */
        if (!k->repeat) tc_viewer_toggle_pause(v);
        return;
    }
    int action = input_action(sc);
    if (action >= 0) tc_viewer_set_action(v, (tc_view_action)action, true);
}

/* The level select's selected level (Enter, Cross). */
static void open_selected(app *a) {
    picker *p = &a->picker;
    if (p->selected < 0 || p->selected >= p->count) return;
    char *path = SDL_strdup(p->entries[p->selected].path);  /* opening closes the picker */
    if (path) open_level(a, path, NULL);
    SDL_free(path);
}

static void picker_key(app *a, const SDL_KeyboardEvent *k) {
    picker *p = &a->picker;
    switch (k->scancode) {
    case SDL_SCANCODE_LEFT: picker_move(p, -1, 0); break;
    case SDL_SCANCODE_RIGHT: picker_move(p, 1, 0); break;
    case SDL_SCANCODE_UP: picker_move(p, 0, -1); break;
    case SDL_SCANCODE_DOWN: picker_move(p, 0, 1); break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
        if (!k->repeat) open_selected(a);
        break;
    case SDL_SCANCODE_V:  /* V: the intro movie */
        if (!k->repeat && !(k->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) play_intro(a);
        break;
    case SDL_SCANCODE_R:  /* R: the replays */
        if (!k->repeat && !(k->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) show_replays(a);
        break;
    default: break;
    }
}

/* ---- the Replays menu ---- */

static void play_replay(app *a, int i) {
    if (i < 0 || i >= a->rp.count || !a->rp.entries[i].playable) return;
    char *path = SDL_strdup(a->rp.entries[i].path);  /* opening a level closes the menu */
    if (!start_playback(a, path)) SDL_Log("Stayed on the Replays menu");
    SDL_free(path);
}

static void delete_replay(app *a, int i) {
    if (replays_delete(&a->rp, i)) return;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Replays", "That recording could not be deleted.", a->window);
}

/* Enter or Cross on the selected row: its question's yes, else play. */
static void replays_accept(app *a) {
    replays *r = &a->rp;
    if (r->confirm >= 0) delete_replay(a, r->confirm);
    else play_replay(a, r->selected);
}

/* Esc or Circle: the question, else the menu. */
static void replays_cancel(app *a) {
    if (a->rp.confirm >= 0) a->rp.confirm = -1;
    else {
        close_replays(a);
        set_title(a, "Open Tonic");
    }
}

/* Record runs: from the next run on. */
static void replays_toggle_record(app *a) {
    replays *r = &a->rp;
    if (!r->can_record) return;
    r->recording = !r->recording;
    record_set_enabled(r->recording);
}

static void replays_key(app *a, const SDL_KeyboardEvent *k) {
    replays *r = &a->rp;
    switch (k->scancode) {
    case SDL_SCANCODE_UP: replays_move(r, -1); break;
    case SDL_SCANCODE_DOWN: replays_move(r, 1); break;
    case SDL_SCANCODE_R:
        if (!k->repeat) replays_toggle_record(a);
        break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
        if (!k->repeat) replays_accept(a);
        break;
    case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE:
        if (!k->repeat) replays_ask(r, r->selected);
        break;
    case SDL_SCANCODE_ESCAPE:
        if (!k->repeat) replays_cancel(a);
        break;
    default: break;
    }
}

static void replays_click(app *a, float x, float y) {
    replays *r = &a->rp;
    replays_hit h = replays_at(r, x, y);
    switch (h.action) {
    case REPLAYS_BACK: r->confirm = -1; replays_cancel(a); break;
    case REPLAYS_ROW: r->selected = h.row; r->confirm = -1; break;
    case REPLAYS_PLAY: play_replay(a, h.row); break;
    case REPLAYS_DELETE: replays_ask(r, h.row); break;
    case REPLAYS_YES: delete_replay(a, h.row); break;
    case REPLAYS_KEEP: r->confirm = -1; break;
    case REPLAYS_RECORD: replays_toggle_record(a); break;
    case REPLAYS_NOTHING: break;
    }
}

/* "Look at this moment": kept in the recording with the frame and the state (docs/RECORDING.md). */
static void mark_moment(app *a) {
    uint32_t n = a->recorder && !a->replaying ? tc_recorder_mark(a->recorder, NULL) : 0;
    if (!n) return;
    a->marks = n;
    SDL_Log("Mark %u", n);
    update_title(a);
}

static void key_down(app *a, const SDL_KeyboardEvent *k) {
    SDL_Scancode sc = k->scancode;
    bool ctrl = (k->mod & SDL_KMOD_CTRL) != 0, alt = (k->mod & SDL_KMOD_ALT) != 0;
    if (ctrl && sc == SDL_SCANCODE_Q) { a->quit = true; return; }
    if (ctrl && sc == SDL_SCANCODE_L) { if (!k->repeat && a->view) show_picker(a); return; }
    /* The intro: Esc, Space, Return, keypad Enter skip; M, F11 and Ctrl+Q as everywhere (only those on the loading
     * screen after it). */
    if (a->intro || a->next_level) {
        if (k->repeat) return;
        bool enter = sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER;
        if (a->intro && (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_SPACE || (enter && !alt))) {
            finish_intro(a);
            return;
        }
        if (sc == SDL_SCANCODE_M) {
            music_set_muted(!music_muted());
            if (a->intro) intro_set_muted(a->intro, music_muted());
            return;
        }
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
    if (sc == SDL_SCANCODE_F9) {  /* F9: mark this moment in the recording */
        if (!k->repeat) mark_moment(a);
        return;
    }
    if (sc == SDL_SCANCODE_F3) {  /* F3: supersampling off / 1.5x / 2x */
        if (!k->repeat) {
            gpu_set_scale(&a->g, gpu_next_scale(&a->g));
            SDL_Log("Supersampling: %d %%", a->g.scale);
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
    if (a->view) {
        if (!a->replaying) level_key(a, k);
        else if (a->replay_paused) {  /* the look keys and no others: the rest would change the run */
            int action = input_action(sc);
            if (action >= 0) tc_viewer_set_action(&a->view->viewer, (tc_view_action)action, true);
        }
    }
    else if (a->replays_on) replays_key(a, k);
    else if (a->picker_open) picker_key(a, k);
}

/* ---- gamepad ---- */

static void pad_actions(app *a, uint32_t actions, bool down) {
    for (int i = 0; i < TC_VA_COUNT; i++)
        if ((actions >> i) & 1u) tc_viewer_set_action(&a->view->viewer, (tc_view_action)i, down);
}

/* Buttons (positional: SOUTH is Cross / A). The level's actions are input.c's; Start, Select, R3 and the level select
 * and intro buttons are here, as the keys in key_down. */
static void pad_button(app *a, const SDL_GamepadButtonEvent *b) {
    SDL_GamepadButton button = (SDL_GamepadButton)b->button;
    a->pad = b->which;
    if (!b->down) {
        if (a->view) pad_actions(a, input_pad_actions(button), false);
        return;
    }
    if (a->intro) {  /* Cross, Circle, Start skip */
        if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_START)
            finish_intro(a);
        return;
    }
    if (a->next_level) return;  /* the loading screen after it */
    if (button == SDL_GAMEPAD_BUTTON_BACK) {  /* Select: the controls (F1) */
        a->help = !a->help;
        if (a->help) set_captured(a, false);
        return;
    }
    if (a->help) {  /* Circle closes them (Esc) */
        if (button == SDL_GAMEPAD_BUTTON_EAST) a->help = false;
        return;
    }
    if (a->view) {
        if (button == SDL_GAMEPAD_BUTTON_TOUCHPAD) { mark_moment(a); return; }  /* F9 */
        if (button == SDL_GAMEPAD_BUTTON_START) { show_picker(a); return; }  /* level select (Ctrl+L) */
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {  /* L3: first person (0) */
            tc_viewer_toggle_first_person(&a->view->viewer);
            update_title(a);
            return;
        }
        if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {  /* R3: play as Ed / free-fly (Tab) */
            tc_viewer_toggle_play(&a->view->viewer);
            update_title(a);
            return;
        }
        pad_actions(a, input_pad_actions(button), true);
        return;
    }
    if (a->replays_on) {
        switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: replays_move(&a->rp, -1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: replays_move(&a->rp, 1); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: replays_accept(a); break;
        case SDL_GAMEPAD_BUTTON_WEST: replays_ask(&a->rp, a->rp.selected); break;  /* Square: delete */
        case SDL_GAMEPAD_BUTTON_NORTH: replays_toggle_record(a); break;  /* Triangle: Record runs */
        case SDL_GAMEPAD_BUTTON_EAST: replays_cancel(a); break;
        default: break;
        }
        return;
    }
    if (!a->picker_open) return;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: picker_move(&a->picker, -1, 0); break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: picker_move(&a->picker, 1, 0); break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: picker_move(&a->picker, 0, -1); break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: picker_move(&a->picker, 0, 1); break;
    case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: open_selected(a); break;
    case SDL_GAMEPAD_BUTTON_NORTH: play_intro(a); break;  /* Triangle: the intro (V) */
    case SDL_GAMEPAD_BUTTON_WEST: show_replays(a); break;  /* Square: the replays (R) */
    default: break;
    }
}

static float pad_axis(SDL_Gamepad *g, SDL_GamepadAxis axis) {
    return SDL_max(-1.0f, (float)SDL_GetGamepadAxis(g, axis) / 32767.0f);
}

/* The left stick's step on the menu showing. */
static void nav_move(app *a, int dx, int dy) {
    if (!a->replays_on) picker_move(&a->picker, dx, dy);
    else if (dy) replays_move(&a->rp, dy);
}

/* Every frame, from the last-used gamepad: in a level the sticks (the core's dead zones), R2 as Fast and the D-pad's
 * up / down as the mouse wheel (camera distance in play, fly speed in free-fly); in the level select the left stick
 * moves the selection, repeating while held, and the right one scrolls. */
static void pad_update(app *a, float dt) {
    SDL_Gamepad *g = a->pad ? SDL_GetGamepadFromID(a->pad) : NULL;
    if (!g || a->intro || a->next_level || (a->replaying && !a->replay_paused)) return;  /* a playback's inputs are the recording's */
    float lx = pad_axis(g, SDL_GAMEPAD_AXIS_LEFTX), ly = -pad_axis(g, SDL_GAMEPAD_AXIS_LEFTY);  /* SDL: y down */
    float rx = pad_axis(g, SDL_GAMEPAD_AXIS_RIGHTX), ry = -pad_axis(g, SDL_GAMEPAD_AXIS_RIGHTY);
    if (a->view) {
        tc_viewer *v = &a->view->viewer;
        if (a->help) lx = ly = rx = ry = 0;
        tc_viewer_set_move_stick(v, lx, ly);
        tc_viewer_set_look_stick(v, rx, ry);
        bool fast = !a->help && pad_axis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f;
        if (fast != a->pad_fast) {
            a->pad_fast = fast;
            tc_viewer_set_action(v, TC_VA_FAST, fast);
        }
        bool recenter = !a->help && pad_axis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f;  /* L2: recentre the camera */
        if (recenter && !a->pad_recenter && !a->replaying) { tc_viewer_recenter_camera(v); update_title(a); }
        a->pad_recenter = recenter;
        int wheel = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) - SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        if (wheel && !a->help) tc_viewer_wheel(v, (float)wheel * 10 * dt);  /* 10 notches a second */
    } else if ((a->picker_open || a->replays_on) && !a->help) {
        int dx = SDL_fabsf(lx) > 0.5f && SDL_fabsf(lx) >= SDL_fabsf(ly) ? (lx > 0 ? 1 : -1) : 0;
        int dy = SDL_fabsf(ly) > 0.5f && SDL_fabsf(ly) > SDL_fabsf(lx) ? (ly > 0 ? -1 : 1) : 0;  /* stick up: row up */
        if (dx != a->nav_dx || dy != a->nav_dy) {
            a->nav_dx = dx;
            a->nav_dy = dy;
            a->nav_repeat = 0.4f;
            if (dx || dy) nav_move(a, dx, dy);
        } else if ((dx || dy) && (a->nav_repeat -= dt) <= 0) {
            a->nav_repeat = 0.12f;
            nav_move(a, dx, dy);
        }
        if (SDL_fabsf(ry) > 0.2f) {
            if (a->replays_on) replays_scroll(&a->rp, -ry * 1500 * dt * a->rp.scale);
            else picker_scroll_pane(&a->picker, -ry * 1500 * dt * a->picker.scale);
        }
    }
}

/* Window coordinates -> swapchain pixels. */
static float to_pixels(const app *a, float v) { return v * SDL_GetWindowPixelDensity(a->window); }

/* In a playback the game's inputs are the recording's: Esc and Start go back to the list, P and Cross pause; paused, the look
 * inputs get through, otherwise clicks and pad buttons do nothing, and of the keys only the app's own (key_down:
 * never level_key). True: the event is used up. */
static bool playback_event(app *a, const SDL_Event *e) {
    if (e->type == SDL_EVENT_KEY_DOWN) {
        SDL_Scancode sc = e->key.scancode;
        if (sc == SDL_SCANCODE_P && !(e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
            if (!e->key.repeat) toggle_playback_pause(a);
            return true;
        }
        if (sc != SDL_SCANCODE_ESCAPE || a->help) return false;
        if (!e->key.repeat) back_to_replays(a, "back to the list");
        return true;
    }
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        SDL_GamepadButton button = (SDL_GamepadButton)e->gbutton.button;
        bool down = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (down && button == SDL_GAMEPAD_BUTTON_START) back_to_replays(a, "back to the list");
        else if (down && button == SDL_GAMEPAD_BUTTON_SOUTH) toggle_playback_pause(a);
        else if (a->replay_paused) pad_actions(a, input_pad_actions(button), down);
        return true;
    }
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (a->replay_paused && e->button.button == SDL_BUTTON_LEFT) set_captured(a, true);
        return true;
    }
    return e->type == SDL_EVENT_KEY_UP && !a->replay_paused;
}

static void handle(app *a, const SDL_Event *e) {
    if (a->replaying && playback_event(a, e)) return;
    switch (e->type) {
    case SDL_EVENT_QUIT: a->quit = true; break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        set_captured(a, false);
        if (a->view) tc_viewer_release_all(&a->view->viewer);
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if (a->picker_open) a->picker.scale = SDL_GetWindowDisplayScale(a->window);
        break;
    case SDL_EVENT_KEY_DOWN: key_down(a, &e->key); break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_OpenGamepad(e->gdevice.which)) {
            if (!a->pad) a->pad = e->gdevice.which;
            SDL_Log("Gamepad: %s", SDL_GetGamepadNameForID(e->gdevice.which));
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED: {
        SDL_Gamepad *g = SDL_GetGamepadFromID(e->gdevice.which);
        if (g) SDL_CloseGamepad(g);
        if (a->pad == e->gdevice.which) {
            a->pad = 0;
            a->pad_fast = false;
            if (a->view) tc_viewer_release_all(&a->view->viewer);
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: pad_button(a, &e->gbutton); break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:  /* a stick or trigger in real use makes that pad the one read */
        if (e->gaxis.value > 12000 || e->gaxis.value < -12000) a->pad = e->gaxis.which;
        break;
    case SDL_EVENT_KEY_UP:
        if (a->view) {
            int action = input_action(e->key.scancode);
            if (action >= 0) tc_viewer_set_action(&a->view->viewer, (tc_view_action)action, false);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button == SDL_BUTTON_MIDDLE && a->view && !a->help && !a->intro && !a->next_level) {
            tc_viewer_recenter_camera(&a->view->viewer);  /* the middle button: recentre the camera (X) */
            update_title(a);
            break;
        }
        if (e->button.button != SDL_BUTTON_LEFT || a->help || a->intro || a->next_level) break;
        if (a->view) set_captured(a, true);
        else if (a->replays_on) replays_click(a, to_pixels(a, e->button.x), to_pixels(a, e->button.y));
        else if (a->picker_open) {
            int i = picker_hit(&a->picker, to_pixels(a, e->button.x), to_pixels(a, e->button.y));
            if (i == PICKER_INTRO) play_intro(a);
            else if (i == PICKER_REPLAYS) show_replays(a);
            else if (i == PICKER_CONTROLS) a->help = true;
            else if (i == PICKER_PANE_LEFT) picker_pane(&a->picker, -1);
            else if (i == PICKER_PANE_RIGHT) picker_pane(&a->picker, 1);
            else if (i >= 0) {
                char *path = SDL_strdup(a->picker.entries[i].path);  /* opening closes the picker */
                if (path) open_level(a, path, NULL);
                SDL_free(path);
            }
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (a->view && a->captured) {
            if (a->skip_motion) { a->skip_motion = false; break; }
            tc_viewer_mouse_look(&a->view->viewer, e->motion.xrel, e->motion.yrel);
        } else if (a->replays_on) {
            replays_set_hover(&a->rp, to_pixels(a, e->motion.x), to_pixels(a, e->motion.y));
        } else if (a->picker_open && !a->intro && !a->next_level) {
            picker_set_hover(&a->picker, to_pixels(a, e->motion.x), to_pixels(a, e->motion.y));
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float y = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        if (a->view) {
            if (!a->replaying || a->replay_paused) tc_viewer_wheel(&a->view->viewer, y);  /* camera zoom in play mode, fly speed in free-fly */
        } else if (a->replays_on) {
            replays_scroll(&a->rp, -y * 80 * a->rp.scale);
        } else if (a->picker_open && !a->intro && !a->next_level) {
            picker_wheel(&a->picker, to_pixels(a, e->wheel.mouse_x), to_pixels(a, e->wheel.mouse_y), -y * 80 * a->picker.scale);
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

/* The loading screen's vignette for `level`, made when it changes (before the frame: uploads). */
static void loading_prepare(app *a, const char *level) {
    if (!level || SDL_strcmp(level, a->loading_level) == 0) return;
    loading_free(a);
    SDL_strlcpy(a->loading_level, level, sizeof a->loading_level);
    a->loading_start_ns = SDL_GetTicksNS();
    /* Vignette names differ in case from the level names (Cock01.png / cock01). Next to the shown level's folder (the
     * Special Edition's: out/se/Vignette/Level, for its arches' level changes), else the retail ones. */
    char *base = a->current_folder ? path_dir(a->current_folder) : NULL;
    char *vignette = base ? path_join(base, "Vignette") : NULL;
    char *dir = vignette ? path_join(vignette, "Level") : NULL;
    if (!dir || !path_is_dir(dir)) {
        SDL_free(dir);
        SDL_free(vignette);
        vignette = path_join(a->data_dir, "Vignette");
        dir = vignette ? path_join(vignette, "Level") : NULL;
    }
    SDL_free(base);
    char pattern[80];
    SDL_snprintf(pattern, sizeof pattern, "%s.png", level);
    int n = 0;
    char **found = dir ? SDL_GlobDirectory(dir, pattern, SDL_GLOB_CASEINSENSITIVE, &n) : NULL;
    char *path = found && n > 0 ? path_join(dir, found[0]) : NULL;
    /* The bar's texture: out/Textures/Sud0, next to out/Vignette (without it the bar fills plain white). */
    char *textures = a->loading_bar_tried ? NULL : path_join(a->data_dir, "Textures");
    char *pool = textures ? path_join(textures, "Sud0/o_zarbi_auntxy.png") : NULL;
    a->loading_bar_tried = true;
    gpu_upload u;
    if ((path || pool) && gpu_upload_begin(&a->g, &u)) {
        if (path) {
            a->loading_vignette = gpu_upload_png(&a->g, &u, path, true, &a->vignette_w, &a->vignette_h);
            if (!a->loading_vignette) SDL_Log("vignette %s: %s", path, SDL_GetError());
        }
        if (pool && path_is_file(pool)) {
            a->loading_bar = gpu_upload_png(&a->g, &u, pool, true, NULL, NULL);
            if (!a->loading_bar) SDL_Log("loading bar %s: %s", pool, SDL_GetError());
        }
        gpu_upload_end(&a->g, &u);
    }
    SDL_free(pool);
    SDL_free(textures);
    SDL_free(path);
    SDL_free(found);
    SDL_free(dir);
    SDL_free(vignette);
}

/* The level scripts' picture (TC_VO_PICTURE: "Credits/Credit06" = Vignette/Credits/Credit06.png next to the shown
 * level's folder, else the retail one; each part matched case-insensitively), made when it changes (before the frame). */
static void picture_prepare(app *a, const char *name) {
    if (!name || SDL_strcmp(name, a->picture_name) == 0) return;
    if (a->picture) SDL_ReleaseGPUTexture(a->g.device, a->picture);
    a->picture = NULL;
    SDL_strlcpy(a->picture_name, name, sizeof a->picture_name);
    char *base = a->current_folder ? path_dir(a->current_folder) : NULL;
    char *dir = base ? path_join(base, "Vignette") : NULL;
    SDL_free(base);
    if (!dir || !path_is_dir(dir)) {
        SDL_free(dir);
        dir = path_join(a->data_dir, "Vignette");
    }
    char part[64];
    const char *p = name;
    while (dir && *p) {
        const char *slash = SDL_strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : SDL_strlen(p);
        SDL_snprintf(part, sizeof part, "%.*s%s", (int)n, p, slash ? "" : ".png");
        int count = 0;
        char **found = SDL_GlobDirectory(dir, part, SDL_GLOB_CASEINSENSITIVE, &count);
        char *next = found && count > 0 ? path_join(dir, found[0]) : NULL;
        SDL_free(found);
        SDL_free(dir);
        dir = next;
        p = slash ? slash + 1 : p + n;
    }
    gpu_upload u;
    if (dir && path_is_file(dir) && gpu_upload_begin(&a->g, &u)) {
        a->picture = gpu_upload_png(&a->g, &u, dir, true, &a->picture_w, &a->picture_h);
        if (!a->picture) SDL_Log("picture %s: %s", dir, SDL_GetError());
        gpu_upload_end(&a->g, &u);
    } else {
        SDL_Log("picture %s: not found", name);
    }
    SDL_free(dir);
}

/* Over the overlay's black, as main.swift's PictureScreen: the picture in the largest 4:3 box, centred. */
static void draw_picture(app *a, gpu_frame *f, SDL_GPURenderPass *pass) {
    float W = (float)f->width, H = (float)f->height, w = SDL_min(W, H * 4 / 3), h = w * 3 / 4;
    if (!a->picture || a->picture_w <= 0 || a->picture_h <= 0 || w <= 0) return;
    float s = SDL_min(w / (float)a->picture_w, h / (float)a->picture_h);
    float iw = (float)a->picture_w * s, ih = (float)a->picture_h * s;
    gpu_quad(&a->g, f, pass, a->picture, (SDL_FRect){(W - iw) / 2, (H - ih) / 2, iw, ih}, NULL, (SDL_FColor){1, 1, 1, 1});
}

/* Over the overlay's black, laid out as main.swift's LoadingScreen: the vignette (4:3, 80 % wide at most), then the
 * rainbow progress bar under it. */
static void draw_loading(app *a, gpu_frame *f, SDL_GPURenderPass *pass, float progress) {
    float k = SDL_GetWindowDisplayScale(a->window), W = (float)f->width, H = (float)f->height;
    float w = SDL_min(W * 0.8f, (H - 120 * k) * 4 / 3), h = w * 3 / 4;
    if (w <= 0) return;
    float x = (W - w) / 2, y = (H - h) / 2 - 40 * k;
    if (a->loading_vignette && a->vignette_w > 0 && a->vignette_h > 0) {
        float s = SDL_min(w / (float)a->vignette_w, h / (float)a->vignette_h);
        float iw = (float)a->vignette_w * s, ih = (float)a->vignette_h * s;
        gpu_quad(&a->g, f, pass, a->loading_vignette, (SDL_FRect){x + (w - iw) / 2, y + (h - ih) / 2, iw, ih}, NULL,
                 (SDL_FColor){1, 1, 1, 1});
    }
    float bw = SDL_floorf(w * 0.6f), bh = SDL_roundf(18 * k), bx = SDL_floorf((W - bw) / 2), by = SDL_floorf(y + h + 56 * k);
    float t = (float)(SDL_GetTicksNS() - a->loading_start_ns) / 1e9f;
    gpu_loading_bar(&a->g, f, pass, a->loading_bar, (SDL_FRect){bx, by, bw, bh}, progress, t, k);
}

static void draw(app *a) {
    tc_view_overlay o;
    SDL_zero(o);
    if (a->view) {
        tc_viewer_overlay(&a->view->viewer, &o);
        if (o.kind == TC_VO_LOADING) loading_prepare(a, o.level);
        if (o.kind == TC_VO_PICTURE) picture_prepare(a, o.picture);
    } else if (a->next_level) {
        loading_prepare(a, a->next_name);
    }
    gpu_frame f;
    if (!gpu_frame_begin(&a->g, &f)) { SDL_Delay(16); return; }  /* minimised */
    if (a->intro) {
        intro_upload(a->intro, &f);
        SDL_GPURenderPass *pass = gpu_pass_2d(&a->g, &f, true, (SDL_FColor){0, 0, 0, 1});
        if (pass) { intro_draw(a->intro, &f, pass); SDL_EndGPURenderPass(pass); }
    } else if (a->next_level) {  /* the loading screen after the intro, on black */
        SDL_GPURenderPass *pass = gpu_pass_2d(&a->g, &f, true, (SDL_FColor){0, 0, 0, 1});
        if (pass) {
            draw_loading(a, &f, pass, SDL_min(1.0f, a->next_time / TC_VIEWER_LOADING_SECONDS));
            SDL_EndGPURenderPass(pass);
        }
    } else if (a->view) {
        view_draw_frame(a->view, &f);
        bool sight = view_draw_crosshair(a->view, &f, NULL);
        if (o.kind != TC_VO_NONE || a->help || sight) {  /* screen transition or loading screen, the gunsight, the controls */
            SDL_GPURenderPass *pass = gpu_pass_2d(&a->g, &f, false, (SDL_FColor){0, 0, 0, 1});
            if (pass) {
                view_draw_overlay(a->view, &f, pass, &o);
                if (sight) view_draw_crosshair(a->view, &f, pass);
                if (o.kind == TC_VO_LOADING) draw_loading(a, &f, pass, o.progress);
                if (o.kind == TC_VO_PICTURE) draw_picture(a, &f, pass);
                if (a->help) draw_help(a, &f, pass);
                SDL_EndGPURenderPass(pass);
            }
        }
    } else {
        SDL_GPURenderPass *pass = gpu_pass_2d(&a->g, &f, true, (SDL_FColor){0.08f, 0.09f, 0.14f, 1});
        if (pass) {
            if (a->replays_on) replays_draw(&a->rp, &f, pass);
            else if (a->picker_open) picker_draw(&a->picker, &f, pass);
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

/* Default data, first that exists: data/ttlv next to the binary (an install by tools/install_linux.sh, as the Mac
 * app's bundle); files/out/ttlv in the working directory; next to the binary in the checkout (sdl/build/opentonic
 * -> files/out/ttlv); in the checkout this binary was built from; out/ttlv in the working directory (older layout). */
static char *default_level_folder(void) {
    const char *base = SDL_GetBasePath();
    char *candidates[5] = {base ? path_join(base, "data/ttlv") : NULL, SDL_strdup("files/out/ttlv"),
                           base ? path_join(base, "../../files/out/ttlv") : NULL,
                           SDL_strdup(TONIC_REPO_DIR "/files/out/ttlv"), SDL_strdup("out/ttlv")};
    char *found = NULL;
    for (int i = 0; i < 5; i++) {
        if (!found && candidates[i] && path_is_dir(candidates[i])) found = candidates[i];
        else SDL_free(candidates[i]);
    }
    return found ? found : SDL_strdup("files/out/ttlv");
}

/* TONIC_SCALE=100/150/200: the level's starting supersampling (F3 cycles it), percent of the window's pixels per axis. */
static int scale_setting(void) {
    const char *e = SDL_getenv("TONIC_SCALE");
    return e ? SDL_atoi(e) : 100;
}

/* TONIC_LOOK=percent (25-400, default 100) and TONIC_INVERT=x, y or xy: the look settings at the start (; ' Y change
 * them). */
static void look_settings(app *a) {
    const char *e = SDL_getenv("TONIC_LOOK"), *inv = SDL_getenv("TONIC_INVERT");
    a->look_sensitivity = e && SDL_atoi(e) > 0 ? (float)SDL_atoi(e) / 100 : 1;
    a->invert_x = inv && (SDL_strchr(inv, 'x') || SDL_strchr(inv, 'X'));
    a->invert_y = inv && (SDL_strchr(inv, 'y') || SDL_strchr(inv, 'Y'));
}

/* TONIC_MSAA=1/2/4/8: the level's starting antialiasing (F2 cycles it). */
static int msaa_setting(void) {
    const char *e = SDL_getenv("TONIC_MSAA");
    int n = e ? SDL_atoi(e) : 8;
    return n >= 8 ? 8 : n >= 4 ? 4 : n >= 2 ? 2 : 1;
}

int main(int argc, char **argv) {
    SDL_SetAppMetadata("Open Tonic", "0.1", "org.opentonic.sdl");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    app a;
    SDL_zero(a);
    a.window = SDL_CreateWindow("Open Tonic", 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!a.window || !gpu_init(&a.g, a.window, msaa_setting())) {
        char msg[512];
        SDL_snprintf(msg, sizeof msg, "Could not set up the GPU: %s", SDL_GetError());
        SDL_Log("%s", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open Tonic", msg, a.window);
        gpu_quit(&a.g);
        if (a.window) SDL_DestroyWindow(a.window);
        SDL_Quit();
        return 1;
    }
    gpu_set_scale(&a.g, scale_setting());
    look_settings(&a);

    /* --replay [file]: plays a recording back (the newest without one); the rest as usual. */
    bool want_replay = false;
    char *replay_file = NULL;
    for (int i = 1; i < argc; i++)
        if (!SDL_strcmp(argv[i], "--replay")) {
            int used = 1;
            want_replay = true;
            if (i + 1 < argc && SDL_strstr(argv[i + 1], "." TC_REC_EXTENSION)) {
                replay_file = SDL_strdup(argv[i + 1]);
                used = 2;
            }
            for (int k = i; k + used <= argc; k++) argv[k] = argv[k + used];
            argc -= used;
            break;
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
    char *icon_path = icon_dir ? path_join(icon_dir, "OpenTonic.png") : NULL;
    if (icon_path && !path_is_file(icon_path)) {  /* the name before the rename */
        SDL_free(icon_path);
        icon_path = path_join(icon_dir, "TonicViewer.png");
    }
    SDL_Surface *icon = icon_path && path_is_file(icon_path) ? SDL_LoadPNG(icon_path) : NULL;
    if (icon) { SDL_SetWindowIcon(a.window, icon); SDL_DestroySurface(icon); }
    SDL_free(icon_dir);
    SDL_free(icon_path);

    SDL_Log("Controls: WASD move, arrow keys look, R/F up/down, Shift fast; click to capture mouse-look, Esc to release; "
            "wheel = fly speed / play-mode zoom; , / . field of view; P pause animation; F2 antialiasing; F3 supersampling; starts as Ed when the level has "
            "him, Tab toggles free-fly (Space jump); Ctrl+L level select; F1 controls; F11 fullscreen");
    if (want_replay && !replay_file) replay_file = record_latest();
    if (want_replay && start_playback(&a, replay_file)) {}
    else if (!level_file || !open_level(&a, level_file, NULL)) show_picker(&a);
    SDL_free(replay_file);

    Uint64 last = SDL_GetTicksNS();
    while (!a.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(&a, &e);
        if (a.quit) break;
        Uint64 now = SDL_GetTicksNS();
        float dt = SDL_min((float)(now - last) / 1e9f, 0.1f);
        last = now;
        if (a.intro && !intro_update(a.intro, dt)) finish_intro(&a);
        if (a.next_level) a.next_time += dt;
        update_cursor(&a);
        pad_update(&a, dt);
        if (a.view && a.replaying) playback_tick(&a, dt);
        else if (a.view) {
            tc_viewer_update(&a.view->viewer, dt);
            const uint16_t *sounds;
            uint32_t n = tc_viewer_sounds(&a.view->viewer, &sounds);
            if (n > 0) sfx_play(sounds, n);
        }
        if (a.view) {
            /* Refresh the AI's behaviour/state and the light changes in the title. */
            if (tc_viewer_status_live(&a.view->viewer) && now - a.last_title_ns > 250000000) {
                a.last_title_ns = now;
                update_title(&a);
            }
        }
        draw(&a);
        /* After a red portal's loading screen (drawn at least once at full progress): the next level. */
        const tc_viewer_carry *pending = a.view && !a.replaying ? tc_viewer_pending_level(&a.view->viewer) : NULL;
        if (pending) open_portal_level(&a, pending);
        /* The same after the intro's loading screen: Totalski. */
        if (a.next_level && a.next_time >= TC_VIEWER_LOADING_SECONDS) finish_loading(&a);
    }

    music_stop();
    sfx_quit();
    intro_close(a.intro);
    stop_playback(&a, "quit");
    close_replays(&a);
    close_level(&a);
    end_recording(&a);
    close_picker(&a);
    SDL_WaitForGPUIdle(a.g.device);
    loading_free(&a);  /* quitting on the loading screen after the intro (no level holds it then) */
    if (a.loading_bar) SDL_ReleaseGPUTexture(a.g.device, a.loading_bar);
    if (a.help_text) SDL_ReleaseGPUTexture(a.g.device, a.help_text);
    SDL_free(a.next_level);
    gpu_quit(&a.g);
    SDL_DestroyWindow(a.window);
    SDL_free(a.level_folder);
    SDL_free(a.data_dir);
    SDL_free(a.current_folder);
    SDL_Quit();
    return 0;
}
