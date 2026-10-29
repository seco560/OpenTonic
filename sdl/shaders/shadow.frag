#version 450
// Ed's round shadow: black, darkest in the middle, soft towards the rim, faded by the level's fog.
layout(std140, set = 3, binding = 0) uniform Shadow {
    mat4 viewProjection;
    vec4 center;
    vec4 axisU;
    vec4 axisV;
    vec4 normal;   // w: the share of the radius at full darkness
    vec4 fog;      // near, far, enabled (0/1), darkness (alpha)
    vec4 eye;
} s;

layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec3 vWorld;
layout(location = 0) out vec4 outColor;

void main() {
    float r = length(vLocal);
    if (r >= 1.0) discard;
    float a = s.fog.w * (1.0 - smoothstep(s.normal.w, 1.0, r));
    if (s.fog.z > 0.5 && s.fog.y > s.fog.x)
        a *= 1.0 - clamp((distance(vWorld, s.eye.xyz) - s.fog.x) / (s.fog.y - s.fog.x), 0.0, 1.0);
    outColor = vec4(0.0, 0.0, 0.0, a);
}
