import AppKit
import MetalKit

/// TonicViewer: level viewer and barebones collision tester; prototype / MVP of a native macOS port.
final class GameView: MTKView {
    var renderer: Renderer?
    override var acceptsFirstResponder: Bool { true }

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
            self?.setCaptured(false)
        }
    }

    override func mouseDown(with event: NSEvent) { setCaptured(true) }

    override func mouseMoved(with event: NSEvent) {
        if captured { renderer?.input.mouseDelta += SIMD2(Float(event.deltaX), Float(event.deltaY)) }
    }

    override func scrollWheel(with event: NSEvent) {
        guard let r = renderer else { return }
        let step = pow(1.1, Float(event.scrollingDeltaY) * (event.hasPreciseScrollingDeltas ? 0.1 : 1))
        if r.playing {
            r.orbitDistance = max(1.5, min(25, r.orbitDistance / step))  // play mode: camera zoom
        } else {
            r.input.baseSpeed = max(0.5, min(500, r.input.baseSpeed * step))  // free-fly: fly speed
        }
    }

    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { setCaptured(false); return }  // Esc
        if event.keyCode == 33 || event.keyCode == 30, !event.isARepeat, let r = renderer {  // [ / ]: animation speed
            let steps: [Float] = [0.25, 1.0 / 3, 0.5, 2.0 / 3, 0.75, 1, 1.5, 2]
            let i = steps.firstIndex { $0 >= r.animationSpeed - 0.001 } ?? 5
            r.animationSpeed = steps[max(0, min(steps.count - 1, i + (event.keyCode == 30 ? 1 : -1)))]
            updateTitle()
            return
        }
        // 1-9: baked lighting set (day/night variants, only some levels); - / =: display gamma.
        let digits: [UInt16: UInt32] = [18: 0, 19: 1, 20: 2, 21: 3, 23: 4, 22: 5, 26: 6, 28: 7, 25: 8]
        if let n = digits[event.keyCode], let r = renderer {
            if n < r.lightSetCount { r.lightSet = n }
            updateTitle()
            return
        }
        if event.keyCode == 27 || event.keyCode == 24, let r = renderer {
            r.gamma = max(0.5, min(2.5, r.gamma * (event.keyCode == 24 ? 1.1 : 1 / 1.1)))
            updateTitle()
            return
        }
        if event.keyCode == 48 {  // Tab: play as Ed / free-fly
            if !event.isARepeat, let r = renderer { r.togglePlay(); updateTitle() }
            return
        }
        if event.keyCode == 43 || event.keyCode == 47, let r = renderer {  // , / .: field of view (vertical, 5° steps)
            let deg = (r.camera.verticalFOV * 180 / .pi).rounded() + (event.keyCode == 47 ? 5 : -5)
            r.camera.verticalFOV = max(30, min(100, deg)) * .pi / 180
            updateTitle()
            return
        }
        if event.keyCode == 46, !event.isARepeat {  // M: mute/unmute level music
            Music.shared.muted.toggle()
            return
        }
        if event.keyCode == 35 {  // P: pause/resume animation
            if !event.isARepeat, let r = renderer { r.paused.toggle() }
            return
        }
        if renderer?.input.key(event.keyCode, down: true) != true { super.keyDown(with: event) }
    }

    /// Live viewer settings appended to the window title.
    func updateTitle() {
        guard let r = renderer, let w = window else { return }
        let base = w.title.components(separatedBy: "  ·  view:").first ?? w.title
        var s = String(format: "  ·  view: anim %.2f×, gamma %.2f, FOV %.0f°", r.animationSpeed, r.gamma, r.camera.verticalFOV * 180 / .pi)
        if r.lightSetCount > 1 { s += ", lighting \(r.lightSet + 1)/\(r.lightSetCount)" }
        if r.playing { s += "  ·  PLAYING as Ed (Tab: free-fly)" }
        w.title = base + s
    }

    override func keyUp(with event: NSEvent) {
        if renderer?.input.key(event.keyCode, down: false) != true { super.keyUp(with: event) }
    }

    override func flagsChanged(with event: NSEvent) {
        _ = renderer?.input.key(event.keyCode, down: event.modifierFlags.contains(.shift))
    }

    override func mouseDragged(with event: NSEvent) {
        renderer?.input.mouseDelta += SIMD2(Float(event.deltaX), Float(event.deltaY))
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var renderer: Renderer?
    private var gameView: GameView?
    /// Default data: inside TonicViewer.app (Contents/Resources/data/ttlv, built by tools/make_app.sh),
    /// otherwise out/ttlv relative to the working directory (running from the repo).
    private var levelFolder: URL = {
        if let res = Bundle.main.resourceURL?.appendingPathComponent("data/ttlv"),
           FileManager.default.fileExists(atPath: res.path) { return res }
        return URL(fileURLWithPath: "out/ttlv")
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
        // Dock icon: Ed's original icon on the sud0 water swirl, generated into out/icon by tools/make_icon.sh.
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
            if let picker, let scroll { picker.relayout(width: scroll.contentView.bounds.width) }
        }
        picker.relayout(width: scroll.contentView.bounds.width)
        // Container so the "?" button floats in the top-right corner above the scrolling grid.
        let container = NSView(frame: window.contentLayoutRect)
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
        window.title = "TonicViewer"
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
          Move:  W A S D, relative to the camera  ·  Run:  hold Shift  ·  Jump:  Space
          Camera:  arrow keys or mouse orbit around Ed
          (simple collision and physics, not the original game's)

        Animation:  P pause / resume  ·  [ slower  ·  ] faster
        Lighting variant:  1, 2, 3 …  (levels with day/night lighting, e.g. South Plain)
        Gamma:  - darker  ·  = brighter
        (current speed, gamma, field of view, lighting variant and play mode are shown in the title bar)

        Music:  M mute / unmute  (level → track table: out/audio/music.tsv)
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
            r.load(try Level(url: url))
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
        let s = renderer!.stats
        window.title = "TonicViewer — \(url.deletingPathExtension().lastPathComponent)  ·  \(s.meshes) meshes, \(s.persos) animated objects, \(s.triangles) tris, \(s.textures) textures (\(s.missingTextures) missing)  ·  P pause  ·  ⌘L level select\(renderer!.player != nil ? "  ·  Tab: play as Ed" : "")"
        view.updateTitle()  // play mode is on from the start when the level has Ed
        print("Loaded \(url.lastPathComponent): \(s.meshes) meshes, \(s.persos) persos (\(s.parts) parts), \(s.triangles) triangles, \(s.textures) textures, \(s.missingTextures) missing")
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
