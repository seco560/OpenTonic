#version 450
#extension GL_GOOGLE_include_directive : require
// Port of level_fragment_textured / level_fragment_flat (built with -DFLAT) in Sources/TonicViewer/Shaders.metal.
#define UNIFORM_SET 3
#include "level_common.glsl"

#ifdef FLAT
layout(set = 2, binding = 0) uniform sampler2D lightmap;   // linear, clamp to edge
#else
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 2, binding = 1) uniform sampler2D lightmap;
// flags: TC_FLAG_*. coverage: drawn with alpha to coverage (opaque pipeline, MSAA on; SDL build only).
layout(std140, set = 3, binding = 1) uniform Flags { uint flags; uint coverage; } f;
#endif

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vColor;
layout(location = 3) in vec3 vWorld;
layout(location = 4) in vec2 vUV2;

layout(location = 0) out vec4 outColor;

// Static geometry: the engine's baked per-vertex radiosity (Gouraud), texture x light, up to ~2x.
// Persos (and the few IPOs without radiosity, alpha 0) keep the placeholder until sector lights are
// decoded: a fixed key light plus ambient, so geometry reads clearly in the viewer.
vec3 shade() {
    if (vColor.a > 0.5) return vColor.rgb * u.bakedScale;
    vec3 n = vNormal;
    float l = length(n) > 0.0 ? clamp(dot(normalize(n), u.lightDirection), 0.0, 1.0) : 1.0;
    return vec3(u.ambient + (1.0 - u.ambient) * l);
}

// Per-triangle lightmaps (bar, death, ...): the atlas texel replaces the baked vertex light, same scale.
vec3 light() {
    if (vUV2.x >= 0.0) return texture(lightmap, vUV2).rgb * u.bakedScale;
    return shade();
}

// The level's linear fog (visual environment), applied to the displayed colour.
vec3 fogged(vec3 rgb) {
    if (u.fog.z < 0.5 || u.fog.y <= u.fog.x) return rgb;
    float t = clamp((distance(vWorld, u.eye.xyz) - u.fog.x) / (u.fog.y - u.fog.x), 0.0, 1.0);
    return mix(rgb, u.fogColor.rgb, t);
}

void main() {
#ifdef FLAT
    outColor = vec4(fogged(pow(clamp(vec3(0.7) * light(), 0.0, 1.0), vec3(1.0 / u.gamma))), 1.0);
#else
    vec4 c = texture(tex, vUV);
    float a = c.a;
    if (f.coverage != 0u) {
        // Alpha to coverage: the alpha becomes the share of covered samples, so it must be 1 on opaque
        // surfaces. Alpha-tested cutouts keep the 0.5 threshold, sharpened to about a pixel wide around it:
        // a crisp edge as with discard, but antialiased.
        a = 1.0;
        if ((f.flags & 1u) != 0u) {
            a = clamp((c.a - 0.5) / max(fwidth(c.a), 1e-4) + 0.5, 0.0, 1.0);
            if (a <= 0.0) discard;
        }
    } else if ((f.flags & 1u) != 0u && c.a < 0.5) {
        discard;
    }
    outColor = vec4(fogged(pow(clamp(c.rgb * light(), 0.0, 1.0), vec3(1.0 / u.gamma))), a);
#endif
}
