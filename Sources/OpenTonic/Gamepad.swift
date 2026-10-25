import GameController
import TonicCore

/// Gamepads through the GameController framework, as the SDL build's (sdl/src/main.c, input.c): the controller last
/// used drives the viewer. The sticks go to the core as they are (tc_viewer_set_move_stick / _look_stick; the dead
/// zones are the core's), buttons hold the same actions as their keys. Positional: buttonA is Cross on a PlayStation
/// pad, A on an Xbox one. Polled once a frame by the renderer, before tc_viewer_update.
@MainActor final class Gamepad {
    /// What the app does rather than the viewer: Start = level select, Select = the controls, R3 = play / free-fly.
    enum Command { case levelSelect, controls, togglePlay }

    private var pressed: [ObjectIdentifier: Bool] = [:]
    private var fast = false

    /// Applies the current controller to the viewer; returns the commands pressed since the last call.
    func poll(_ v: UnsafeMutablePointer<tc_viewer>, dt: Float) -> [Command] {
        guard let pad = GCController.current?.extendedGamepad else { return [] }
        // Both sticks y up, as the core expects.
        tc_viewer_set_move_stick(v, pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value)
        tc_viewer_set_look_stick(v, pad.rightThumbstick.xAxis.value, pad.rightThumbstick.yAxis.value)
        let f = pad.rightTrigger.value > 0.5  // R2: fly faster
        if f != fast {
            fast = f
            tc_viewer_set_action(v, TC_VA_FAST, f)
        }
        // D-pad up / down: the scroll wheel (camera distance in play, fly speed in free-fly), 10 notches a second.
        let wheel = (pad.dpad.up.isPressed ? 1 : 0) - (pad.dpad.down.isPressed ? 1 : 0)
        if wheel != 0 { tc_viewer_wheel(v, Float(wheel) * 10 * dt) }
        let bindings: [(GCControllerButtonInput, [tc_view_action])] = [
            (pad.buttonA, [TC_VA_JUMP]), (pad.buttonX, [TC_VA_ACTION]), (pad.buttonB, [TC_VA_SHOOT]),
            (pad.rightShoulder, [TC_VA_LATERAL, TC_VA_UP]), (pad.leftShoulder, [TC_VA_DOWN]),  // R1 also up, L1 down in free-fly
        ]
        for (button, actions) in bindings {
            if let down = changed(button) { for a in actions { tc_viewer_set_action(v, a, down) } }
        }
        var commands: [Command] = []
        if changed(pad.buttonMenu) == true { commands.append(.levelSelect) }
        if let select = pad.buttonOptions, changed(select) == true { commands.append(.controls) }
        if let r3 = pad.rightThumbstickButton, changed(r3) == true { commands.append(.togglePlay) }
        return commands
    }

    /// The button's new state when it changed since the last poll, else nil.
    private func changed(_ button: GCControllerButtonInput) -> Bool? {
        let id = ObjectIdentifier(button), now = button.isPressed
        defer { pressed[id] = now }
        return pressed[id, default: false] != now ? now : nil
    }
}
