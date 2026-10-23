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

/* ---- loading ---- */

/* Every mesh of the viewer's table concatenated into one vertex and one index array. */
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

/* Empty meshes (and ones that don't fit in memory) stay at index_count 0. */
static void add_mesh(mesh_builder *b, const tc_mesh *m, view_mesh *out) {
    SDL_zerop(out);
    if (m->index_count == 0 || m->vertex_count == 0 || !m->vertices || !m->indices) return;
    if (!grow((void **)&b->vertices, &b->vertex_cap, b->vertex_count + m->vertex_count, sizeof(tc_vertex)) ||
        !grow((void **)&b->indices, &b->index_cap, b->index_count + m->index_count, sizeof(Uint32)))
        return;
    SDL_memcpy(b->vertices + b->vertex_count, m->vertices, m->vertex_count * sizeof(tc_vertex));
    SDL_memcpy(b->indices + b->index_count, m->indices, m->index_count * sizeof(Uint32));
    out->first_index = b->index_count;
    out->index_count = m->index_count;
    out->vertex_offset = (Sint32)b->vertex_count;
    b->vertex_count += m->vertex_count;
    b->index_count += m->index_count;
}

bool view_load(view *v, gpu *g, const char *ttlv_path, char *err, size_t err_size) {
    SDL_zerop(v);
    v->g = g;
    if (!tv_level_load(&v->level, ttlv_path, err, err_size)) return false;
    const tc_level *r = &v->level.raw;

    /* Ed's AI from the .ttai next to the .ttlv (none is fine: the simple controller then). */
    char *ai_path = path_with_ext(ttlv_path, ".ttai");
    size_t ai_size = 0;
    void *ai = ai_path ? SDL_LoadFile(ai_path, &ai_size) : NULL;
    tc_viewer_desc desc = {r, ai, ai_size, v->level.name};
    tc_result vr = tc_viewer_init(&v->viewer, &desc);
    if (ai && v->viewer.ai_result != TC_OK) SDL_Log("%s: %s", ai_path, tc_result_string(v->viewer.ai_result));
    SDL_free(ai);
    SDL_free(ai_path);
    if (vr != TC_OK) {
        SDL_snprintf(err, err_size, "%s", tc_result_string(vr));
        view_free(v);
        return false;
    }
    v->has_viewer = true;

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
    for (uint32_t i = 0; v->textures && i < r->texture_count; i++) {
        char *path = path_join(v->level.folder, r->textures[i]);
        v->textures[i] = gpu_upload_png(g, &up, path, true, NULL, NULL);
        if (!v->textures[i] && ++v->missing_textures <= 5) SDL_Log("texture %s: %s", path, SDL_GetError());
        SDL_free(path);
    }
    if (v->missing_textures > 5) SDL_Log("... %u textures missing in all", v->missing_textures);

    mesh_builder b;
    SDL_zero(b);
    uint32_t mesh_count = tc_viewer_mesh_count(&v->viewer);
    v->meshes = SDL_calloc(SDL_max(1u, mesh_count), sizeof *v->meshes);
    for (uint32_t i = 0; v->meshes && i < mesh_count; i++) add_mesh(&b, tc_viewer_mesh(&v->viewer, i), &v->meshes[i]);

    v->vertices = gpu_upload_buffer(g, &up, SDL_GPU_BUFFERUSAGE_VERTEX, b.vertices, b.vertex_count * sizeof(tc_vertex));
    v->indices = gpu_upload_buffer(g, &up, SDL_GPU_BUFFERUSAGE_INDEX, b.indices, b.index_count * sizeof(Uint32));
    bool uploaded = gpu_upload_end(g, &up);
    SDL_free(b.vertices);
    SDL_free(b.indices);
    if (!uploaded || !v->meshes || (b.index_count > 0 && (!v->vertices || !v->indices))) {
        SDL_snprintf(err, err_size, "GPU upload: %s", SDL_GetError());
        view_free(v);
        return false;
    }
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
    if (v->has_viewer) tc_viewer_free(&v->viewer);
    tv_level_free(&v->level);
    SDL_zerop(v);
}

/* ---- drawing ---- */

typedef struct draw_ctx {
    view *v;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPURenderPass *pass;
    SDL_GPUGraphicsPipeline *bound;
} draw_ctx;

static void bind(draw_ctx *c, SDL_GPUGraphicsPipeline *p) {
    if (c->bound == p) return;
    SDL_BindGPUGraphicsPipeline(c->pass, p);
    c->bound = p;
}

