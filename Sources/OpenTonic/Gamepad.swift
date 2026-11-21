import Foundation
import GameController
import TonicCore

/// Start (buttonMenu) and Select (buttonOptions) on macOS: the GameController framework reports their press briefly,
/// and the system may take them for its own gestures, so a poll of isPressed once a frame can miss them (the user,
/// 2026-11-17: Start did not open the game menu on macOS; the SDL build on Linux was fine). Their presses are counted
/// from the buttons' own handler instead, the system gestures turned off; each reader keeps the count it last saw.
final class PadPresses: @unchecked Sendable {
    static let shared = PadPresses()
    private let lock = NSLock()
    private var counts: [ObjectIdentifier: Int] = [:]
    private var hooked = Set<ObjectIdentifier>()

    /// How many times the button went down since the app first asked (its handler is set up on that first ask).
    func count(_ button: GCControllerButtonInput) -> Int {
        let id = ObjectIdentifier(button)
        lock.lock()
        let fresh = hooked.insert(id).inserted
        let n = counts[id, default: 0]
        lock.unlock()
        if fresh {
            button.preferredSystemGestureState = .disabled
            button.pressedChangedHandler = { @Sendable [unowned self] _, _, pressed in
                guard pressed else { return }
                self.lock.lock()
                self.counts[id, default: 0] += 1
                self.lock.unlock()
            }
        }
        return n
    }
}

/// A reader of PadPresses: whether the button went down since this reader last asked.
struct PadPressReader {
    private var seen: [ObjectIdentifier: Int] = [:]
    mutating func pressed(_ button: GCControllerButtonInput) -> Bool {
        let id = ObjectIdentifier(button), n = PadPresses.shared.count(button)
        defer { seen[id] = n }
        return n > seen[id, default: n]
    }
}

/// Gamepads through the GameController framework, as the SDL build's (sdl/src/main.c, input.c): the controller last
/// used drives the viewer. The sticks go to the core as they are (tc_viewer_set_move_stick / _look_stick; the dead
/// zones are the core's), buttons hold the same actions as their keys. Positional: buttonA is Cross on a PlayStation
/// pad, A on an Xbox one. Polled once a frame by the renderer, before tc_viewer_update.
@MainActor final class Gamepad {
    /// What the app does rather than the viewer: Start = the game menu (Esc; a playback: back to the list), Select = the
    /// controls, R3 = play / free-fly, L2 = recentre the camera, the touchpad = mark this moment in the recording (F9),
    /// Cross = pause a playback, Triangle = the close-up camera (5); press: one of the face buttons went down. The debug
    /// menu is the keyboard's (F4). The HUD's auto-hide (D-pad right before) is the game menu's OPTIONS now.
    enum Command { case levelSelect, controls, togglePlay, recenter, firstPerson, closeup, press, mark, pausePlayback, photo }

    private var pressed: [ObjectIdentifier: Bool] = [:]
    private var presses = PadPressReader()
    private var fast = false, recenter = false

    /// Applies the current controller to the viewer; returns the commands pressed since the last call. watching: a
    /// playback runs, whose inputs are the recording's: only the commands (Start goes back to the list).
    /// photo: photo mode is on, where Cross / A takes a photo (Command.photo) and holds no action.
    func poll(_ v: UnsafeMutablePointer<tc_viewer>, dt: Float, watching: Bool = false, photo: Bool = false) -> [Command] {
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
        var anyDown = false, cross = false, snap = false
        for (button, actions) in bindings {
            if let down = changed(button) {
                if photo && button === pad.buttonA {  // photo mode: Cross takes a photo
                    snap = snap || down
                    if !down { tc_viewer_set_action(v, TC_VA_JUMP, false) }
                    continue
                }
                for a in actions { tc_viewer_set_action(v, a, down) }
                anyDown = anyDown || down
                cross = cross || (down && button === pad.buttonA)
            }
        }
        var commands: [Command] = cross ? [.pausePlayback] : []
        if snap { commands.append(.photo) }
        if presses.pressed(pad.buttonMenu) { commands.append(.levelSelect) }
        if let select = pad.buttonOptions, presses.pressed(select) { commands.append(.controls) }
        if let r3 = pad.rightThumbstickButton, changed(r3) == true { commands.append(.togglePlay) }
        if let l3 = pad.leftThumbstickButton, changed(l3) == true { commands.append(.firstPerson) }  // L3: first person (0)
        let touchpad = (pad as? GCDualSenseGamepad)?.touchpadButton ?? (pad as? GCDualShockGamepad)?.touchpadButton
        if let touchpad, changed(touchpad) == true { commands.append(.mark) }
        if recentred { commands.append(.recenter) }
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
