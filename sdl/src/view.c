#include "view.h"
#include <stddef.h>
#include "util.h"

/* Must match the Uniforms block in shaders/level_common.glsl (std140). */
typedef struct gpu_uniforms {
    float view_projection[16];
    float light_direction[3];
    float ambient;
    float time;
    float baked_scale;   /* vertex colour (0..1) -> light factor: 255/128, see level.RADIOSITY_ONE */
    uint32_t light_set;  /* baked set shown (number keys); falls back to set 0 per vertex */
    float gamma;         /* display gamma (- / = keys), 1 = unchanged */
    float fog[4];        /* near, far, enabled, unused */
    float fog_color[4];
    float eye[4];
    float cam_right[4];  /* camera axes, for billboards */
    float cam_up[4];
} gpu_uniforms;
_Static_assert(offsetof(gpu_uniforms, light_direction) == 64, "std140: vec3 at 64");
_Static_assert(offsetof(gpu_uniforms, ambient) == 76, "std140: float packs after the vec3");
_Static_assert(offsetof(gpu_uniforms, fog) == 96, "std140: vec4 at 96");
_Static_assert(sizeof(gpu_uniforms) == 176, "gpu_uniforms must match shaders/level_common.glsl");

/* Must match the DrawParams block in shaders/level.vert (std140, padded to 16). */
typedef struct gpu_draw_params {
    float model[16];
    float scroll[2];
    uint32_t billboard;  /* TC_FLAG_BILLBOARD sprite quads */
    uint32_t pad;
} gpu_draw_params;
_Static_assert(sizeof(gpu_draw_params) == 80, "gpu_draw_params must match shaders/level.vert");

/* The Flags block in shaders/level.frag. */
typedef struct gpu_flags { uint32_t flags, coverage, pad[2]; } gpu_flags;

static const SDL_FColor background = {0.08f, 0.09f, 0.14f, 1};

/* ---- loading ---- */

/* Every mesh of the level (static, perso parts, Ed's parts) concatenated into one vertex and one index array. */
typedef struct mesh_builder {
    tc_vertex *vertices;
    Uint32 vertex_count, vertex_cap;
    Uint32 *indices;
    Uint32 index_count, index_cap;
} mesh_builder;

static bool grow(void **p, Uint32 *cap, Uint32 need, size_t elem) {
    if (need <= *cap) return true;
    Uint32 c = *cap ? *cap : 4096;
    while (c < need) c *= 2;
    void *n = SDL_realloc(*p, (size_t)c * elem);
    if (!n) return false;
    *p = n;
    *cap = c;
    return true;
}

static bool add_mesh(mesh_builder *b, const tc_mesh *m, view_mesh *out) {
    if (m->index_count == 0 || m->vertex_count == 0 || !m->vertices || !m->indices) return false;
    if (!grow((void **)&b->vertices, &b->vertex_cap, b->vertex_count + m->vertex_count, sizeof(tc_vertex)) ||
        !grow((void **)&b->indices, &b->index_cap, b->index_count + m->index_count, sizeof(Uint32)))
        return false;
    SDL_memcpy(b->vertices + b->vertex_count, m->vertices, m->vertex_count * sizeof(tc_vertex));
    SDL_memcpy(b->indices + b->index_count, m->indices, m->index_count * sizeof(Uint32));
    out->src = m;
    out->first_index = b->index_count;
    out->index_count = m->index_count;
    out->vertex_offset = (Sint32)b->vertex_count;
    out->loop_seconds = 0;
    for (uint32_t i = 0; i < m->surface.frame_count; i++) out->loop_seconds += m->surface.frames[i].seconds;
    b->vertex_count += m->vertex_count;
    b->index_count += m->index_count;
    return true;
}

static void reset_state(view *v) {
    v->gamma = 1;
    v->fog_enabled = true;
    v->animation_speed = 1;
    v->light_set_count = 1;
    v->ed_static_perso = -1;
    v->orbit_distance = 4.5f;
    camera_init(&v->cam);
    input_init(&v->in);
}