static void draw_mesh(draw_ctx *c, const tc_view_draw *d) {
    view *v = c->v;
    gpu *g = v->g;
    const view_mesh *m = &v->meshes[d->mesh];
    if (m->index_count == 0) return;
    const tc_surface *s = &tc_viewer_mesh(&v->viewer, d->mesh)->surface;
    bool transparent = (d->flags & TC_FLAG_TRANSPARENT) != 0;
    SDL_GPUTexture *tex = d->texture >= 0 && v->textures ? v->textures[d->texture] : NULL;
    if (tex) {
        bind(c, transparent ? g->textured_blended : g->textured);
        SDL_GPUTextureSamplerBinding samplers[2] = {{tex, g->repeat_sampler}, {v->lightmap, g->clamp_sampler}};
        SDL_BindGPUFragmentSamplers(c->pass, 0, samplers, 2);
        gpu_flags f = {d->flags, !transparent && g->msaa > 1, {0, 0}};  /* coverage: as the textured pipeline */
        SDL_PushGPUFragmentUniformData(c->cmd, 1, &f, sizeof f);
    } else {
        bind(c, transparent ? g->flat_no_depth_write : g->flat);
        SDL_GPUTextureSamplerBinding lightmap = {v->lightmap, g->clamp_sampler};
        SDL_BindGPUFragmentSamplers(c->pass, 0, &lightmap, 1);
    }
    gpu_draw_params params;
    SDL_memcpy(params.model, d->model.m, sizeof params.model);
    params.scroll[0] = s->scroll[0];
    params.scroll[1] = s->scroll[1];
    params.billboard = (d->flags & TC_FLAG_BILLBOARD) ? 1 : 0;
    params.pad = 0;
    SDL_PushGPUVertexUniformData(c->cmd, 1, &params, sizeof params);
    SDL_DrawGPUIndexedPrimitives(c->pass, m->index_count, 1, m->first_index, m->vertex_offset, 0);
}

static void vec4(float out[4], tc_vec3 v, float w) { out[0] = v.x; out[1] = v.y; out[2] = v.z; out[3] = w; }

void view_draw_frame(view *v, gpu_frame *f) {
    tc_view_frame fr;
    tc_viewer_frame(&v->viewer, (float)f->width / (float)SDL_max(f->height, 1u), &fr);
    SDL_FColor clear = {fr.clear_color[0], fr.clear_color[1], fr.clear_color[2], 1};
    SDL_GPURenderPass *pass = gpu_pass_3d(v->g, f, clear);
    if (!pass) return;
    if (!v->vertices || !v->indices) { SDL_EndGPURenderPass(pass); return; }

    gpu_uniforms u;
    SDL_zero(u);
    SDL_memcpy(u.view_projection, fr.view_projection.m, sizeof u.view_projection);
    u.light_direction[0] = fr.light_direction.x;
    u.light_direction[1] = fr.light_direction.y;
    u.light_direction[2] = fr.light_direction.z;
    u.ambient = fr.ambient;
    u.time = fr.time;
    u.baked_scale = fr.baked_scale;
    u.light_set = fr.light_set;
    u.gamma = fr.gamma;
    u.fog[0] = fr.fog_near;
    u.fog[1] = fr.fog_far;
    u.fog[2] = fr.fog_on ? 1 : 0;
    vec4(u.fog_color, tc_v3(fr.fog_color[0], fr.fog_color[1], fr.fog_color[2]), 0);
    vec4(u.eye, fr.eye, 0);
    vec4(u.cam_right, fr.cam_right, 0);
    vec4(u.cam_up, fr.cam_up, 0);
    SDL_PushGPUVertexUniformData(f->cmd, 0, &u, sizeof u);
    SDL_PushGPUFragmentUniformData(f->cmd, 0, &u, sizeof u);

    SDL_GPUBufferBinding vb = {v->vertices, 0}, ib = {v->indices, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    /* Opaque draws, then the transparent ones (their pipelines don't write depth). */
    draw_ctx c = {v, f->cmd, pass, NULL};
    uint32_t count, opaque;
    const tc_view_draw *draws = tc_viewer_draws(&v->viewer, &count, &opaque);
    for (uint32_t i = 0; i < count; i++) draw_mesh(&c, &draws[i]);
    SDL_EndGPURenderPass(pass);
}
