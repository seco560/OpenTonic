import AppKit
import GameController
import MetalKit
import TonicCore

/// The main menu (tc_menu.h; docs/ENGINE.md "The menus"): the core keeps the screens and lays them out, this view draws
/// its layers (the two space pictures, uv per corner, repeating) and its letters (quads over the atlas, tinted), and
/// feeds it keys and the gamepad. menu.ttmn (tools/ttre/state.py) next to the levels has the fonts and pictures.
final class MenuView: MTKView, MTKViewDelegate {
    /// What the menu asked for: the command, the game's index (load / delete), the new game's name.
    var onCommand: ((tc_menu_command, Int, String) -> Void)?
    nonisolated(unsafe) let state: UnsafeMutablePointer<tc_menu>
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let repeatSampler: MTLSamplerState, clampSampler: MTLSamplerState
    private var atlas: MTLTexture?
    private var images: [MTLTexture?] = []
    /// Over a level (the game menu): the dim's white texel and the swirl's picture (TC_MENU_IMAGE_DIM / _SWIRL).
    private var white: MTLTexture?, swirl: MTLTexture?
    private var last = CACurrentMediaTime()
    private var pad = MenuPad()

    private struct Vertex { var position: SIMD2<Float>; var uv: SIMD2<Float>; var color: SIMD4<Float> }

    private static let shader = """
    #include <metal_stdlib>
    using namespace metal;
    struct MenuVertex { float2 position; float2 uv; float4 color; };
    struct MenuOut { float4 position [[position]]; float2 uv; float4 color; };
    vertex MenuOut menu_vertex(uint vid [[vertex_id]], const device MenuVertex *v [[buffer(0)]]) {
        MenuOut o;
        o.position = float4(v[vid].position.x * 2 - 1, 1 - v[vid].position.y * 2, 0, 1);
        o.uv = v[vid].uv;
        o.color = v[vid].color;
        return o;
    }
    fragment float4 menu_fragment(MenuOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]]) {
        return tex.sample(s, in.uv) * in.color;
    }
    """

