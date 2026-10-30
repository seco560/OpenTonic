/* The sectors (.ttlv SECT, tc_sector.h) of every converted level, checked against the game's own data:
 *   - the chunk is there and tiles every static mesh's indices with its ranges, one sector per perso;
 *   - tc_sectors_find (FUN_00451240) at each level perso's position gives the sector the snapshot holds for it
 *     (SectorInfo +4; the game tracked most by portal crossings, so a few differ: persos with no floor straight below,
 *     on sector boundaries, or in a virtual sector, which the lookup never picks);
 *   - each portal face, crossed from just in front of its centre to just behind, leads into its target
 *     (tc_sectors_cross, FUN_00451670), unless a nearer face of the same sector is in the way;
 *   - Ed's start has a sector;
 *   - the lighting (.ttlv LITE): every sector's lights fit the frame (TC_VIEW_MAX_LIGHTS), every vertex got its object's
 *     centre, and a viewer started on the level lights its first frame with the lights of Ed's sector, at once.
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

/* The original's light of a baked byte without lights (tc_viewer.c formula_light). */
static float formula(uint32_t byte, float ambient, float diffuse) {
    float l = ((float)byte / 255 * (255.0f / 256) * diffuse + ambient) * (512.0f / 255);
    return l < 0 ? 0 : l > 1 ? 1 : l;
}

static void lighting_check(const char *file, const tc_level *l) {
    const tc_sectors *s = &l->sectors;
    uint32_t i, k, most = 0, fogs = 0, envs = 0, uncentred = 0, verts = 0, baked = 0, spheres = 0, persos = 0, flat = 0;
    double old_sum = 0, new_sum = 0;
    tc_viewer v;
    tc_viewer_desc desc;
    tc_view_frame *f;
    uint16_t sec;
    if (!l->has_lighting) {
        printf("  FAIL: no LITE chunk\n");
        failures++;
        return;
    }
    for (i = 0; i < s->count; i++) {
        if (s->sectors[i].light_count > most) most = s->sectors[i].light_count;
        for (k = 0; k < s->sectors[i].env_count; k++) envs++, fogs += s->sectors[i].envs[k].fog != 0;
    }
    for (i = 0; i < l->light_count; i++) spheres += l->lights[i].type == TC_LIGHT_SPHERICAL;
    for (i = 0; i < l->mesh_count; i++) {
        const tc_mesh *m = &l->meshes[i];
        for (k = 0; k < m->vertex_count; k++) {
            const tc_vertex *x = &m->vertices[k];
            uint32_t a = x->color[0] >> 24, r = x->color[0] & 0xFF;
            verts++;
            uncentred += x->centre[0] == x->position[0] && x->centre[1] == x->position[1] && x->centre[2] == x->position[2];
            if (a != 255 || x->uv2[0] >= 0) continue; /* skies, unbaked, lightmapped */
            baked++;
            old_sum += (float)r / 128 > 2 ? 2 : (float)r / 128;
            new_sum += formula(r, m->surface.ambient[0], m->surface.diffuse[0]);
        }
    }
    for (i = 0; i < l->perso_count; i++)
        for (k = 0; k < l->persos[i].part_count; k++) {
            const tc_surface *u = &l->persos[i].parts[k].mesh.surface;
            persos++;
            flat += formula(0, u->ambient[0], u->diffuse[0]) >= 1; /* full light whatever the lights do */
        }
    printf("  lighting: %u lights (%u spherical), at most %u in a sector; %u of %u envs fogged; %u of %u static vertices "
           "at their own position; red of the baked ones, old %.2f -> formula %.2f; perso parts flat %u of %u\n",
           l->light_count, spheres, most, fogs, envs, uncentred, verts, baked ? old_sum / baked : 0,
           baked ? new_sum / baked : 0, flat, persos);
    if (most > TC_VIEW_MAX_LIGHTS || uncentred * 100 > verts) {
        printf("  FAIL: too many lights in a sector, or vertices without their object's centre\n");
        failures++;
    }
    memset(&desc, 0, sizeof desc);
    desc.level = l;
    desc.level_name = file;
    if (tc_viewer_init(&v, &desc) != TC_OK) {
        printf("  FAIL: the viewer does not start\n");
        failures++;
        return;
    }
    tc_viewer_update(&v, 1.0f / 60);
    f = (tc_view_frame *)malloc(sizeof *f);
    tc_viewer_frame(&v, 16.0f / 9, f);
    sec = tc_viewer_ed_sector(&v);
    if (sec != TC_SECTOR_NONE) {
        uint32_t want = 0;
        for (k = 0; k < s->sectors[sec].light_count; k++) {
            const tc_light *L = &l->lights[s->sectors[sec].lights[k]];
            want += L->on && L->alpha_mode != 1 && (L->type == TC_LIGHT_SPHERICAL || L->type == TC_LIGHT_PARALLEL);
        }
        printf("  first frame in %s: %u lights (%u wanted), ambient %.2f, fog %.0f..%.0f x %.2f, sky %.2f\n",
               tc_sectors_name(s, sec), f->light_count, want, f->ambient_light[0], f->fog_near, f->fog_far,
               f->fog_strength, f->sky_light[0]);
        if (f->light_count != (want < TC_VIEW_MAX_LIGHTS ? want : TC_VIEW_MAX_LIGHTS) || f->lighting != TC_VL_GAME) {
            printf("  FAIL: the first frame's lights are not Ed's sector's\n");
            failures++;
        }
    }
    free(f);
    tc_viewer_free(&v);
}

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
    lighting_check(file, &l);
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
