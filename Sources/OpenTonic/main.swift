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
        // out/Vignette/Level -> out/Textures/Sud0 (the same layout inside the app bundle)
        bar = RainbowBar(texture: vignettes.deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("Textures/Sud0/o_zarbi_auntxy.png"))
        super.init(frame: frame)
        image.imageScaling = .scaleProportionallyUpOrDown
        for v in [image, bar] as [NSView] { addSubview(v) }
        autoresizingMask = [.width, .height]
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    func show(level: String, progress: Float) {
        if level != self.level {
            self.level = level
            // Vignette names differ in case from the level names (Cock01.png / cock01).
            let files = (try? FileManager.default.contentsOfDirectory(at: vignettes, includingPropertiesForKeys: nil)) ?? []
            image.image = files.first { $0.deletingPathExtension().lastPathComponent.lowercased() == level.lowercased() }
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

/// The black behind the loading screen after the intro. Takes the keys (a loading screen can't be skipped), but M
/// mutes as in a level.
final class LoadingBackdrop: NSView {
    override var acceptsFirstResponder: Bool { true }
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
    /// In the demo: a key (but M), a click or a pad button ends it.
    var onDemoInput: (() -> Void)?
    private var inDemo: Bool { viewer.map { tc_viewer_demo($0) } ?? false }

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

    func setCaptured(_ on: Bool) {
        guard on != captured else { return }
        captured = on
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

    override func mouseDown(with event: NSEvent) {
        if inDemo { onDemoInput?(); return }
        setCaptured(true)
    }

    override func mouseMoved(with event: NSEvent) {
        if captured, let v = viewer { tc_viewer_mouse_look(v, Float(event.deltaX), Float(event.deltaY)) }
    }

    override func scrollWheel(with event: NSEvent) {
        guard let v = viewer else { return }
        // Play mode: camera zoom; free-fly: fly speed.
        tc_viewer_wheel(v, Float(event.scrollingDeltaY) * (event.hasPreciseScrollingDeltas ? 0.1 : 1))
    }

    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { setCaptured(false); return }  // Esc
        if event.keyCode == 122 {  // F1: the controls (as Cmd-/ and the SDL build's F1)
            if !event.isARepeat { NSApp.sendAction(#selector(AppDelegate.showControls), to: nil, from: self) }
            return
        }
        if event.keyCode == 46, !event.isARepeat {  // M: mute/unmute music and sound effects
            Music.shared.muted.toggle()
            return
        }
        if inDemo {
            if !event.isARepeat { onDemoInput?() }
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
        if event.keyCode == 33 || event.keyCode == 30 {  // [ / ]: animation speed
            if !event.isARepeat { tc_viewer_step_speed(v, event.keyCode == 30 ? 1 : -1); updateTitle() }
            return
        }
        // 1-9: hold a baked lighting set (sud0 and Land have three); 0: the level's own light again (sud0's day /
        // night cycle, Land's lightning); - / =: display gamma.
        let digits: [UInt16: UInt32] = [18: 0, 19: 1, 20: 2, 21: 3, 23: 4, 22: 5, 26: 6, 28: 7, 25: 8]
        if let n = digits[event.keyCode] {
            tc_viewer_set_light_set(v, n)
            updateTitle()
            return
        }
        if event.keyCode == 29 {
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
        if event.keyCode == 35 {  // P: pause/resume animation
            if !event.isARepeat { tc_viewer_toggle_pause(v) }
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
        } ?? ""
        w.title = base + "  ·  " + viewer.status(extra: aa)
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
    /// The demo (the level select's Demo button, D): random main levels, Ed wandering by himself (tc_viewer_set_demo),
    /// the next level every demoSeconds or through a red portal he walks into; a key, click or pad button ends it.
    private var demo = false
    private var demoTimer: Timer?
    private let demoSeconds: TimeInterval = 60
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

        let args = CommandLine.arguments
        var isDir: ObjCBool = false
        if args.count > 1, FileManager.default.fileExists(atPath: args[1], isDirectory: &isDir), !isDir.boolValue {
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

    @MainActor @objc func showPicker() {
        demo = false
        demoTimer?.invalidate()
        demoTimer = nil
        Sfx.shared.stopAll()
        Music.shared.play(level: "menu", audioDir: levelFolder.deletingLastPathComponent().appendingPathComponent("audio"))
        gameView?.setCaptured(false)
        gameView?.isPaused = true
        gameView = nil
        renderer = nil
        // Loading screens live next to the ttlv folder: out/ttlv -> out/Vignette/Level.
        let thumbs = levelFolder.deletingLastPathComponent().appendingPathComponent("Vignette/Level")
        let picker = LevelPicker(folder: levelFolder, thumbnails: thumbs) { [weak self] url in self?.openLevel(url) }
        picker.onIntro = { [weak self] in self?.playIntro() }
        picker.onDemo = { [weak self] in self?.startDemo() }
        if picker.isEmpty {
            fail("No .ttlv levels in \(levelFolder.path). Please run your genuine installed Tonic Trouble files through the conversion step.")
        }
        let scroll = NSScrollView(frame: window.contentLayoutRect)
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = true
        scroll.backgroundColor = NSColor(calibratedRed: 0.08, green: 0.09, blue: 0.14, alpha: 1)
        scroll.documentView = picker
        scroll.contentView.postsFrameChangedNotifications = true
        NotificationCenter.default.addObserver(forName: NSView.frameDidChangeNotification, object: scroll.contentView, queue: .main) { [weak picker, weak scroll] _ in
            MainActor.assumeIsolated {  // queue: .main
                if let picker, let scroll { picker.relayout(width: scroll.contentView.bounds.width) }
            }
        }
        picker.relayout(width: scroll.contentView.bounds.width)
        // Container so the Controls button floats in the top-right corner above the scrolling grid; it also takes V (intro).
        let container = PickerContainer(frame: window.contentLayoutRect)
        container.appearance = NSAppearance(named: .darkAqua)  // a dark screen in light mode too (readable text)
        container.onIntro = { [weak self] in self?.playIntro() }
        container.onDemo = { [weak self] in self?.startDemo() }
        scroll.frame = container.bounds
        scroll.autoresizingMask = [.width, .height]
        container.addSubview(scroll)
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

    /// The levels the demo picks from: the game's main ones (no cutscenes, no imported models), with Ed's AI (.ttai).
    private func demoLevels() -> [URL] {
        let fm = FileManager.default
        let imported = levelFolder.deletingLastPathComponent().appendingPathComponent("Imported")
        return ((try? fm.contentsOfDirectory(at: levelFolder, includingPropertiesForKeys: nil)) ?? []).filter { url in
            let name = url.deletingPathExtension().lastPathComponent
            return url.pathExtension.lowercased() == "ttlv" && tc_level_is_cutscene(name) == 0
                && !fm.fileExists(atPath: imported.appendingPathComponent(name).path)
                && fm.fileExists(atPath: url.deletingPathExtension().appendingPathExtension("ttai").path)
        }
    }

    @MainActor func startDemo() {
        demo = true
        nextDemoLevel()
    }

    /// Another random main level (not the one shown), in the demo.
    private func nextDemoLevel() {
        guard demo else { return }
        let all = demoLevels(), current = renderer?.viewer?.level.name.lowercased()
        guard let url = all.filter({ $0.deletingPathExtension().lastPathComponent.lowercased() != current }).randomElement()
                ?? all.first else {
            showPicker()
            return
        }
        openLevel(url)
        guard renderer != nil else { showPicker(); return }  // it didn't load (openLevel showed why)
        demoTimer?.invalidate()
        demoTimer = Timer.scheduledTimer(withTimeInterval: demoSeconds, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.nextDemoLevel() }
        }
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

    /// After the intro (watched or skipped): Totalski, as the original game, after its loading screen (as a red
    /// portal's, on black); the level select if it isn't converted.
    func startAfterIntro() {
        let files = (try? FileManager.default.contentsOfDirectory(at: levelFolder, includingPropertiesForKeys: nil)) ?? []
        guard let ski = files.first(where: { $0.lastPathComponent.lowercased() == "totalski.ttlv" }) else {
            showPicker()
            return
        }
        let backdrop = LoadingBackdrop(frame: window.contentLayoutRect)
        backdrop.wantsLayer = true
        backdrop.layer?.backgroundColor = NSColor.black.cgColor
        let screen = LoadingScreen(frame: backdrop.bounds,
                                   vignettes: levelFolder.deletingLastPathComponent().appendingPathComponent("Vignette/Level"))
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
            + "Gamepad: left stick moves Ed (push further to run) or flies, right stick turns the camera, R3 (click) "
            + "free-fly / play. Cross / A jump, Square / X action, Circle / B shoot, R1 lateral (free-fly: up), L1 "
            + "free-fly down, R2 fly faster, D-pad up / down camera distance or fly speed, Start level select, "
            + "Select / Options these controls."
        alert.accessoryView = ControlsView(frame: .zero)
        alert.runModal()
    }

    /// A red portal's level change: the .ttlv of that name (any case) in the levels folder, entered with the carry.
    func openPortalLevel(_ carry: tc_viewer_carry) {
        let name = cString(carry.level)
        let files = (try? FileManager.default.contentsOfDirectory(at: levelFolder, includingPropertiesForKeys: nil)) ?? []
        guard let url = files.first(where: { $0.pathExtension.lowercased() == "ttlv"
                                             && $0.deletingPathExtension().lastPathComponent.lowercased() == name }) else {
            let alert = NSAlert()
            alert.messageText = "No level \(name)"
            alert.informativeText = "The portal leads to \(name), which is not in \(levelFolder.path)."
            alert.runModal()
            showPicker()
            return
        }
        openLevel(url, carry: carry)
    }

    func openLevel(_ url: URL, carry: tc_viewer_carry? = nil) {
        // The old view may hold the mouse (cursor hidden and detached): release it, and capture again in the new view so
        // a portal's level change keeps playing (and Esc still releases it).
        let wasCaptured = gameView?.isCaptured ?? false
        gameView?.setCaptured(false)
        Sfx.shared.stopAll()
        Sfx.shared.load(audioDir: url.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("audio"))
        // Level music from the user-edited table out/audio/music.tsv (next to out/ttlv).
        Music.shared.play(level: url.deletingPathExtension().lastPathComponent,
                          audioDir: url.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("audio"))
        let view = GameView(frame: window.contentLayoutRect, device: MTLCreateSystemDefaultDevice())
        do {
            let r = try Renderer(view: view)
            r.load(try Viewer(level: Level(url: url), carry: carry))
            r.onLoading = { [weak view] in view?.showLoading($0) }
            r.onPendingLevel = { [weak self] in self?.openPortalLevel($0) }
            r.onGamepad = { [weak self, weak view] command in
                switch command {
                case .levelSelect: self?.showPicker()
                case .controls: self?.showControls()
                case .togglePlay:
                    if let v = view?.renderer?.viewer?.raw {
                        tc_viewer_toggle_play(v)
                        view?.updateTitle()
                    }
                case .press:
                    if let v = view?.renderer?.viewer?.raw, tc_viewer_demo(v) { self?.showPicker() }
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
        view.vignettes = levelFolder.deletingLastPathComponent().appendingPathComponent("Vignette/Level")
        gameView = view
        window.contentView = view
        window.makeFirstResponder(view)
        if wasCaptured { view.setCaptured(true) }
        let r = renderer!, viewer = r.viewer!, s = viewer.stats
        window.title = "Open Tonic — \(viewer.level.name)"
        if demo {  // through a red portal too: still the demo
            tc_viewer_set_demo(viewer.raw, true, UInt32.random(in: 1...UInt32.max))
            window.title = "Open Tonic — Demo — \(viewer.level.name)"
            view.onDemoInput = { [weak self] in self?.showPicker() }
        }
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
