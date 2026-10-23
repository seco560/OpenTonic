import Metal
import MetalKit
import simd
import TonicCore

/// Draws one converted level. The drawable always matches the view's backing pixels
/// (native Retina resolution); aspect changes widen the view horizontally (Hor+).
/// Animation runs on one clock (`animationTime`, paused with P): perso parts take their channel's
/// baked transform for the current sample, materials scroll UVs and step animated textures.
@MainActor final class Renderer: NSObject, MTKViewDelegate {
    private struct GPUMesh {
        var vertices: MTLBuffer
        var indices: MTLBuffer
        var indexCount: Int
        var texture: MTLTexture?
        var flags: UInt32
        var scroll: SIMD2<Float>
        var frames: [(texture: MTLTexture?, seconds: Float)]
        var loopSeconds: Float

        /// The texture shown at time t (animated textures loop through their frames).
        func texture(at t: Float) -> MTLTexture? {
            guard loopSeconds > 0 else { return texture }
            var x = t.truncatingRemainder(dividingBy: loopSeconds)
            for f in frames { if x < f.seconds { return f.texture }; x -= f.seconds }
            return frames.last?.texture
        }
    }

    private struct GPUPart {
        var channel: Int
        var object: UInt16
        var mesh: GPUMesh
    }

    private struct GPUPerso {
        var world: float4x4
        var source: UnsafePointer<tc_perso>  // baked samples, owned by `level`
        var parts: [GPUPart]
        var sourceIndex: Int  // index in the level's persos (Ed's static copy is hidden while playing)
    }

    private struct Uniforms {
        var viewProjection: float4x4
        var lightDirection: SIMD3<Float>
        var ambient: Float
        var time: Float
        var bakedScale: Float  // vertex colour (0..1) -> light factor: 255/128, see level.RADIOSITY_ONE
        var lightSet: UInt32   // baked set shown (number keys); falls back to set 0 per vertex
        var gamma: Float       // display gamma (- / = keys), 1 = unchanged
        var fog: SIMD4<Float>  // near, far, enabled, unused (matches Shaders.metal)
        var fogColor: SIMD4<Float>
        var eye: SIMD4<Float>
        var camRight: SIMD4<Float>  // camera axes, for billboards
        var camUp: SIMD4<Float>
    }

    /// Per-draw vertex constants (matches DrawParams in Shaders.metal).
    private struct DrawParams {
        var model: float4x4
        var scroll: SIMD2<Float>
        var billboard: UInt32  // TC_FLAG_BILLBOARD sprite quads
    }

    let device: MTLDevice
    private let queue: MTLCommandQueue
    private let texturedPipeline: MTLRenderPipelineState
    private let flatPipeline: MTLRenderPipelineState
    /// TC_FLAG_TRANSPARENT surfaces: alpha blended, drawn after everything opaque without writing depth.
    private let blendedPipeline: MTLRenderPipelineState
    private let blendedDepthState: MTLDepthStencilState
    private let depthState: MTLDepthStencilState
    private let sampler: MTLSamplerState
    /// Per-triangle lightmap atlas (.ttlv v8), or a 1x1 white texture for levels without lightmaps.
    private var lightmap: MTLTexture?
    private var meshes: [GPUMesh] = []
    private var persos: [GPUPerso] = []
    private(set) var stats = (meshes: 0, triangles: 0, textures: 0, missingTextures: 0, persos: 0, parts: 0)
    var paused = false
    /// Baked lighting set (1-9 keys; only levels with day/night variants have more than one) and display gamma.
    var lightSet: UInt32 = 0
    var gamma: Float = 1
    /// The level's fog from its visual environment (.ttlv v7); G toggles it.
    private(set) var fog: (near: Float, far: Float, color: SIMD3<Float>)?
    var fogEnabled = true
    private(set) var lightSetCount = 1
    /// Viewer-only playback rate ([ and ] keys) for comparing against the original; 1 = engine speed.
    var animationSpeed: Float = 1
    private var animationTime: Float = 0

