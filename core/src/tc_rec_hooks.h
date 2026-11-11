/* What tc_viewer.c tells the recorder (tc_rec.h). */
#ifndef TONIC_TC_REC_HOOKS_H
#define TONIC_TC_REC_HOOKS_H
#include "tonic/tc_rec.h"

/* The settings doors, in the order a frontend called them: the inputs tc_viewer_update reads are taken with the frame
 * instead. Numbers are the file format: only add. DEMO_ON / OFF: Ed's autopilot, removed; old files still hold them. */
typedef enum tc_rec_door {
    TC_RD_STEP_SPEED = 1, TC_RD_TOGGLE_PAUSE, TC_RD_RECENTER, TC_RD_CLOSEUP, TC_RD_FIRST_PERSON, TC_RD_SET_LOOK,
    TC_RD_STEP_LOOK, TC_RD_INVERT_Y, TC_RD_LIGHT_SET, TC_RD_FOLLOW_LIGHT, TC_RD_STEP_GAMMA, TC_RD_STEP_FOV, TC_RD_FOG,
    TC_RD_LIGHTING, TC_RD_SECTORS, TC_RD_WHEEL, TC_RD_TOGGLE_PLAY, TC_RD_TOGGLE_AI, TC_RD_DEMO_ON, TC_RD_DEMO_OFF,
    TC_RD_DEBUG, /* the debug menu opened or closed; its moves are the frames' held actions */
    TC_RD_COUNT
} tc_rec_door_id;

void tc_rec_begin(tc_recorder *r, tc_viewer *v, const tc_viewer_desc *desc);
void tc_rec_end(tc_recorder *r, tc_viewer *v);
void tc_rec_door(tc_recorder *r, tc_rec_door_id door, int32_t a, float x, float y);
void tc_rec_frame_in(tc_recorder *r, tc_viewer *v, float dt);
void tc_rec_frame_out(tc_recorder *r, tc_viewer *v, float dt);
void tc_rec_draw(tc_recorder *r, tc_viewer *v, const tc_view_draw *draws, uint32_t count);
#endif
