import Foundation
import simd
import TonicCore

// Thin Swift wrappers over the portable C core (port/core). The core owns level data, collision
// and Ed's controller; these classes only manage lifetimes and convert to simd types.

struct CoreError: Error, CustomStringConvertible {
    let description: String
}

func float4x4(_ m: tc_mat4) -> simd_float4x4 { unsafeBitCast(m, to: simd_float4x4.self) }  // both column-major 4x4
func float4x4(_ m: tc_mat34) -> simd_float4x4 { var m = m; return float4x4(tc_mat4_from34(&m)) }
extension SIMD3 where Scalar == Float {
    init(_ v: tc_vec3) { self.init(v.x, v.y, v.z) }
}
func tcVec3(_ v: SIMD3<Float>) -> tc_vec3 { tc_vec3(x: v.x, y: v.y, z: v.z) }

/// A .ttlv level loaded by tc_level_load (file read here, parsed in C). Freed on deinit.
final class Level {
    private(set) var raw = tc_level()
    let folder: URL

    init(url: URL) throws {
        folder = url.deletingLastPathComponent()
        let data = try Data(contentsOf: url)
        var level = tc_level()
        let r = data.withUnsafeBytes { tc_level_load(&level, $0.bindMemory(to: UInt8.self).baseAddress, data.count) }
        guard r == TC_OK else { throw CoreError(description: String(cString: tc_result_string(r))) }
        raw = level
    }

    deinit { tc_level_free(&raw) }

    /// Texture files, resolved against the .ttlv's folder.
    var textures: [URL] {
        (0..<Int(raw.texture_count)).map { folder.appendingPathComponent(String(cString: raw.textures[$0]!)) }
    }
    var meshes: UnsafeBufferPointer<tc_mesh> { UnsafeBufferPointer(start: raw.meshes, count: Int(raw.mesh_count)) }
    var persos: UnsafeBufferPointer<tc_perso> { UnsafeBufferPointer(start: raw.persos, count: Int(raw.perso_count)) }
    var edParts: UnsafeBufferPointer<tc_ed_part> { UnsafeBufferPointer(start: raw.ed.parts, count: Int(raw.ed.part_count)) }
    var edAnimations: UnsafeBufferPointer<tc_ed_anim> { UnsafeBufferPointer(start: raw.ed.anims, count: Int(raw.ed.anim_count)) }
    var hasEd: Bool { raw.has_ed != 0 && raw.collision_triangle_count > 0 }

    var bounds: (min: SIMD3<Float>, max: SIMD3<Float>) {
        var lo = tc_vec3(), hi = tc_vec3()
        withUnsafePointer(to: &raw) { tc_level_bounds($0, &lo, &hi) }
        return (SIMD3(lo), SIMD3(hi))
    }
}

/// Play mode: the level's collision grid plus Ed's controller (both C), at stable addresses
/// because the tc_player keeps a pointer to its world.
final class PlaySession {
    private let world = UnsafeMutablePointer<tc_collision>.allocate(capacity: 1)
    private let player = UnsafeMutablePointer<tc_player>.allocate(capacity: 1)

    init?(level: Level) {
        var start = level.raw.ed.start
        guard level.hasEd, tc_collision_init(world, level.raw.collision, 3 * level.raw.collision_triangle_count, level.raw.collision_floors) == 0 else {
            world.deallocate(); player.deallocate()
            return nil
        }
        tc_player_init(player, world, tc_vec3(x: start.m.3, y: start.m.7, z: start.m.11), tc_heading_of(&start))
        tc_player_apply_floors(player)
    }

    deinit {
        tc_collision_free(world)
        world.deallocate()
        player.deallocate()
    }

    var position: SIMD3<Float> { SIMD3(player.pointee.position) }
    var heading: Float { player.pointee.heading }
    var horizontalSpeed: Float { tc_player_horizontal_speed(player) }
    var modelMatrix: simd_float4x4 { float4x4(tc_player_model_matrix(player)) }
    /// Current animation name ("idle", "walk", ...), matching the .ttlv clip names.
    var animation: String { String(cString: tc_anim_state_name(player.pointee.animation)) }

    /// Length of a one-shot clip, by .ttlv animation name (ignored for names the controller doesn't use).
    func setClipLength(_ name: String, _ seconds: Float) {
        for s in 0..<TC_ANIM_COUNT.rawValue where String(cString: tc_anim_state_name(tc_anim_state(s))) == name {
            withUnsafeMutableBytes(of: &player.pointee.clip_length) { $0.bindMemory(to: Float.self)[Int(s)] = seconds }
        }
    }

    func step(dt: Float, move: SIMD2<Float>, run: Bool, jump: Bool) {
        var input = tc_player_input(move: tc_vec2(x: move.x, y: move.y), run: run, jump: jump)
        tc_player_step(player, dt, &input)
    }

    func teleport(to p: SIMD3<Float>, heading: Float) { tc_player_teleport(player, tcVec3(p), heading) }

    /// Distance to the nearest hit along the segment, or nil.
    func raycast(from o: SIMD3<Float>, dir: SIMD3<Float>, maxDistance: Float) -> Float? {
        var hit = tc_hit()
        return tc_collision_raycast(world, tcVec3(o), tcVec3(dir), maxDistance, &hit) != 0 ? hit.distance : nil
    }
}
