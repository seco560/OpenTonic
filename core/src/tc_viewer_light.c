/* The viewer's lighting: the baked light sets and the light persos; the game's lighting (TC_VL_GAME). */
#include "tc_viewer_internal.h"

void tcv_light_destroy(tc_viewer_light *l) {
    uint32_t i;
    if (!l) return;
    for (i = 0; i < l->count; i++) {
        tc_ai_free(&l->persos[i]);
        tc_ai_model_free(&l->models[i]);
    }
    tc_free(l->models);
    tc_free(l->persos);
    tc_free(l);
}

/* NULL when the level has none (or one set only); *oom on running out of memory. Persos whose scripts don't load
 * are left out. Each gets its own random stream from the viewer's. */
tc_viewer_light *tcv_light_create(tc_viewer *v, const tc_level *l, bool *oom) {
    tc_viewer_light *lt;
    uint32_t i;
    *oom = false;
    if (!l->light_perso_count || v->light_set_count < 2) return NULL;
    lt = (tc_viewer_light *)tc_calloc(1, sizeof(*lt));
    if (lt) {
        lt->models = (tc_ai_model *)tc_calloc(l->light_perso_count, sizeof(tc_ai_model));
        lt->persos = (tc_ai_perso *)tc_calloc(l->light_perso_count, sizeof(tc_ai_perso));
    }
    if (!lt || !lt->models || !lt->persos) { tcv_light_destroy(lt); *oom = true; return NULL; }
    for (i = 0; i < l->light_perso_count; i++) {
        tc_result e = tc_ai_model_load_scripts(&lt->models[lt->count], l->light_persos[i].scripts, l->light_persos[i].size);
        if (e == TC_OK && (e = tc_ai_init(&lt->persos[lt->count], &lt->models[lt->count])) != TC_OK)
            tc_ai_model_free(&lt->models[lt->count]);
        if (e == TC_ERR_OUT_OF_MEMORY) { tcv_light_destroy(lt); *oom = true; return NULL; }
        if (e != TC_OK) continue;
        v->rng = v->rng * 1103515245u + 12345u;
        lt->persos[lt->count].rng = v->rng;
        lt->count++;
    }
    if (!lt->count) { tcv_light_destroy(lt); return NULL; }
    return lt;
}

static uint32_t light_mod(const tc_viewer *v, int32_t n) {
    int32_t c = (int32_t)v->light_set_count;
    return (uint32_t)(((n % c) + c) % c);
}

/* SetLightSet(n, duration, absolute), 0x449ad0: refused while a change runs; duration 0 switches at once (to n if
 * absolute, else n sets on); otherwise the target is n or n sets on, and the radiosity blends set by set (next =
 * current + 1) at sets to go / duration. */
static void set_light(tc_viewer *v, const tc_ai_light_call *c) {
    uint32_t target, to_go;
    if (v->light_rate > 0) return;
    target = light_mod(v, c->absolute ? c->set : (int32_t)v->light_set + c->set);
    if (c->duration_ms <= 0) {
        v->light_set = v->light_next = v->light_target = target;
        v->light_blend = 0;
        return;
    }
    to_go = (target + v->light_set_count - v->light_set) % v->light_set_count;
    if (!to_go) return;
    v->light_target = target;
    v->light_next = (v->light_set + 1) % v->light_set_count;
    v->light_blend = 0;
    v->light_rate = (float)to_go * 1000.0f / (float)c->duration_ms;
}

/* FUN_00449860's blend: on to the next set at 1, until the target. */
static void blend_light(tc_viewer *v, float dt) {
    if (v->light_rate <= 0) return;
    v->light_blend += v->light_rate * dt;
    while (v->light_blend >= 1) {
        v->light_set = v->light_next;
        v->light_blend -= 1;
        if (v->light_set == v->light_target) {
            v->light_blend = 0;
            v->light_rate = 0;
            return;
        }
        v->light_next = (v->light_next + 1) % v->light_set_count;
    }
}

void tcv_update_light(tc_viewer *v, float dt) {
    tc_viewer_light *lt = v->light;
    if (lt && dt > 0) {
        uint64_t ms;
        uint32_t i, k;
        lt->clock += dt;
        ms = (uint64_t)(lt->clock * 1000) - lt->stepped_ms;
        if (ms > 0) {
            lt->stepped_ms += ms;
            for (i = 0; i < lt->count; i++) {
                tc_ai_perso *p = &lt->persos[i];
                tc_ai_step(p, (float)ms / 1000);
                for (k = 0; k < p->light_call_count && v->light_scripted; k++) set_light(v, &p->light_calls[k]);
            }
        }
    }
    blend_light(v, dt);
}

