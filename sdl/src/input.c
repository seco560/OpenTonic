#include "input.h"

int input_action(SDL_Scancode key) {
    switch (key) {
    case SDL_SCANCODE_W: return TC_VA_FORWARD;
    case SDL_SCANCODE_S: return TC_VA_BACK;
    case SDL_SCANCODE_A: return TC_VA_LEFT;
    case SDL_SCANCODE_D: return TC_VA_RIGHT;
    case SDL_SCANCODE_R: return TC_VA_UP;
    case SDL_SCANCODE_F: return TC_VA_DOWN;
    case SDL_SCANCODE_LEFT: return TC_VA_LOOK_LEFT;
    case SDL_SCANCODE_RIGHT: return TC_VA_LOOK_RIGHT;
    case SDL_SCANCODE_UP: return TC_VA_LOOK_UP;
    case SDL_SCANCODE_DOWN: return TC_VA_LOOK_DOWN;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return TC_VA_FAST;
    case SDL_SCANCODE_SPACE: return TC_VA_JUMP;          /* play mode */
    case SDL_SCANCODE_E: return TC_VA_ACTION;            /* play mode, Ed's AI */
    case SDL_SCANCODE_Q: return TC_VA_SHOOT;
    case SDL_SCANCODE_C: return TC_VA_LATERAL;
    default: return -1;
    }
}

uint32_t input_pad_actions(SDL_GamepadButton button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return 1u << TC_VA_JUMP;
    case SDL_GAMEPAD_BUTTON_WEST: return 1u << TC_VA_ACTION;
    case SDL_GAMEPAD_BUTTON_EAST: return 1u << TC_VA_SHOOT;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 1u << TC_VA_LATERAL | 1u << TC_VA_UP;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 1u << TC_VA_DOWN;
    default: return 0;
    }
}
