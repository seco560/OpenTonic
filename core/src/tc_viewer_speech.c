/* The viewer's text window and prompts (docs/ENGINE.md "Speech", "Walkable scenes"); the lines and the hint scenes
 * themselves are the game's (tc_game_speech.c). */
#include "tc_viewer_internal.h"

const tc_dialogue *tc_viewer_speech(const tc_viewer *v) { return v->game ? v->game->speech.dialogue : NULL; }

bool tc_viewer_talk_prompt(const tc_viewer *v) {
    return v->game && !v->director && !v->tape && tc_game_talk_prompt(v->game);
}

/* The prompt this frame, NULL none: a waiting speaker's, else custom gameplay's (a course's finish, tcx.h). */
static const char *prompt_text(const tc_viewer *v) {
    if (v->level->speech.dialogue_count && tc_viewer_talk_prompt(v)) return "Press Action to talk";
    if (!v->game || !v->playing || v->game->phase != TC_GP_PLAY || v->game->speech.dialogue || v->director || v->tape ||
        v->paused)
        return NULL;
    return tcx_prompt(v->game->custom);
}

bool tc_viewer_speech_panel(tc_viewer *v, float aspect, tc_view_speech_panel *out) {
    static const uint32_t PROMPT_WINDOW[4] = {75, 95, 5, 95}; /* a level without lines: the game's usual window */
    const tc_speech_data *sd = &v->level->speech;
    tc_speech *sp = v->game ? &v->game->speech : NULL;
    const tc_dialogue *d = sp ? sp->dialogue : NULL;
    const uint32_t *window;
    const char *prompt = d ? NULL : prompt_text(v);
    float box, x;
    if (!sp || (d ? !tc_speech_render(sp) : !prompt || !tc_speech_render_prompt(sp, prompt)))
        return false;
    window = d ? d->window : sd->dialogue_count ? sd->dialogues[0].window : PROMPT_WINDOW; /* the prompt: the same window */
    box = aspect > 4.0f / 3 ? 4.0f / 3 / aspect : 1; /* the window is the 4:3 frame's: its text keeps its shape */
    x = 0.5f - box / 2;
    out->rgba = sp->pixels;
    out->width = TC_SPEECH_PANEL_W;
    out->height = TC_SPEECH_PANEL_H;
    out->version = sp->version;
    out->rect[0] = x + box * (float)window[2] / 100;
    out->rect[1] = (float)window[0] / 100;
    out->rect[2] = x + box * (float)window[3] / 100;
    out->rect[3] = (float)window[1] / 100;
    return true;
}
