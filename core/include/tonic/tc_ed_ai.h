/* Ed driven by his own AI: couples tc_ai (decisions, states) to tc_player (gravity, collisions). Each frame the
 * AI sees Ed's contacts, buttons and the camera, then its motion request becomes the physics' target:
 *   ground: forward at the playing state's walk stride x its frame rate (Ed's walk/run speed comes from the
 *           animation; the scripts only pick the state and its rate), or SetMoveDirection's vector;
 *   ProposalVecA: velocity impulse (local frame; the jump); the Velocity field: set velocity; StopMotion;
 *   turns (TurnBy / TurnBySpeed) change the heading; in the air the horizontal momentum is kept.
 * An approximation of the exe's MEC handlers, which are not decoded yet (docs/NOTES.md, "AI interpreter"). */
#ifndef TONIC_TC_ED_AI_H
#define TONIC_TC_ED_AI_H
#include "tc_ai.h"
#include "tc_player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_ed_ai {
    tc_player *player;           /* physics, owned by the caller */
    tc_ai_perso ai;
    int camera;                  /* index into ai.model->others of the camera perso, -1 none */
    /* Hand tuning until the MEC handlers are decoded: factors on the animation-derived ground speed, for run cycles
     * (> 4 u/s at their own frame rate: states 10, 25, 30, 96) and the rest (walks). Defaults 2 and 1. */
    float run_scale, walk_scale;
} tc_ed_ai;

tc_result tc_ed_ai_init(tc_ed_ai *e, const tc_ai_model *model, tc_player *player, const char *level_name);
void tc_ed_ai_free(tc_ed_ai *e);
/* inputs: 1 << TC_AI_IN_* held; stick -1..1 (y forward, 0 with a keyboard); camera: the eye position. */
void tc_ed_ai_step(tc_ed_ai *e, float dt, uint32_t inputs, float stick_x, float stick_y, tc_vec3 camera);

/* What to draw: the playing state's animation key (.ttlv Ed animation "s<key>"), the frame into it, and whether
 * it loops (blend the last frame into the first). */
uint16_t tc_ed_ai_anim(const tc_ed_ai *e);
float tc_ed_ai_frame(const tc_ed_ai *e);
bool tc_ed_ai_loops(const tc_ed_ai *e);

#ifdef __cplusplus
}
#endif
#endif
