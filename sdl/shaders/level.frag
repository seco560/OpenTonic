#version 450
#extension GL_GOOGLE_include_directive : require
// Port of level_fragment_textured / level_fragment_flat (built with -DFLAT) in Sources/OpenTonic/Shaders.metal.
#define UNIFORM_SET 3
#include "level_common.glsl"

#ifdef FLAT
layout(set = 2, binding = 0) uniform sampler2D lightmap;   // linear, clamp to edge
#else
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 2, binding = 1) uniform sampler2D lightmap;
// flags: TC_FLAG_*. coverage: drawn with alpha to coverage (opaque pipeline, MSAA on; SDL build only).
layout(std140, set = 3, binding = 1) uniform Flags { uint flags; uint coverage; } f;
// The original's alpha test for cut-outs: GREATEREQUAL 225 (TC_ALPHA_REF, GliDX6vr.dll; docs/DECOMP.md).
const float ALPHA_REF = 225.0 / 255.0;
#endif

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vColor;
layout(location = 3) in vec3 vWorld;
layout(location = 4) in vec2 vUV2;

layout(location = 0) out vec4 outColor;

// The original's lighting: the vertex's (level.vert). The port's earlier look: the baked radiosity x 255/128 (up to
// ~2x), skies neutral, persos (and the few IPOs without radiosity, alpha 0) a fixed key light plus ambient.
vec3 shade() {
    if (u.lighting == 0u) return vColor.rgb;
    if (vColor.a > 0.25 && vColor.a < 0.75) return vec3(1.0);  // a sky (TC_SKY_ALPHA)
    if (vColor.a > 0.25) return vColor.rgb * u.bakedScale;
    vec3 n = vNormal;
    float l = length(n) > 0.0 ? clamp(dot(normalize(n), u.lightDirection), 0.0, 1.0) : 1.0;
    return vec3(u.ambient + (1.0 - u.ambient) * l);
}

// Per-triangle lightmaps (bar, death, ...): the atlas texel replaces the baked vertex light, same scale.
vec3 light() {
    if (vUV2.x >= 0.0) return texture(lightmap, vUV2).rgb * u.bakedScale;
    return shade();
}

// The level's linear fog (visual environment), applied to the displayed colour. By view depth, as the original's
// driver (GLI_DRV_vComputeFogEffect: the projected vertex's z; docs/DECOMP.md "Read from GliDX6vr.dll"): none at
// near, full from far. The camera's forward axis is up x right (tc_viewer_frame: up = right x forward).
vec3 fogged(vec3 rgb) {
    if (u.fog.z < 0.5 || u.fog.y <= u.fog.x) return rgb;
    float depth = dot(vWorld - u.eye.xyz, cross(u.camUp.xyz, u.camRight.xyz));
    float t = clamp((depth - u.fog.x) / (u.fog.y - u.fog.x), 0.0, 1.0) * u.fog.w;  // w: faded in or out
    return mix(rgb, u.fogColor.rgb, t);
}

#ifndef FLAT
// Water sheen (TC_FLAG_WATER; water_sheen in Shaders.metal): upward faces of a water material get a Fresnel sky
// reflection and the light's highlight over moving ripples. rgb to add, the alpha the surface should at least have.
vec4 water_sheen() {
    vec3 n = normalize(vNormal);
    if (n.z < 0.9) return vec4(0.0);
    vec2 p = vWorld.xy;
    float t = u.time;
    vec2 r = vec2(sin(p.x * 0.9 + t * 1.3) + sin(p.y * 1.7 - t * 0.9) * 0.6,
                  cos(p.y * 1.1 + t * 1.1) + cos((p.x + p.y) * 0.7 - t * 1.6) * 0.6);
    n = normalize(vec3(r * 0.06, 1.0));
    vec3 v = normalize(u.eye.xyz - vWorld);
    // Seen from below (the eye under the surface): no sheen and the texture's own alpha, so the world above shows.
    if (v.z <= 0.0) return vec4(0.0);
    float fresnel = 0.04 + 0.96 * pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 5.0);
    vec3 sky = vec3(0.62, 0.74, 0.86);
    vec3 h = normalize(v + normalize(u.lightDirection));
    float spec = pow(clamp(dot(n, h), 0.0, 1.0), 80.0) * 0.9;
    // with the sky's light: dim at night (sud0's day / night cycle)
    return vec4((sky * fresnel * 0.8 + vec3(spec)) * u.skyLight.rgb, clamp(0.35 + fresnel, 0.0, 1.0));
}
#endif

void main() {
#ifdef FLAT
    outColor = vec4(fogged(pow(clamp(vec3(0.7) * light(), 0.0, 1.0), vec3(1.0 / u.gamma))), 1.0);
#else
    vec4 c = texture(tex, vUV);
    float a = c.a;
    if (f.coverage != 0u) {
        // Alpha to coverage: the alpha becomes the share of covered samples, so it must be 1 on opaque
        // surfaces. Alpha-tested cutouts keep the original's threshold, sharpened to about a pixel wide around
        // it: a crisp edge as with discard, but antialiased.
        a = 1.0;
        if ((f.flags & 1u) != 0u) {
            a = clamp((c.a - ALPHA_REF) / max(fwidth(c.a), 1e-4) + 0.5, 0.0, 1.0);
            if (a <= 0.0) discard;
        }
    } else if ((f.flags & 1u) != 0u && c.a < ALPHA_REF) {
        discard;
    }
    vec3 rgb = c.rgb * light();
    if ((f.flags & 8u) != 0u) {
        vec4 w = water_sheen();
        rgb += w.rgb;
        if (f.coverage == 0u) a = max(a, w.a);
    }
    outColor = vec4(fogged(pow(clamp(rgb, 0.0, 1.0), vec3(1.0 / u.gamma))), a);
#endif
}