bool view_load(view *v, gpu *g, const char *ttlv_path, char *err, size_t err_size) {
    SDL_zerop(v);
    v->g = g;
    reset_state(v);
    if (!tv_level_load(&v->level, ttlv_path, err, err_size)) return false;
    const tc_level *r = &v->level.raw;
    v->has_fog = r->fog_far > r->fog_near;
    if (v->has_fog) {
        v->fog_near = r->fog_near;
        v->fog_far = r->fog_far;
        SDL_memcpy(v->fog_color, r->fog_rgb, sizeof v->fog_color);
    }

    gpu_upload up;
    if (!gpu_upload_begin(g, &up)) {
        SDL_snprintf(err, err_size, "GPU upload: %s", SDL_GetError());
        view_free(v);
        return false;
    }
    if (r->lightmap_atlas) {
        char *path = path_join(v->level.folder, r->lightmap_atlas);
        v->lightmap = gpu_upload_png(g, &up, path, false, NULL, NULL);
        if (!v->lightmap) SDL_Log("lightmap %s: %s", path, SDL_GetError());
        SDL_free(path);
    }
    /* Shaders always bind the lightmap slot; only lightmapped vertices sample it. */
    if (!v->lightmap) v->lightmap = g->white;

    v->texture_count = r->texture_count;
    v->textures = SDL_calloc(SDL_max(1u, r->texture_count), sizeof *v->textures);
    uint32_t missing = 0;
    for (uint32_t i = 0; v->textures && i < r->texture_count; i++) {
        char *path = path_join(v->level.folder, r->textures[i]);
        v->textures[i] = gpu_upload_png(g, &up, path, true, NULL, NULL);
        if (!v->textures[i] && ++missing <= 5) SDL_Log("texture %s: %s", path, SDL_GetError());
        SDL_free(path);
    }
    if (missing > 5) SDL_Log("... %u textures missing in all", missing);

    mesh_builder b;
    SDL_zero(b);
    v->meshes = SDL_calloc(SDL_max(1u, r->mesh_count), sizeof *v->meshes);
    for (uint32_t i = 0; v->meshes && i < r->mesh_count; i++)
        if (add_mesh(&b, &r->meshes[i], &v->meshes[v->mesh_count])) v->mesh_count++;

    uint32_t part_total = 0;
    for (uint32_t i = 0; i < r->perso_count; i++) part_total += r->persos[i].part_count;
    v->persos = SDL_calloc(SDL_max(1u, r->perso_count), sizeof *v->persos);
    v->part_storage = SDL_calloc(SDL_max(1u, part_total), sizeof *v->part_storage);
    uint32_t used_parts = 0;
    for (uint32_t i = 0; v->persos && v->part_storage && i < r->perso_count; i++) {
        const tc_perso *p = &r->persos[i];
        view_perso *vp = &v->persos[v->perso_count];
        vp->parts = v->part_storage + used_parts;
        vp->part_count = 0;
        for (uint32_t k = 0; k < p->part_count; k++) {
            view_part *part = &vp->parts[vp->part_count];
            if (!add_mesh(&b, &p->parts[k].mesh, &part->mesh)) continue;
            part->channel = p->parts[k].channel;
            part->object = p->parts[k].object;
            vp->part_count++;
        }
        if (vp->part_count == 0 || p->sample_count == 0) continue;
        vp->world = tc_mat4_from34(&p->world);
        vp->src = p;
        vp->source_index = i;
        used_parts += vp->part_count;
        v->perso_count++;
    }

    if (tv_level_has_ed(&v->level)) {
        v->ed_parts = SDL_calloc(SDL_max(1u, r->ed.part_count), sizeof *v->ed_parts);
        for (uint32_t i = 0; v->ed_parts && i < r->ed.part_count; i++) {
            view_ed_part *e = &v->ed_parts[v->ed_part_count];
            if (!add_mesh(&b, &r->ed.parts[i].mesh, &e->mesh)) continue;
            e->object = r->ed.parts[i].object;
            v->ed_part_count++;
        }
        if (tv_level_ed_anim(&v->level, "idle") >= 0) v->player = play_create(&v->level);
        if (v->player) {
            for (uint32_t i = 0; i < r->ed.anim_count; i++) {
                const tc_ed_anim *a = &r->ed.anims[i];
                if (a->sample_count > 0 && a->name && a->loop == 0)
                    play_set_clip_length(v->player, a->name, (float)a->sample_count / fmaxf(a->sample_rate, 1e-3f));
            }
        }
        v->ed_static_perso = r->ed.static_perso;
    }

    v->vertices = gpu_upload_buffer(g, &up, SDL_GPU_BUFFERUSAGE_VERTEX, b.vertices, b.vertex_count * sizeof(tc_vertex));
    v->indices = gpu_upload_buffer(g, &up, SDL_GPU_BUFFERUSAGE_INDEX, b.indices, b.index_count * sizeof(Uint32));
    bool uploaded = gpu_upload_end(g, &up);
    SDL_free(b.vertices);
    SDL_free(b.indices);
    if (!uploaded || (b.index_count > 0 && (!v->vertices || !v->indices))) {
        SDL_snprintf(err, err_size, "GPU upload: %s", SDL_GetError());
        view_free(v);
        return false;
    }

    view_stats *s = &v->stats;
    s->meshes = v->mesh_count;
    s->persos = v->perso_count;
    for (uint32_t i = 0; i < v->mesh_count; i++) s->triangles += v->meshes[i].index_count / 3;
    for (uint32_t i = 0; i < v->perso_count; i++) {
        s->parts += v->persos[i].part_count;
        for (uint32_t k = 0; k < v->persos[i].part_count; k++) s->triangles += v->persos[i].parts[k].mesh.index_count / 3;
    }
    s->textures = r->texture_count - missing;
    s->missing_textures = missing;
    v->light_set_count = SDL_max(1u, r->light_set_count);

    tc_vec3 lo, hi;
    tc_level_bounds(r, &lo, &hi);
    float size = tc_v3_length(tc_v3_sub(hi, lo));
    /* Start in the middle: levels are rooms or sit inside a sky dome, so outside the bounds sees nothing. */
    v->cam.position = tc_v3_scale(tc_v3_add(lo, hi), 0.5f);
    v->cam.yaw = 0;
    v->cam.pitch = -0.2f;
    v->cam.far_z = fmaxf(2000, size * 3);
    v->in.base_speed = fmaxf(2, size / 40);
    /* Start as Ed when the level has him (Tab still switches to free-fly). */
    if (v->player) view_toggle_play(v);
    return true;
}

