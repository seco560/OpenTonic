#version 450
// The particles (tc_viewer_particles; particle_vertex in Sources/OpenTonic/Shaders.metal): triangles the core built
// facing the camera. Must match struct particle_params in src/gpu.c (std140).
layout(std140, set = 1, binding = 0) uniform Particles {
    mat4 viewProjection;
    vec4 fog;        // near, far, enabled (0/1), strength (0..1)
    vec4 fogColor;
    vec4 eye;
    vec4 forward;    // the camera's (view depth for the fog)
    vec4 misc;       // gamma, textured (0/1), additive (0/1), unused
} p;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vWorld;

void main() {
    vUV = aUV;
    vColor = aColor;
    vWorld = aPosition;
    gl_Position = p.viewProjection * vec4(aPosition, 1.0);
}
