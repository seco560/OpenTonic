import AppKit
import MetalKit
import TonicCore

/// TonicViewer: level viewer and barebones collision tester; prototype / MVP of a native macOS port.
final class GameView: MTKView {
    var renderer: Renderer?
    private let bindings = KeyBindings()
    override var acceptsFirstResponder: Bool { true }

    /// The core's viewer state of the open level (tc_viewer.h).
    private var viewer: UnsafeMutablePointer<tc_viewer>? { renderer?.viewer?.raw }

    /// Mouse-look capture (viewer convenience, not part of the original controls):
    /// click to capture, move to look, Esc to release.
    private var captured = false

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

    override func mouseDown(with event: NSEvent) { setCaptured(true) }

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
        if event.keyCode == 46, !event.isARepeat {  // M: mute/unmute level music
            Music.shared.muted.toggle()
            return
        }
        guard let v = viewer else { super.keyDown(with: event); return }
        if event.keyCode == 33 || event.keyCode == 30 {  // [ / ]: animation speed
            if !event.isARepeat { tc_viewer_step_speed(v, event.keyCode == 30 ? 1 : -1); updateTitle() }
            return
        }
        // 1-9: baked lighting set (day/night variants, only some levels); - / =: display gamma.
        let digits: [UInt16: UInt32] = [18: 0, 19: 1, 20: 2, 21: 3, 23: 4, 22: 5, 26: 6, 28: 7, 25: 8]
        if let n = digits[event.keyCode] {
            tc_viewer_set_light_set(v, n)
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
        if event.keyCode == 35 {  // P: pause/resume animation
            if !event.isARepeat { tc_viewer_toggle_pause(v) }
            return
        }
        if let a = bindings.action(event.keyCode) { tc_viewer_set_action(v, a, true) } else { super.keyDown(with: event) }
    }