    var camera = Camera()
    var input = InputState()

    /// The loaded level (C core); kept alive because persos and Ed's clips point into it.
    private var level: Level?

    // Play mode (the default when the level has Ed; Tab toggles free-fly): Ed driven by the core's tc_player against the level's collision triangles.
    private(set) var playing = false
    private(set) var player: PlaySession?
    private var edParts: [UInt16: [GPUMesh]] = [:]
    private var edAnimations: [String: UnsafePointer<tc_ed_anim>] = [:]
    private var edStaticPerso = -1
    private var edClip = ""
    private var edPhase: Float = 0  // samples into the current clip
    private var edLoop = false      // AI clips: whether the playing state loops (blend last -> first frame)
    var orbitDistance: Float = 4.5
    private var lastTime: CFTimeInterval?

    init(view: MTKView) throws {
        guard let device = view.device ?? MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
            throw NSError(domain: "TonicViewer", code: 1, userInfo: [NSLocalizedDescriptionKey: "No Metal device"])
        }
        self.device = device
        self.queue = queue
        view.device = device
        view.colorPixelFormat = .bgra8Unorm
        view.depthStencilPixelFormat = .depth32Float
        view.clearColor = MTLClearColor(red: 0.08, green: 0.09, blue: 0.14, alpha: 1)
        view.preferredFramesPerSecond = 120

        // Inside TonicViewer.app the shader sits in Contents/Resources; `swift run` builds use the SwiftPM bundle.
        let shaderURL = Bundle.main.url(forResource: "Shaders", withExtension: "metal")
            ?? Bundle.module.url(forResource: "Shaders", withExtension: "metal")!
        let library = try device.makeLibrary(source: String(contentsOf: shaderURL, encoding: .utf8), options: nil)

        let vd = MTLVertexDescriptor()
        vd.attributes[0].format = .float3; vd.attributes[0].offset = 0
        vd.attributes[1].format = .float3; vd.attributes[1].offset = 12
        vd.attributes[2].format = .float2; vd.attributes[2].offset = 24
        for (i, off) in [(3, 32), (4, 36), (5, 40)] { vd.attributes[i].format = .uchar4Normalized; vd.attributes[i].offset = off }
        vd.attributes[6].format = .float2; vd.attributes[6].offset = 44  // lightmap atlas uv (v8)
        for i in 0..<7 { vd.attributes[i].bufferIndex = 0 }
        vd.layouts[0].stride = MemoryLayout<tc_vertex>.stride  // 52: pos3 + normal3 + uv floats, 3 x rgba8 baked sets, uv2

        func pipeline(_ fragment: String, blended: Bool = false) throws -> MTLRenderPipelineState {
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
            d.vertexDescriptor = vd
            d.colorAttachments[0].pixelFormat = view.colorPixelFormat
            d.depthAttachmentPixelFormat = view.depthStencilPixelFormat
            return try device.makeRenderPipelineState(descriptor: d)
        }
        texturedPipeline = try pipeline("level_fragment_textured")
        flatPipeline = try pipeline("level_fragment_flat")
        blendedPipeline = try pipeline("level_fragment_textured", blended: true)

        let ds = MTLDepthStencilDescriptor()
        ds.depthCompareFunction = .less
        ds.isDepthWriteEnabled = true
        depthState = device.makeDepthStencilState(descriptor: ds)!
        ds.isDepthWriteEnabled = false
        blendedDepthState = device.makeDepthStencilState(descriptor: ds)!

