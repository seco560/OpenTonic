import AppKit
import MetalKit
import TonicCore

/// A fixed-size C char array (imported as a tuple) as a String.
func cString<T>(_ chars: T) -> String {
    withUnsafeBytes(of: chars) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
}

/// The loading bar, filled with sud0's psychedelic rainbow pool (SUD0\O_ZARBI_AUNTXY): the texture scrolls at the
/// pool's own 0.6 UV/s and rippling strips make it wave. Plain white without the texture.
final class RainbowBar: NSView {
    var progress: Double = 0 { didSet { needsDisplay = true } }
    private let texture: CGImage?
    private let start = CACurrentMediaTime()
    private var timer: Timer?

    init(texture url: URL) {
        texture = NSImage(contentsOf: url)?.cgImage(forProposedRect: nil, context: nil, hints: nil)
        super.init(frame: .zero)
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        timer?.invalidate()
        timer = window == nil ? nil : Timer.scheduledTimer(withTimeInterval: 1.0 / 30, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.needsDisplay = true }
        }
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        let r = bounds.insetBy(dx: 1, dy: 1), radius = r.height / 2
        ctx.addPath(CGPath(roundedRect: r, cornerWidth: radius, cornerHeight: radius, transform: nil))
        ctx.setFillColor(NSColor(white: 0, alpha: 0.55).cgColor)
        ctx.fillPath()
        let width = r.width * CGFloat(min(max(progress, 0), 1))
        if width > 0 {
            let fill = CGRect(x: r.minX, y: r.minY, width: max(width, r.height), height: r.height)
            let t = CGFloat(CACurrentMediaTime() - start)
            ctx.saveGState()
            ctx.addPath(CGPath(roundedRect: fill, cornerWidth: radius, cornerHeight: radius, transform: nil))
            ctx.clip()
            if let texture {
                let tile = r.height * 3, strip: CGFloat = 3
                let scroll = (t * 0.6).truncatingRemainder(dividingBy: 1) * tile
                var x = fill.minX
                while x < fill.maxX {
                    let wave = sin(x / 16 + t * 4) * r.height * 0.3
                    ctx.saveGState()
                    ctx.clip(to: CGRect(x: x, y: fill.minY, width: strip, height: fill.height))
                    ctx.draw(texture, in: CGRect(x: fill.minX - tile + x * 0.15, y: fill.minY - scroll + wave,
                                                 width: tile, height: tile), byTiling: true)
                    ctx.restoreGState()
                    x += strip
                }
            } else {
                ctx.setFillColor(NSColor.white.cgColor)
                ctx.fill(fill)
            }
            // a soft gloss on the upper half
            let gloss = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(),
                                   colors: [NSColor(white: 1, alpha: 0.35).cgColor, NSColor(white: 1, alpha: 0).cgColor] as CFArray,
                                   locations: [0, 1])!
            ctx.drawLinearGradient(gloss, start: CGPoint(x: 0, y: fill.maxY), end: CGPoint(x: 0, y: fill.midY), options: [])
            ctx.restoreGState()
        }
        ctx.addPath(CGPath(roundedRect: r, cornerWidth: radius, cornerHeight: radius, transform: nil))
        ctx.setStrokeColor(NSColor(white: 1, alpha: 0.6).cgColor)
        ctx.setLineWidth(1)
        ctx.strokePath()
    }
}

/// The loading screen of a red portal (docs/PORTALS.md): the coming level's vignette, a bar and its name, over the
/// black the renderer draws.
final class LoadingScreen: NSView {
    private let image = NSImageView()
    private let bar: RainbowBar
    private var level = ""
    let vignettes: URL

    init(frame: NSRect, vignettes: URL) {
        self.vignettes = vignettes
        // out/Vignette/Level -> out/Textures/Sud0 (the same layout inside the app bundle); the Special Edition's
        // out/se/Vignette/Level has none in out/se/Textures and uses the retail one.
        let out = vignettes.deletingLastPathComponent().deletingLastPathComponent(), pool = "Textures/Sud0/o_zarbi_auntxy.png"
        let own = out.appendingPathComponent(pool)
        bar = RainbowBar(texture: FileManager.default.fileExists(atPath: own.path) ? own
            : out.deletingLastPathComponent().appendingPathComponent(pool))
        super.init(frame: frame)
        image.imageScaling = .scaleProportionallyUpOrDown
        for v in [image, bar] as [NSView] { addSubview(v) }
        autoresizingMask = [.width, .height]
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    func show(level: String, progress: Float) {
        if level != self.level {
            self.level = level
            // A custom-layer portal names its edition ("se:sud0", "retail:sud0"; tools/ttre/custom.py).
            let parts = level.split(separator: ":", maxSplits: 1).map(String.init)
            let name = parts.last ?? level
            var folder = vignettes
            if parts.count == 2 {
                var out = vignettes.deletingLastPathComponent().deletingLastPathComponent()
                if out.lastPathComponent == "se" { out = out.deletingLastPathComponent() }
                if parts[0] == "se" { out = out.appendingPathComponent("se") }
                folder = out.appendingPathComponent("Vignette/Level")
            }
            // Vignette names differ in case from the level names (Cock01.png / cock01).
            let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
            image.image = files.first { $0.deletingPathExtension().lastPathComponent.lowercased() == name.lowercased() }
                .flatMap { NSImage(contentsOf: $0) }
            needsLayout = true
        }
        bar.progress = Double(progress)
    }

    override func layout() {
        super.layout()
        let b = bounds, w = min(b.width * 0.8, (b.height - 120) * 4 / 3), h = w * 3 / 4
        image.frame = NSRect(x: (b.width - w) / 2, y: (b.height - h) / 2 + 40, width: w, height: h)
        bar.frame = NSRect(x: (b.width - w * 0.6) / 2, y: image.frame.minY - 74, width: w * 0.6, height: 18)
    }
}

/// A picture the level's scripts show (TC_VO_PICTURE, PROC 210: the Special Edition outro's credits), laid out as the
/// loading screen's vignette, over the black the renderer draws. `vignettes` is the Vignette folder.
final class PictureScreen: NSView {
    private let image = NSImageView()
    private var name = ""
    let vignettes: URL

