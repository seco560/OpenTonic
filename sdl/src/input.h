/* Keyboard state for the viewer. Port of Sources/TonicViewer/Input.swift (same keys, as SDL scancodes so the
 * layout's physical positions match: WASD on AZERTY is ZQSD).
 * WASD = move, arrow keys = camera; plus R/F = up/down and Shift = faster (viewer-only extras).
 * Play mode (default when the level has Ed; Tab toggles free-fly): the same WASD/arrows/Shift drive Ed and the
 * orbit camera; Space jumps. */
#ifndef TV_INPUT_H
#define TV_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_scancode.h>
#include "camera.h"

typedef enum input_action {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN,
    ACT_LOOK_LEFT, ACT_LOOK_RIGHT, ACT_LOOK_UP, ACT_LOOK_DOWN, ACT_FAST, ACT_JUMP,
    ACT_ACTION, ACT_SHOOT, ACT_LATERAL, ACT_COUNT
} input_action;

typedef struct input {
    uint32_t held;       /* 1 << input_action */
    float base_speed;
    float turn_speed;    /* radians per second */
    float mouse_dx, mouse_dy;
} input;

void input_init(input *in);
/* Updates the held set; false when the key isn't bound. */
bool input_key(input *in, SDL_Scancode key, bool down);
static inline bool input_held(const input *in, input_action a) { return (in->held >> a) & 1u; }
/* Arrow keys and mouse turn the camera (both modes). */
void input_look(input *in, camera *cam, float dt);
/* Play mode: WASD as a world-space XY direction relative to the camera's yaw (length <= 1). */
tc_vec2 input_move_direction(const input *in, float yaw);
/* Play mode with Ed's AI: the game's own inputs (1 << TC_AI_IN_*). WASD = the direction keys (the scripts make
 * them camera-relative), Shift = SpeedUp (on a keyboard Ed runs; SpeedUp makes him walk), Space = Jump,
 * E = Action (stick), Q = Shoot, C = Lateral. */
uint32_t input_ai_inputs(const input *in);
/* Free-fly camera. */
void input_apply(input *in, camera *cam, float dt);

#endif