        let sd = MTLSamplerDescriptor()
        sd.minFilter = .linear; sd.magFilter = .linear; sd.mipFilter = .linear
        sd.sAddressMode = .repeat; sd.tAddressMode = .repeat
        sd.maxAnisotropy = 8
        sampler = device.makeSamplerState(descriptor: sd)!
        super.init()
    }

    func load(_ level: Level) {
        self.level = level
        let r = level.raw
        fog = r.fog_far > r.fog_near ? (r.fog_near, r.fog_far, SIMD3(r.fog_rgb.0, r.fog_rgb.1, r.fog_rgb.2)) : nil
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
        var missing = 0
        let textures: [MTLTexture?] = level.textures.map { url in
            do {
                return try loader.newTexture(URL: url, options: [.SRGB: false, .generateMipmaps: true,
                                                                  .textureStorageMode: MTLStorageMode.private.rawValue])
            } catch { missing += 1; return nil }
        }
        func texture(_ i: Int) -> MTLTexture? { i >= 0 && i < textures.count ? textures[i] : nil }
        func gpuMesh(_ m: tc_mesh) -> GPUMesh? {
            guard m.index_count > 0, m.vertex_count > 0, let v = m.vertices, let i = m.indices else { return nil }
            // tc_vertex is already the packed GPU layout: pos3, normal3, uv2 floats + 3 x rgba8 (44 bytes).
            guard let vb = device.makeBuffer(bytes: v, length: Int(m.vertex_count) * MemoryLayout<tc_vertex>.stride),
                  let ib = device.makeBuffer(bytes: i, length: Int(m.index_count) * 4) else { return nil }
            let s = m.surface
            let frames = UnsafeBufferPointer(start: s.frames, count: Int(s.frame_count))
                .map { (texture: texture(Int($0.texture)), seconds: $0.seconds) }
            return GPUMesh(vertices: vb, indices: ib, indexCount: Int(m.index_count), texture: texture(Int(s.texture)), flags: s.flags,
                           scroll: SIMD2(s.scroll.0, s.scroll.1), frames: frames, loopSeconds: frames.reduce(0) { $0 + $1.seconds })
        }
        meshes = level.meshes.compactMap(gpuMesh)
        persos = level.persos.indices.compactMap { i in
            let p = level.persos.baseAddress! + i
            let parts = UnsafeBufferPointer(start: p.pointee.parts, count: Int(p.pointee.part_count)).compactMap { part in
                gpuMesh(part.mesh).map { GPUPart(channel: Int(part.channel), object: part.object, mesh: $0) }
            }
            guard !parts.isEmpty, p.pointee.sample_count > 0 else { return nil }
            return GPUPerso(world: float4x4(p.pointee.world), source: p, parts: parts, sourceIndex: i)
        }
        playing = false
        playExitCamera = nil
        player = nil
        edParts = [:]
        edAnimations = [:]
        if level.hasEd {
            for part in level.edParts { if let m = gpuMesh(part.mesh) { edParts[part.object, default: []].append(m) } }
            for i in level.edAnimations.indices {
                let a = level.edAnimations.baseAddress! + i
                guard a.pointee.sample_count > 0, let name = a.pointee.name else { continue }
                edAnimations[String(cString: name)] = UnsafePointer(a)
            }
            let ai = AIModel(url: level.url.deletingPathExtension().appendingPathExtension("ttai"))
            if edAnimations["idle"] != nil, let p = PlaySession(level: level, ai: ai) {
                for (name, a) in edAnimations where a.pointee.loop == 0 {
                    p.setClipLength(name, Float(a.pointee.sample_count) / max(a.pointee.sample_rate, 1e-3))
                }
                player = p
            }
            edStaticPerso = Int(level.raw.ed.static_perso)
        }
        let partCount = persos.reduce(0) { $0 + $1.parts.count }
        let tris = meshes.reduce(0) { $0 + $1.indexCount / 3 } + persos.reduce(0) { $0 + $1.parts.reduce(0) { $0 + $1.mesh.indexCount / 3 } }
        stats = (meshes.count, tris, textures.count - missing, missing, persos.count, partCount)
        lightSetCount = max(1, Int(level.raw.light_set_count))
        lightSet = 0
        animationTime = 0

        let b = level.bounds
        let size = b.max - b.min
        // Start in the middle: levels are rooms or sit inside a sky dome, so outside the bounds sees nothing.
        camera.position = (b.min + b.max) / 2
        camera.yaw = 0
        camera.pitch = -0.2
        camera.far = max(2000, simd_length(size) * 3)
        input.baseSpeed = max(2, simd_length(size) / 40)
        // Start as Ed when the level has him (Tab still switches to free-fly).
        if player != nil { togglePlay() }
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    /// Where the camera was when play mode was last left; if it has moved since, Ed is dropped there.
    private var playExitCamera: SIMD3<Float>?

    /// Tab: enter/leave play mode. Entering puts the orbit camera behind Ed; leaving keeps the camera where it is.
    /// Re-entering after flying elsewhere drops Ed onto the ground below the camera, facing the view direction.
    /// I: Ed's own AI <-> the simple controller (play mode, levels with a .ttai).
    func toggleAI() {
        guard let p = player, p.hasAI else { return }
        p.useAI.toggle()
        edClip = ""
        edPhase = 0
    }

    func togglePlay() {
        guard let p = player else { return }
        playing.toggle()
        if !playing { playExitCamera = camera.position; return }
        if let exit = playExitCamera, simd_distance(exit, camera.position) > 1 {
            let o = camera.position
            let ground = p.raycast(from: o, dir: SIMD3(0, 0, -1), maxDistance: 2000).map { o - SIMD3(0, 0, $0) }
            p.teleport(to: (ground ?? o) + SIMD3(0, 0, 0.3), heading: .pi - camera.yaw)
        }
        if playing {
            camera.yaw = .pi - p.heading  // camera forward (sin yaw, cos yaw) = Ed's front (sin h, -cos h)
            camera.pitch = -0.3
        }
    }

    private func updatePlay(dt: Float) {
        guard let p = player else { return }
        input.look(&camera, dt: dt)
        if p.useAI {
            // Ed's own scripts; they see last frame's camera eye (it makes the direction keys camera-relative).
            p.stepAI(dt: paused ? 0 : dt, inputs: input.aiInputs, camera: camera.position)
        } else {
            p.step(dt: dt, move: input.moveDirection(yaw: camera.yaw), run: input.held.contains(.fast), jump: input.held.contains(.jump))
        }
        // Third-person orbit around Ed's chest; pulled in when the level is in the way.
        let target = p.position + SIMD3(0, 0, 1.1), back = -camera.forward
        var d = orbitDistance
        if let hit = p.raycast(from: target, dir: back, maxDistance: d + 0.3) { d = max(0.4, hit - 0.3) }
        camera.position = target + back * d

        if p.useAI, let a = p.aiAnimation {  // the AI's state clock picks the frame (one sample per frame)
            edClip = edAnimations[a.name] != nil ? a.name : "idle"
            edPhase = a.frame
            edLoop = a.loops
            return
        }
        // Clip from the movement state; walk/run play at the rate that matches Ed's ground speed.
        if p.animation != edClip { edClip = p.animation; edPhase = 0 }
        edLoop = (edAnimations[edClip] ?? edAnimations["idle"])?.pointee.loop != 0
        guard let a = (edAnimations[edClip] ?? edAnimations["idle"])?.pointee else { return }
        let rate = a.ground_speed > 0 ? a.sample_rate * p.horizontalSpeed / a.ground_speed : a.sample_rate
        edPhase = tc_phase_advance(edPhase, dt * (rate / 4) * (paused ? 0 : animationSpeed), UInt32(a.sample_count), a.loop != 0)
    }

    func draw(in view: MTKView) {
        let now = CACurrentMediaTime()
        let dt = Float(min(now - (lastTime ?? now), 0.1))
        lastTime = now
        if playing { updatePlay(dt: dt) } else { input.apply(to: &camera, dt: dt) }
        if !paused { animationTime += dt * animationSpeed }

        let fogOn = fogEnabled && fog != nil
        // With fog, the background is the fog colour (the horizon fades into it).
        view.clearColor = fogOn ? MTLClearColor(red: Double(fog!.color.x), green: Double(fog!.color.y), blue: Double(fog!.color.z), alpha: 1)
                                : MTLClearColor(red: 0.08, green: 0.09, blue: 0.14, alpha: 1)
        guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
              let cmd = queue.makeCommandBuffer(), let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }
        let size = view.drawableSize
        var uniforms = Uniforms(viewProjection: camera.viewProjection(aspect: Float(size.width / max(size.height, 1))),
                                lightDirection: normalize(SIMD3(0.4, -0.3, 0.85)), ambient: 0.45, time: animationTime,
                                bakedScale: 255.0 / 128.0, lightSet: lightSet, gamma: gamma,
                                fog: SIMD4(fog?.near ?? 0, fog?.far ?? 0, fogOn ? 1 : 0, 0),
                                fogColor: SIMD4(fog?.color ?? .zero, 0), eye: SIMD4(camera.position, 0),
                                camRight: SIMD4(camera.right, 0), camUp: SIMD4(cross(camera.right, camera.forward), 0))
        enc.setDepthStencilState(depthState)
        enc.setCullMode(.none)  // winding of OpenSpace elements not yet confirmed
        enc.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        enc.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        enc.setFragmentSamplerState(sampler, index: 0)
        let t = animationTime
        var blended: [(GPUMesh, float4x4)] = []  // transparent surfaces, drawn last
        var blendPass = false
        func draw(_ m: GPUMesh, _ model: float4x4) {
            var flags = m.flags
            let transparent = m.flags & TC_FLAG_TRANSPARENT != 0
            if transparent && !blendPass { blended.append((m, model)); return }
            if let tex = m.texture(at: t) {
                enc.setRenderPipelineState(transparent ? blendedPipeline : texturedPipeline)
                enc.setFragmentTexture(tex, index: 0)
                enc.setFragmentTexture(lightmap, index: 1)
                enc.setFragmentBytes(&flags, length: 4, index: 2)
            } else {
                enc.setRenderPipelineState(flatPipeline)
                enc.setFragmentTexture(lightmap, index: 1)
            }
            var params = DrawParams(model: model, scroll: m.scroll, billboard: m.flags & TC_FLAG_BILLBOARD != 0 ? 1 : 0)
            enc.setVertexBytes(&params, length: MemoryLayout<DrawParams>.stride, index: 2)
            enc.setVertexBuffer(m.vertices, offset: 0, index: 0)
            enc.drawIndexedPrimitives(type: .triangle, indexCount: m.indexCount, indexType: .uint32, indexBuffer: m.indices, indexBufferOffset: 0)
        }
        for m in meshes { draw(m, matrix_identity_float4x4) }
        for p in persos where !(playing && p.sourceIndex == edStaticPerso) {
            // Baked samples loop; blend each channel between neighbouring samples so motion is smooth
            // at any display rate and playback speed. Visibility follows the earlier sample.
            let src = p.source.pointee, nch = Int(src.channel_count)
            let s = tc_sample_at_time(t, src.sample_rate, UInt32(src.sample_count))
            let base = Int(s.s0) * nch
            for part in p.parts where part.channel < nch && src.shown![base + part.channel] == part.object {
                // Lerping full matrices is fine for samples this close together; skipped across a part swap.
                let m = float4x4(tc_channel_matrix(src.transforms, src.shown, UInt32(nch), s, UInt32(part.channel)))
                draw(part.mesh, p.world * m)
            }
        }
        if playing, let pl = player, let a = (edAnimations[edClip] ?? edAnimations["idle"])?.pointee {
            let s = tc_sample_at_phase(edPhase, UInt32(a.sample_count), edLoop)
            let nch = Int(a.channel_count), model = pl.modelMatrix
            for ch in 0..<nch {
                let obj = a.shown![Int(s.s0) * nch + ch]
                guard obj != UInt16(TC_HIDDEN), let meshes = edParts[obj] else { continue }
                let m = float4x4(tc_channel_matrix(a.transforms, a.shown, UInt32(nch), s, UInt32(ch)))
                for mesh in meshes { draw(mesh, model * m) }
            }
        }
        if !blended.isEmpty {
            blendPass = true
            enc.setDepthStencilState(blendedDepthState)
            for (m, model) in blended { draw(m, model) }
        }
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }
}
