#version 450
// The particles: the vertex colour times the texture (when textured), the display gamma and the level's fog by view
// depth; added ones fade out into the fog. particle_fragment in Sources/OpenTonic/Shaders.metal.
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(std140, set = 3, binding = 0) uniform Particles {
    mat4 viewProjection;
    vec4 fog;
    vec4 fogColor;
    vec4 eye;
    vec4 forward;
    vec4 misc;
} p;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vWorld;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 c = vColor;
    if (p.misc.y > 0.5) c *= texture(tex, vUV);
    if (c.a <= 0.0) discard;
    vec3 rgb = pow(clamp(c.rgb, 0.0, 1.0), vec3(1.0 / p.misc.x));
    if (p.fog.z > 0.5 && p.fog.y > p.fog.x) {
        float t = clamp((dot(vWorld - p.eye.xyz, p.forward.xyz) - p.fog.x) / (p.fog.y - p.fog.x), 0.0, 1.0) * p.fog.w;
        rgb = p.misc.z > 0.5 ? rgb * (1.0 - t) : mix(rgb, p.fogColor.rgb, t);
    }
    outColor = vec4(rgb, c.a);
}