void view_free(view *v) {
    if (v->g) {
        SDL_GPUDevice *d = v->g->device;
        for (uint32_t i = 0; v->textures && i < v->texture_count; i++)
            if (v->textures[i]) SDL_ReleaseGPUTexture(d, v->textures[i]);
        if (v->lightmap && v->lightmap != v->g->white) SDL_ReleaseGPUTexture(d, v->lightmap);
        if (v->vertices) SDL_ReleaseGPUBuffer(d, v->vertices);
        if (v->indices) SDL_ReleaseGPUBuffer(d, v->indices);
    }
    SDL_free(v->textures);
    SDL_free(v->meshes);
    SDL_free(v->persos);
    SDL_free(v->part_storage);
    SDL_free(v->ed_parts);
    SDL_free(v->blended);
    play_destroy(v->player);
    tv_level_free(&v->level);
    SDL_zerop(v);
}

/* ---- play mode ---- */

bool view_has_ai(const view *v) { return v->player && v->player->has_driver; }

void view_toggle_ai(view *v) {
    if (!view_has_ai(v)) return;
    v->player->use_ai = !v->player->use_ai;
    v->ed_clip[0] = 0;
    v->ed_phase = 0;
}

void view_toggle_play(view *v) {
    play_session *p = v->player;
    if (!p) return;
    v->playing = !v->playing;
    if (!v->playing) {
        v->has_exit_camera = true;
        v->exit_camera = v->cam.position;
        return;
    }
    if (v->has_exit_camera && tc_v3_distance(v->exit_camera, v->cam.position) > 1) {
        tc_vec3 o = v->cam.position;
        float d = play_raycast(p, o, tc_v3(0, 0, -1), 2000);
        tc_vec3 ground = d >= 0 ? tc_v3_sub(o, tc_v3(0, 0, d)) : o;
        play_teleport(p, tc_v3_add(ground, tc_v3(0, 0, 0.3f)), TC_PI - v->cam.yaw);
    }
    v->cam.yaw = TC_PI - p->player.heading;  /* camera forward (sin yaw, cos yaw) = Ed's front (sin h, -cos h) */
    v->cam.pitch = -0.3f;
}

