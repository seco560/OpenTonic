/* The viewer's side of the particles (tc_particles.h; docs/ENGINE.md "Particles"): who has a source each game tick (the
 * camera, the main character, the cast), and this frame's triangles. */
#include "tc_viewer_internal.h"
#include <math.h>

/* MEC_EnvLookup for the environment test: in water when the sector's environment there is a water one. */
static bool particle_water(void *user, uint16_t sector, tc_vec3 p, float *depth) {
    const tc_sectors *s = (const tc_sectors *)user;
    tc_sector_env env;
    float d = s ? tc_sectors_env(s, sector, p, &env) : 0;
    *depth = d;
    return s && env.water == 1;
}

/* A perso's axes (tc_ai_perso: its drawn ones, else its heading's: local -Y along (sin, -cos)) and place. */
static tc_mat34 perso_matrix(const tc_ai_perso *p) {
    tc_mat34 m;
    tc_vec3 x, y, z;
    if (p->has_axes) {
        x = p->axes[0], y = p->axes[1], z = p->axes[2];
    } else {
        float s = sinf(p->heading), c = cosf(p->heading);
        x = tc_v3(c, s, 0), y = tc_v3(-s, c, 0), z = tc_v3(0, 0, 1);
    }
    m.m[0] = x.x, m.m[1] = y.x, m.m[2] = z.x, m.m[3] = p->position.x;
    m.m[4] = x.y, m.m[5] = y.y, m.m[6] = z.y, m.m[7] = p->position.y;
    m.m[8] = x.z, m.m[9] = y.z, m.m[10] = z.z, m.m[11] = p->position.z;
    return m;
}

/* The camera perso's matrix (docs/ENGINE.md: front = -Y, the pan axis -X): the play camera's eye and axes. */
static tc_mat34 camera_matrix(const tc_camera *c) {
    tc_mat34 m;
    tc_vec3 f = tc_camera_forward(c), r = tc_camera_right(c), u = tc_v3_cross(r, f);
    m.m[0] = -r.x, m.m[1] = -f.x, m.m[2] = u.x, m.m[3] = c->position.x;
    m.m[4] = -r.y, m.m[5] = -f.y, m.m[6] = u.y, m.m[7] = c->position.y;
    m.m[8] = -r.z, m.m[9] = -f.z, m.m[10] = u.z, m.m[11] = c->position.z;
    return m;
}

static const tc_ai_globals *game_globals(const tc_game *g) {
    if (g->has_cast) return &g->cast.globals;
    if (g->main.has_driver) return g->main.driver.ai.globals ? g->main.driver.ai.globals : &g->main.driver.ai.own_globals;
    return NULL;
}

static void add(tc_particle_emitter *e, uint32_t *n, const tc_ai_perso *p, uint16_t sector, bool awake) {
    if (!p || p->particle_source <= 0 || *n >= TC_PARTICLE_MAX_EMITTERS) return;
    e[*n].source = p->particle_source;
    e[*n].on = p->particle_on && awake;
    e[*n].serial = p->particle_serial;
    e[*n].matrix = perso_matrix(p);
    e[*n].sector = sector;
    (*n)++;
}

/* This tick's emitters, in the exe's order as near as the port has it: the camera (always awake), the main character,
 * then the cast's walk. */
static uint32_t emitters(const tc_viewer *v, tc_particle_emitter *e) {
    const tc_game *g = v->game;
    const tc_ai_globals *G = game_globals(g);
    uint32_t n = 0, i;
    if (G && G->camera_particle_source > 0) {
        e[n].source = G->camera_particle_source;
        e[n].on = G->camera_particle_on;
        e[n].serial = G->camera_particle_serial;
        e[n].matrix = camera_matrix(&v->camera);
        e[n].sector = TC_SECTOR_NONE;
        n++;
    }
    /* the camera as the cast holds it: a ghost (it does not run here), the fix's perso 1 */
    for (i = 0; g->has_cast && i < g->cast.ghost_count && n < TC_PARTICLE_MAX_EMITTERS; i++) {
        const tc_cast_ghost_perso *gh = &g->cast.ghosts[i];
        if (gh->fix_role != 1 || gh->ai.particle_source <= 0) continue;
        e[n].source = gh->ai.particle_source;
        e[n].on = gh->ai.particle_on;
        e[n].serial = gh->ai.particle_serial;
        e[n].matrix = camera_matrix(&v->camera);
        e[n].sector = TC_SECTOR_NONE;
        n++;
    }
    if (g->main.has_driver && !tc_main_riding(&g->main)) add(e, &n, &g->main.driver.ai, tc_main_sector(&g->main), true);
    for (i = 0; g->has_cast && i < g->cast.count; i++) {
        const tc_cast_actor *a = &g->cast.actors[i];
        if (a->ai == &g->main.driver.ai) continue;
        add(e, &n, a->ai, a->sector.sector, a->awake);
    }
    return n;
}

