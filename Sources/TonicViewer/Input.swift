import AppKit
import simd
import TonicCore

/// Keyboard state for the viewer. Defaults follow the port's control scheme:
/// WASD = move, arrow keys = camera; plus R/F = up/down and Shift = faster (viewer-only extras).
/// Play mode (default when the level has Ed; Tab toggles free-fly): the same WASD/arrows/Shift drive Ed and the orbit camera; Space jumps.
struct InputState {
    enum Action: CaseIterable { case forward, back, left, right, up, down, lookLeft, lookRight, lookUp, lookDown, fast, jump,
                                action, shoot, lateral }

    /// Rebindable map from macOS virtual key codes to actions.
    var bindings: [UInt16: Action] = [
        13: .forward, 1: .back, 0: .left, 2: .right,          // W S A D
        15: .up, 3: .down,                                     // R F
        123: .lookLeft, 124: .lookRight, 126: .lookUp, 125: .lookDown,  // arrows
        56: .fast, 60: .fast,                                  // left/right Shift
        49: .jump,                                             // Space (play mode)
        14: .action, 12: .shoot, 8: .lateral,                  // E Q C (play mode, Ed's AI)
    ]
    var held: Set<Action> = []
    var baseSpeed: Float = 10
    var turnSpeed: Float = 1.6  // radians per second
    var mouseDelta = SIMD2<Float>(0, 0)

    mutating func key(_ code: UInt16, down: Bool) -> Bool {
        guard let a = bindings[code] else { return false }
        if down { held.insert(a) } else { held.remove(a) }
        return true
    }

    private func axis(_ pos: Action, _ neg: Action) -> Float { (held.contains(pos) ? 1 : 0) - (held.contains(neg) ? 1 : 0) }

    /// Arrow keys and mouse turn the camera (both modes).
    mutating func look(_ cam: inout Camera, dt: Float) {
        cam.yaw += (axis(.lookRight, .lookLeft) * turnSpeed * dt) + mouseDelta.x * 0.003
        cam.pitch = max(-1.5, min(1.5, cam.pitch + axis(.lookUp, .lookDown) * turnSpeed * dt - mouseDelta.y * 0.003))
        mouseDelta = .zero
    }

    /// Play mode: WASD as a world-space XY direction relative to the camera's yaw (length <= 1).
    func moveDirection(yaw: Float) -> SIMD2<Float> {
        let f = SIMD2(sin(yaw), cos(yaw)), r = SIMD2(cos(yaw), -sin(yaw))
        let m = f * axis(.forward, .back) + r * axis(.right, .left)
        return simd_length(m) > 1 ? normalize(m) : m
    }

    /// Play mode with Ed's AI: the game's own inputs (1 << TC_AI_IN_*). WASD = the direction keys (the scripts make
    /// them camera-relative), Shift = SpeedUp (on a keyboard Ed runs; SpeedUp makes him walk), Space = Jump,
    /// E = Action (stick), Q = Shoot, C = Lateral.
    var aiInputs: UInt32 {
        let map: [(Action, Int)] = [(.forward, TC_AI_IN_UP), (.back, TC_AI_IN_DOWN), (.left, TC_AI_IN_LEFT),
                                      (.right, TC_AI_IN_RIGHT), (.fast, TC_AI_IN_SPEEDUP), (.jump, TC_AI_IN_JUMP),
                                      (.action, TC_AI_IN_ACTION), (.shoot, TC_AI_IN_SHOOT), (.lateral, TC_AI_IN_LATERAL)]
        return map.reduce(0) { held.contains($1.0) ? $0 | (UInt32(1) << UInt32($1.1)) : $0 }
    }

    /// Free-fly camera.
    mutating func apply(to cam: inout Camera, dt: Float) {
        look(&cam, dt: dt)
        // W/S fly along the view direction (pitch included); R/F stay world up/down.
        var move = cam.forward * axis(.forward, .back) + cam.right * axis(.right, .left)
        move.z += axis(.up, .down)
        if simd_length(move) > 0 {
            cam.position += normalize(move) * baseSpeed * (held.contains(.fast) ? 4 : 1) * dt
        }
    }
}