/* Ed's clip to draw: the current one, else idle; -1 when neither exists. */
static int ed_anim(const view *v) {
    int a = tv_level_ed_anim(&v->level, v->ed_clip);
    return a >= 0 ? a : tv_level_ed_anim(&v->level, "idle");
}

static void update_play(view *v, float dt) {
    play_session *p = v->player;
    input_look(&v->in, &v->cam, dt);
    if (p->use_ai) {
        /* Ed's own scripts; they see last frame's camera eye (it makes the direction keys camera-relative). */
        play_step_ai(p, v->paused ? 0 : dt, input_ai_inputs(&v->in), v->cam.position);
    } else {
        play_step(p, dt, input_move_direction(&v->in, v->cam.yaw), input_held(&v->in, ACT_FAST),
                  input_held(&v->in, ACT_JUMP));
    }
    /* Third-person orbit around Ed's chest; pulled in when the level is in the way. */
    tc_vec3 target = tc_v3_add(p->player.position, tc_v3(0, 0, 1.1f));
    tc_vec3 back = tc_v3_scale(camera_forward(&v->cam), -1);
    float d = v->orbit_distance;
    float hit = play_raycast(p, target, back, d + 0.3f);
    if (hit >= 0) d = fmaxf(0.4f, hit - 0.3f);
    v->cam.position = tc_v3_add(target, tc_v3_scale(back, d));

    char name[32];
    float frame;
    bool loops;
    if (p->use_ai && play_ai_animation(p, name, sizeof name, &frame, &loops)) {
        /* The AI's state clock picks the frame (one sample per frame). */
        SDL_strlcpy(v->ed_clip, tv_level_ed_anim(&v->level, name) >= 0 ? name : "idle", sizeof v->ed_clip);
        v->ed_phase = frame;
        v->ed_loop = loops;
        return;
    }
    /* Clip from the movement state; walk/run play at the rate that matches Ed's ground speed. */
    const char *state = tc_anim_state_name(p->player.animation);
    if (SDL_strcmp(state, v->ed_clip) != 0) {
        SDL_strlcpy(v->ed_clip, state, sizeof v->ed_clip);
        v->ed_phase = 0;
    }
    int i = ed_anim(v);
    v->ed_loop = i < 0 || v->level.raw.ed.anims[i].loop != 0;
    if (i < 0) return;
    const tc_ed_anim *a = &v->level.raw.ed.anims[i];
    float rate = a->ground_speed > 0 ? a->sample_rate * tc_player_horizontal_speed(&p->player) / a->ground_speed
                                     : a->sample_rate;
    v->ed_phase = tc_phase_advance(v->ed_phase, dt * (rate / 4) * (v->paused ? 0 : v->animation_speed),
                                   a->sample_count, a->loop != 0);
}

void view_update(view *v, float dt) {
    if (v->playing) update_play(v, dt);
    else input_apply(&v->in, &v->cam, dt);
    if (!v->paused) v->animation_time += dt * v->animation_speed;
}

/* ---- drawing ---- */

typedef struct draw_ctx {
    view *v;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPURenderPass *pass;
    SDL_GPUGraphicsPipeline *bound;
    bool blend_pass;
} draw_ctx;

