#include "record.h"
#include <SDL3/SDL.h>
#include "util.h"

#ifndef OPENTONIC_BUILD
#define OPENTONIC_BUILD "unknown"
#endif

static SDL_IOStream *file;   /* opened by the first write: a run without a level leaves no file */
static bool failed;

char *record_folder(void) {
    const char *env = SDL_getenv("TONIC_RECORDINGS");
    if (env && !SDL_strcasecmp(env, "off")) return NULL;
    if (env && *env) return SDL_strdup(env);
    char *pref = SDL_GetPrefPath("", "OpenTonic");
    char *folder = pref ? path_join(pref, "Recordings") : NULL;
    SDL_free(pref);
    return folder;
}

/* The toggle is a file beside the recordings: there means on. */
static char *enabled_path(void) {
    char *pref = SDL_GetPrefPath("", "OpenTonic");
    char *path = pref ? path_join(pref, "record-runs") : NULL;
    SDL_free(pref);
    return path;
}

bool record_enabled(void) {
    char *path = enabled_path();
    bool on = path && SDL_GetPathInfo(path, NULL);
    SDL_free(path);
    return on;
}

void record_set_enabled(bool on) {
    char *path = enabled_path();
    if (!path) return;
    if (on) {
        SDL_IOStream *f = SDL_IOFromFile(path, "wb");
        if (f) SDL_CloseIO(f);
        else SDL_Log("Can't keep Record runs on: %s", SDL_GetError());
    } else if (!SDL_RemovePath(path)) {
        SDL_Log("Can't keep Record runs off: %s", SDL_GetError());
    }
    SDL_free(path);
}

static void stamp(char *out, size_t size) {
    SDL_Time now;
    SDL_DateTime t;
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &t, true))
        SDL_snprintf(out, size, "%04d-%02d-%02d_%02d%02d%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
    else SDL_snprintf(out, size, "session");
}

static void sink(void *user, const void *data, size_t size) {
    (void)user;
    if (failed) return;
    if (!file) {
        char *folder = record_folder(), name[64], when[32];
        char *path = NULL;
        stamp(when, sizeof when);
        if (folder && SDL_CreateDirectory(folder))
            for (int n = 1; n < 1000; n++) {  /* two runs in a second: never over the first */
                if (n == 1) SDL_snprintf(name, sizeof name, "%s_sdl.%s", when, TC_REC_EXTENSION);
                else SDL_snprintf(name, sizeof name, "%s_sdl_%d.%s", when, n, TC_REC_EXTENSION);
                SDL_free(path);
                path = path_join(folder, name);
                if (path && !path_is_file(path)) break;
            }
        file = path ? SDL_IOFromFile(path, "wb") : NULL;
        if (file) SDL_Log("Recording to %s", path);
        else {
            SDL_Log("Not recording: %s", SDL_GetError());
            failed = true;
        }
        SDL_free(path);
        SDL_free(folder);
        if (failed) return;
    }
    /* flushed: what a crash leaves behind is the point */
    if (SDL_WriteIO(file, data, size) != size || !SDL_FlushIO(file)) {
        SDL_Log("Recording stopped: %s", SDL_GetError());
        failed = true;
    }
}

tc_recorder *record_start(void) {
    char *folder = record_folder(), when[32];
    tc_recorder *r;
    if (!folder) return NULL;
    SDL_free(folder);
    if (!record_enabled()) return NULL;
    failed = false;
    if (!(r = tc_recorder_create(sink, NULL))) return NULL;
    stamp(when, sizeof when);
    tc_recorder_note(r, "frontend", "sdl");
    tc_recorder_note(r, "build", OPENTONIC_BUILD);
    tc_recorder_note(r, "started", when);
    tc_recorder_note(r, "os", SDL_GetPlatform());
#if defined(__aarch64__)
    tc_recorder_note(r, "cpu", "arm64");
#elif defined(__x86_64__)
    tc_recorder_note(r, "cpu", "x86_64");
#endif
    return r;
}

void record_stop(tc_recorder *r) {
    tc_recorder_free(r);
    if (file) SDL_CloseIO(file);
    file = NULL;
}

char *record_latest(void) {
    char *folder = record_folder(), *path = NULL;
    int n = 0;
    char **names = folder ? SDL_GlobDirectory(folder, "*." TC_REC_EXTENSION, 0, &n) : NULL;
    const char *best = NULL;
    for (int i = 0; i < n; i++)  /* the names start with their date */
        if (!best || SDL_strcmp(names[i], best) > 0) best = names[i];
    if (best) path = path_join(folder, best);
    SDL_free(names);
    SDL_free(folder);
    return path;
}
