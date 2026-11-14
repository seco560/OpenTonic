import GameController
import TonicCore

/// Gamepads through the GameController framework, as the SDL build's (sdl/src/main.c, input.c): the controller last
/// used drives the viewer. The sticks go to the core as they are (tc_viewer_set_move_stick / _look_stick; the dead
/// zones are the core's), buttons hold the same actions as their keys. Positional: buttonA is Cross on a PlayStation
/// pad, A on an Xbox one. Polled once a frame by the renderer, before tc_viewer_update.
@MainActor final class Gamepad {
    /// What the app does rather than the viewer: Start = level select, Select = the controls, R3 = play / free-fly,
    /// L2 = recentre the camera, the touchpad = mark this moment in the recording (F9), Cross = pause a playback,
    /// Triangle = the close-up camera (5), D-pad right = the HUD's auto-hide (H); press: one of the face buttons went down. The debug menu is the keyboard's (F4).
    enum Command { case levelSelect, controls, togglePlay, recenter, firstPerson, closeup, hudAutoHide, press, mark, pausePlayback }

    private var pressed: [ObjectIdentifier: Bool] = [:]
    private var fast = false, recenter = false

    /// Applies the current controller to the viewer; returns the commands pressed since the last call. watching: a
    /// playback runs, whose inputs are the recording's: only the commands (Start goes back to the list).
    func poll(_ v: UnsafeMutablePointer<tc_viewer>, dt: Float, watching: Bool = false) -> [Command] {
        guard let pad = GCController.current?.extendedGamepad else { return [] }
        // Both sticks y up, as the core expects.
        tc_viewer_set_move_stick(v, pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value)
        tc_viewer_set_look_stick(v, pad.rightThumbstick.xAxis.value, pad.rightThumbstick.yAxis.value)
        let f = pad.rightTrigger.value > 0.5  // R2: fly faster
        if f != fast {
            fast = f
            tc_viewer_set_action(v, TC_VA_FAST, f)
        }
        let l2 = pad.leftTrigger.value > 0.5  // L2: recentre the camera (on the press)
        let recentred = l2 && !recenter
        recenter = l2
        // D-pad up / down: the scroll wheel (camera distance in play, fly speed in free-fly), 10 notches a second.
        let wheel = (pad.dpad.up.isPressed ? 1 : 0) - (pad.dpad.down.isPressed ? 1 : 0)
        if wheel != 0, !watching, !tc_viewer_debug_open(v) { tc_viewer_wheel(v, Float(wheel) * 10 * dt) }
        let bindings: [(GCControllerButtonInput, [tc_view_action])] = [
            (pad.buttonA, [TC_VA_JUMP]), (pad.buttonX, [TC_VA_ACTION]), (pad.buttonB, [TC_VA_SHOOT]),
            (pad.rightShoulder, [TC_VA_LATERAL, TC_VA_UP]), (pad.leftShoulder, [TC_VA_DOWN]),  // R1 also up, L1 down in free-fly
        ]
        var anyDown = false, cross = false
        for (button, actions) in bindings {
            if let down = changed(button) {
                for a in actions { tc_viewer_set_action(v, a, down) }
                anyDown = anyDown || down
                cross = cross || (down && button === pad.buttonA)
            }
        }
        var commands: [Command] = cross ? [.pausePlayback] : []
        if changed(pad.buttonMenu) == true { commands.append(.levelSelect) }
        if let select = pad.buttonOptions, changed(select) == true { commands.append(.controls) }
        if let r3 = pad.rightThumbstickButton, changed(r3) == true { commands.append(.togglePlay) }
        if let l3 = pad.leftThumbstickButton, changed(l3) == true { commands.append(.firstPerson) }  // L3: first person (0)
        let touchpad = (pad as? GCDualSenseGamepad)?.touchpadButton ?? (pad as? GCDualShockGamepad)?.touchpadButton
        if let touchpad, changed(touchpad) == true { commands.append(.mark) }
        if recentred { commands.append(.recenter) }
        if changed(pad.dpad.right) == true, !watching { commands.append(.hudAutoHide) }  // D-pad right: the HUD's auto-hide (H)
        if changed(pad.buttonY) == true {  // Triangle: the close-up camera (5)
            commands.append(.press)
            if !watching { commands.append(.closeup) }
        } else if anyDown { commands.append(.press) }
        return commands
    }

    /// The button's new state when it changed since the last poll, else nil.
    private func changed(_ button: GCControllerButtonInput) -> Bool? {
        let id = ObjectIdentifier(button), now = button.isPressed
        defer { pressed[id] = now }
        return pressed[id, default: false] != now ? now : nil
    }
}