/* The original's vertex light for a stored baked colour channel (TC_RADIOSITY) without lights. */
static float formula_light(uint32_t byte, float ambient, float diffuse) {
    return fminf(1, fmaxf(0, (TC_RADIOSITY(byte) / 256 * diffuse + ambient) * (512.0f / 255)));
}

/* Per baked set, the mean light of the sky's vertices (TC_SKY_ALPHA) under the formula: how lit the sky is, for the
 * water's sheen (sud0's day / night). 1 without a sky. */
void tcv_measure_sky(tc_viewer *v) {
    const tc_level *l = v->level;
    double sum[3][3] = {{0}};
    uint32_t i, k, n = 0, set, ch;
    for (i = 0; i < l->mesh_count; i++) {
        const tc_mesh *m = &l->meshes[i];
        for (k = 0; k < m->vertex_count; k++) {
            uint32_t a = m->vertices[k].color[0] >> 24;
            if (a < 64 || a > 192) continue; /* not a sky's (TC_SKY_ALPHA) */
            n++;
            for (set = 0; set < 3; set++) {
                uint32_t c = m->vertices[k].color[set];
                if ((c >> 24) < 64) c = m->vertices[k].color[0]; /* the shaders' fallback to set 0 */
                for (ch = 0; ch < 3; ch++)
                    sum[set][ch] += formula_light((c >> (8 * ch)) & 0xFF, m->surface.ambient[ch], m->surface.diffuse[ch]);
            }
        }
    }
    for (set = 0; set < 3; set++)
        for (ch = 0; ch < 3; ch++) v->sky_light[set][ch] = n ? (float)(sum[set][ch] / n) : 1;
}

/* Ed's sector's lights and its fog at the camera (FUN_00451d80 pushes the lights of the sector the frame is drawn from;
 * FUN_00451e80 sets the fog of its env at the camera, FUN_00451c40), faded over TC_VIEW_LIGHT_FADE where the original
 * switches at once (the user's choice, 2026-10-30). */
/* The camera's sector (TC_VIEW_LIGHT_FADE's note): from Ed's head to the camera through the portals of his. */
static uint16_t camera_sector(const tc_viewer *v) {
    uint16_t s = tc_viewer_ed_sector(v);
    if (s == TC_SECTOR_NONE || !v->play) return s;
    return tc_sectors_cross(v->sectors, s, tc_v3_add(v->play->main.player.position, tc_v3(0, 0, 1.1f)), v->camera.position);
}

void tcv_view_light(tc_view_light *o, const tc_light *L, float w) {
    uint32_t k;
    o->position[0] = L->position.x, o->position[1] = L->position.y, o->position[2] = L->position.z;
    o->position[3] = (float)L->type;
    o->direction[0] = L->direction.x, o->direction[1] = L->direction.y, o->direction[2] = L->direction.z;
    o->direction[3] = L->near_range;
    for (k = 0; k < 3; k++) o->color[k] = L->color[k] * w;
    o->color[3] = L->far_range;
}

/* Sector s's characters' lights into the frame's share, while there is room (the frame's own lights come first). */
static void gather_char_lights(tc_viewer *v, uint16_t s) {
    const tc_sector *sec = &v->sectors->sectors[s];
    struct tc_viewer_char_lit *lit = &v->sector_lit[s];
    uint32_t k, c;
    if (lit->count || lit->ambient[0] || lit->ambient[1] || lit->ambient[2]) return; /* done */
    lit->first = v->char_light_count;
    for (k = 0; k < sec->char_light_count; k++) {
        const tc_light *L = &v->level->lights[sec->char_lights[k]];
        if (!L->on) continue;
        if (L->type == TC_LIGHT_AMBIENT) {
            for (c = 0; c < 3; c++) lit->ambient[c] += L->color[c];
        } else if ((L->type == TC_LIGHT_SPHERICAL || L->type == TC_LIGHT_PARALLEL) &&
                   v->char_light_count < TC_VIEW_MAX_LIGHTS) { /* hotspots: not read yet, as in tc_viewer_frame */
            tcv_view_light(&v->char_lights[v->char_light_count++], L, 1);
            lit->count++;
        }
    }
}

/* The sector a perso's lights come from: where it is now, for those that move. */
uint16_t tcv_perso_light_sector(const tc_viewer *v, uint32_t i) {
    const tc_cast_actor *a = v->playing && v->play && v->play->has_cast && v->play->use_ai
                                 ? tc_cast_actor_of(&v->play->cast, i) : NULL;
    if (a && a->sector.sector < v->sectors->count) return a->sector.sector;
    return v->sectors->perso_count == v->level->perso_count ? v->sectors->perso_sector[i] : TC_SECTOR_NONE;
}

