#include "play.h"
#include <SDL3/SDL.h>
#include "util.h"

bool tv_level_load(tv_level *l, const char *path, char *err, size_t err_size) {
    SDL_zerop(l);
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) {
        SDL_snprintf(err, err_size, "%s", SDL_GetError());
        return false;
    }
    tc_result r = tc_level_load(&l->raw, data, size);
    SDL_free(data);
    if (r != TC_OK) {
        SDL_snprintf(err, err_size, "%s", tc_result_string(r));
        tc_level_free(&l->raw);
        return false;
    }
    l->path = SDL_strdup(path);
    l->folder = path_dir(path);
    l->name = path_stem(path);
    return true;
}

void tv_level_free(tv_level *l) {
    tc_level_free(&l->raw);
    SDL_free(l->path);
    SDL_free(l->folder);
    SDL_free(l->name);
    SDL_zerop(l);
}
