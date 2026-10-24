import Metal
import MetalKit
import simd
import TonicCore

/// Draws one converted level. The drawable always matches the view's backing pixels
/// (native Retina resolution). The core's tc_viewer (`Viewer`) owns the cameras, play mode, settings and
/// what to draw each frame; this uploads its mesh table and the level's textures once, then packs its shading
/// values into `Uniforms` and draws its list.
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
    /// Screen transitions and the loading screen's black (docs/PORTALS.md): 2D quads over the frame.
    private let overlayPipeline: MTLRenderPipelineState
    private let overlayDepthState: MTLDepthStencilState
    private let depthState: MTLDepthStencilState
    private let sampler: MTLSamplerState
    /// Per-triangle lightmap atlas (.ttlv v8), or a 1x1 white texture for levels without lightmaps.
    private var lightmap: MTLTexture?
    private var textures: [MTLTexture?] = []  // level.textures order; nil = missing
    private var meshes: [GPUMesh?] = []        // tc_viewer_mesh order; nil = empty
    private var effects: [MTLTexture?] = []    // tc_viewer_effect_texture order
    /// Called from draw: the loading screen to show (level, progress 0...1), or nil.
    var onLoading: ((String, Float)?) -> Void = { _ in }
    /// Called once when a red portal's loading screen is done: open that level with this carry.
    var onPendingLevel: (tc_viewer_carry) -> Void = { _ in }
    private var pendingSent = false
    private(set) var missingTextures = 0
    var textureCount: Int { textures.count - missingTextures }

    /// Cameras, play mode, settings, draw list (C core); keeps the level alive.
    private(set) var viewer: Viewer?
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
        let od = MTLRenderPipelineDescriptor()
        od.vertexFunction = library.makeFunction(name: "overlay_vertex")
        od.fragmentFunction = library.makeFunction(name: "overlay_fragment")
        od.colorAttachments[0].pixelFormat = view.colorPixelFormat
        od.colorAttachments[0].isBlendingEnabled = true
        od.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
        od.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
        od.depthAttachmentPixelFormat = view.depthStencilPixelFormat
        overlayPipeline = try device.makeRenderPipelineState(descriptor: od)
        texturedPipeline = try pipeline("level_fragment_textured")
        flatPipeline = try pipeline("level_fragment_flat")
        blendedPipeline = try pipeline("level_fragment_textured", blended: true)

        let ds = MTLDepthStencilDescriptor()
        ds.depthCompareFunction = .less
        ds.isDepthWriteEnabled = true
        depthState = device.makeDepthStencilState(descriptor: ds)!
        ds.isDepthWriteEnabled = false
        blendedDepthState = device.makeDepthStencilState(descriptor: ds)!
        ds.depthCompareFunction = .always
        overlayDepthState = device.makeDepthStencilState(descriptor: ds)!

        let sd = MTLSamplerDescriptor()
        sd.minFilter = .linear; sd.magFilter = .linear; sd.mipFilter = .linear
        sd.sAddressMode = .repeat; sd.tAddressMode = .repeat
        sd.maxAnisotropy = 8
        sampler = device.makeSamplerState(descriptor: sd)!
        super.init()
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
        textures = level.textures.map { url in
            do {
                return try loader.newTexture(URL: url, options: [.SRGB: false, .generateMipmaps: true,
                                                                  .textureStorageMode: MTLStorageMode.private.rawValue])
            } catch { missingTextures += 1; return nil }
        }
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

    func draw(in view: MTKView) {
        guard let v = viewer?.raw else { return }
        let now = CACurrentMediaTime()
        let dt = Float(min(now - (lastTime ?? now), 0.1))
        lastTime = now
        tc_viewer_update(v, dt)
        var sounds: UnsafePointer<UInt16>?
        let n = Int(tc_viewer_sounds(v, &sounds))
        if n > 0, let sounds { Sfx.shared.play(Array(UnsafeBufferPointer(start: sounds, count: n))) }

        let size = view.drawableSize
        var f = tc_view_frame()
        tc_viewer_frame(v, Float(size.width / max(size.height, 1)), &f)
        view.clearColor = MTLClearColor(red: Double(f.clear_color.0), green: Double(f.clear_color.1), blue: Double(f.clear_color.2), alpha: 1)
        guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
              let cmd = queue.makeCommandBuffer(), let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }
        var uniforms = Uniforms(viewProjection: float4x4(f.view_projection), lightDirection: SIMD3(f.light_direction),
                                ambient: f.ambient, time: f.time, bakedScale: f.baked_scale, lightSet: f.light_set, gamma: f.gamma,
                                fog: SIMD4(f.fog_near, f.fog_far, f.fog_on ? 1 : 0, 0),
                                fogColor: SIMD4(f.fog_color.0, f.fog_color.1, f.fog_color.2, 0), eye: SIMD4(SIMD3(f.eye), 0),
                                camRight: SIMD4(SIMD3(f.cam_right), 0), camUp: SIMD4(SIMD3(f.cam_up), 0))
        enc.setDepthStencilState(depthState)
        enc.setCullMode(.none)  // winding of OpenSpace elements not yet confirmed
        enc.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        enc.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        enc.setFragmentSamplerState(sampler, index: 0)

        var count: UInt32 = 0, opaque: UInt32 = 0
        let draws = tc_viewer_draws(v, &count, &opaque)!
        for i in 0..<Int(count) {
            // Opaque draws first, then the transparent ones: alpha blended, no depth writes.
            if i == Int(opaque) { enc.setDepthStencilState(blendedDepthState) }
            let d = draws[i]
            guard let m = meshes[Int(d.mesh)] else { continue }
            let transparent = d.flags & TC_FLAG_TRANSPARENT != 0
            var flags = d.flags
            if d.texture >= 0, let tex = textures[Int(d.texture)] {
                enc.setRenderPipelineState(transparent ? blendedPipeline : texturedPipeline)
                enc.setFragmentTexture(tex, index: 0)
                enc.setFragmentTexture(lightmap, index: 1)
                enc.setFragmentBytes(&flags, length: 4, index: 2)
            } else {
                enc.setRenderPipelineState(flatPipeline)
                enc.setFragmentTexture(lightmap, index: 1)
            }
            var params = DrawParams(model: float4x4(d.model), scroll: m.scroll, billboard: d.flags & TC_FLAG_BILLBOARD != 0 ? 1 : 0)
            enc.setVertexBytes(&params, length: MemoryLayout<DrawParams>.stride, index: 2)
            enc.setVertexBuffer(m.vertices, offset: 0, index: 0)
            enc.drawIndexedPrimitives(type: .triangle, indexCount: m.indexCount, indexType: .uint32, indexBuffer: m.indices, indexBufferOffset: 0)
        }
        drawOverlay(enc, viewer!)
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
        if !pendingSent, let carry = viewer?.pendingLevel {
            pendingSent = true
            let send = onPendingLevel
            DispatchQueue.main.async { send(carry) }  // opening a level replaces this renderer: not from inside draw
        }
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
