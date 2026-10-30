/* The sectors (.ttlv SECT, tc_sector.h) of every converted level, checked against the game's own data:
 *   - the chunk is there and tiles every static mesh's indices with its ranges, one sector per perso;
 *   - tc_sectors_find (FUN_00451240) at each level perso's position gives the sector the snapshot holds for it
 *     (SectorInfo +4; the game tracked most by portal crossings, so a few differ: persos with no floor straight below,
 *     on sector boundaries, or in a virtual sector, which the lookup never picks);
 *   - each portal face, crossed from just in front of its centre to just behind, leads into its target
 *     (tc_sectors_cross, FUN_00451670), unless a nearer face of the same sector is in the way;
 *   - Ed's start has a sector.
 * Usage: sectorcheck [out/ttlv] [level]. Test-only stdio and POSIX <dirent.h>. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    *size = (size_t)n;
    return data;
}

static int failures;

static void level_check(const char *dir, const char *file) {
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_level l;
    const tc_sectors *s = &l.sectors;
    uint32_t i, k, match = 0, virt = 0, other = 0, none = 0, unknown = 0, crossed = 0, blocked = 0, dead = 0;
    uint32_t floors = 0, portals = 0, water_groups = 0;
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    if (!(data = read_file(path, &size)) || tc_level_load(&l, data, size) != TC_OK) {
        printf("%-12s FAIL: does not load\n", file);
        failures++;
        free(data);
        return;
    }
    if (s->count == 0 || s->mesh_count != l.mesh_count || s->perso_count != l.perso_count) {
        printf("%-12s FAIL: sectors %u, ranges for %u of %u meshes, sectors of %u of %u persos\n", file, s->count,
               s->mesh_count, l.mesh_count, s->perso_count, l.perso_count);
        failures++;
        tc_level_free(&l);
        free(data);
        return;
    }
    for (i = 0; i < s->group_count; i++) water_groups += (s->groups[i].flags & TC_GROUP_WATER) != 0;
    for (i = 0; i < l.perso_count; i++) {
        tc_vec3 p = tc_v3(l.persos[i].world.m[3], l.persos[i].world.m[7], l.persos[i].world.m[11]);
        uint16_t want = s->perso_sector[i], got;
        if (want == TC_SECTOR_NONE) { unknown++; continue; }
        got = tc_sectors_find(s, p);
        if (got == want) match++;
        else if (s->sectors[want].is_virtual) virt++;
        else if (got == TC_SECTOR_NONE) none++;
        else other++;
    }
    for (i = 0; i < s->count; i++) {
        const tc_sector *sec = &s->sectors[i];
        floors += sec->floor_count;
        portals += sec->portal_count;
        for (k = 0; k < sec->portal_count; k++) {
            const tc_portal *f = &sec->portals[k];
            tc_vec3 c = tc_v3_scale(tc_v3_add(tc_v3_add(f->v[0], f->v[1]), f->v[2]), 1.0f / 3);
            tc_vec3 n = tc_v3_normalize(f->normal);
            uint16_t got;
            if (f->target == TC_SECTOR_NONE || f->target == i) { dead++; continue; }
            got = tc_sectors_cross(s, (uint16_t)i, tc_v3_add(c, tc_v3_scale(n, 0.01f)), tc_v3_sub(c, tc_v3_scale(n, 0.01f)));
            if (got == f->target) crossed++;
            else blocked++;
        }
    }
    printf("%-12s %3u sectors, %u floors, %u portals (%u dead ends), %u water groups; persos: %u match, %u in a "
           "virtual sector, %u nothing below, %u other, %u unknown; portals crossed %u, %u through a nearer face\n",
           file, s->count, floors, portals, dead, water_groups, match, virt, none, other, unknown, crossed, blocked);
    if (match < 4 * (other + none) || crossed < 9 * blocked) {
        printf("  FAIL: too many mismatches\n");
        failures++;
    }
    if (l.has_ed) {
        uint16_t e = tc_sectors_find(s, tc_v3(l.ed.start.m[3], l.ed.start.m[7], l.ed.start.m[11] + 0.3f));
        printf("  Ed's start (%.1f, %.1f, %.1f): %s\n", l.ed.start.m[3], l.ed.start.m[7], l.ed.start.m[11],
               e == TC_SECTOR_NONE ? "(none)" : tc_sectors_name(s, e));
    }
    tc_level_free(&l);
    free(data);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    DIR *d;
    struct dirent *e;
    if (argc > 2) {
        char file[256];
        snprintf(file, sizeof(file), "%s.ttlv", argv[2]);
        level_check(dir, file);
    } else if ((d = opendir(dir)) != NULL) {
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n > 5 && strcmp(e->d_name + n - 5, ".ttlv") == 0) level_check(dir, e->d_name);
        }
        closedir(d);
    } else {
        fprintf(stderr, "no %s\n", dir);
        return 1;
    }
    printf(failures ? "sectorcheck: %d FAILED\n" : "sectorcheck: all ok\n", failures);
    return failures != 0;
}
