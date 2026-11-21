/* Particles (tc_particles.h; docs/ENGINE.md "Particles") headless: the levels whose scripts set a particle source play
 * a while, and the sources' pools fill and stay in their fields. Totalski: the camera's snow (YBA_DeclencheNeige), Land:
 * its rain (WLand), marmite: JET_Tiboutte's spray. Also: the triangles built for drawing are finite and come in as many
 * batches as sources drawn. Usage: particlecheck [out/ttlv]. Test-only stdio. */
#include <math.h>
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

static int failures, skipped;

static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* source: the PRTC type wanted on the camera (TC_PRT_*), or 0: any perso's. */
static void level(const char *dir, const char *name, int camera_type, uint32_t want_min) {
    char path[1024], what[256];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level l;
    tc_viewer v;
    tc_viewer_desc desc;
    uint32_t i, frames = 0, peak = 0, nv = 0, nb, bad = 0, outside = 0, drawn_frames = 0;
    const tc_particle_vertex *verts;
    const tc_particle_batch *batches;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&l, data, size) != TC_OK) {
        free(data);
        printf("  (no %s: skipped)\n", name);
        skipped++;
        return;
    }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &l;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.level_name = name;
    desc.seed = 7;
    printf("%s: %u particle sources\n", name, l.particle_source_count);
    check(l.particle_source_count > 0, "the level has particle sources (.ttlv PRTC)");
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "viewer"); free(ai); tc_level_free(&l); return; }
    free(ai);
    for (frames = 0; frames < 60 * 8; frames++) {
        uint32_t alive;
        tc_viewer_update(&v, 1.0f / 60);
        tc_viewer_draws(&v, &i, &nb);
        alive = tc_particles_alive(&v.particles);
        if (alive > peak) peak = alive;
        nb = tc_viewer_particles(&v, &verts, &nv, &batches);
        if (nb) drawn_frames++;
        for (i = 0; i < nv; i++)
            for (int c = 0; c < 3; c++)
                if (!isfinite(verts[i].position[c])) bad++;
        for (i = 0; i < nb; i++)
            if (batches[i].first + batches[i].count > nv || batches[i].count % 3) bad++;
    }
    {
        const tc_game *g = v.game;
        const tc_ai_globals *G = g && g->has_cast ? &g->cast.globals : NULL;
        if (camera_type) {
            int32_t s = G ? G->camera_particle_source : 0;
            bool on = G && G->camera_particle_on;
            for (i = 0; g && g->has_cast && i < g->cast.ghost_count; i++)
                if (g->cast.ghosts[i].fix_role == 1 && g->cast.ghosts[i].ai.particle_source > 0)
                    s = g->cast.ghosts[i].ai.particle_source, on = g->cast.ghosts[i].ai.particle_on;
            snprintf(what, sizeof(what), "the camera's source set and on (source %d, on %d)", s, on);
            check(s > 0 && on && l.particle_sources[s - 1].type == camera_type, what);
        }
    }
    snprintf(what, sizeof(what), "particles alive after 8 s: %u (peak %u, want >= %u)", tc_particles_alive(&v.particles), peak,
             want_min);
    check(tc_particles_alive(&v.particles) >= want_min, what);
    /* every particle within its source's field */
    for (i = 0; i < v.particles.emitter_count; i++) {
        const tc_particle_emitter *e = &v.particles.emitters[i];
        const tc_particle_source *s;
        const tc_particle_pool *p;
        if (e->source <= 0) continue;
        s = &l.particle_sources[e->source - 1];
        p = &v.particles.pools[e->source - 1];
        for (uint32_t k = 0; k < p->count; k++) {
            tc_vec3 q = p->position[k];
            bool in = s->sphere ? tc_v3_length(tc_v3_sub(q, p->centre)) <= s->radius + 1e-3f
                                : q.x >= p->box_min.x - 1e-3f && q.x <= p->box_max.x + 1e-3f && q.y >= p->box_min.y - 1e-3f &&
                                      q.y <= p->box_max.y + 1e-3f && q.z >= p->box_min.z - 1e-3f && q.z <= p->box_max.z + 1e-3f;
            if (!in) outside++;
        }
    }
    snprintf(what, sizeof(what), "every particle inside its field (%u outside)", outside);
    check(outside == 0, what);
    nb = tc_viewer_particles(&v, &verts, &nv, &batches);
    snprintf(what, sizeof(what), "drawn in %u of %u frames, none bad (%u); last: %u batches, %u vertices (%s)", drawn_frames,
             frames, bad, nb, nv, nb && batches[0].texture >= 0 ? l.textures[batches[0].texture] : "untextured");
    check(bad == 0 && (drawn_frames > frames / 4 || want_min == 0), what);
    tc_viewer_free(&v);
    tc_level_free(&l);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    level(dir, "Totalski", TC_PRT_SPRITES, 100);
    level(dir, "Land", TC_PRT_LINES, 40);
    level(dir, "marmite", 0, 0);
    if (failures) printf("particlecheck: %d FAILED\n", failures);
    else printf("particlecheck: all passed%s\n", skipped ? " (some skipped)" : "");
    return failures ? 1 : 0;
}