/* Each frame, the shown sectors' characters' lights: Ed's and the camera's sector first, then those of the sectors with
 * a perso in them (the exe's limit is 50 lights at once; a sector past TC_VIEW_MAX_LIGHTS keeps its ambient only). */
static void update_char_lights(tc_viewer *v) {
    const tc_sectors *s = v->sectors;
    const uint8_t *shown;
    uint16_t ed, k;
    uint32_t i;
    v->char_light_count = 0;
    if (!v->sector_lit) return;
    memset(v->sector_lit, 0, s->count * sizeof *v->sector_lit);
    if (v->lighting != TC_VL_GAME) return;
    shown = tcv_shown_sectors(v);
    ed = tc_viewer_ed_sector(v);
    if (ed < s->count) gather_char_lights(v, ed);
    if (v->light_sector < s->count) gather_char_lights(v, v->light_sector);
    for (i = 0; i < v->level->perso_count; i++) {
        k = tcv_perso_light_sector(v, i);
        if (k < s->count && (!shown || (shown[k] & TC_SHOWN_SECTOR))) gather_char_lights(v, k);
    }
}

void tcv_update_lighting(tc_viewer *v, float dt) {
    const tc_level *l = v->level;
    uint16_t s = v->light_sector = camera_sector(v), k;
    const tc_sector *sec = v->sectors && s < v->sectors->count ? &v->sectors->sectors[s] : NULL;
    float step = v->lights_started ? dt / TC_VIEW_LIGHT_FADE : 1, ease;
    const tc_sector_env *env = NULL;
    tc_sector_env level_fog;
    uint32_t i;
    if (!l->has_lighting) return;
    if (v->light_wanted) {
        memset(v->light_wanted, 0, l->light_count);
        for (k = 0; sec && k < sec->light_count; k++) v->light_wanted[sec->lights[k]] = 1;
        for (i = 0; i < l->light_count; i++) {
            float w = v->light_instant[i] ? (v->light_wanted[i] ? 1.0f : 0.0f)
                                          : v->light_weight[i] + (v->light_wanted[i] ? step : -step);
            v->light_weight[i] = w < 0 ? 0 : w > 1 ? 1 : w;
        }
    }
    k = sec ? tc_sectors_env_index(v->sectors, s, v->camera.position, NULL) : TC_SECTOR_NONE;
    if (k != TC_SECTOR_NONE && sec->envs[k].fog && sec->envs[k].fog_far > sec->envs[k].fog_near) env = &sec->envs[k];
    if (!sec && v->has_fog) { /* no sector known (or none in the level): the level's fog */
        level_fog.fog = 1;
        level_fog.fog_near = l->fog_near;
        level_fog.fog_far = l->fog_far;
        memcpy(level_fog.fog_rgb, l->fog_rgb, sizeof(level_fog.fog_rgb));
        env = &level_fog;
    }
    if (env && v->sector_fog_strength <= 0) { /* fading in from none: its own values */
        v->sector_fog_near = env->fog_near;
        v->sector_fog_far = env->fog_far;
        memcpy(v->sector_fog_color, env->fog_rgb, sizeof(v->sector_fog_color));
    } else if (env) {
        ease = step >= 1 ? 1 : 1 - expf(-3 * step); /* most of the way within the fade */
        v->sector_fog_near += (env->fog_near - v->sector_fog_near) * ease;
        v->sector_fog_far += (env->fog_far - v->sector_fog_far) * ease;
        for (i = 0; i < 3; i++) v->sector_fog_color[i] += (env->fog_rgb[i] - v->sector_fog_color[i]) * ease;
    }
    v->sector_fog_strength += env ? step : -step;
    v->sector_fog_strength = v->sector_fog_strength < 0 ? 0 : v->sector_fog_strength > 1 ? 1 : v->sector_fog_strength;
    v->lights_started = true;
    update_char_lights(v);
}

void tc_viewer_set_light_set(tc_viewer *v, uint32_t n) {
    REC_DOOR(TC_RD_LIGHT_SET, (int32_t)n, 0);
    if (n >= v->light_set_count) return;
    v->light_set = v->light_next = v->light_target = n;
    v->light_blend = v->light_rate = 0;
    v->light_scripted = false;
}

void tc_viewer_follow_light(tc_viewer *v) {
    REC_DOOR(TC_RD_FOLLOW_LIGHT, 0, 0);
    v->light_scripted = v->light != NULL;
}
