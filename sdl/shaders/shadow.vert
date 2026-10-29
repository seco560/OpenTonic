#version 450
// Ed's round shadow (tc_viewer_shadow): a disc lying on the ground under him, 6 vertices, no vertex buffer.
// Must match struct shadow_params in src/gpu.c (std140).
layout(std140, set = 1, binding = 0) uniform Shadow {
    mat4 viewProjection;
    vec4 center;   // xyz on the ground, w radius
    vec4 axisU;    // two axes along the ground (unit), w unused
    vec4 axisV;
    vec4 normal;   // the ground's normal, w the share of the radius at full darkness
    vec4 fog;      // near, far, enabled (0/1), darkness (alpha)
    vec4 eye;      // camera position
} s;

layout(location = 0) out vec2 vLocal;   // -1..1 across the disc
layout(location = 1) out vec3 vWorld;

void main() {
    const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(-1, 1), vec2(1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 c = corners[gl_VertexIndex];
    // a few centimetres off the ground (the pipeline's depth bias does the rest)
    vec3 p = s.center.xyz + (s.axisU.xyz * c.x + s.axisV.xyz * c.y) * s.center.w + s.normal.xyz * 0.04;
    vLocal = c;
    vWorld = p;
    gl_Position = s.viewProjection * vec4(p, 1.0);
}
