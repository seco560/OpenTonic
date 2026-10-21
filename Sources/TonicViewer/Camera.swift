import simd

/// Free-fly camera in engine space (Z up).
/// Port control defaults: WASD moves, arrow keys turn the camera (replacing the original's numpad camera).
struct Camera {
    var position = SIMD3<Float>(0, 0, 0)
    var yaw: Float = 0      // radians, 0 = looking along +Y
    var pitch: Float = 0    // radians, positive = up

    /// Vertical field of view. Widescreen gets a wider horizontal view (Hor+), never a stretched image.
    var verticalFOV: Float = 55 * .pi / 180
    var near: Float = 0.05
    var far: Float = 2000

    var forward: SIMD3<Float> {
        SIMD3(sin(yaw) * cos(pitch), cos(yaw) * cos(pitch), sin(pitch))
    }

    var right: SIMD3<Float> { SIMD3(cos(yaw), -sin(yaw), 0) }

    func viewProjection(aspect: Float) -> float4x4 {
        perspective(fovY: verticalFOV, aspect: aspect, near: near, far: far) * lookAt(eye: position, center: position + forward, up: SIMD3(0, 0, 1))
    }
}

private func lookAt(eye: SIMD3<Float>, center: SIMD3<Float>, up: SIMD3<Float>) -> float4x4 {
    let f = normalize(center - eye)
    let s = normalize(cross(f, up))
    let u = cross(s, f)
    return float4x4(columns: (
        SIMD4(s.x, u.x, -f.x, 0),
        SIMD4(s.y, u.y, -f.y, 0),
        SIMD4(s.z, u.z, -f.z, 0),
        SIMD4(-dot(s, eye), -dot(u, eye), dot(f, eye), 1)))
}

/// Right-handed perspective with Metal's 0...1 clip-space depth.
private func perspective(fovY: Float, aspect: Float, near: Float, far: Float) -> float4x4 {
    let y = 1 / tan(fovY / 2), x = y / aspect, z = far / (near - far)
    return float4x4(columns: (
        SIMD4(x, 0, 0, 0),
        SIMD4(0, y, 0, 0),
        SIMD4(0, 0, z, -1),
        SIMD4(0, 0, z * near, 0)))
}
