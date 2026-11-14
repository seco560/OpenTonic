import Foundation
import TonicCore

/// Key bindings for the viewer: macOS virtual key codes -> the core's actions (tc_viewer.h, which also decides what
/// they do). WASD = move, arrow keys = camera, R/F = up/down, Shift = faster (with Ed's AI: SpeedUp, he walks),
/// Space = jump, E = Action, Q = Shoot, C = Lateral.
struct KeyBindings {
    var actions: [UInt16: tc_view_action] = [
        13: TC_VA_FORWARD, 1: TC_VA_BACK, 0: TC_VA_LEFT, 2: TC_VA_RIGHT,     // W S A D
        15: TC_VA_UP, 3: TC_VA_DOWN,                                         // R F
        123: TC_VA_LOOK_LEFT, 124: TC_VA_LOOK_RIGHT, 126: TC_VA_LOOK_UP, 125: TC_VA_LOOK_DOWN,  // arrows
        86: TC_VA_LOOK_LEFT, 88: TC_VA_LOOK_RIGHT,                           // Numpad 4 / 6: the original's camera orbit
        56: TC_VA_FAST, 60: TC_VA_FAST,                                      // left/right Shift
        49: TC_VA_JUMP,                                                      // Space (play mode)
        14: TC_VA_ACTION, 12: TC_VA_SHOOT, 8: TC_VA_LATERAL,                 // E Q C (play mode, Ed's AI)
    ]

    func action(_ code: UInt16) -> tc_view_action? { actions[code] }
}

/// The look settings (tc_viewer_set_look): sensitivity and invert, for mouse, keys and stick alike. Kept across levels
/// and launches in UserDefaults ("lookSensitivity", default 1; "invertX", "invertY"); ; ' and Y change them.
@MainActor enum LookSettings {
    static func apply(_ v: UnsafeMutablePointer<tc_viewer>) {
        let d = UserDefaults.standard
        tc_viewer_set_look(v, d.object(forKey: "lookSensitivity") as? Float ?? 1, d.bool(forKey: "invertX"),
                           d.bool(forKey: "invertY"))
    }

    static func save(_ v: UnsafeMutablePointer<tc_viewer>) {
        let d = UserDefaults.standard
        d.set(v.pointee.look_sensitivity, forKey: "lookSensitivity")
        d.set(v.pointee.invert_x, forKey: "invertX")
        d.set(v.pointee.invert_y, forKey: "invertY")
    }
}

/// The HUD's auto-hide (tc_viewer_set_hud_auto_hide: each element shows for five seconds when it changes, then slides
/// out; off, the default, it stays). Kept across levels and launches in UserDefaults ("hudAutoHide"); H and the pad's
/// D-pad right toggle it.
@MainActor enum HudSettings {
    static func apply(_ v: UnsafeMutablePointer<tc_viewer>) {
        tc_viewer_set_hud_auto_hide(v, UserDefaults.standard.bool(forKey: "hudAutoHide"))
    }

    static func toggle(_ v: UnsafeMutablePointer<tc_viewer>) {
        let on = !tc_viewer_hud_auto_hide(v)
        tc_viewer_set_hud_auto_hide(v, on)
        UserDefaults.standard.set(on, forKey: "hudAutoHide")
    }
}