    init(frame: NSRect, vignettes: URL) {
        self.vignettes = vignettes
        super.init(frame: frame)
        image.imageScaling = .scaleProportionallyUpOrDown
        addSubview(image)
        autoresizingMask = [.width, .height]
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    /// name: "Credits/Credit06" (folders under Vignette, no extension); matched case-insensitively, as a .png.
    func show(_ name: String) {
        guard name != self.name else { return }
        self.name = name
        var dir = vignettes
        let parts = name.split(separator: "/").map(String.init)
        for (i, part) in parts.enumerated() {
            let files = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? []
            let want = i == parts.count - 1 ? part.lowercased() + ".png" : part.lowercased()
            guard let next = files.first(where: { $0.lastPathComponent.lowercased() == want }) else { image.image = nil; return }
            dir = next
        }
        image.image = NSImage(contentsOf: dir)
        needsLayout = true
    }

    override func layout() {
        super.layout()
        let b = bounds, w = min(b.width, b.height * 4 / 3), h = w * 3 / 4
        image.frame = NSRect(x: (b.width - w) / 2, y: (b.height - h) / 2, width: w, height: h)
    }
}

extension NSCursor {
    /// A transparent cursor: the pointer is hidden over the intro, its loading screen and a playback (cursor rects, so it
    /// shows again over the title bar, the menu bar and other windows).
    @MainActor static let blank = NSCursor(image: NSImage(size: NSSize(width: 16, height: 16), flipped: false) { _ in true },
                                           hotSpot: .zero)
}

/// The black behind the loading screen after the intro. Takes the keys (a loading screen can't be skipped), but M
/// mutes as in a level. No pointer over it, as over the intro.
final class LoadingBackdrop: NSView {
    override var acceptsFirstResponder: Bool { true }
    override func resetCursorRects() { addCursorRect(bounds, cursor: .blank) }
    override func keyDown(with event: NSEvent) {
        if event.keyCode == 46, !event.isARepeat { Music.shared.muted.toggle() }
    }
}

/// Open Tonic: the native macOS app (began as Tonic Viewer, a level viewer and collision tester).
final class GameView: MTKView {
    var renderer: Renderer?
    private let bindings = KeyBindings()
    override var acceptsFirstResponder: Bool { true }

    private var loadingScreen: LoadingScreen?
    var vignettes: URL?
    /// Playing a recording back: the game's inputs are the recording's; Esc goes back to the list, P pauses, and paused the
    /// look inputs get through.
    var onPlaybackBack: (() -> Void)?
    var onPlaybackPause: (() -> Void)?
    private var inPlayback: Bool { renderer?.playback != nil }
    private var playbackPaused: Bool { renderer?.playback?.paused == true }

    private var pictureScreen: PictureScreen?

    /// The renderer's scripted picture, every frame (nil: none). Pictures live in the Vignette folder next to the one
    /// the loading screens come from (vignettes = .../Vignette/Level).
    func showPicture(_ name: String?) {
        guard let name, let vignettes else {
            pictureScreen?.removeFromSuperview()
            pictureScreen = nil
            return
        }
        if pictureScreen == nil {
            let p = PictureScreen(frame: bounds, vignettes: vignettes.deletingLastPathComponent())
            addSubview(p)
            pictureScreen = p
        }
        pictureScreen?.show(name)
    }

    /// The renderer's loading screen state, every frame.
    func showLoading(_ state: (String, Float)?) {
        guard let (level, progress) = state, let vignettes else {
            loadingScreen?.removeFromSuperview()
            loadingScreen = nil
            return
        }
        if loadingScreen == nil {
            let l = LoadingScreen(frame: bounds, vignettes: vignettes)
            addSubview(l)
            loadingScreen = l
        }
        loadingScreen?.show(level: level, progress: progress)
    }

    /// The core's viewer state of the open level (tc_viewer.h).
    private var viewer: UnsafeMutablePointer<tc_viewer>? { renderer?.viewer?.raw }

    /// Mouse-look capture (viewer convenience, not part of the original controls):
    /// click to capture, move to look, Esc to release.
    private var captured = false
    var isCaptured: Bool { captured }

    /// The first mouse movement after capturing is dropped: detaching the pointer can report a jump of its own.
    private var skipMove = false

    func setCaptured(_ on: Bool) {
        guard on != captured else { return }
        captured = on
        skipMove = on
        if on { NSCursor.hide() } else { NSCursor.unhide() }
        CGAssociateMouseAndMouseCursorPosition(on ? 0 : 1)
        window?.acceptsMouseMovedEvents = on
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        NotificationCenter.default.addObserver(forName: NSWindow.didResignKeyNotification, object: window, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.setCaptured(false) }  // queue: .main
        }
    }

    /// No pointer in a playback (in play, mouse-look capture hides it).
    override func resetCursorRects() { if inPlayback { addCursorRect(bounds, cursor: .blank) } }

    override func mouseDown(with event: NSEvent) {
        if inPlayback && !playbackPaused { return }
        setCaptured(true)
    }

