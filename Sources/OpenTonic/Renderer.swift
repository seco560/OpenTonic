import Metal
import MetalKit
import simd
import TonicCore

/// Draws one converted level. The drawable always matches the view's backing pixels (native Retina resolution); the
/// level is drawn into the renderer's own targets (MSAA, supersampling) and ends up in it. The core's tc_viewer
/// (`Viewer`) owns the cameras, play mode, settings and what to draw each frame; this uploads its mesh table and the
/// level's textures once, then packs its shading values into `Uniforms` and draws its list.
@MainActor final class Renderer: NSObject, MTKViewDelegate {
    /// One tc_viewer_mesh on the GPU.
    private struct GPUMesh {
        var vertices: MTLBuffer
        var indices: MTLBuffer
        var indexCount: Int
        var scroll: SIMD2<Float>
    }

    private struct Uniforms {
        var viewProjection: float4x4
        var lightDirection: SIMD3<Float>
        var ambient: Float
        var time: Float
        var bakedScale: Float  // vertex colour (0..1) -> light factor: 255/128, see level.RADIOSITY_ONE
        var lightSet: UInt32   // baked set shown (light persos, number keys); falls back to set 0 per vertex
        var gamma: Float       // display gamma (- / = keys), 1 = unchanged
        var fog: SIMD4<Float>  // near, far, enabled, unused (matches Shaders.metal)
        var fogColor: SIMD4<Float>
        var eye: SIMD4<Float>
        var camRight: SIMD4<Float>  // camera axes, for billboards
        var camUp: SIMD4<Float>
        var lightNext: UInt32  // the set a change blends towards
        var lightBlend: Float  // 0..1
    }

    /// Per-draw vertex constants (matches DrawParams in Shaders.metal).
    private struct DrawParams {
        var model: float4x4
        var scroll: SIMD2<Float>
        var billboard: UInt32  // TC_FLAG_BILLBOARD sprite quads
    }

    /// Ed's shadow disc (matches ShadowParams in Shaders.metal).
    private struct ShadowParams {
        var viewProjection: float4x4
        var center: SIMD4<Float>  // xyz on the ground, w radius
        var axisU: SIMD4<Float>
        var axisV: SIMD4<Float>
        var normal: SIMD4<Float>  // w: the share of the radius at full darkness
        var fog: SIMD4<Float>     // near, far, enabled, darkness (alpha)
        var eye: SIMD4<Float>
    }

