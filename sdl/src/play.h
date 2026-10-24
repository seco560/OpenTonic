/* A .ttlv level read from disk and parsed by the core (Sources/OpenTonic/Core.swift's Level). Play mode itself
 * (collision, Ed's controller and AI) lives in the core's tc_viewer. */
#ifndef TV_PLAY_H
#define TV_PLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <tonic/tonic.h>

typedef struct tv_level {
    tc_level raw;
    char *path;
    char *folder;   /* texture paths are relative to it */
    char *name;     /* file name without extension ("bar") */
} tv_level;

bool tv_level_load(tv_level *l, const char *path, char *err, size_t err_size);
void tv_level_free(tv_level *l);

#endif