    /// Live viewer settings appended to the window title.
    func updateTitle() {
        guard let viewer = renderer?.viewer, let w = window else { return }
        let base = w.title.components(separatedBy: "  ·  view:").first ?? w.title
        w.title = base + "  ·  " + viewer.status
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
    /// Default data, first that exists: inside TonicViewer.app (Contents/Resources/data/ttlv, built by
    /// tools/make_app.sh); files/out/ttlv in the working directory (`swift run` from the repo); files/out/ttlv in
    /// the source checkout this binary was built from (Xcode runs it from DerivedData, so re-exported levels show
    /// up without copying); out/ttlv in the working directory (older layout).
    private var levelFolder: URL = {
        let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent()  // Sources/TonicViewer
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
        if let icon = NSImage(contentsOf: levelFolder.deletingLastPathComponent().appendingPathComponent("icon/TonicViewer.png")) {
            NSApp.applicationIconImage = icon
        }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        print("Controls: WASD move, arrow keys look, R/F up/down, Shift fast; click to capture mouse-look, Esc to release; scroll = fly speed / play-mode zoom; , / . field of view; P pause animation; starts as Ed when the level has him, Tab toggles free-fly (Space jump); ⌘L level select; ⌃⌘F fullscreen")
    }

    @MainActor @objc func showPicker() {
        Music.shared.play(level: "menu", audioDir: levelFolder.deletingLastPathComponent().appendingPathComponent("audio"))
        gameView?.setCaptured(false)
        gameView?.isPaused = true
        gameView = nil
        renderer = nil
        // Loading screens live next to the ttlv folder: out/ttlv -> out/Vignette/Level.
        let thumbs = levelFolder.deletingLastPathComponent().appendingPathComponent("Vignette/Level")
        let picker = LevelPicker(folder: levelFolder, thumbnails: thumbs) { [weak self] url in self?.openLevel(url) }
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
        // Container so the "?" button floats in the top-right corner above the scrolling grid; it also takes V (intro).
        let container = PickerContainer(frame: window.contentLayoutRect)
        container.onIntro = { [weak self] in self?.playIntro() }
        scroll.frame = container.bounds
        scroll.autoresizingMask = [.width, .height]
        container.addSubview(scroll)
        let help = NSButton(title: "?", target: self, action: #selector(showControls))
        help.bezelStyle = .helpButton
        help.title = ""
        help.toolTip = "Controls"
        help.sizeToFit()
        help.frame.origin = NSPoint(x: container.bounds.width - help.frame.width - 16, y: container.bounds.height - help.frame.height - 12)
        help.autoresizingMask = [.minXMargin, .minYMargin]
        container.addSubview(help)
        window.contentView = container
        window.makeFirstResponder(container)
        window.title = "TonicViewer  ·  V: intro movie"
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
        let view = IntroView(frame: window.contentLayoutRect, url: url) { [weak self] in self?.showPicker() }
        window.contentView = view
        window.makeFirstResponder(view)
        window.title = "TonicViewer — intro  ·  Esc skips"
        view.start()
    }

    @objc func showControls() {
        gameView?.setCaptured(false)
        let alert = NSAlert()
        alert.messageText = "TonicViewer controls"
        alert.informativeText = """
        Move:  W A S D  (W/S fly where the camera looks)
        Up / down:  R / F
        Look:  arrow keys, or click to capture the mouse (Esc releases), or drag
        Faster:  hold Shift  ·  scroll wheel: fly speed (free-fly) / camera zoom (play mode)
        Field of view:  , narrower  ·  . wider

        Play as Ed:  the default; Tab toggles free-fly, fly somewhere and press Tab again to drop Ed there
          Move:  W A S D, relative to the camera  ·  Jump:  Space
          With Ed's AI (levels with a .ttai, the default; I switches to the simple controller):
            Ed runs by default, hold Shift to walk  ·  E stick (Action)  ·  Q Shoot  ·  C Lateral
          Simple controller:  hold Shift to run
          Camera:  arrow keys or mouse orbit around Ed
          (simple collision and physics, not the original game's)

        Animation:  P pause / resume  ·  [ slower  ·  ] faster
        Lighting variant:  1, 2, 3 …  (levels with day/night lighting, e.g. South Plain)
        Gamma:  - darker  ·  = brighter  ·  Fog:  G on / off (levels with fog, e.g. north, Totalski)
        (current speed, gamma, field of view, lighting variant and play mode are shown in the title bar)

        Music:  M mute / unmute  (level → track table: out/audio/music.tsv)
        Intro movie:  V on the level select screen (Esc skips)
        Level select:  ⌘L  ·  Controls:  ⌘/  ·  Full screen:  ⌃⌘F  ·  Quit:  ⌘Q
        """
        alert.runModal()
    }

    func openLevel(_ url: URL) {
        // Level music from the user-edited table out/audio/music.tsv (next to out/ttlv).
        Music.shared.play(level: url.deletingPathExtension().lastPathComponent,
                          audioDir: url.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("audio"))
        let view = GameView(frame: window.contentLayoutRect, device: MTLCreateSystemDefaultDevice())
        do {
            let r = try Renderer(view: view)
            r.load(try Viewer(level: Level(url: url)))
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
        gameView = view
        window.contentView = view
        window.makeFirstResponder(view)
        let r = renderer!, viewer = r.viewer!, s = viewer.stats
        window.title = "TonicViewer — \(viewer.level.name)  ·  \(s.meshes) meshes, \(s.persos) animated objects, \(s.triangles) tris, \(r.textureCount) textures (\(r.missingTextures) missing)  ·  P pause  ·  ⌘L level select\(viewer.canPlay ? "  ·  Tab: play as Ed" : "")"
        view.updateTitle()  // play mode is on from the start when the level has Ed
        titleTimer?.invalidate()
        titleTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak view] _ in
            MainActor.assumeIsolated {
                if let v = view, v.renderer?.viewer?.aiActive == true { v.updateTitle() }
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
