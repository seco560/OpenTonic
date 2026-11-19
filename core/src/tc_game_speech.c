/* The game session's lines and hint scenes (tc_game.h; docs/ENGINE.md "Speech", "Walkable scenes"). Moved out of
 * tc_viewer_speech.c on 2026-11-19 as it was; the text window stays the viewer's. */
#include "tc_internal.h"
#include <string.h>
#include "tonic/tc_game.h"

/* The line on show (GAM_0042cce0's game mode 2, GAM_00417a10): it starts when a perso's META 7 says so, its voice plays
 * and its text moves on; a fresh Jump, Action or Shoot ends it (the exe's own key, DAT_008bc3bc), whenever: with text on,
 * only the key does. Port: a line without text ends with its voice, and held Up / Down scroll the text once it is out
 * (DAT_008bc3b4, DAT_008bc3b8). */
void tc_game_update_speech(tc_game *g, float dt, uint32_t held, bool fresh) {
    tc_speech *s = &g->speech;
    tc_ai_globals *gl;
    if (!g->has_cast) return;
    gl = &g->cast.globals;
    if (!s->dialogue && gl->speech) {
        if (gl->speech > g->level->speech.dialogue_count) { tc_ai_speech_end(gl); return; }
        tc_speech_start(s, &g->level->speech.dialogues[gl->speech - 1]);
        g->speech_event = 0;
        g->speech_held = true;
        if (s->dialogue->voice != TC_DIALOGUE_VOICE_NONE) tc_game_push_sound(g, s->dialogue->voice, NULL);
        fresh = false; /* the press that set it off is not the one that ends it */
    }
    if (!s->dialogue) {
        if (g->speech_held && !(held & TC_GAME_SKIP_MASK)) g->speech_held = false;
        return;
    }
    {
        const tc_dialogue *d = s->dialogue;
        float length = d->rate ? (float)d->frames / (float)d->rate : 0;
        int scroll = ((held >> TC_VA_BACK) & 1u ? 1 : 0) - ((held >> TC_VA_FORWARD) & 1u ? 1 : 0);
        bool gone = !gl->speaker || !gl->speaker->active || gl->speaker->frozen || gl->speech_stop;
        gl->speech_stop = false;
        tc_speech_step(s, dt, scroll);
        while (!gone && g->speech_event < d->event_count &&
               (float)d->events[g->speech_event].frame <= s->time * (float)d->rate)
            tc_ai_set_state(gl->speaker, d->events[g->speech_event++].state);
        if (fresh || gone || (!d->text && s->time >= length)) {
            tc_game_push_sound(g, TC_AI_VOICE_STOP, NULL);
            /* a hint scene plays on at its own pace now: the speaker's waits pace its way out (Ed walking off, the
             * cover), as they do in the original */
            if (g->level->speech.has_arrival && gl->speaker) gl->speaker->brisk = false;
            tc_speech_end(s);
            tc_ai_speech_end(gl);
            g->scene_said = g->level->speech.has_arrival;
        }
    }
}

/* Port: a hint scene whose speaker is left in an empty behaviour after its line waits for the scene's Ed, who is gone, to
 * force it on (SpyCarota's B9): nothing will, so the cover and the level it would have gone to. */
void tc_game_update_scene(tc_game *g) {
    uint32_t i;
    if (!g->has_cast || !g->level->speech.has_arrival) return;
    for (i = 0; i < g->cast.count; i++) { /* its walks (SCNX): the behaviour that sends the scene's Ed sends Ed */
        const tc_cast_actor *a = &g->cast.actors[i];
        uint16_t k;
        if (!(a->entry->flags & TC_CAST_TALK) || !a->talking) continue;
        for (k = 0; k < g->level->speech.exit_count && k < 32; k++)
            if (a->ai->normal.behaviour == g->level->speech.exits[k].behaviour && !(g->scene_exits_done & (1u << k))) {
                g->scene_exits_done |= 1u << k;
                g->scene_exit = k;
            }
    }
    if (!g->scene_said) return;
    if (g->scene_leaving) {
        if (g->main.driver.screen_covered && !g->main.driver.events.level) g->main.driver.events.level = tc_level_after_cutscene(g->main.level_name);
        return;
    }
    for (i = 0; i < g->cast.count; i++) {
        const tc_cast_actor *a = &g->cast.actors[i];
        const tc_ai_behaviour *b;
        if (!(a->entry->flags & TC_CAST_TALK) || !a->talking) continue;
        b = &a->ai->model->behaviours[a->ai->normal.behaviour];
        if (b->script_count == 0 && b->schedule < 0 && tc_level_after_cutscene(g->main.level_name)) {
            g->scene_leaving = true;
            g->main.driver.events.cover = true;
            g->main.driver.screen_covered = false;
        }
    }
}

bool tc_game_talk_prompt(const tc_game *g) {
    uint32_t i;
    if (!g->has_cast || !g->cast.main || g->phase != TC_GP_PLAY || g->speech.dialogue) return false;
    for (i = 0; i < g->cast.count; i++) {
        const tc_cast_actor *a = &g->cast.actors[i];
        if (!(a->entry->flags & TC_CAST_TALK) || a->talking || !a->ai->active) continue;
        if (tc_cast_talk_reach(&g->cast, a, g->cast.main->position)) return true;
    }
    return false;
}
