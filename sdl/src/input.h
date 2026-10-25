/* Key bindings for the viewer as SDL scancodes (Sources/OpenTonic/Input.swift's table; scancodes so the layout's
 * physical positions match: WASD on AZERTY is ZQSD). What the actions do is the core's (tc_viewer.h):
 * WASD = move, arrow keys = camera, R/F = up/down, Shift = faster (with Ed's AI: SpeedUp, he walks), Space = jump,
 * E = Action, Q = Shoot, C = Lateral. Gamepad buttons below. */
#ifndef TV_INPUT_H
#define TV_INPUT_H
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <tonic/tonic.h>

/* The action bound to the key, or -1. */
int input_action(SDL_Scancode key);
/* The actions a gamepad button holds in a level (1 << tc_view_action), 0 = none. Positional names, so Cross on a
 * PlayStation pad and A on an Xbox one are both SOUTH: Jump; Square / X Action; Circle / B Shoot; R Lateral and, in
 * free-fly, up; L down. The sticks, triggers, D-pad, Start and Select are main.c's (docs/SDL3.md, Gamepad). */
uint32_t input_pad_actions(SDL_GamepadButton button);

#endif
