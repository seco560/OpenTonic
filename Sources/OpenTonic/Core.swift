import Foundation
import simd
import TonicCore

// Thin Swift wrappers over the portable C core (core/). The core owns level data, collision, Ed's controller and
// the viewer's logic (tc_viewer.h); these classes only read files and manage lifetimes.

struct CoreError: Error, CustomStringConvertible {
    let description: String
}

func float4x4(_ m: tc_mat4) -> simd_float4x4 { unsafeBitCast(m, to: simd_float4x4.self) }  // both column-major 4x4
extension SIMD3 where Scalar == Float {
    init(_ v: tc_vec3) { self.init(v.x, v.y, v.z) }
}

/// A .ttlv level loaded by tc_level_load (file read here, parsed in C) at a stable address, because the viewer points
/// into it. Freed on deinit.
final class Level {
    let pointer: UnsafeMutablePointer<tc_level>
    let folder: URL
    let url: URL

    init(url: URL) throws {
        let data = try Data(contentsOf: url)
        let p = UnsafeMutablePointer<tc_level>.allocate(capacity: 1)
        p.initialize(to: tc_level())
        let r = data.withUnsafeBytes { tc_level_load(p, $0.bindMemory(to: UInt8.self).baseAddress, data.count) }
        guard r == TC_OK else {
            p.deallocate()  // left empty by tc_level_load
            throw CoreError(description: String(cString: tc_result_string(r)))
        }
        pointer = p
        self.url = url
        folder = url.deletingLastPathComponent()
    }

    deinit { tc_level_free(pointer); pointer.deallocate() }

    var raw: tc_level { pointer.pointee }
    /// File name without extension ("bar").
    var name: String { url.deletingPathExtension().lastPathComponent }
    /// Texture files, resolved against the .ttlv's folder.
    var textures: [URL] {
        (0..<Int(raw.texture_count)).map { folder.appendingPathComponent(String(cString: raw.textures[$0]!)) }
    }
}

/// The core's viewer (tc_viewer.h): cameras, play mode (Ed's AI from the .ttai next to the .ttlv, or the simple
/// controller), settings and the per-frame draw list. Keeps its level alive.
final class Viewer {
    let raw: UnsafeMutablePointer<tc_viewer>
    let level: Level

    /// carry: arriving through a portal (the last level's `pendingLevel`).
    init(level: Level, carry: tc_viewer_carry? = nil) throws {
        let v = UnsafeMutablePointer<tc_viewer>.allocate(capacity: 1)
        let aiURL = level.url.deletingPathExtension().appendingPathExtension("ttai")
        let ai = try? Data(contentsOf: aiURL)
        var carried = carry ?? tc_viewer_carry()
        let r = level.name.withCString { name in
            (ai ?? Data()).withUnsafeBytes { bytes in
                withUnsafePointer(to: &carried) { carryPointer in
                    var desc = tc_viewer_desc(level: level.pointer, ai_data: ai != nil ? bytes.baseAddress : nil,
                                              ai_size: ai?.count ?? 0, level_name: name,
                                              carry: carry != nil ? carryPointer : nil, seed: UInt32.random(in: 1...UInt32.max))
                    return tc_viewer_init(v, &desc)  // copies the model, the name and the carry
                }
            }
        }
        guard r == TC_OK else {
            v.deallocate()  // left empty by tc_viewer_init
            throw CoreError(description: String(cString: tc_result_string(r)))
        }
        if ai != nil, v.pointee.ai_result != TC_OK {
            print("\(aiURL.lastPathComponent): \(String(cString: tc_result_string(v.pointee.ai_result)))")
        }
        raw = v
        self.level = level
    }

    deinit { tc_viewer_free(raw); raw.deallocate() }

    var playing: Bool { raw.pointee.playing }
    var canPlay: Bool { tc_viewer_can_play(raw) }
    var aiActive: Bool { tc_viewer_ai_active(raw) }
    var stats: tc_view_stats { raw.pointee.stats }

    /// What to draw over the frame this frame (screen transitions, black, the loading screen).
    var overlay: tc_view_overlay {
        var o = tc_view_overlay()
        tc_viewer_overlay(raw, &o)
        return o
    }
    /// After a red portal's loading screen: the level to open and what Ed brings along (a copy).
    var pendingLevel: tc_viewer_carry? { tc_viewer_pending_level(raw)?.pointee }
    /// The transition textures, relative to the level folder.
    var effectTextures: [String] {
        (0..<tc_viewer_effect_count(raw)).map { String(cString: tc_viewer_effect_texture(raw, $0)) }
    }

    /// The settings readout for the title bar ("view: anim 1.00×, ..."); extra = the app's own settings after the FOV.
    func status(extra: String = "") -> String {
        var buf = [CChar](repeating: 0, count: 512)
        tc_viewer_status(raw, extra, &buf, buf.count)
        return String(decoding: buf.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
    }
}