    /// nil: no Metal device, or menu.ttmn doesn't load (the caller falls back to the levels grid). swirl: over a level
    /// (the game menu), the swirls' picture; the view is then see-through.
    init?(frame: NSRect, data: Data, options: tc_options, games: [tc_save_info], swirl swirlURL: URL? = nil) {
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else { return nil }
        let m = UnsafeMutablePointer<tc_menu>.allocate(capacity: 1)
        tc_menu_init(m)
        let loaded = data.withUnsafeBytes { tc_menu_load(m, $0.baseAddress, data.count) }
        guard loaded == TC_OK,
              let library = try? device.makeLibrary(source: MenuView.shader, options: nil) else {
            print("menu.ttmn: \(String(cString: tc_result_string(loaded)))")
            tc_menu_free(m)
            m.deallocate()
            return nil
        }
        state = m
        let d = MTLRenderPipelineDescriptor()
        d.vertexFunction = library.makeFunction(name: "menu_vertex")
        d.fragmentFunction = library.makeFunction(name: "menu_fragment")
        d.colorAttachments[0].pixelFormat = .bgra8Unorm
        d.colorAttachments[0].isBlendingEnabled = true
        d.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
        d.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
        d.colorAttachments[0].sourceAlphaBlendFactor = .one
        d.colorAttachments[0].destinationAlphaBlendFactor = .oneMinusSourceAlpha
        guard let p = try? device.makeRenderPipelineState(descriptor: d) else { return nil }
        pipeline = p
        let s = MTLSamplerDescriptor()
        s.minFilter = .linear
        s.magFilter = .linear
        s.sAddressMode = .repeat
        s.tAddressMode = .repeat
        repeatSampler = device.makeSamplerState(descriptor: s)!
        s.sAddressMode = .clampToEdge
        s.tAddressMode = .clampToEdge
        clampSampler = device.makeSamplerState(descriptor: s)!
        self.queue = queue
        super.init(frame: frame, device: device)
        colorPixelFormat = .bgra8Unorm
        clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
        autoresizingMask = [.width, .height]
        delegate = self
        let md = state.pointee.data
        atlas = texture(md.atlas, Int(md.atlas_width), Int(md.atlas_height))
        for k in 0..<Int(md.image_count) {
            let im = md.images[k]
            images.append(texture(im.rgba, Int(im.width), Int(im.height)))
        }
        tc_menu_set_options(state, [options])
        setGames(games)
        var one: [UInt8] = [255, 255, 255, 255]
        white = texture(&one, 1, 1)
        if let swirlURL {
            swirl = try? MTKTextureLoader(device: device).newTexture(URL: swirlURL, options: [.SRGB: false])
        }
        if swirlURL != nil {  // see-through: the level shows under the dim
            clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 0)
            layer?.isOpaque = false
        }
    }

    required init(coder: NSCoder) { fatalError("not used") }

    deinit {
        tc_menu_free(state)
        state.deallocate()
    }

    private func texture(_ rgba: UnsafeMutablePointer<UInt8>?, _ w: Int, _ h: Int) -> MTLTexture? {
        guard let rgba, w > 0, h > 0, let device else { return nil }
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: w, height: h, mipmapped: false)
        let t = device.makeTexture(descriptor: d)
        t?.replace(region: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0, withBytes: rgba, bytesPerRow: w * 4)
        return t
    }

    func setGames(_ games: [tc_save_info]) {
        games.withUnsafeBufferPointer { tc_save_menu_games(state, $0.baseAddress, Int32($0.count)) }
    }

    func show(_ screen: tc_menu_screen) { tc_menu_show(state, screen) }

    override var acceptsFirstResponder: Bool { true }

    // The pointer: over a choice selects it, a click takes it, a right click goes back.
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect], owner: self))
    }

    private func viewport(_ event: NSEvent) -> (Float, Float) {
        let p = convert(event.locationInWindow, from: nil)
        return (Float(p.x / max(bounds.width, 1)), Float(1 - p.y / max(bounds.height, 1)))
    }

    override func mouseMoved(with event: NSEvent) {
        let (x, y) = viewport(event)
        _ = tc_menu_pointer(state, x, y)
    }

    override func mouseDown(with event: NSEvent) {
        let (x, y) = viewport(event)
        if tc_menu_click(state, x, y) { takeCommand() }
    }

    /// Held: a slider the click took hold of follows the pointer.
    override func mouseDragged(with event: NSEvent) {
        let (x, y) = viewport(event)
        if tc_menu_drag(state, x, y) { takeCommand() }
    }

    /// The wheel: the lists (OPTIONS, LOAD / DELETE GAME) a row at a time; a trackpad's small steps add up to rows.
    private var wheel: CGFloat = 0
    override func scrollWheel(with event: NSEvent) {
        wheel += event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 24 : (event.scrollingDeltaY > 0 ? 1 : event.scrollingDeltaY < 0 ? -1 : 0)
        let rows = Int(wheel)
        if rows != 0 {
            wheel -= CGFloat(rows)
            tc_menu_scroll(state, Int32(-rows))
        }
    }

    override func rightMouseDown(with event: NSEvent) { press(TC_MI_BACK) }

    private func press(_ input: tc_menu_input) {
        tc_menu_input_press(state, input)
        takeCommand()
    }

    private func takeCommand() {
        let c = tc_menu_take_command(state)
        guard c != TC_MC_NONE else { return }
        onCommand?(c, Int(state.pointee.command_game), String(cString: tc_menu_name(state)))
    }

    override func keyDown(with event: NSEvent) {
        let naming = state.pointee.screen == TC_MS_NEW_GAME
        switch event.keyCode {
        case 126: press(TC_MI_UP)
        case 125: press(TC_MI_DOWN)
        case 123: press(TC_MI_LEFT)
        case 124: press(TC_MI_RIGHT)
        case 36, 76: if !event.isARepeat { press(TC_MI_CONFIRM) }  // Return, Enter
        case 53: if !event.isARepeat { press(TC_MI_BACK) }         // Esc
        case 51, 117: press(TC_MI_ERASE)                           // Delete, forward delete
        default:
            if naming {
                for u in (event.characters ?? "").unicodeScalars { tc_menu_type(state, u.value) }
            } else if event.keyCode == 49, !event.isARepeat {  // Space
                press(TC_MI_CONFIRM)
            } else if event.keyCode == 13 || event.keyCode == 1 {  // W / S
                press(event.keyCode == 13 ? TC_MI_UP : TC_MI_DOWN)
            } else if event.keyCode == 0 || event.keyCode == 2 {   // A / D
                press(event.keyCode == 0 ? TC_MI_LEFT : TC_MI_RIGHT)
            } else if event.keyCode == 46, !event.isARepeat {      // M: mute, as everywhere
                Music.shared.muted.toggle()
            } else {
                super.keyDown(with: event)
            }
        }
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        let now = CACurrentMediaTime(), dt = Float(min(now - last, 0.25))
        last = now
        for input in pad.poll(dt: dt) { press(input) }
        tc_menu_update(state, dt)
        let size = drawableSize
        guard size.width > 0, size.height > 0 else { return }
        tc_menu_build(state, Float(size.width / size.height))
        guard let pass = currentRenderPassDescriptor, let drawable = currentDrawable,
              let cmd = queue.makeCommandBuffer(), let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }
        enc.setRenderPipelineState(pipeline)
        // the layers: a picture over the whole viewport, uv per corner (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right)
        var n: UInt32 = 0
        if let layers = tc_menu_layer_list(state, &n) {
            for k in 0..<Int(n) {
                let l = layers[k]
                let dim = Int(l.image) == Int(TC_MENU_IMAGE_DIM), swirled = Int(l.image) == Int(TC_MENU_IMAGE_SWIRL)
                guard let tex = dim ? white : swirled ? swirl : Int(l.image) < images.count ? images[Int(l.image)] : nil
                else { continue }
                let uv = withUnsafeBytes(of: l.uv) { Array($0.bindMemory(to: Float.self)) }
                let c = dim ? SIMD4<Float>(0, 0, 0, l.alpha) : SIMD4<Float>(1, 1, 1, l.alpha)
                let corner = { (i: Int, x: Float, y: Float) in Vertex(position: [x, y], uv: [uv[2 * i], uv[2 * i + 1]], color: c) }
                var verts = [corner(0, 0, 0), corner(1, 1, 0), corner(2, 0, 1), corner(1, 1, 0), corner(3, 1, 1), corner(2, 0, 1)]
                enc.setVertexBytes(&verts, length: MemoryLayout<Vertex>.stride * verts.count, index: 0)
                enc.setFragmentTexture(tex, index: 0)
                enc.setFragmentSamplerState(repeatSampler, index: 0)
                enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
            }
        }
        // the letters
        if let atlas, let quads = tc_menu_quad_list(state, &n), n > 0 {
            var verts: [Vertex] = []
            verts.reserveCapacity(Int(n) * 6)
            for k in 0..<Int(n) {
                let q = quads[k], r = q.rect, t = q.uv, c = SIMD4<Float>(q.color.0, q.color.1, q.color.2, q.color.3)
                let a = Vertex(position: [r.0, r.1], uv: [t.0, t.1], color: c), b = Vertex(position: [r.2, r.1], uv: [t.2, t.1], color: c)
                let d = Vertex(position: [r.0, r.3], uv: [t.0, t.3], color: c), e = Vertex(position: [r.2, r.3], uv: [t.2, t.3], color: c)
                verts += [a, b, d, b, e, d]
            }
            if let buffer = device?.makeBuffer(bytes: verts, length: MemoryLayout<Vertex>.stride * verts.count,
                                               options: .storageModeShared) {
                enc.setVertexBuffer(buffer, offset: 0, index: 0)
                enc.setFragmentTexture(atlas, index: 0)
                enc.setFragmentSamplerState(clampSampler, index: 0)
                enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: verts.count)
            }
        }
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }
}