void tcv_update_particles(tc_viewer *v) {
    tc_game *g = v->game;
    tc_particle_emitter e[TC_PARTICLE_MAX_EMITTERS];
    uint32_t ticks;
    if (!g || !v->level->particle_source_count) return;
    if (!v->has_particles) {
        if (!tc_particles_init(&v->particles, v->level, v->rng ^ 0x9e3779b9u)) return;
        v->particles.water = particle_water;
        v->particles.water_user = (void *)v->sectors;
        v->has_particles = true;
        v->particle_tick = g->tick_serial;
    }
    ticks = g->tick_serial - v->particle_tick;
    v->particle_tick = g->tick_serial;
    if (ticks > 8) ticks = 8;
    while (ticks--) {
        uint32_t n = emitters(v, e);
        tc_particles_tick(&v->particles, e, n, TC_ED_AI_TICK);
    }
}

void tcv_free_particles(tc_viewer *v) {
    if (v->has_particles) tc_particles_free(&v->particles);
    v->has_particles = false;
    tc_free(v->particle_vertices);
    v->particle_vertices = NULL;
    v->particle_capacity = 0;
}

uint32_t tc_viewer_particles(tc_viewer *v, const tc_particle_vertex **vertices, uint32_t *vertex_count,
                             const tc_particle_batch **batches) {
    const tc_particles *ps = &v->particles;
    bool hidden[TC_PARTICLE_MAX_EMITTERS];
    uint32_t alive, i, nb;
    const tc_camera *c = &v->camera;
    tc_vec3 right = tc_camera_right(c), up = tc_v3_cross(right, tc_camera_forward(c));
    float t, pixel;
    *vertices = NULL, *vertex_count = 0, *batches = NULL;
    if (!v->has_particles || !v->game || !v->playing) return 0;
    alive = tc_particles_alive(ps);
    if (!alive) return 0;
    if (alive * 6 > v->particle_capacity) {
        uint32_t want = alive * 6 + 1200;
        tc_particle_vertex *nv = (tc_particle_vertex *)tc_alloc(sizeof(tc_particle_vertex) * want);
        if (!nv) return 0;
        tc_free(v->particle_vertices);
        v->particle_vertices = nv;
        v->particle_capacity = want;
    }
    /* GAM_0042e680: a source with the ceiling test is not drawn under a ceiling nearer than its distance over its
     * perso (a ray straight up through the static world, DAT_008c1c80): Totalski's tunnels, Land's caves */
    for (i = 0; i < ps->emitter_count; i++) {
        const tc_particle_emitter *e = &ps->emitters[i];
        const tc_particle_source *s = e->source > 0 && (uint32_t)e->source <= v->level->particle_source_count
                                          ? &v->level->particle_sources[e->source - 1] : NULL;
        tc_hit hit;
        tc_vec3 o = tc_v3(e->matrix.m[3], e->matrix.m[7], e->matrix.m[11]);
        hidden[i] = s && s->ceiling && tc_collision_raycast(&v->game->world, o, tc_v3(0, 0, 1), s->ceiling_distance, &hit);
    }
    t = v->game->tick_time / TC_ED_AI_TICK;
    /* a line is one pixel wide at 600 lines (the original drew one-pixel lines at its own resolution) */
    pixel = 2 * tanf(c->vertical_fov * 0.5f) / 600.0f;
    nb = tc_particles_build(ps, t, c->position, right, up, pixel, hidden, v->particle_vertices, v->particle_capacity,
                            vertex_count, v->particle_batches);
    *vertices = v->particle_vertices;
    *batches = v->particle_batches;
    return nb;
}