    let device: MTLDevice
    private let queue: MTLCommandQueue
    private let library: MTLLibrary
    private let vertexDescriptor = MTLVertexDescriptor()
    private let colorFormat: MTLPixelFormat, depthFormat: MTLPixelFormat
    /// The level and overlay pipelines for the current sample count (rebuilt by setMSAA). The opaque textured one uses
    /// alpha to coverage when multisampling, so alpha-tested cutouts (foliage, fences, sprites) are antialiased too.
    private var texturedPipeline: MTLRenderPipelineState!
    private var flatPipeline: MTLRenderPipelineState!
    /// TC_FLAG_TRANSPARENT surfaces: alpha blended, drawn after everything opaque without writing depth.
    private var blendedPipeline: MTLRenderPipelineState!
    private let blendedDepthState: MTLDepthStencilState
    /// Screen transitions and the loading screen's black (docs/PORTALS.md): 2D quads over the frame.
    private var overlayPipeline: MTLRenderPipelineState!
    /// Ed's round shadow (tc_viewer_shadow): blended, depth-tested without writing, biased towards the camera. Optional:
    /// the level draws without it.
    private var shadowPipeline: MTLRenderPipelineState?
    private let shadowDepthState: MTLDepthStencilState
    /// Filters a supersampled frame down into the drawable (one sample, no depth).
    private let downsamplePipeline: MTLRenderPipelineState
    /// Multisample antialiasing: 1 (off), 2, 4 or 8 samples, as far as the GPU goes; F2 cycles it. Kept across levels and
    /// launches (UserDefaults "msaa", default 8).
    private(set) var msaa = 1
    static var preferredMSAA: Int {
        get { UserDefaults.standard.object(forKey: "msaa") as? Int ?? 8 }
        set { UserDefaults.standard.set(newValue, forKey: "msaa") }
    }
    /// Supersampling, percent of the drawable's pixels per axis: 100 (off), 150, 200; F3 cycles it. The level is drawn
    /// at that size (on top of the MSAA) into `scaled`, then filtered down into the drawable (at 200 a 2 x 2 box
    /// filter). Kept across levels and launches (UserDefaults "supersampling", default 200); off for this run when its
    /// targets can't be made.
    private(set) var scale = 100
    static var preferredScale: Int {
        get { UserDefaults.standard.object(forKey: "supersampling") as? Int ?? 200 }
        set { UserDefaults.standard.set(newValue, forKey: "supersampling") }
    }
    var nextScale: Int { scale >= 200 ? 100 : scale >= 150 ? 200 : 150 }
    /// The 3D targets: depth and, with MSAA, the multisampled colour (both memoryless on Apple GPUs: they live in tile
    /// memory and cost nothing), and when supersampling the single-sampled colour they resolve into.
    private var depth: MTLTexture?, msaaColor: MTLTexture?, scaled: MTLTexture?
    private var targetKey = (width: 0, height: 0, samples: 0)
    private let memoryless: Bool
    private let overlayDepthState: MTLDepthStencilState
    private let depthState: MTLDepthStencilState
    /// Level textures: trilinear, 16x anisotropy; repeating or clamped per axis as the surface's TC_FLAG_WRAP_U / _V
    /// say (the original's driver addressing, from the texture's name). Index: wrap u | wrap v << 1.
    private let samplers: [MTLSamplerState]
    /// Per-triangle lightmap atlas (.ttlv LMAP), or a 1x1 white texture for levels without lightmaps.
    private var lightmap: MTLTexture?
    private var textures: [MTLTexture?] = []  // level.textures order; nil = missing
    private var meshes: [GPUMesh?] = []        // tc_viewer_mesh order; nil = empty
    private var effects: [MTLTexture?] = []    // tc_viewer_effect_texture order
    /// Called from draw: the loading screen to show (level, progress 0...1), or nil.
    var onLoading: ((String, Float)?) -> Void = { _ in }
    /// Called once when a red portal's loading screen is done: open that level with this carry.
    var onPendingLevel: (tc_viewer_carry) -> Void = { _ in }
    /// Gamepad buttons for the app (level select, controls, play / free-fly), after the frame that read them.
    var onGamepad: (Gamepad.Command) -> Void = { _ in }
    private lazy var gamepad = Gamepad()  // made on first use, in draw (main actor)
    private var pendingSent = false
    private(set) var missingTextures = 0
    var textureCount: Int { textures.count - missingTextures }

    /// Cameras, play mode, settings, draw list (C core); keeps the level alive.
    private(set) var viewer: Viewer?
    private var lastTime: CFTimeInterval?

    init(view: MTKView) throws {
        guard let device = view.device ?? MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
            throw NSError(domain: "OpenTonic", code: 1, userInfo: [NSLocalizedDescriptionKey: "No Metal device"])
        }
        self.device = device
        self.queue = queue
        view.device = device
        view.colorPixelFormat = .bgra8Unorm
        // The view only provides the drawable: depth and multisampling are the renderer's own targets (ensureTargets).
        view.depthStencilPixelFormat = .invalid
        view.sampleCount = 1
        view.preferredFramesPerSecond = 120
        memoryless = device.supportsFamily(.apple7)  // Apple silicon

        // Inside OpenTonic.app the shader sits in Contents/Resources; `swift run` builds use the SwiftPM bundle.
        let shaderURL = Bundle.main.url(forResource: "Shaders", withExtension: "metal")
            ?? Bundle.module.url(forResource: "Shaders", withExtension: "metal")!
        library = try device.makeLibrary(source: String(contentsOf: shaderURL, encoding: .utf8), options: nil)
        colorFormat = view.colorPixelFormat
        depthFormat = .depth32Float

