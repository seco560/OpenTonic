/* Photo mode's photos (photo.h). The JPEG encoder is stb_image_write (third_party, public domain / MIT). */
#include "photo.h"
#include <SDL3/SDL.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STBIW_MALLOC(sz) SDL_malloc(sz)
#define STBIW_REALLOC(p, sz) SDL_realloc(p, sz)
#define STBIW_FREE(p) SDL_free(p)
#include "stb_image_write.h"

#define PHOTO_QUALITY 92

typedef struct photo_job { uint8_t *rgba; int w, h; } photo_job;

/* The Pictures folder (XDG_PICTURES_DIR on Linux, ~/Pictures on macOS), else ~/Pictures, made if missing. */
static char *pictures_folder(void) {
    const char *p = SDL_GetUserFolder(SDL_FOLDER_PICTURES);
    if (p && *p) return SDL_strdup(p);
    const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    char *dir = NULL;
    if (SDL_asprintf(&dir, "%s%sPictures/", home ? home : "./", home && home[SDL_strlen(home) - 1] == '/' ? "" : "/") < 0)
        return NULL;
    SDL_CreateDirectory(dir);
    return dir;
}

static void write_io(void *context, void *data, int size) {
    SDL_WriteIO((SDL_IOStream *)context, data, (size_t)size);
}

static int SDLCALL save_thread(void *data) {
    photo_job *j = (photo_job *)data;
    char *dir = pictures_folder(), *path = NULL;
    SDL_Time now = 0;
    SDL_DateTime t;
    SDL_zero(t);
    SDL_GetCurrentTime(&now);
    SDL_TimeToDateTime(now, &t, true);
    for (int n = 1; dir && n < 100; n++) { /* two in one second: _2, _3 ... */
        SDL_free(path);
        path = NULL;
        char suffix[8] = "";
        if (n > 1) SDL_snprintf(suffix, sizeof suffix, "_%d", n);
        size_t len = SDL_strlen(dir);
        if (SDL_asprintf(&path, "%s%sOpenTonicScreenshot%04d-%02d-%02d_%02d-%02d-%02d%s.jpeg", dir,
                         len && dir[len - 1] == '/' ? "" : "/", t.year, t.month, t.day, t.hour, t.minute, t.second,
                         suffix) < 0) {
            path = NULL;
            break;
        }
        if (!SDL_GetPathInfo(path, NULL)) break; /* free */
    }
    SDL_IOStream *io = path ? SDL_IOFromFile(path, "wb") : NULL;
    if (io && stbi_write_jpg_to_func(write_io, io, j->w, j->h, 4, j->rgba, PHOTO_QUALITY) && SDL_CloseIO(io))
        SDL_Log("photo: %s (%d x %d)", path, j->w, j->h);
    else {
        if (io) SDL_CloseIO(io);
        SDL_Log("photo: could not write %s: %s", path ? path : "(no Pictures folder)", SDL_GetError());
    }
    SDL_free(path);
    SDL_free(dir);
    SDL_free(j->rgba);
    SDL_free(j);
    return 0;
}

void photo_save(uint8_t *rgba, int w, int h) {
    photo_job *j = (photo_job *)SDL_malloc(sizeof *j);
    if (!j) { SDL_free(rgba); return; }
    j->rgba = rgba, j->w = w, j->h = h;
    SDL_Thread *th = SDL_CreateThread(save_thread, "photo", j);
    if (th) SDL_DetachThread(th);
    else save_thread(j);
}
