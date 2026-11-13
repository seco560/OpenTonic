/* The viewer's speech and hint scenes (docs/ENGINE.md "Speech", "Walkable scenes"). */
#include "tc_viewer_internal.h"

/* The line on show (GAM_0042cce0's game mode 2, GAM_00417a10): it starts when a perso's META 7 says so, its voice plays
 * and its text moves on; a fresh Jump, Action or Shoot ends it (the exe's own key, DAT_008bc3bc), whenever: with text on,
 * only the key does. Port: a line without text ends with its voice, and held Up / Down scroll the text once it is out
 * (DAT_008bc3b4, DAT_008bc3b8). */
void tcv_update_speech(tc_viewer *v, float dt, bool fresh) {
    tc_viewer_play *p = v->play;
    tc_speech *s = &v->speech;
    tc_ai_globals *g;
    if (!p->has_cast) return;
    g = &p->cast.globals;
    if (!s->dialogue && g->speech) {
        if (g->speech > v->level->speech.dialogue_count) { tc_ai_speech_end(g); return; }
        tc_speech_start(s, &v->level->speech.dialogues[g->speech - 1]);
        v->speech_event = 0;
        v->speech_held = true;
        if (s->dialogue->voice != TC_DIALOGUE_VOICE_NONE && v->sound_count < TC_AI_MAX_SOUNDS)
            v->sounds[v->sound_count++] = s->dialogue->voice;
        fresh = false; /* the press that set it off is not the one that ends it */
    }
    if (!s->dialogue) {
        if (v->speech_held && !(v->held & ((1u << TC_VA_JUMP) | (1u << TC_VA_ACTION) | (1u << TC_VA_SHOOT))))
            v->speech_held = false;
        return;
    }
    {
        const tc_dialogue *d = s->dialogue;
        float length = d->rate ? (float)d->frames / (float)d->rate : 0;
        int scroll = (held(v, TC_VA_BACK) ? 1 : 0) - (held(v, TC_VA_FORWARD) ? 1 : 0);
        bool gone = !g->speaker || !g->speaker->active || g->speaker->frozen || g->speech_stop;
        g->speech_stop = false;
        tc_speech_step(s, dt, scroll);
        while (!gone && v->speech_event < d->event_count &&
               (float)d->events[v->speech_event].frame <= s->time * (float)d->rate)
            tc_ai_set_state(g->speaker, d->events[v->speech_event++].state);
        if (fresh || gone || (!d->text && s->time >= length)) {
            if (v->sound_count < TC_AI_MAX_SOUNDS) v->sounds[v->sound_count++] = TC_AI_VOICE_STOP;
            tc_speech_end(s);
            tc_ai_speech_end(g);
            v->scene_said = v->level->speech.has_arrival;
        }
    }
}

/* Port: a hint scene whose speaker is left in an empty behaviour after its line waits for the scene's Ed, who is gone, to
 * force it on (SpyCarota's B9): nothing will, so the cover and the level it would have gone to. */
void tcv_update_scene(tc_viewer *v) {
    tc_viewer_play *p = v->play;
    uint32_t i;
    if (!v->scene_said || !p->has_cast) return;
    if (v->scene_leaving) {
        if (p->main.driver.screen_covered && !p->main.driver.events.level) p->main.driver.events.level = tc_level_after_cutscene(p->main.level_name);
        return;
    }
    for (i = 0; i < p->cast.count; i++) {
        const tc_cast_actor *a = &p->cast.actors[i];
        const tc_ai_behaviour *b;
        if (!(a->entry->flags & TC_CAST_TALK) || !a->talking) continue;
        b = &a->ai->model->behaviours[a->ai->normal.behaviour];
        if (b->script_count == 0 && b->schedule < 0 && tc_level_after_cutscene(p->main.level_name)) {
            v->scene_leaving = true;
            p->main.driver.events.cover = true;
            p->main.driver.screen_covered = false;
        }
    }
}

const tc_dialogue *tc_viewer_speech(const tc_viewer *v) { return v->speech.dialogue; }

bool tc_viewer_talk_prompt(const tc_viewer *v) {
    const tc_viewer_play *p = v->play;
    uint32_t i;
    if (!p || !p->has_cast || !p->cast.main || v->phase != TC_VP_PLAY || v->speech.dialogue || v->director || v->tape) return false;
    for (i = 0; i < p->cast.count; i++) {
        const tc_cast_actor *a = &p->cast.actors[i];
        float dx, dy;
        if (!(a->entry->flags & TC_CAST_TALK) || a->talking || !a->ai->active) continue;
        dx = a->ai->position.x - p->cast.main->position.x, dy = a->ai->position.y - p->cast.main->position.y;
        if (dx * dx + dy * dy <= TC_CAST_TALK_REACH * TC_CAST_TALK_REACH) return true;
    }
    return false;
}

bool tc_viewer_speech_panel(tc_viewer *v, float aspect, tc_view_speech_panel *out) {
    const tc_speech_data *sd = &v->level->speech;
    const tc_dialogue *d = v->speech.dialogue;
    float box, x;
    if (d ? !tc_speech_render(&v->speech)
          : !sd->dialogue_count || !tc_viewer_talk_prompt(v) || !tc_speech_render_prompt(&v->speech, "Press Action to talk"))
        return false;
    if (!d) d = &sd->dialogues[0]; /* the prompt: the same window */
    box = aspect > 4.0f / 3 ? 4.0f / 3 / aspect : 1; /* the window is the 4:3 frame's: its text keeps its shape */
    x = 0.5f - box / 2;
    out->rgba = v->speech.pixels;
    out->width = TC_SPEECH_PANEL_W;
    out->height = TC_SPEECH_PANEL_H;
    out->version = v->speech.version;
    out->rect[0] = x + box * (float)d->window[2] / 100;
    out->rect[1] = (float)d->window[0] / 100;
    out->rect[2] = x + box * (float)d->window[3] / 100;
    out->rect[3] = (float)d->window[1] / 100;
    return true;
}