        let vd = vertexDescriptor
        vd.attributes[0].format = .float3; vd.attributes[0].offset = 0
        vd.attributes[1].format = .float3; vd.attributes[1].offset = 12
        vd.attributes[2].format = .float2; vd.attributes[2].offset = 24
        for (i, off) in [(3, 32), (4, 36), (5, 40)] { vd.attributes[i].format = .uchar4Normalized; vd.attributes[i].offset = off }
        vd.attributes[6].format = .float2; vd.attributes[6].offset = 44  // lightmap atlas uv
        for i in 0..<7 { vd.attributes[i].bufferIndex = 0 }
        vd.layouts[0].stride = MemoryLayout<tc_vertex>.stride  // 52: pos3 + normal3 + uv floats, 3 x rgba8 baked sets, uv2


        let ds = MTLDepthStencilDescriptor()
        ds.depthCompareFunction = .less
        ds.isDepthWriteEnabled = true
        depthState = device.makeDepthStencilState(descriptor: ds)!
        ds.isDepthWriteEnabled = false
        blendedDepthState = device.makeDepthStencilState(descriptor: ds)!
        ds.depthCompareFunction = .lessEqual
        shadowDepthState = device.makeDepthStencilState(descriptor: ds)!
        ds.depthCompareFunction = .always
        overlayDepthState = device.makeDepthStencilState(descriptor: ds)!

        let dd = MTLRenderPipelineDescriptor()
        dd.vertexFunction = library.makeFunction(name: "downsample_vertex")
        dd.fragmentFunction = library.makeFunction(name: "downsample_fragment")
        dd.colorAttachments[0].pixelFormat = colorFormat
        downsamplePipeline = try device.makeRenderPipelineState(descriptor: dd)