    /// The middle button: recentre the camera (X).
    override func otherMouseDown(with event: NSEvent) {
        guard event.buttonNumber == 2, !inPlayback, let v = viewer else { return super.otherMouseDown(with: event) }
        tc_viewer_recenter_camera(v)
        updateTitle()
    }

    override func mouseMoved(with event: NSEvent) {
        guard captured, let v = viewer else { return }
        if skipMove { skipMove = false; return }
        tc_viewer_mouse_look(v, Float(event.deltaX), Float(event.deltaY))
    }

    override func scrollWheel(with event: NSEvent) {
        guard !inPlayback || playbackPaused, let v = viewer else { return }
        // Play mode: camera zoom; free-fly: fly speed.
        tc_viewer_wheel(v, Float(event.scrollingDeltaY) * (event.hasPreciseScrollingDeltas ? 0.1 : 1))
    }

    override func keyDown(with event: NSEvent) {
        if inPlayback, event.keyCode != 46, event.keyCode != 122 {  // M mutes and F1 shows the controls, as everywhere
            if event.keyCode == 53 {  // Esc
                if !event.isARepeat { onPlaybackBack?() }
            } else if event.keyCode == 35 {  // P
                if !event.isARepeat { onPlaybackPause?() }
            } else if playbackPaused, let v = viewer, let a = bindings.action(event.keyCode) {
                tc_viewer_set_action(v, a, true)
            }
            return
        }
        if event.keyCode == 53 { setCaptured(false); return }  // Esc
        if event.keyCode == 101 {  // F9: mark this moment in the recording
            if !event.isARepeat, Recording.shared.mark() { updateTitle() }
            return
        }
        if event.keyCode == 122 {  // F1: the controls (as Cmd-/ and the SDL build's F1)
            if !event.isARepeat { NSApp.sendAction(#selector(AppDelegate.showControls), to: nil, from: self) }
            return
        }
        if event.keyCode == 46, !event.isARepeat {  // M: mute/unmute music and sound effects
            Music.shared.muted.toggle()
            return
        }
        guard let v = viewer else { super.keyDown(with: event); return }
        if event.keyCode == 120 {  // F2: antialiasing off / 2x / 4x / 8x (as far as the GPU goes), kept for next time
            if !event.isARepeat, let r = renderer {
                try? r.setMSAA(r.nextMSAA)
                Renderer.preferredMSAA = r.msaa
                updateTitle()
            }
            return
        }
        if event.keyCode == 99 {  // F3: supersampling off / 1.5x / 2x (as the SDL build's F3), kept for next time
            if !event.isARepeat, let r = renderer {
                r.setScale(r.nextScale)
                Renderer.preferredScale = r.scale
                updateTitle()
            }
            return
        }
        if event.keyCode == 118 {  // F4: the debug menu (tc_debug.h; as the SDL build's F4)
            if !event.isARepeat { tc_viewer_toggle_debug(v) }
            return
        }
        if tc_viewer_debug_open(v) {  // the menu takes the held actions and nothing else
            if let a = bindings.action(event.keyCode) { tc_viewer_set_action(v, a, true) }
            return
        }
        if event.keyCode == 33 || event.keyCode == 30 {  // [ / ]: animation speed
            if !event.isARepeat { tc_viewer_step_speed(v, event.keyCode == 30 ? 1 : -1); updateTitle() }
            return
        }
        // 5 and 0, on the number row or the numpad: the original's camera keys (Numpad 5 close-up, Numpad 0 first person).
        if event.keyCode == 23 || event.keyCode == 87 || event.keyCode == 29 || event.keyCode == 82,
           !event.modifierFlags.contains(.option) {
            if !event.isARepeat {
                if event.keyCode == 29 || event.keyCode == 82 { tc_viewer_toggle_first_person(v) } else { tc_viewer_toggle_closeup(v) }
                updateTitle()
            }
            return
        }
        // Option + 1-9: hold a baked lighting set (sud0 and Land have three); Option + 0: the level's own light again
        // (sud0's day / night cycle, Land's lightning); - / =: display gamma.
        let digits: [UInt16: UInt32] = [18: 0, 19: 1, 20: 2, 21: 3, 23: 4, 22: 5, 26: 6, 28: 7, 25: 8]
        if event.modifierFlags.contains(.option), let n = digits[event.keyCode] {
            tc_viewer_set_light_set(v, n)
            updateTitle()
            return
        }
        if event.keyCode == 29, event.modifierFlags.contains(.option) {
            tc_viewer_follow_light(v)
            updateTitle()
            return
        }
        if event.keyCode == 27 || event.keyCode == 24 {
            tc_viewer_step_gamma(v, event.keyCode == 24)
            updateTitle()
            return
        }
        if event.keyCode == 34 {  // I: Ed's AI / simple controller
            if !event.isARepeat { tc_viewer_toggle_ai(v); updateTitle() }
            return
        }
        if event.keyCode == 48 {  // Tab: play as Ed / free-fly
            if !event.isARepeat { tc_viewer_toggle_play(v); updateTitle() }
            return
        }
        if event.keyCode == 43 || event.keyCode == 47 {  // , / .: field of view (vertical, 5° steps)
            tc_viewer_step_fov(v, event.keyCode == 47 ? 1 : -1)
            updateTitle()
            return
        }
        if event.keyCode == 4 {  // H: the HUD's auto-hide (kept across levels)
            if !event.isARepeat { HudSettings.toggle(v); updateTitle() }
            return
        }
        if event.keyCode == 38 {  // J: Dolby Surround (the original's sound menu option; kept across launches)
            if !event.isARepeat { Sfx.shared.dolby.toggle(); updateTitle() }
            return
        }
        if event.keyCode == 5 {  // G: level fog on/off
            if !event.isARepeat { tc_viewer_toggle_fog(v); updateTitle() }
            return
        }
        if event.keyCode == 37 {  // L: lighting: the original's / the port's earlier one (comparing)
            if !event.isARepeat { tc_viewer_cycle_lighting(v); updateTitle() }
            return
        }
        if event.keyCode == 40 {  // K: sectors drawn: Ed's and its list (default) / all / his alone
            if !event.isARepeat { tc_viewer_cycle_sectors(v); updateTitle() }
            return
        }
        if event.keyCode == 7 {  // X: recentre the camera behind Ed
            if !event.isARepeat { tc_viewer_recenter_camera(v); updateTitle() }
            return
        }
        if event.keyCode == 41 || event.keyCode == 39 || event.keyCode == 16 {  // ; ': look sensitivity, Y: invert up / down
            if !event.isARepeat {
                if event.keyCode == 16 { tc_viewer_toggle_invert_y(v) } else { tc_viewer_step_look(v, event.keyCode == 39 ? 1 : -1) }
                LookSettings.save(v)
                updateTitle()
            }
            return
        }
        if event.keyCode == 35 {  // P: pause/resume animation
            if !event.isARepeat { tc_viewer_toggle_pause(v); updateTitle() }
            return
        }
        if let a = bindings.action(event.keyCode) { tc_viewer_set_action(v, a, true) } else { super.keyDown(with: event) }
    }

    /// Live viewer settings appended to the window title.
    func updateTitle() {
        guard let viewer = renderer?.viewer, let w = window else { return }
        let base = w.title.components(separatedBy: "  ·  ").first ?? w.title
        let aa = renderer.map {
            ($0.msaa > 1 ? ", AA \($0.msaa)x" : ", AA off") + ($0.scale > 100 ? ", SS \($0.scale >= 200 ? "2x" : "1.5x")" : "")
                + ($0.viewer.map { tc_viewer_hud_auto_hide($0.raw) } == true ? ", HUD auto-hide" : "")
                + (Sfx.shared.dolby ? ", Dolby Surround" : "")
        } ?? ""
        let marks = Recording.shared.marks > 0 ? ", \(Recording.shared.marks) marks" : ""
        w.title = base + "  ·  " + viewer.status(extra: aa + marks + (playbackPaused ? ", paused" : ""))
    }

    override func keyUp(with event: NSEvent) {
        if let v = viewer, let a = bindings.action(event.keyCode) { tc_viewer_set_action(v, a, false) } else { super.keyUp(with: event) }
    }

    override func flagsChanged(with event: NSEvent) {
        if let v = viewer, let a = bindings.action(event.keyCode) { tc_viewer_set_action(v, a, event.modifierFlags.contains(.shift)) }
    }

    override func mouseDragged(with event: NSEvent) {
        if let v = viewer { tc_viewer_mouse_look(v, Float(event.deltaX), Float(event.deltaY)) }
    }
}

@MainActor final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var renderer: Renderer?
    private var gameView: GameView?
    private var titleTimer: Timer?  // refreshes the AI's behaviour/state in the title while playing
    /// The loading screen after the intro: Totalski opens when its bar is full (shown full for a tick first, as a portal's).
    private var nextLevel: (url: URL, screen: LoadingScreen, start: CFTimeInterval, full: Bool)?
    private var nextLevelTimer: Timer?
    /// Playing a recording back (the Replays menu, --replay [file]): Playback.
    private var playback: Playback?
    /// The original demos' round (the Replays menu's button): the tapes, and the next one to play.
    private var demoTapes: [URL] = []
    private var demoAt = 0
    /// Default data, first that exists: inside OpenTonic.app (Contents/Resources/data/ttlv, built by
    /// tools/install_macos.sh); files/out/ttlv in the working directory (`swift run` from the repo); files/out/ttlv in
    /// the source checkout this binary was built from (Xcode runs it from DerivedData, so re-exported levels show
    /// up without copying); out/ttlv in the working directory (older layout).
    private var levelFolder: URL = {
        let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent()  // Sources/OpenTonic
            .deletingLastPathComponent().deletingLastPathComponent()
        let candidates = [Bundle.main.resourceURL?.appendingPathComponent("data/ttlv"),
                          URL(fileURLWithPath: "files/out/ttlv"),
                          repo.appendingPathComponent("files/out/ttlv"),
                          URL(fileURLWithPath: "out/ttlv")]
        return candidates.compactMap { $0 }.first { FileManager.default.fileExists(atPath: $0.path) }
            ?? URL(fileURLWithPath: "files/out/ttlv")
    }()

    func applicationDidFinishLaunching(_ note: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 800),
                          styleMask: [.titled, .closable, .resizable, .miniaturizable], backing: .buffered, defer: false)
        window.collectionBehavior = [.fullScreenPrimary]
        window.center()

