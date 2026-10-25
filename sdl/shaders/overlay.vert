#version 450
// Screen transitions (docs/PORTALS.md): one quad with free corners over the frame, as overlay_vertex in
// Shaders.metal (the spinning logo is a rotated square). Must match struct overlay_quad in src/gpu.c.
layout(std140, set = 1, binding = 0) uniform Quad {
    vec4 corner[4];  // xy: position in viewport units (0..1, y down); zw: texture coordinates
} q;

layout(location = 0) out vec2 vUV;

void main() {
    const int order[6] = int[](0, 1, 2, 1, 3, 2);  // the exe's two triangles
    vec4 c = q.corner[order[gl_VertexIndex]];
    gl_Position = vec4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);
    vUV = c.zw;
}