/// The gamepad in the menu: the D-pad or the left stick move (held: repeating), Cross / A confirms, Circle / B goes back,
/// Triangle / Y erases (a letter, a game), Start confirms.
struct MenuPad {
    private var pressed: [ObjectIdentifier: Bool] = [:]
    private var held: tc_menu_input?
    private var wait: Float = 0

    mutating func poll(dt: Float) -> [tc_menu_input] {
        guard let pad = GCController.current?.extendedGamepad else { return [] }
        var out: [tc_menu_input] = []
        let x = pad.leftThumbstick.xAxis.value, y = pad.leftThumbstick.yAxis.value
        let dir: tc_menu_input? = pad.dpad.up.isPressed || y > 0.6 ? TC_MI_UP
            : pad.dpad.down.isPressed || y < -0.6 ? TC_MI_DOWN
            : pad.dpad.left.isPressed || x < -0.6 ? TC_MI_LEFT
            : pad.dpad.right.isPressed || x > 0.6 ? TC_MI_RIGHT : nil
        if dir != held {
            held = dir
            wait = 0.4
            if let dir { out.append(dir) }
        } else if let dir {
            wait -= dt
            if wait <= 0 { wait = 0.12; out.append(dir) }
        }
        if changed(pad.buttonA) == true || changed(pad.buttonMenu) == true { out.append(TC_MI_CONFIRM) }
        if changed(pad.buttonB) == true { out.append(TC_MI_BACK) }
        if changed(pad.buttonY) == true { out.append(TC_MI_ERASE) }
        return out
    }

