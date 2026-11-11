/* Which files of a converted folder the checks are about (test-only: stdio, POSIX stat).
 *   - Imported levels (tools/ttre/import_gltf.py) are not the game's: the checks that walk a whole folder pass them by.
 *     They are told apart as the frontends' pickers do it: <out>/Imported/<name> is a folder.
 *   - The custom layer (tools/ttre/custom.py, a CUST chunk) appends its own static meshes after the export's, with the
 *     lighting of the level they were copied from. Checks of the export's own data stop at the pristine copy's mesh
 *     count (<out>/.custom-base/...). */
#ifndef TC_TESTLEVELS_H
#define TC_TESTLEVELS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "tonic/tonic.h"

#define TEST_NO_CUSTOM 0xFFFFFFFFu  /* test_export_meshes: the file has no custom layer */
#define TEST_NO_BASE 0xFFFFFFFEu    /* ... it has one, and its pristine copy is missing or does not load */

/* `file` ("name.ttlv") of `dir` (".../out/ttlv") is an imported level. */
static inline int test_imported(const char *dir, const char *file) {
    char path[1024];
    struct stat st;
    size_t stem = strlen(file);
    if (stem > 5 && strcmp(file + stem - 5, ".ttlv") == 0) stem -= 5;
    snprintf(path, sizeof(path), "%s/../Imported/%.*s", dir, (int)stem, file);
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* The container (tools/ttre/chunks.py): magic, u32, then { tag[4], u32 size, bytes, padding to 4 }. */
static inline int test_has_chunk(const uint8_t *data, size_t size, const char *tag) {
    size_t o = 8;
    while (o + 8 <= size) {
        uint32_t n = (uint32_t)data[o + 4] | (uint32_t)data[o + 5] << 8 | (uint32_t)data[o + 6] << 16 | (uint32_t)data[o + 7] << 24;
        if (memcmp(data + o, tag, 4) == 0) return 1;
        o += 8 + (((size_t)n + 3) & ~(size_t)3);
    }
    return 0;
}

static inline uint32_t test_mesh_count_of(const char *path) {
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long n;
    tc_level l;
    uint32_t count = TEST_NO_BASE;
    if (!f) return count;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (data && fread(data, 1, (size_t)n, f) == (size_t)n && tc_level_load(&l, data, (size_t)n) == TC_OK) {
        count = l.mesh_count;
        tc_level_free(&l);
    }
    fclose(f);
    free(data);
    return count;
}

/* How many of the level's static meshes are the export's own: TEST_NO_CUSTOM (all of them), the pristine copy's count,
 * or TEST_NO_BASE. `dir` is <out>/ttlv or <out>/se/ttlv; the copies are in <out>/.custom-base/ttlv and .../se/ttlv. */
static inline uint32_t test_export_meshes(const char *dir, const char *file, const uint8_t *data, size_t size) {
    char d[900], path[1024];
    const char *last, *before;
    size_t n = strlen(dir);
    uint32_t count;
    if (!test_has_chunk(data, size, "CUST")) return TEST_NO_CUSTOM;
    if (n >= sizeof(d)) return TEST_NO_BASE;
    memcpy(d, dir, n + 1);
    while (n > 1 && d[n - 1] == '/') d[--n] = 0;
    last = strrchr(d, '/');
    last = last ? last + 1 : d;
    snprintf(path, sizeof(path), "%s/../.custom-base/%s/%s", d, last, file);
    if ((count = test_mesh_count_of(path)) != TEST_NO_BASE) return count;
    before = last > d + 1 ? last - 2 : d;  /* the folder above: "se" */
    while (before > d && before[-1] != '/') before--;
    snprintf(path, sizeof(path), "%s/../../.custom-base/%s/%s", d, before, file);
    return test_mesh_count_of(path);
}

#endif
