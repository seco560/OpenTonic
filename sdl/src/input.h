/* Key bindings for the viewer as SDL scancodes (Sources/TonicViewer/Input.swift's table; scancodes so the layout's
 * physical positions match: WASD on AZERTY is ZQSD). What the actions do is the core's (tc_viewer.h):
 * WASD = move, arrow keys = camera, R/F = up/down, Shift = faster (with Ed's AI: SpeedUp, he walks), Space = jump,
 * E = Action, Q = Shoot, C = Lateral. */
#ifndef TV_INPUT_H
#define TV_INPUT_H
#include <SDL3/SDL_scancode.h>
#include <tonic/tonic.h>

/* The action bound to the key, or -1. */
int input_action(SDL_Scancode key);

#endif
