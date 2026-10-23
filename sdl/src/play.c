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

bool tv_level_has_ed(const tv_level *l) { return l->raw.has_ed && l->raw.collision_triangle_count > 0; }

int tv_level_ed_anim(const tv_level *l, const char *name) {
    int i = tc_ed_find_anim(&l->raw.ed, name);
    return i >= 0 && l->raw.ed.anims[i].sample_count > 0 ? i : -1;
}

/* Ed's AI from the .ttai next to the .ttlv; false (and quiet) when there is none. */
static bool load_ai(tc_ai_model *model, const char *ttlv_path) {
    char *path = path_with_ext(ttlv_path, ".ttai");
    size_t size = 0;
    void *data = path ? SDL_LoadFile(path, &size) : NULL;
    bool ok = false;
    if (data) {
        tc_result r = tc_ai_model_load(model, data, size);
        if (r == TC_OK) ok = true;
        else SDL_Log("%s: %s", path, tc_result_string(r));
        SDL_free(data);
    }
    SDL_free(path);
    return ok;
}

play_session *play_create(const tv_level *l) {
    if (!tv_level_has_ed(l)) return NULL;
    play_session *p = SDL_calloc(1, sizeof *p);
    if (!p) return NULL;
    const tc_level *r = &l->raw;
    if (tc_collision_init(&p->world, r->collision, 3 * r->collision_triangle_count, r->collision_floors) != 0) {
        SDL_free(p);
        return NULL;
    }
    tc_mat34 start = r->ed.start;
    tc_player_init(&p->player, &p->world, tc_v3(start.m[3], start.m[7], start.m[11]), tc_heading_of(&start));
    tc_player_apply_floors(&p->player);
    SDL_strlcpy(p->level_name, l->name, sizeof p->level_name);
    p->has_model = load_ai(&p->model, l->path);
    if (p->has_model) p->has_driver = tc_ed_ai_init(&p->driver, &p->model, &p->player, p->level_name) == TC_OK;
    p->use_ai = p->has_driver;
    return p;
}

void play_destroy(play_session *p) {
    if (!p) return;
    if (p->has_driver) tc_ed_ai_free(&p->driver);
    if (p->has_model) tc_ai_model_free(&p->model);
    tc_collision_free(&p->world);
    SDL_free(p);
}

void play_step_ai(play_session *p, float dt, uint32_t inputs, tc_vec3 camera) {
    if (p->has_driver) tc_ed_ai_step(&p->driver, dt, inputs, 0, 0, camera);
}

void play_step(play_session *p, float dt, tc_vec2 move, bool run, bool jump) {
    tc_player_input in;
    in.move = move;
    in.run = run;
    in.jump = jump;
    tc_player_step(&p->player, dt, &in);
}

bool play_ai_animation(const play_session *p, char *name, size_t name_size, float *frame, bool *loops) {
    if (!p->has_driver) return false;
    SDL_snprintf(name, name_size, "s%u", (unsigned)tc_ed_ai_anim(&p->driver));
    *frame = tc_ed_ai_frame(&p->driver);
    *loops = tc_ed_ai_loops(&p->driver);
    return true;
}

static void behaviour_name(char *out, size_t n, uint16_t b, uint16_t normal_count) {
    if (b == 0xFFFF) SDL_snprintf(out, n, "-");
    else if (b < normal_count) SDL_snprintf(out, n, "B%u", (unsigned)b);
    else SDL_snprintf(out, n, "R%u", (unsigned)(b - normal_count));
}

void play_ai_status(const play_session *p, char *out, size_t out_size) {
    if (!p->has_driver) { if (out_size) out[0] = 0; return; }
    const tc_ai_perso *a = &p->driver.ai;
    char normal[16], reflex[16], next[16] = "";
    behaviour_name(normal, sizeof normal, a->normal.behaviour, a->model->normal_count);
    behaviour_name(reflex, sizeof reflex, a->reflex.behaviour, a->model->normal_count);
    if (a->state != a->wanted_state) SDL_snprintf(next, sizeof next, "->%u", (unsigned)a->state);
    SDL_snprintf(out, out_size, "%s %s state %u%s", normal, reflex, (unsigned)a->wanted_state, next);
}

tc_mat4 play_model_matrix(const play_session *p) {
    if (p->use_ai && p->has_driver) return tc_ed_ai_model_matrix(&p->driver);  /* the skier's board tilts with the slope */
    return tc_player_model_matrix(&p->player);
}

void play_set_clip_length(play_session *p, const char *name, float seconds) {
    for (int s = 0; s < TC_ANIM_COUNT; s++)
        if (SDL_strcmp(tc_anim_state_name((tc_anim_state)s), name) == 0) p->player.clip_length[s] = seconds;
}

void play_teleport(play_session *p, tc_vec3 position, float heading) { tc_player_teleport(&p->player, position, heading); }

float play_raycast(play_session *p, tc_vec3 origin, tc_vec3 dir, float max_distance) {
    tc_hit hit;
    return tc_collision_raycast(&p->world, origin, dir, max_distance, &hit) ? hit.distance : -1;
}