    private mutating func changed(_ button: GCControllerButtonInput) -> Bool? {
        let id = ObjectIdentifier(button), now = button.isPressed
        defer { pressed[id] = now }
        return pressed[id, default: false] != now ? now : nil
    }
}

/// The picture the retail game shows while it loads, just before its main menu: game.bpg names TT_Pak.bmp
/// (SNA_0045bd30 loads it as the vignette). Shown for three seconds on black, 4:3; a key, a click or the pad skips it.
final class SplashView: NSView {
    private let image = NSImageView()
    private var done: (() -> Void)?
    private var timer: Timer?

    init(frame: NSRect, picture: URL, seconds: Double, then: @escaping () -> Void) {
        done = then
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        autoresizingMask = [.width, .height]
        image.image = NSImage(contentsOf: picture)
        image.imageScaling = .scaleProportionallyUpOrDown
        addSubview(image)
        timer = Timer.scheduledTimer(withTimeInterval: seconds, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.finish() }
        }
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override var acceptsFirstResponder: Bool { true }
    override func resetCursorRects() { addCursorRect(bounds, cursor: .blank) }
    override func keyDown(with event: NSEvent) { if !event.isARepeat { finish() } }
    override func mouseDown(with event: NSEvent) { finish() }

    override func layout() {
        super.layout()
        let b = bounds, w = min(b.width, b.height * 4 / 3), h = w * 3 / 4
        image.frame = NSRect(x: (b.width - w) / 2, y: (b.height - h) / 2, width: w, height: h)
    }

    private func finish() {
        timer?.invalidate()
        timer = nil
        let d = done
        done = nil
        d?()
    }
}

/// Saved games (tc_save.h): one file per game, <NAME>.ttsv in ~/Library/Application Support/OpenTonic/Saves
/// (TONIC_SAVES: another folder).
@MainActor enum GameSaves {
    static var folder: URL? {
        if let env = ProcessInfo.processInfo.environment["TONIC_SAVES"], !env.isEmpty { return URL(fileURLWithPath: env) }
        return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first?
            .appendingPathComponent("OpenTonic/Saves")
    }

    /// Every readable save, newest first.
    static func list() -> [(url: URL, info: tc_save_info)] {
        guard let folder else { return [] }
        let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        var out: [(URL, tc_save_info)] = []
        for url in files where url.pathExtension == TC_SAVE_EXTENSION {
            guard let data = try? Data(contentsOf: url) else { continue }
            var info = tc_save_info()
            if data.withUnsafeBytes({ tc_save_read_info($0.baseAddress, data.count, &info) }) { out.append((url, info)) }
        }
        return out.sorted { $0.1.time > $1.1.time }
    }