        // --replay [file]: plays a recording back (the newest without one); the rest as usual.
        var args = CommandLine.arguments
        var replay: URL?, wantReplay = false
        if let i = args.firstIndex(of: "--replay") {
            wantReplay = true
            args.remove(at: i)
            if i < args.count, args[i].contains(".\(TC_REC_EXTENSION)") { replay = URL(fileURLWithPath: args.remove(at: i)) }
            replay = replay ?? Recording.latest()
        }
        var isDir: ObjCBool = false
        if wantReplay, args.count > 1 { levelFolder = URL(fileURLWithPath: args[1]) }
        if wantReplay, startPlayback(replay) {
        } else if args.count > 1, FileManager.default.fileExists(atPath: args[1], isDirectory: &isDir), !isDir.boolValue {
            let url = URL(fileURLWithPath: args[1])
            levelFolder = url.deletingLastPathComponent()
            openLevel(url)
        } else {
            if args.count > 1 { levelFolder = URL(fileURLWithPath: args[1]) }
            showPicker()
        }
        // Dock icon: Ed's original icon on the sud0 water swirl, generated into files/out/icon by tools/make_icon.sh.
        let iconDir = levelFolder.deletingLastPathComponent().appendingPathComponent("icon")
        if let icon = NSImage(contentsOf: iconDir.appendingPathComponent("OpenTonic.png"))
            ?? NSImage(contentsOf: iconDir.appendingPathComponent("TonicViewer.png")) {  // the name before the rename
            NSApp.applicationIconImage = icon
        }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        print("Controls: WASD move, arrow keys look, R/F up/down, Shift fast; click to capture mouse-look, Esc to release; scroll = fly speed / play-mode zoom; , / . field of view; P pause animation; starts as Ed when the level has him, Tab toggles free-fly (Space jump); ⌘L level select; ⌃⌘F fullscreen")
    }