static SDL_GPUTexture *texture_index(const view *v, int32_t i) {
    return v->textures && i >= 0 && (uint32_t)i < v->texture_count ? v->textures[i] : NULL;
}

/* The texture shown at time t (animated textures loop through their frames). */
static SDL_GPUTexture *texture_at(const view *v, const view_mesh *m, float t) {
    const tc_surface *s = &m->src->surface;
    if (m->loop_seconds <= 0) return texture_index(v, s->texture);
    float x = fmodf(t, m->loop_seconds);
    for (uint32_t i = 0; i < s->frame_count; i++) {
        if (x < s->frames[i].seconds) return texture_index(v, s->frames[i].texture);
        x -= s->frames[i].seconds;
    }
    return texture_index(v, s->frames[s->frame_count - 1].texture);
}

static void bind(draw_ctx *c, SDL_GPUGraphicsPipeline *p) {
    if (c->bound == p) return;
    SDL_BindGPUGraphicsPipeline(c->pass, p);
    c->bound = p;
}

static void draw_mesh(draw_ctx *c, const view_mesh *m, const tc_mat4 *model) {
    view *v = c->v;
    gpu *g = v->g;
    uint32_t flags = m->src->surface.flags;
    bool transparent = (flags & TC_FLAG_TRANSPARENT) != 0;
    if (transparent && !c->blend_pass) {
        if (v->blended_count == v->blended_cap &&
            !grow((void **)&v->blended, &v->blended_cap, v->blended_count + 1, sizeof *v->blended))
            return;
        v->blended[v->blended_count].mesh = m;
        v->blended[v->blended_count].model = *model;
        v->blended_count++;
        return;
    }
    SDL_GPUTexture *tex = texture_at(v, m, v->animation_time);
    if (tex) {
        bind(c, transparent ? g->textured_blended : g->textured);
        SDL_GPUTextureSamplerBinding samplers[2] = {{tex, g->repeat_sampler}, {v->lightmap, g->clamp_sampler}};
        SDL_BindGPUFragmentSamplers(c->pass, 0, samplers, 2);
        gpu_flags f = {flags, !transparent && g->msaa > 1, {0, 0}};  /* coverage: as the textured pipeline */
        SDL_PushGPUFragmentUniformData(c->cmd, 1, &f, sizeof f);
    } else {
        bind(c, transparent ? g->flat_no_depth_write : g->flat);
        SDL_GPUTextureSamplerBinding lightmap = {v->lightmap, g->clamp_sampler};
        SDL_BindGPUFragmentSamplers(c->pass, 0, &lightmap, 1);
    }
    gpu_draw_params params;
    SDL_memcpy(params.model, model->m, sizeof params.model);
    params.scroll[0] = m->src->surface.scroll[0];
    params.scroll[1] = m->src->surface.scroll[1];
    params.billboard = (flags & TC_FLAG_BILLBOARD) ? 1 : 0;
    params.pad = 0;
    SDL_PushGPUVertexUniformData(c->cmd, 1, &params, sizeof params);
    SDL_DrawGPUIndexedPrimitives(c->pass, m->index_count, 1, m->first_index, m->vertex_offset, 0);
}

static void vec4(float out[4], tc_vec3 v, float w) { out[0] = v.x; out[1] = v.y; out[2] = v.z; out[3] = w; }

