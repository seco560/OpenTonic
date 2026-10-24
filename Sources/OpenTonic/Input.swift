import TonicCore

/// Key bindings for the viewer: macOS virtual key codes -> the core's actions (tc_viewer.h, which also decides what
/// they do). WASD = move, arrow keys = camera, R/F = up/down, Shift = faster (with Ed's AI: SpeedUp, he walks),
/// Space = jump, E = Action, Q = Shoot, C = Lateral.
struct KeyBindings {
    var actions: [UInt16: tc_view_action] = [
        13: TC_VA_FORWARD, 1: TC_VA_BACK, 0: TC_VA_LEFT, 2: TC_VA_RIGHT,     // W S A D
        15: TC_VA_UP, 3: TC_VA_DOWN,                                         // R F
        123: TC_VA_LOOK_LEFT, 124: TC_VA_LOOK_RIGHT, 126: TC_VA_LOOK_UP, 125: TC_VA_LOOK_DOWN,  // arrows
        56: TC_VA_FAST, 60: TC_VA_FAST,                                      // left/right Shift
        49: TC_VA_JUMP,                                                      // Space (play mode)
        14: TC_VA_ACTION, 12: TC_VA_SHOOT, 8: TC_VA_LATERAL,                 // E Q C (play mode, Ed's AI)
    ]

    func action(_ code: UInt16) -> tc_view_action? { actions[code] }
}