    func applicationWillTerminate(_ note: Notification) { Recording.shared.stop() }

    /// False (nothing opened) when the file can't be played.
    private func startPlayback(_ url: URL?) -> Bool {
        guard let url, let p = Playback(url: url) else {
            print("Can't play \(url?.path ?? "(no recording)")")
            let alert = NSAlert()
            alert.messageText = "This recording can't be played"
            alert.runModal()
            return false
        }
        print("Playing back \(url.path): \(p.segmentCount) levels")
        playback = p
        return playbackLevel(0)
    }

    /// out/demos/<Level>/DemoNNN.sav.
    private func findDemos() -> [URL] {
        let fm = FileManager.default, root = levelFolder.deletingLastPathComponent().appendingPathComponent("demos")
        let folders = (try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)) ?? []
        return folders.flatMap { (try? fm.contentsOfDirectory(at: $0, includingPropertiesForKeys: nil)) ?? [] }
            .filter { $0.pathExtension.lowercased() == "sav" }
    }

    /// The Replays menu's button: the original demos in a shuffled round, watched as a recording is.
    private func startDemos() {
        demoTapes = findDemos().shuffled()
        demoAt = 0
        guard !demoTapes.isEmpty else { return }
        playback = Playback()
        if !nextDemo() { backToReplays("no original demo could be played") }
    }

    /// The next tape that plays: its level opened and the tape on it. False: none of them does.
    @discardableResult private func nextDemo() -> Bool {
        guard let p = playback else { return false }
        for _ in demoTapes.indices {
            if demoAt >= demoTapes.count {  // a new round, not starting with the one just seen
                let last = demoTapes.last
                demoTapes.shuffle()
                if demoTapes.count > 1, demoTapes.first == last { demoTapes.swapAt(0, demoTapes.count - 1) }
                demoAt = 0
            }
            let tape = demoTapes[demoAt]
            demoAt += 1
            let name = tape.deletingLastPathComponent().lastPathComponent.lowercased()
            let files = (try? FileManager.default.contentsOfDirectory(at: levelFolder, includingPropertiesForKeys: nil)) ?? []
            guard let url = files.first(where: { $0.pathExtension.lowercased() == "ttlv"
                                                 && $0.deletingPathExtension().lastPathComponent.lowercased() == name }),
                  let data = try? Data(contentsOf: tape) else { continue }
            openLevel(url, seed: UInt32.random(in: 0...UInt32.max))
            guard let r = renderer, let v = r.viewer, v.level.url == url,
                  data.withUnsafeBytes({ tc_viewer_play_tape(v.raw, $0.baseAddress, $0.count) }) else {
                print("Can't play the demo \(tape.path) on its level")
                continue
            }
            print("Original demo: \(tape.path)")
            r.playback = p
            r.onPlaybackEnd = { [weak self] in
                guard let self else { return }
                if !self.nextDemo() { self.backToReplays("no original demo could be played") }
            }
            gameView?.onPlaybackBack = { [weak self] in self?.backToReplays("back to the list") }
            gameView?.onPlaybackPause = nil
            if let view = gameView { window.invalidateCursorRects(for: view) }  // the pointer goes (resetCursorRects)
            window.title = "Open Tonic — Original demo — \(v.level.name)"
            return true
        }
        return false
    }

    /// A playback's end, or Esc / Start in one: back to the list.
    private func backToReplays(_ why: String) {
        stopPlayback(why)
        showReplays()
    }

    /// The Replays menu (the level select's button, R).
    @MainActor @objc func showReplays() {
        stopPlayback("the Replays menu")
        leaveLevel()
        let screen = ReplaysScreen(frame: window.contentLayoutRect, folder: Recording.folder, hasDemos: !findDemos().isEmpty)
        screen.onBack = { [weak self] in self?.showPicker() }
        screen.onPlay = { [weak self] url in _ = self?.startPlayback(url) }
        screen.onDemos = { [weak self] in self?.startDemos() }
        window.contentView = screen
        window.makeFirstResponder(screen)
        window.title = "Open Tonic — Replays"
    }

    /// Out of the level on show, into the menu's music. The run ends here: what follows from a menu is another.
    private func leaveLevel() {
        Recording.shared.stop()
        Sfx.shared.stopAll()
        Music.shared.play(level: "menu", audioDir: levelFolder.deletingLastPathComponent().appendingPathComponent("audio"))
        gameView?.setCaptured(false)
        gameView?.isPaused = true
        gameView = nil
        renderer = nil
    }

    /// The level of segment i, as the recording's run opened it; false (the playback over) when it isn't here.
    @discardableResult private func playbackLevel(_ i: UInt32) -> Bool {
        guard let p = playback else { return false }
        let s = p.info(i), name = cString(s.level).lowercased()
        let folder = s.edition != 0 ? levelFolder.deletingLastPathComponent().appendingPathComponent("se/ttlv") : levelFolder
        let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        guard let url = files.first(where: { $0.pathExtension.lowercased() == "ttlv"
                                             && $0.deletingPathExtension().lastPathComponent.lowercased() == name }) else {
            noLevel(name)
            return false
        }
        openLevel(url, carry: s.has_carry ? s.carry : nil, seed: s.seed)
        guard let r = renderer, let v = r.viewer, v.level.url == url else {
            noLevel(name)
            return false
        }
        p.begin(i, v.raw)
        r.playback = p
        r.onPlaybackEnd = { [weak self] in
            guard let self, let p = self.playback else { return }
            if p.segment + 1 < p.segmentCount { self.playbackLevel(p.segment + 1) } else { self.backToReplays("the end of the recording") }
        }
        gameView?.onPlaybackBack = { [weak self] in self?.backToReplays("back to the list") }
        gameView?.onPlaybackPause = { [weak self] in self?.togglePlaybackPause() }
        if let view = gameView { window.invalidateCursorRects(for: view) }  // the pointer goes (resetCursorRects)
        window.title = "Open Tonic — Playback — \(v.level.name)"
        return true
    }

    /// P, Cross: the frames wait and the mouse looks, or the run goes on.
    private func togglePlaybackPause() {
        guard let p = playback else { return }
        p.togglePause()
        gameView?.setCaptured(p.paused)
        gameView?.updateTitle()
    }

    /// The recording's next level can't be shown: the list again, saying why.
    private func noLevel(_ name: String) {
        backToReplays("no level \(name)")
        let alert = NSAlert()
        alert.messageText = "No level \(name)"
        alert.informativeText = "This recording goes to \(name), which is not here or doesn't load."
        alert.runModal()
    }

    private func stopPlayback(_ why: String) {
        guard let p = playback else { return }
        print("Playback ended: \(why)")
        playback = nil
        demoTapes = []
        renderer?.playback = nil
        p.close()
    }

    @MainActor @objc func showPicker() {
        stopPlayback("the level select")
        leaveLevel()
        // Loading screens live next to the ttlv folder: out/ttlv -> out/Vignette/Level. The Special Edition's levels
        // (tools/ttre/se.py): out/se/ttlv, their pictures out/se/Vignette/Level.
        let out = levelFolder.deletingLastPathComponent()
        let thumbs = out.appendingPathComponent("Vignette/Level")
        let seFolder = out.appendingPathComponent("se/ttlv")
        // Back from a level: on its pane (its edition's, or the retail cutscenes').
        let fromSpecial = currentLevelFolder?.standardizedFileURL == seFolder.standardizedFileURL
        let fromCutscene = currentLevelName.map { tc_level_is_cutscene($0) != 0 } ?? false
        let picker = LevelPicker(folder: levelFolder, thumbnails: thumbs, seFolder: seFolder,
                                 seThumbnails: out.appendingPathComponent("se/Vignette/Level"),
                                 showSection: fromSpecial ? 1 : fromCutscene ? 2 : 0) { [weak self] url in
            self?.openLevel(url)
        }
        picker.onIntro = { [weak self] in self?.playIntro() }
        picker.onReplays = { [weak self] in self?.showReplays() }
        if picker.isEmpty {
            fail("No .ttlv levels in \(levelFolder.path). Please run your genuine installed Tonic Trouble files through the conversion step.")
        }
        let scroll = NSScrollView(frame: window.contentLayoutRect)
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.verticalScrollElasticity = .none  // the page fits the window: the wheel at a pane's end mustn't bounce it
        scroll.drawsBackground = true
        scroll.backgroundColor = NSColor(calibratedRed: 0.08, green: 0.09, blue: 0.14, alpha: 1)
        scroll.documentView = picker
        scroll.contentView.postsFrameChangedNotifications = true
        NotificationCenter.default.addObserver(forName: NSView.frameDidChangeNotification, object: scroll.contentView, queue: .main) { [weak picker, weak scroll] _ in
            MainActor.assumeIsolated {  // queue: .main
                if let picker, let scroll { picker.relayout(size: scroll.contentView.bounds.size) }
            }
        }
        // Container so the Controls button floats in the top-right corner above the scrolling grid; it also takes V (intro).
        let container = PickerContainer(frame: window.contentLayoutRect)
        container.appearance = NSAppearance(named: .darkAqua)  // a dark screen in light mode too (readable text)
        container.onIntro = { [weak self] in self?.playIntro() }
        container.onSlide = { [weak picker] dir in picker?.slide(dir) }
        scroll.frame = container.bounds
        scroll.autoresizingMask = [.width, .height]
        container.addSubview(scroll)
        picker.relayout(size: scroll.contentView.bounds.size)
        let help = NSButton(title: "Controls", target: self, action: #selector(showControls))
        help.bezelStyle = .rounded
        help.toolTip = "Keyboard and mouse controls (F1, Cmd-/)"
        help.sizeToFit()
        help.frame.origin = NSPoint(x: container.bounds.width - help.frame.width - 16, y: container.bounds.height - help.frame.height - 12)
        help.autoresizingMask = [.minXMargin, .minYMargin]
        container.addSubview(help)
        window.contentView = container
        window.makeFirstResponder(container)
        window.title = "Open Tonic"
    }

    /// The intro movie, from the level select screen (V). files/out/video/intro.mp4 next to the ttlv folder.
    @MainActor func playIntro() {
        let url = levelFolder.deletingLastPathComponent().appendingPathComponent("video/intro.mp4")
        guard FileManager.default.fileExists(atPath: url.path) else {
            let alert = NSAlert()
            alert.messageText = "No intro movie"
            alert.informativeText = "\(url.path) is missing. Build it from the disc's Videos/EDPROL.AVI (docs/BUILD.md, \"Intro video\")."
            alert.runModal()
            return
        }
        Music.shared.stop()
        // As the original: when the intro ends, or is skipped, the game starts on the ski slope.
        let view = IntroView(frame: window.contentLayoutRect, url: url) { [weak self] in self?.startAfterIntro() }
        window.contentView = view
        window.makeFirstResponder(view)
        window.title = "Open Tonic — intro"
        view.start()
    }

    /// After the intro (watched or skipped): the retail Totalski, as the original game, after its loading screen (as a
    /// red portal's, on black); the level select when it isn't converted. (Until 2026-11-14 a coin toss between retail
    /// and the Special Edition: the user's stopgap, retired; the Special Edition's levels are an optional extension.)
    func startAfterIntro() {
        let out = levelFolder.deletingLastPathComponent()
        let files = (try? FileManager.default.contentsOfDirectory(at: levelFolder, includingPropertiesForKeys: nil)) ?? []
        guard let ski = files.first(where: { $0.lastPathComponent.lowercased() == "totalski.ttlv" }) else {
            showPicker()
            return
        }
        let backdrop = LoadingBackdrop(frame: window.contentLayoutRect)
        backdrop.wantsLayer = true
        backdrop.layer?.backgroundColor = NSColor.black.cgColor
        // The edition's vignette, as in openLevel.
        let own = ski.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("Vignette/Level")
        let screen = LoadingScreen(frame: backdrop.bounds,
                                   vignettes: FileManager.default.fileExists(atPath: own.path) ? own
                                       : out.appendingPathComponent("Vignette/Level"))
        backdrop.addSubview(screen)
        screen.show(level: ski.deletingPathExtension().lastPathComponent, progress: 0)
        window.contentView = backdrop
        window.makeFirstResponder(backdrop)
        window.title = "Open Tonic"
        nextLevel = (ski, screen, CACurrentMediaTime(), false)
        nextLevelTimer?.invalidate()
        nextLevelTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.nextLevelTick() }
        }
    }

    private func nextLevelTick() {
        // Gone when something else took the window meanwhile (the level select, Cmd-L).
        guard let next = nextLevel, window.contentView === next.screen.superview else {
            nextLevel = nil
            nextLevelTimer?.invalidate()
            nextLevelTimer = nil
            return
        }
        if next.full {
            nextLevel = nil
            nextLevelTimer?.invalidate()
            nextLevelTimer = nil
            openLevel(next.url)
            if renderer == nil { showPicker() }  // it didn't load (openLevel showed why)
            return
        }
        let progress = min(1, (CACurrentMediaTime() - next.start) / Double(TC_VIEWER_LOADING_SECONDS))
        next.screen.show(level: next.url.deletingPathExtension().lastPathComponent, progress: Float(progress))
        nextLevel?.full = progress >= 1
    }

    @objc func showControls() {
        gameView?.setCaptured(false)
        let alert = NSAlert()
        alert.messageText = "Open Tonic Controls"
        alert.informativeText = "Coloured keys are bound to their respective functions\n\n"
            + "Gamepad: left stick moves Ed (push further to run) or flies, right stick turns the camera round him "
            + "(L2 recentres it, Triangle / Y close-up, L3 first person), R3 (click) free-fly / play. Cross / A jump, Square / X action, Circle / B shoot, "
            + "R1 lateral (free-fly: up), L1 free-fly down, R2 fly faster, D-pad up / down camera distance or fly speed, "
            + "Start level select, Select / Options these controls; the debug menu is F4 on the keyboard."
        alert.accessoryView = ControlsView(frame: .zero)
        alert.runModal()
    }

    /// The folder of the level shown (the retail levels', or the Special Edition's out/se/ttlv): a portal's level
    /// change stays in it, so the Special Edition's arches lead to its own levels.
    private var currentLevelFolder: URL?
    /// Its file name without the extension (the level select opens on the Cutscenes pane after a cutscene level).
    private var currentLevelName: String?

    /// A red portal's level change: the .ttlv of that name (any case) in the shown level's folder, entered with the carry.
    func openPortalLevel(_ carry: tc_viewer_carry) {
        // A custom-layer portal names its edition ("se:sud0", "retail:sud0"; tools/ttre/custom.py).
        let parts = cString(carry.level).split(separator: ":", maxSplits: 1).map(String.init)
        let name = (parts.last ?? "").lowercased()  // the custom layer's names keep their case (LibertyCity2001, SSX_Alaska)
        let folder = parts.count < 2 ? (currentLevelFolder ?? levelFolder)
            : parts[0] == "se" ? levelFolder.deletingLastPathComponent().appendingPathComponent("se/ttlv") : levelFolder
        let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        guard let url = files.first(where: { $0.pathExtension.lowercased() == "ttlv"
                                             && $0.deletingPathExtension().lastPathComponent.lowercased() == name }) else {
            let alert = NSAlert()
            alert.messageText = "No level \(name)"
            alert.informativeText = "The portal leads to \(name), which is not in \(folder.path)."
            alert.runModal()
            showPicker()
            return
        }
        openLevel(url, carry: carry)
    }

    func openLevel(_ url: URL, carry: tc_viewer_carry? = nil, seed: UInt32 = 0) {
        currentLevelFolder = url.deletingLastPathComponent()
        currentLevelName = url.deletingPathExtension().lastPathComponent
        // The old view may hold the mouse (cursor hidden and detached): release it, and capture again in the new view so
        // a portal's level change keeps playing (and Esc still releases it).
        let wasCaptured = gameView?.isCaptured ?? false
        gameView?.setCaptured(false)
        Sfx.shared.stopAll()
        // Sound effects and the user-edited music table out/audio/music.tsv live next to out/ttlv; the Special Edition's
        // levels (out/se/ttlv) have none of their own and use those (their names are the retail levels').
        let audio = levelFolder.deletingLastPathComponent().appendingPathComponent("audio")
        Sfx.shared.load(audioDir: audio)
        Music.shared.play(level: url.deletingPathExtension().lastPathComponent, audioDir: audio)
        let view = GameView(frame: window.contentLayoutRect, device: MTLCreateSystemDefaultDevice())
        do {
            let r = try Renderer(view: view)
            // No carry: from a menu, a new run; a portal's level goes on in this one. A playback isn't a run: the file is
            // the record.
            if carry == nil, playback == nil { Recording.shared.start() }
            let loaded = try Viewer(level: Level(url: url), carry: carry, seed: seed,
                                    recorder: playback == nil ? Recording.shared.recorder : nil)
            LookSettings.apply(loaded.raw)
            HudSettings.apply(loaded.raw)
            r.load(loaded)
            r.onLoading = { [weak view] in view?.showLoading($0) }
            r.onPicture = { [weak view] in view?.showPicture($0) }
            r.onPendingLevel = { [weak self] in self?.openPortalLevel($0) }
            r.onGamepad = { [weak self, weak view] command in
                if self?.playback != nil {  // Start: back to the list, Cross: pause; the others are the recording's
                    if command == .levelSelect { self?.backToReplays("back to the list") }
                    if command == .pausePlayback { self?.togglePlaybackPause() }
                    return
                }
                switch command {
                case .mark:
                    if Recording.shared.mark() { view?.updateTitle() }
                case .levelSelect: self?.showPicker()
                case .controls: self?.showControls()
                case .togglePlay:
                    if let v = view?.renderer?.viewer?.raw {
                        tc_viewer_toggle_play(v)
                        view?.updateTitle()
                    }
                case .firstPerson:
                    if let v = view?.renderer?.viewer?.raw {
                        tc_viewer_toggle_first_person(v)
                        view?.updateTitle()
                    }
                case .recenter:
                    if let v = view?.renderer?.viewer?.raw {
                        tc_viewer_recenter_camera(v)
                        view?.updateTitle()
                    }
                case .hudAutoHide:  // as H on the keyboard
                    if let v = view?.renderer?.viewer?.raw {
                        HudSettings.toggle(v)
                        view?.updateTitle()
                    }
                case .closeup:  // as 5 on the keyboard, which the debug menu takes while it is open
                    if let v = view?.renderer?.viewer?.raw, !tc_viewer_debug_open(v) {
                        tc_viewer_toggle_closeup(v)
                        view?.updateTitle()
                    }
                case .press, .pausePlayback: break
                }
            }
            renderer = r
        } catch {
            let alert = NSAlert()
            alert.messageText = "Could not load \(url.lastPathComponent)"
            alert.informativeText = "\(error)"
            alert.runModal()
            return
        }
        view.renderer = renderer
        view.delegate = renderer
        // Its portals' loading screens: next to its own folder (the Special Edition's out/se/Vignette/Level for its
        // levels), else the retail ones.
        let own = url.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("Vignette/Level")
        view.vignettes = FileManager.default.fileExists(atPath: own.path) ? own
            : levelFolder.deletingLastPathComponent().appendingPathComponent("Vignette/Level")
        gameView = view
        window.contentView = view
        window.makeFirstResponder(view)
        // Mouse look from the start (not in a playback, and only while the app is in front: capturing detaches the
        // pointer system-wide); Esc releases it, a click takes it again.
        if (wasCaptured || NSApp.isActive) && playback == nil { view.setCaptured(true) }
        let r = renderer!, viewer = r.viewer!, s = viewer.stats
        window.title = "Open Tonic — \(viewer.level.name)"
        view.updateTitle()  // play mode is on from the start when the level has Ed
        titleTimer?.invalidate()
        titleTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak view] _ in
            MainActor.assumeIsolated {
                if let v = view, v.renderer?.viewer?.statusLive == true { v.updateTitle() }
            }
        }
        print("Loaded \(url.lastPathComponent): \(s.meshes) meshes, \(s.persos) persos (\(s.parts) parts), \(s.triangles) triangles, \(r.textureCount) textures, \(r.missingTextures) missing")
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ app: NSApplication) -> Bool { true }

    private func fail(_ message: String) -> Never {
        FileHandle.standardError.write(Data((message + "\n").utf8))
        exit(1)
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegate = AppDelegate()
let menu = NSMenu(), appItem = NSMenuItem()
menu.addItem(appItem)
let appMenu = NSMenu()
appMenu.addItem(withTitle: "Level Select", action: #selector(AppDelegate.showPicker), keyEquivalent: "l").target = delegate
appMenu.addItem(withTitle: "Controls", action: #selector(AppDelegate.showControls), keyEquivalent: "/").target = delegate
appMenu.addItem(withTitle: "Toggle Full Screen", action: #selector(NSWindow.toggleFullScreen(_:)), keyEquivalent: "f")
    .keyEquivalentModifierMask = [.control, .command]
appMenu.addItem(withTitle: "Quit", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
appItem.submenu = appMenu
app.mainMenu = menu
app.delegate = delegate
app.run()