void view_draw_frame(view *v, gpu_frame *f) {
    bool fog_on = v->fog_enabled && v->has_fog;
    /* With fog, the background is the fog colour (the horizon fades into it). */
    SDL_FColor clear = fog_on ? (SDL_FColor){v->fog_color[0], v->fog_color[1], v->fog_color[2], 1} : background;
    SDL_GPURenderPass *pass = gpu_pass_3d(v->g, f, clear);
    if (!pass) return;
    if (!v->vertices || !v->indices) { SDL_EndGPURenderPass(pass); return; }

    gpu_uniforms u;
    SDL_zero(u);
    tc_mat4 vp = camera_view_projection(&v->cam, (float)f->width / (float)SDL_max(f->height, 1u));
    SDL_memcpy(u.view_projection, vp.m, sizeof u.view_projection);
    tc_vec3 light = tc_v3_normalize(tc_v3(0.4f, -0.3f, 0.85f));
    u.light_direction[0] = light.x;
    u.light_direction[1] = light.y;
    u.light_direction[2] = light.z;
    u.ambient = 0.45f;
    u.time = v->animation_time;
    u.baked_scale = 255.0f / 128.0f;
    u.light_set = v->light_set;
    u.gamma = v->gamma;
    u.fog[0] = v->has_fog ? v->fog_near : 0;
    u.fog[1] = v->has_fog ? v->fog_far : 0;
    u.fog[2] = fog_on ? 1 : 0;
    vec4(u.fog_color, v->has_fog ? tc_v3(v->fog_color[0], v->fog_color[1], v->fog_color[2]) : tc_v3(0, 0, 0), 0);
    vec4(u.eye, v->cam.position, 0);
    tc_vec3 right = camera_right(&v->cam);
    vec4(u.cam_right, right, 0);
    vec4(u.cam_up, tc_v3_cross(right, camera_forward(&v->cam)), 0);
    SDL_PushGPUVertexUniformData(f->cmd, 0, &u, sizeof u);
    SDL_PushGPUFragmentUniformData(f->cmd, 0, &u, sizeof u);

    SDL_GPUBufferBinding vb = {v->vertices, 0}, ib = {v->indices, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    draw_ctx c = {v, f->cmd, pass, NULL, false};
    v->blended_count = 0;
    const tc_mat4 identity = tc_mat4_identity();
    for (uint32_t i = 0; i < v->mesh_count; i++) draw_mesh(&c, &v->meshes[i], &identity);

    float t = v->animation_time;
    for (uint32_t i = 0; i < v->perso_count; i++) {
        const view_perso *p = &v->persos[i];
        if (v->playing && (int)p->source_index == v->ed_static_perso) continue;
        /* Baked samples loop; blend each channel between neighbouring samples so motion is smooth at any display
         * rate and playback speed. Visibility follows the earlier sample. */
        const tc_perso *src = p->src;
        uint32_t nch = src->channel_count;
        tc_sample s = tc_sample_at_time(t, src->sample_rate, src->sample_count);
        uint32_t base = s.s0 * nch;
        for (uint32_t k = 0; k < p->part_count; k++) {
            const view_part *part = &p->parts[k];
            if (part->channel >= nch || src->shown[base + part->channel] != part->object) continue;
            /* Lerping full matrices is fine for samples this close together; skipped across a part swap. */
            tc_mat4 m = tc_channel_matrix(src->transforms, src->shown, nch, s, part->channel);
            tc_mat4 model = tc_mat4_mul(&p->world, &m);
            draw_mesh(&c, &part->mesh, &model);
        }
    }

    int ai = v->playing && v->player ? ed_anim(v) : -1;
    if (ai >= 0) {
        const tc_ed_anim *a = &v->level.raw.ed.anims[ai];
        tc_sample s = tc_sample_at_phase(v->ed_phase, a->sample_count, v->ed_loop);
        uint32_t nch = a->channel_count;
        tc_mat4 model = play_model_matrix(v->player);
        for (uint32_t ch = 0; ch < nch; ch++) {
            uint16_t obj = a->shown[s.s0 * nch + ch];
            if (obj == TC_HIDDEN) continue;
            tc_mat4 m = tc_channel_matrix(a->transforms, a->shown, nch, s, ch);
            tc_mat4 world = tc_mat4_mul(&model, &m);
            for (uint32_t k = 0; k < v->ed_part_count; k++)
                if (v->ed_parts[k].object == obj) draw_mesh(&c, &v->ed_parts[k].mesh, &world);
        }
    }

    c.blend_pass = true;
    for (uint32_t i = 0; i < v->blended_count; i++) draw_mesh(&c, v->blended[i].mesh, &v->blended[i].model);
    SDL_EndGPURenderPass(pass);
}
