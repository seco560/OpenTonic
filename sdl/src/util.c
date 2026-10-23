#include "util.h"
#include <SDL3/SDL.h>

char *path_join(const char *a, const char *b) {
    char *r = NULL;
    size_t n = SDL_strlen(a);
    if (n == 0) return SDL_strdup(b);
    SDL_asprintf(&r, "%s%s%s", a, a[n - 1] == '/' ? "" : "/", b);
    return r;
}

char *path_dir(const char *path) {
    const char *slash = SDL_strrchr(path, '/');
    if (!slash) return SDL_strdup(".");
    if (slash == path) return SDL_strdup("/");
    size_t n = (size_t)(slash - path);
    char *r = SDL_malloc(n + 1);
    if (r) { SDL_memcpy(r, path, n); r[n] = 0; }
    return r;
}

char *path_stem(const char *path) {
    const char *slash = SDL_strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    char *r = SDL_strdup(name);
    char *dot = r ? SDL_strrchr(r, '.') : NULL;
    if (dot && dot != r) *dot = 0;
    return r;
}

char *path_with_ext(const char *path, const char *ext) {
    const char *slash = SDL_strrchr(path, '/');
    const char *dot = SDL_strrchr(path, '.');
    size_t n = (dot && (!slash || dot > slash + 1)) ? (size_t)(dot - path) : SDL_strlen(path);
    char *r = NULL;
    SDL_asprintf(&r, "%.*s%s", (int)n, path, ext);
    return r;
}

bool path_is_file(const char *path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

bool path_is_dir(const char *path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}