        let sd = MTLSamplerDescriptor()
        sd.minFilter = .linear; sd.magFilter = .linear; sd.mipFilter = .linear
        sd.maxAnisotropy = 16  // sharp floors and walls at grazing angles
        samplers = (0..<4).map { i in
            sd.sAddressMode = i & 1 != 0 ? .repeat : .clampToEdge
            sd.tAddressMode = i & 2 != 0 ? .repeat : .clampToEdge
            return device.makeSamplerState(descriptor: sd)!
        }
        super.init()
        try setMSAA(Renderer.preferredMSAA)
        setScale(Renderer.preferredScale)
    }

    /// The largest supported sample count up to `wanted` (1, 2, 4, 8).
    private func supported(_ wanted: Int) -> Int {
        [8, 4, 2].first { $0 <= wanted && device.supportsTextureSampleCount($0) } ?? 1
    }

    /// The next setting after the current one: off -> 2x -> 4x -> 8x -> off, skipping what the GPU lacks.
    var nextMSAA: Int { [2, 4, 8, 1].first { $0 > msaa && supported($0) == $0 } ?? 1 }

    /// Switches multisampling: the 3D targets and every pipeline (they must match its sample count).
    func setMSAA(_ wanted: Int) throws {
        let n = supported(wanted)
        func pipeline(_ fragment: String, blended: Bool = false, coverage: Bool = false) throws -> MTLRenderPipelineState {
            let d = MTLRenderPipelineDescriptor()
            if blended {
                let c = d.colorAttachments[0]!
                c.isBlendingEnabled = true
                c.sourceRGBBlendFactor = .sourceAlpha
                c.destinationRGBBlendFactor = .oneMinusSourceAlpha
                c.sourceAlphaBlendFactor = .one
                c.destinationAlphaBlendFactor = .oneMinusSourceAlpha
            }
            d.vertexFunction = library.makeFunction(name: "level_vertex")
            d.fragmentFunction = library.makeFunction(name: fragment)
            d.vertexDescriptor = vertexDescriptor
            d.colorAttachments[0].pixelFormat = colorFormat
            d.depthAttachmentPixelFormat = depthFormat
            d.rasterSampleCount = n
            d.isAlphaToCoverageEnabled = coverage && n > 1
            return try device.makeRenderPipelineState(descriptor: d)
        }
        let od = MTLRenderPipelineDescriptor()
        od.vertexFunction = library.makeFunction(name: "overlay_vertex")
        od.fragmentFunction = library.makeFunction(name: "overlay_fragment")
        od.colorAttachments[0].pixelFormat = colorFormat
        od.colorAttachments[0].isBlendingEnabled = true
        od.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
        od.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
        od.depthAttachmentPixelFormat = depthFormat
        od.rasterSampleCount = n
        overlayPipeline = try device.makeRenderPipelineState(descriptor: od)
        texturedPipeline = try pipeline("level_fragment_textured", coverage: true)
        flatPipeline = try pipeline("level_fragment_flat")
        blendedPipeline = try pipeline("level_fragment_textured", blended: true)
        let sd = MTLRenderPipelineDescriptor()
        sd.vertexFunction = library.makeFunction(name: "shadow_vertex")
        sd.fragmentFunction = library.makeFunction(name: "shadow_fragment")
        let c = sd.colorAttachments[0]!
        c.pixelFormat = colorFormat
        c.isBlendingEnabled = true
        c.sourceRGBBlendFactor = .sourceAlpha
        c.destinationRGBBlendFactor = .oneMinusSourceAlpha
        c.sourceAlphaBlendFactor = .one
        c.destinationAlphaBlendFactor = .oneMinusSourceAlpha
        sd.depthAttachmentPixelFormat = depthFormat
        sd.rasterSampleCount = n
        shadowPipeline = try? device.makeRenderPipelineState(descriptor: sd)
        msaa = n
        releaseTargets()
    }

    /// Switches supersampling (100, 150 or 200 % of the drawable's pixels per axis; anything else rounds down).
    func setScale(_ percent: Int) {
        let p = percent >= 200 ? 200 : percent >= 150 ? 150 : 100
        guard p != scale else { return }
        scale = p
        releaseTargets()
    }

    private func releaseTargets() {
        depth = nil; msaaColor = nil; scaled = nil
        targetKey = (0, 0, 0)
    }

    /// The 3D targets for a drawable of w x h: at scale % of it, msaa samples. Supersampling goes off when they can't be
    /// made (too big for the GPU).
    private func ensureTargets(_ w: Int, _ h: Int) -> Bool {
        let sw = w * scale / 100, sh = h * scale / 100
        if depth != nil, targetKey == (sw, sh, msaa) { return true }
        releaseTargets()
        func make(_ format: MTLPixelFormat, samples: Int, kept: Bool) -> MTLTexture? {
            let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: format, width: sw, height: sh, mipmapped: false)
            d.textureType = samples > 1 ? .type2DMultisample : .type2D
            d.sampleCount = samples
            d.usage = kept ? [.renderTarget, .shaderRead] : .renderTarget
            d.storageMode = kept ? .private : memoryless ? .memoryless : .private
            return device.makeTexture(descriptor: d)
        }
        let limit = 16384  // the largest 2D texture on every Mac GPU Metal runs on
        if sw <= limit, sh <= limit {
            depth = make(depthFormat, samples: msaa, kept: false)
            if msaa > 1 { msaaColor = make(colorFormat, samples: msaa, kept: false) }
            if scale > 100 { scaled = make(colorFormat, samples: 1, kept: true) }
        }
        if depth != nil, msaa == 1 || msaaColor != nil, scale == 100 || scaled != nil {
            targetKey = (sw, sh, msaa)
            return true
        }
        NSLog("3D targets (%d x %d, %dx MSAA) could not be made", sw, sh, msaa)
        guard scale > 100 else { return false }
        NSLog("supersampling off")
        setScale(100)
        return ensureTargets(w, h)
    }

    func load(_ viewer: Viewer) {
        self.viewer = viewer
        let level = viewer.level, r = level.raw
        lightmap = nil
        if let p = r.lightmap_atlas {
            lightmap = try? MTKTextureLoader(device: device).newTexture(URL: level.folder.appendingPathComponent(String(cString: p)),
                                                                    options: [.SRGB: false, .generateMipmaps: false])
        }
        if lightmap == nil {  // shaders always bind slot 1; only lightmapped vertices sample it
            let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: 1, height: 1, mipmapped: false)
            let t = device.makeTexture(descriptor: d)
            var white: UInt32 = 0xFFFF_FFFF
            t?.replace(region: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0, withBytes: &white, bytesPerRow: 4)
            lightmap = t
        }
        let loader = MTKTextureLoader(device: device)
        missingTextures = 0
        let mips = queue.makeCommandBuffer(), blit = mips?.makeBlitCommandEncoder()
        textures = level.textures.map { url in
            if let t = loadBled(url, blit: blit) { return t }
            missingTextures += 1
            return nil
        }
        blit?.endEncoding()
        mips?.commit()
        effects = viewer.effectTextures.map { path in
            try? loader.newTexture(URL: level.folder.appendingPathComponent(path), options: [.SRGB: false, .generateMipmaps: false])
        }
        pendingSent = false
        meshes = (0..<tc_viewer_mesh_count(viewer.raw)).map { i in
            let m = tc_viewer_mesh(viewer.raw, i)!.pointee
            guard m.index_count > 0, m.vertex_count > 0, let v = m.vertices, let i = m.indices else { return nil }
            // tc_vertex is already the packed GPU layout (52 bytes).
            guard let vb = device.makeBuffer(bytes: v, length: Int(m.vertex_count) * MemoryLayout<tc_vertex>.stride),
                  let ib = device.makeBuffer(bytes: i, length: Int(m.index_count) * 4) else { return nil }
            return GPUMesh(vertices: vb, indices: ib, indexCount: Int(m.index_count), scroll: SIMD2(m.surface.scroll.0, m.surface.scroll.1))
        }
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    /// A level texture with mipmaps (generated by `blit`). Fully transparent texels take the colour of their opaque
    /// neighbours first (a ring per pass, 16 passes; the rest the image's average opaque colour), so filtering and the
    /// mipmaps don't fringe cutout edges with whatever colour the artist left under the transparency. As the SDL
    /// build's gpu_upload_surface.
    private func loadBled(_ url: URL, blit: MTLBlitCommandEncoder?) -> MTLTexture? {
        guard let src = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(src, 0, nil) else { return nil }
        let w = image.width, h = image.height
        var px = [UInt8](repeating: 0, count: w * h * 4)
        guard let space = CGColorSpace(name: CGColorSpace.sRGB),
              let ctx = CGContext(data: &px, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4, space: space,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
        Renderer.unpremultiplyAndBleed(&px, w, h, passes: 16)
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: w, height: h, mipmapped: true)
        d.usage = .shaderRead
        d.storageMode = .shared
        guard let t = device.makeTexture(descriptor: d) else { return nil }
        t.replace(region: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0, withBytes: px, bytesPerRow: w * 4)
        blit?.generateMipmaps(for: t)
        return t
    }

    private static func unpremultiplyAndBleed(_ px: inout [UInt8], _ w: Int, _ h: Int, passes: Int) {
        var known = [UInt8](repeating: 0, count: w * h)
        var sum = (0, 0, 0), opaque = 0, unknown = 0
        for i in 0..<(w * h) {
            let a = Int(px[4 * i + 3])
            if a == 0 { unknown += 1; continue }
            if a < 255 { for c in 0..<3 { px[4 * i + c] = UInt8(min(255, Int(px[4 * i + c]) * 255 / a)) } }
            known[i] = 1
            sum.0 += Int(px[4 * i]); sum.1 += Int(px[4 * i + 1]); sum.2 += Int(px[4 * i + 2]); opaque += 1
        }
        guard unknown > 0, opaque > 0 else { return }
        for _ in 0..<passes where unknown > 0 {
            var filled = 0
            for y in 0..<h {
                for x in 0..<w where known[y * w + x] == 0 {
                    var r = 0, g = 0, b = 0, n = 0
                    for dy in -1...1 {
                        for dx in -1...1 {
                            let nx = x + dx, ny = y + dy
                            guard nx >= 0, ny >= 0, nx < w, ny < h, known[ny * w + nx] == 1 else { continue }
                            let q = 4 * (ny * w + nx)
                            r += Int(px[q]); g += Int(px[q + 1]); b += Int(px[q + 2]); n += 1
                        }
                    }
                    guard n > 0 else { continue }
                    let p = 4 * (y * w + x)
                    px[p] = UInt8(r / n); px[p + 1] = UInt8(g / n); px[p + 2] = UInt8(b / n)
                    known[y * w + x] = 2
                    filled += 1
                }
            }
            for i in 0..<(w * h) where known[i] == 2 { known[i] = 1 }
            unknown -= filled
            if filled == 0 { break }
        }
        if unknown > 0 {
            for i in 0..<(w * h) where known[i] == 0 {
                px[4 * i] = UInt8(sum.0 / opaque); px[4 * i + 1] = UInt8(sum.1 / opaque); px[4 * i + 2] = UInt8(sum.2 / opaque)
            }
        }
    }

    func draw(in view: MTKView) {
        guard let v = viewer?.raw else { return }
        let now = CACurrentMediaTime()
        let dt = Float(min(now - (lastTime ?? now), 0.1))
        lastTime = now
        let commands = gamepad.poll(v, dt: dt)
        if !commands.isEmpty {
            let send = onGamepad
            DispatchQueue.main.async { commands.forEach(send) }  // the level select replaces this renderer: not from inside draw
        }
        tc_viewer_update(v, dt)
        var sounds: UnsafePointer<UInt16>?
        let n = Int(tc_viewer_sounds(v, &sounds))
        if n > 0, let sounds { Sfx.shared.play(Array(UnsafeBufferPointer(start: sounds, count: n))) }

        let size = view.drawableSize
        var f = tc_view_frame()
        tc_viewer_frame(v, Float(size.width / max(size.height, 1)), &f)
        guard size.width >= 1, size.height >= 1, ensureTargets(Int(size.width), Int(size.height)),
              let drawable = view.currentDrawable, let cmd = queue.makeCommandBuffer() else { return }
        // Draw multisampled and resolve, into the drawable or the supersampled target (filtered into the drawable after).
        let pass = MTLRenderPassDescriptor()
        let color = pass.colorAttachments[0]!
        color.clearColor = MTLClearColor(red: Double(f.clear_color.0), green: Double(f.clear_color.1), blue: Double(f.clear_color.2), alpha: 1)
        color.loadAction = .clear
        let out = scaled ?? drawable.texture
        if let msaaColor {
            color.texture = msaaColor
            color.resolveTexture = out
            color.storeAction = .multisampleResolve
        } else {
            color.texture = out
            color.storeAction = .store
        }
        pass.depthAttachment.texture = depth
        pass.depthAttachment.loadAction = .clear
        pass.depthAttachment.clearDepth = 1
        pass.depthAttachment.storeAction = .dontCare
        guard let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }
        var uniforms = Uniforms(viewProjection: float4x4(f.view_projection), lightDirection: SIMD3(f.light_direction),
                                ambient: f.ambient, time: f.time, bakedScale: f.baked_scale, lightSet: f.light_set, gamma: f.gamma,
                                fog: SIMD4(f.fog_near, f.fog_far, f.fog_on ? 1 : 0, 0),
                                fogColor: SIMD4(f.fog_color.0, f.fog_color.1, f.fog_color.2, 0), eye: SIMD4(SIMD3(f.eye), 0),
                                camRight: SIMD4(SIMD3(f.cam_right), 0), camUp: SIMD4(SIMD3(f.cam_up), 0),
                                lightNext: f.light_next, lightBlend: f.light_blend)
        enc.setDepthStencilState(depthState)
        enc.setCullMode(.none)  // winding of OpenSpace elements not yet confirmed
        enc.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        enc.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)

        var count: UInt32 = 0, opaque: UInt32 = 0
        let draws = tc_viewer_draws(v, &count, &opaque)!
        func drawMesh(_ d: tc_view_draw) {
            // d's index range: all of the mesh, or the shown sectors' part of a static (tc_view_draw)
            guard let m = meshes[Int(d.mesh)], d.index_count > 0, Int(d.first_index) + Int(d.index_count) <= m.indexCount else { return }
            let transparent = d.flags & TC_FLAG_TRANSPARENT != 0
            var flags = SIMD2<UInt32>(d.flags, !transparent && msaa > 1 ? 1 : 0)  // coverage: as the textured pipeline
            if d.texture >= 0, let tex = textures[Int(d.texture)] {
                enc.setRenderPipelineState(transparent ? blendedPipeline : texturedPipeline)
                let wrap = (d.flags & TC_FLAG_WRAP_U != 0 ? 1 : 0) | (d.flags & TC_FLAG_WRAP_V != 0 ? 2 : 0)
                enc.setFragmentSamplerState(samplers[wrap], index: 0)
                enc.setFragmentTexture(tex, index: 0)
                enc.setFragmentTexture(lightmap, index: 1)
                enc.setFragmentBytes(&flags, length: MemoryLayout<SIMD2<UInt32>>.stride, index: 2)
            } else {
                enc.setRenderPipelineState(flatPipeline)
                enc.setFragmentTexture(lightmap, index: 1)
            }
            var params = DrawParams(model: float4x4(d.model), scroll: m.scroll, billboard: d.flags & TC_FLAG_BILLBOARD != 0 ? 1 : 0)
            enc.setVertexBytes(&params, length: MemoryLayout<DrawParams>.stride, index: 2)
            enc.setVertexBuffer(m.vertices, offset: 0, index: 0)
            enc.drawIndexedPrimitives(type: .triangle, indexCount: Int(d.index_count), indexType: .uint32, indexBuffer: m.indices,
                                      indexBufferOffset: Int(d.first_index) * 4)
        }
        // Opaque draws, Ed's shadow on them, then the transparent ones (alpha blended, no depth writes): water drawn
        // over shallow ground stays on top of the shadow.
        for i in 0..<Int(opaque) { drawMesh(draws[i]) }
        drawShadow(enc, v, f)
        enc.setDepthStencilState(blendedDepthState)
        for i in Int(opaque)..<Int(count) { drawMesh(draws[i]) }
        drawOverlay(enc, viewer!)
        enc.endEncoding()
        if let scaled {  // supersampled: filter it down into the drawable
            let p = MTLRenderPassDescriptor()
            p.colorAttachments[0].texture = drawable.texture
            p.colorAttachments[0].loadAction = .dontCare
            p.colorAttachments[0].storeAction = .store
            if let down = cmd.makeRenderCommandEncoder(descriptor: p) {
                down.setRenderPipelineState(downsamplePipeline)
                down.setFragmentTexture(scaled, index: 0)
                down.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
                down.endEncoding()
            }
        }
        cmd.present(drawable)
        cmd.commit()
        if !pendingSent, let carry = viewer?.pendingLevel {
            pendingSent = true
            let send = onPendingLevel
            DispatchQueue.main.async { send(carry) }  // opening a level replaces this renderer: not from inside draw
        }
    }

    /// Ed's round shadow (tc_viewer_shadow, after tc_viewer_draws), as the SDL build's gpu_draw_shadow: a disc on the
    /// ground under him, faded by the fog. Restores nothing but the depth bias: every level draw sets its own state.
    private func drawShadow(_ enc: MTLRenderCommandEncoder, _ v: UnsafeMutablePointer<tc_viewer>, _ f: tc_view_frame) {
        var s = tc_view_shadow()
        guard let shadowPipeline, tc_viewer_shadow(v, &s), s.alpha > 0, s.radius > 0 else { return }
        // two axes along the ground: across the normal, from whichever world axis is least along it
        let n = SIMD3<Float>(s.normal)
        let across = cross(n, abs(n.x) < 0.9 ? SIMD3<Float>(1, 0, 0) : SIMD3<Float>(0, 1, 0))
        guard length(across) > 0 else { return }
        let u = normalize(across), w = cross(n, u)
        var p = ShadowParams(viewProjection: float4x4(f.view_projection), center: SIMD4(SIMD3(s.position), s.radius),
                             axisU: SIMD4(u, 0), axisV: SIMD4(w, 0), normal: SIMD4(n, s.inner),
                             fog: SIMD4(f.fog_near, f.fog_far, f.fog_on ? 1 : 0, s.alpha), eye: SIMD4(SIMD3(f.eye), 0))
        enc.setRenderPipelineState(shadowPipeline)
        enc.setDepthStencilState(shadowDepthState)
        enc.setDepthBias(-2, slopeScale: -2, clamp: 0)  // pulled towards the camera: no fighting the ground it lies on
        enc.setVertexBytes(&p, length: MemoryLayout<ShadowParams>.stride, index: 3)
        enc.setFragmentBytes(&p, length: MemoryLayout<ShadowParams>.stride, index: 3)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
        enc.setDepthBias(0, slopeScale: 0, clamp: 0)
    }

    private struct OverlayVertex { var position: SIMD2<Float>; var uv: SIMD2<Float> }
    private struct OverlayParams { var mode: UInt32; var threshold: Float }

    /// The screen transition (mask wipe or spinning logo), black, or the loading screen's black backdrop.
    private func drawOverlay(_ enc: MTLRenderCommandEncoder, _ viewer: Viewer) {
        let o = viewer.overlay
        if o.kind == TC_VO_LOADING {
            onLoading((o.level.map { String(cString: $0) } ?? "", o.progress))
        } else {
            onLoading(nil)
        }
        let full = [OverlayVertex(position: [0, 0], uv: [0, 0]), OverlayVertex(position: [1, 0], uv: [1, 0]),
                    OverlayVertex(position: [0, 1], uv: [0, 1]), OverlayVertex(position: [1, 0], uv: [1, 0]),
                    OverlayVertex(position: [1, 1], uv: [1, 1]), OverlayVertex(position: [0, 1], uv: [0, 1])]
        var verts: [OverlayVertex]
        var params: OverlayParams
        switch o.kind {
        case TC_VO_MASK:
            verts = full
            params = OverlayParams(mode: 0, threshold: o.threshold)
        case TC_VO_LOGO:
            let c = withUnsafeBytes(of: o.corners) { Array($0.bindMemory(to: Float.self)) }
            let t = withUnsafeBytes(of: o.uv) { Array($0.bindMemory(to: Float.self)) }
            verts = [0, 1, 2, 1, 3, 2].map { OverlayVertex(position: [c[2 * $0], c[2 * $0 + 1]], uv: [t[2 * $0], t[2 * $0 + 1]]) }
            params = OverlayParams(mode: 1, threshold: 0)
        case TC_VO_BLACK, TC_VO_LOADING:
            verts = full
            params = OverlayParams(mode: 2, threshold: 0)
        default:
            return
        }
        let tex = o.texture >= 0 && Int(o.texture) < effects.count ? effects[Int(o.texture)] : nil
        if params.mode != 2 && tex == nil { verts = full; params = OverlayParams(mode: 2, threshold: 0) }
        enc.setRenderPipelineState(overlayPipeline)
        enc.setDepthStencilState(overlayDepthState)
        enc.setVertexBytes(&verts, length: MemoryLayout<OverlayVertex>.stride * verts.count, index: 0)
        enc.setFragmentBytes(&params, length: MemoryLayout<OverlayParams>.stride, index: 0)
        enc.setFragmentTexture(tex ?? lightmap, index: 0)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: verts.count)
    }
}