    static func url(for name: String) -> URL? {
        var buf = [CChar](repeating: 0, count: 64)
        tc_save_file_name(name, &buf, buf.count)
        let file = String(decoding: buf.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
        return folder?.appendingPathComponent(file + ".\(TC_SAVE_EXTENSION)")
    }

    /// The game on show, saved under its name. False: nothing saved (no game to save there, or the file failed).
    @discardableResult static func save(_ v: UnsafeMutablePointer<tc_viewer>, name: String) -> Bool {
        guard let url = url(for: name) else { return false }
        var size = 0
        guard let bytes = tc_save_make(v, name, UInt64(Date().timeIntervalSince1970), &size) else { return false }
        defer { tc_free(bytes) }
        do {
            try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
            try Data(bytes: bytes, count: size).write(to: url, options: .atomic)
            return true
        } catch {
            print("Can't save \(url.path): \(error)")
            return false
        }
    }

    /// The game in the file; its level-saved values are put in place for the level about to open.
    static func read(_ url: URL) -> tc_save_game? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        var game = tc_save_game()
        let r = data.withUnsafeBytes { tc_save_read($0.baseAddress, data.count, &game) }
        if r != TC_OK { print("\(url.lastPathComponent): \(String(cString: tc_result_string(r)))") }
        return r == TC_OK ? game : nil
    }
}

/// The menu's options (tc_options) across launches, in UserDefaults: "gamma", "animationSpeed", "fog", "lighting" here;
/// the field of view ("fieldOfView", as , . step it); the volumes ("musicVolume", Music; "soundVolume", Sfx),
/// Dolby Surround ("dolbySurround", Sfx), the HUD's auto-hide ("hudAutoHide") and the look ("lookSensitivity",
/// "invertY") as their keys already kept them. Every level's viewer gets them (apply); the keys that change them in a
/// level keep the new value (save).
@MainActor enum ViewSettings {
    static var options: tc_options {
        get {
            var o = tc_options()
            tc_options_defaults(&o)
            let d = UserDefaults.standard
            o.gamma = d.object(forKey: "gamma") as? Float ?? 1
            o.animation_speed = d.object(forKey: "animationSpeed") as? Float ?? 1
            o.fog = d.object(forKey: "fog") as? Bool ?? true
            o.lighting = UInt8(d.integer(forKey: "lighting") & 1)
            o.dolby = Sfx.shared.dolby
            o.hud_auto_hide = d.bool(forKey: "hudAutoHide")
            o.invert_y = d.bool(forKey: "invertY")
            o.look_sensitivity = d.object(forKey: "lookSensitivity") as? Float ?? 1
            o.music_volume = Music.shared.volume
            o.sound_volume = Sfx.shared.volume
            o.fov = d.object(forKey: "fieldOfView") as? Float ?? 86
            return o
        }
        set {
            let d = UserDefaults.standard
            d.set(newValue.gamma, forKey: "gamma")
            d.set(newValue.animation_speed, forKey: "animationSpeed")
            d.set(newValue.fog, forKey: "fog")
            d.set(Int(newValue.lighting), forKey: "lighting")
            if Sfx.shared.dolby != newValue.dolby { Sfx.shared.dolby = newValue.dolby }
            d.set(newValue.hud_auto_hide, forKey: "hudAutoHide")
            d.set(newValue.invert_y, forKey: "invertY")
            d.set(newValue.look_sensitivity, forKey: "lookSensitivity")
            if Music.shared.volume != newValue.music_volume { Music.shared.volume = newValue.music_volume }
            if Sfx.shared.volume != newValue.sound_volume { Sfx.shared.volume = newValue.sound_volume }
            d.set(newValue.fov, forKey: "fieldOfView")
        }
    }

    static func apply(_ v: UnsafeMutablePointer<tc_viewer>) {
        let o = options
        tc_viewer_set_view(v, o.gamma, o.animation_speed, o.fog, UInt32(o.lighting))
        if tc_viewer_fov(v) != o.fov { tc_viewer_set_fov(v, o.fov) }
    }

    /// After a key changed gamma, the speed, the fog or the lighting in a level.
    static func save(_ v: UnsafeMutablePointer<tc_viewer>) {
        let d = UserDefaults.standard
        d.set(v.pointee.gamma, forKey: "gamma")
        d.set(v.pointee.animation_speed, forKey: "animationSpeed")
        d.set(v.pointee.fog_enabled, forKey: "fog")
        if v.pointee.level.pointee.has_lighting != 0 { d.set(Int(v.pointee.lighting.rawValue), forKey: "lighting") }
    }
}
