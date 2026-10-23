#version 450
// 2D quads for the level picker and overlays: one textured, tinted rectangle per draw (6 vertices, no vertex buffer).
// Must match struct ui_quad in src/renderer.c.
layout(std140, set = 1, binding = 0) uniform Quad {
    vec4 rect;    // x, y, width, height in target pixels (top-left origin)
    vec4 uv;      // u, v, width, height in the texture
    vec4 color;   // multiplies the texel
    vec4 target;  // target width, height in pixels
} q;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
    const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));
    vec2 c = corners[gl_VertexIndex];
    vec2 p = q.rect.xy + c * q.rect.zw;
    gl_Position = vec4(p.x / q.target.x * 2.0 - 1.0, 1.0 - p.y / q.target.y * 2.0, 0.0, 1.0);
    vUV = q.uv.xy + c * q.uv.zw;
    vColor = q.color;
}
