#include "input.h"

static int binding(SDL_Scancode key) {
    switch (key) {
    case SDL_SCANCODE_W: return ACT_FORWARD;
    case SDL_SCANCODE_S: return ACT_BACK;
    case SDL_SCANCODE_A: return ACT_LEFT;
    case SDL_SCANCODE_D: return ACT_RIGHT;
    case SDL_SCANCODE_R: return ACT_UP;
    case SDL_SCANCODE_F: return ACT_DOWN;
    case SDL_SCANCODE_LEFT: return ACT_LOOK_LEFT;
    case SDL_SCANCODE_RIGHT: return ACT_LOOK_RIGHT;
    case SDL_SCANCODE_UP: return ACT_LOOK_UP;
    case SDL_SCANCODE_DOWN: return ACT_LOOK_DOWN;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return ACT_FAST;
    case SDL_SCANCODE_SPACE: return ACT_JUMP;          /* play mode */
    case SDL_SCANCODE_E: return ACT_ACTION;            /* play mode, Ed's AI */
    case SDL_SCANCODE_Q: return ACT_SHOOT;
    case SDL_SCANCODE_C: return ACT_LATERAL;
    default: return -1;
    }
}

void input_init(input *in) {
    in->held = 0;
    in->base_speed = 10;
    in->turn_speed = 1.6f;
    in->mouse_dx = in->mouse_dy = 0;
}

bool input_key(input *in, SDL_Scancode key, bool down) {
    int a = binding(key);
    if (a < 0) return false;
    if (down) in->held |= 1u << a; else in->held &= ~(1u << a);
    return true;
}

static float axis(const input *in, input_action pos, input_action neg) {
    return (input_held(in, pos) ? 1.0f : 0.0f) - (input_held(in, neg) ? 1.0f : 0.0f);
}

void input_look(input *in, camera *cam, float dt) {
    cam->yaw += axis(in, ACT_LOOK_RIGHT, ACT_LOOK_LEFT) * in->turn_speed * dt + in->mouse_dx * 0.003f;
    float pitch = cam->pitch + axis(in, ACT_LOOK_UP, ACT_LOOK_DOWN) * in->turn_speed * dt - in->mouse_dy * 0.003f;
    cam->pitch = fmaxf(-1.5f, fminf(1.5f, pitch));
    in->mouse_dx = in->mouse_dy = 0;
}

tc_vec2 input_move_direction(const input *in, float yaw) {
    float fwd = axis(in, ACT_FORWARD, ACT_BACK), side = axis(in, ACT_RIGHT, ACT_LEFT);
    tc_vec2 m = tc_v2(sinf(yaw) * fwd + cosf(yaw) * side, cosf(yaw) * fwd - sinf(yaw) * side);
    float l = tc_v2_length(m);
    return l > 1 ? tc_v2(m.x / l, m.y / l) : m;
}

uint32_t input_ai_inputs(const input *in) {
    static const struct { input_action a; int bit; } map[] = {
        {ACT_FORWARD, TC_AI_IN_UP}, {ACT_BACK, TC_AI_IN_DOWN}, {ACT_LEFT, TC_AI_IN_LEFT},
        {ACT_RIGHT, TC_AI_IN_RIGHT}, {ACT_FAST, TC_AI_IN_SPEEDUP}, {ACT_JUMP, TC_AI_IN_JUMP},
        {ACT_ACTION, TC_AI_IN_ACTION}, {ACT_SHOOT, TC_AI_IN_SHOOT}, {ACT_LATERAL, TC_AI_IN_LATERAL},
    };
    uint32_t r = 0;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (input_held(in, map[i].a)) r |= 1u << map[i].bit;
    return r;
}

void input_apply(input *in, camera *cam, float dt) {
    input_look(in, cam, dt);
    /* W/S fly along the view direction (pitch included); R/F stay world up/down. */
    tc_vec3 move = tc_v3_add(tc_v3_scale(camera_forward(cam), axis(in, ACT_FORWARD, ACT_BACK)),
                             tc_v3_scale(camera_right(cam), axis(in, ACT_RIGHT, ACT_LEFT)));
    move.z += axis(in, ACT_UP, ACT_DOWN);
    if (tc_v3_length(move) > 0) {
        float speed = in->base_speed * (input_held(in, ACT_FAST) ? 4.0f : 1.0f) * dt;
        cam->position = tc_v3_add(cam->position, tc_v3_scale(tc_v3_normalize(move), speed));
    }
}
