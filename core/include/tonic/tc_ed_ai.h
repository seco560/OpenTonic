/* Ed driven by his own AI: couples tc_ai (decisions, states) to the mechanics. Each frame the AI sees Ed's contacts,
 * buttons and the camera, then its motion requests go to the mechanics of the playing state's ID card:
 *   types 0 / 4 (ground, air; tc_mec_walk, the exe's FUN_004aa6af with its collide-and-move): ground speed = the
 *           state animation's root motion x its frame rate, or SetMoveDirection under mode 12; ProposalVecA = the
 *           jump's velocity; turns change the heading; the body is the state's ZDM spheres (.ttai v4);
 *   type 5 (flight) and type 2 (the skier's board): tc_mec_fly / tc_mec_ski moved by tc_player.
 * Without the v4 data (older .ttai, or a state without spheres) the ground falls back to tc_player with the animation
 * stride x run_scale / walk_scale (the hand tuning from before the mechanics were decoded). */
#ifndef TONIC_TC_ED_AI_H
#define TONIC_TC_ED_AI_H
#include "tc_ai.h"
#include "tc_player.h"
#include "tc_mec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_ed_ai {
    tc_player *player;           /* physics, owned by the caller */
    tc_ai_perso ai;
    int camera;                  /* index into ai.model->others of the camera perso, -1 none */
    /* Fallback only (no v4 data): factors on the stride-derived ground speed, for run cycles (> 4 u/s at their own
     * frame rate: states 10, 25, 30, 96) and the rest (walks). Defaults 2 and 1. */
    float run_scale, walk_scale;
    uint32_t card_type;          /* type of the playing state's ID card last frame (TC_AI_NONE16: none) */
    tc_mec_fly fly;
    tc_mec_ski ski;
    tc_mec_walk walk;            /* card types 0 / 4 (ground, air) when the .ttai has the state's collision spheres */
    uint32_t contacts;           /* what the AI sees next frame (TC_AI_CONTACT_*) */
    float time;                  /* seconds since init */
    float *trigger_wake;         /* per model trigger (tc_ai_trigger): when it may fire again */
    uint32_t triggers_fired;     /* debug count */
    /* Totalski / skymap: the skier starts in a behaviour that waits for Jump behind a text box (B2); with auto_start
     * (default on those levels) the driver presses Jump until that behaviour is left, so the ride starts at once. */
    bool auto_start;
    tc_mec_env env;
} tc_ed_ai;

tc_result tc_ed_ai_init(tc_ed_ai *e, const tc_ai_model *model, tc_player *player, const char *level_name);
void tc_ed_ai_free(tc_ed_ai *e);
/* inputs: 1 << TC_AI_IN_* held; stick -1..1 (y forward, 0 with a keyboard); camera: the eye position. */
void tc_ed_ai_step(tc_ed_ai *e, float dt, uint32_t inputs, float stick_x, float stick_y, tc_vec3 camera);

/* What to draw: the playing state's animation key (.ttlv Ed animation "s<key>"), the frame into it, and whether
 * it loops (blend the last frame into the first). */
uint16_t tc_ed_ai_anim(const tc_ed_ai *e);
/* Where to draw the model: tc_player_model_matrix, except on the skier's board (card type 2): the board's full
 * orientation (tilted with the slope), lifted along its up axis by the pose's drop, so the board rests on the
 * contact point instead of sinking into the snow. */
tc_mat4 tc_ed_ai_model_matrix(const tc_ed_ai *e);
float tc_ed_ai_frame(const tc_ed_ai *e);
bool tc_ed_ai_loops(const tc_ed_ai *e);

#ifdef __cplusplus
}
#endif
#endif
