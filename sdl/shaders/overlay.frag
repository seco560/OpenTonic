#version 450
// overlay_fragment in Shaders.metal. mode 0: black where the mask's alpha <= threshold (effect A: Direct3D alpha test
// LESSEQUAL); 1: black with the texture's alpha (effect B, the spinning logo); 2: solid black.
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(std140, set = 3, binding = 0) uniform Params {
    uint mode;
    float threshold;
} p;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    if (p.mode == 2u) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float a = texture(tex, vUV).a;
    if (p.mode == 0u) {
        if (a > p.threshold) discard;
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    outColor = vec4(0.0, 0.0, 0.0, a);
}
