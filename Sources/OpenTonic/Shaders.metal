#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 color    [[attribute(3)]];  // rgba8 normalised: baked radiosity set 0; a = 0: none (persos)
    float4 color1   [[attribute(4)]];  // alternative baked sets (day/night variants); a = 0: absent
    float4 color2   [[attribute(5)]];
    float2 uv2      [[attribute(6)]];  // lightmap atlas coordinates (.ttlv LMAP); -1 = no lightmap
};

struct Uniforms {
    float4x4 viewProjection;
    float3 lightDirection;   // world space, normalised, pointing towards the light
    float ambient;
    float time;              // animation clock in seconds (paused with P)
    float bakedScale;        // vertex colour -> light factor (255/128: radiosity 128 = texture unchanged)
    uint lightSet;           // baked set to show (0-2)
    float gamma;             // display gamma, 1 = unchanged
    float4 fog;              // near, far (eye distance), enabled (0/1), unused
    float4 fogColor;         // rgb, unused
    float4 eye;              // camera position (world), unused
    float4 camRight;         // camera axes (world), for billboards
    float4 camUp;
};

struct DrawParams {
    float4x4 model;          // object -> world (identity for static meshes; perso world * channel)
    float2 scroll;           // UV per second (material scrolling)
    uint billboard;          // 1: sprite quad (TC_FLAG_BILLBOARD), expanded towards the camera
};

struct VertexOut {
    float4 position [[position]];
    float3 normal;
    float2 uv;
    float4 color;
    float3 world;            // world position, for fog
    float2 uv2;              // lightmap atlas coordinates; x < 0: none
};

vertex VertexOut level_vertex(VertexIn in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                              constant DrawParams &d [[buffer(2)]]) {
    VertexOut out;
    float4 world = d.model * float4(in.position, 1.0);
    out.normal = (d.model * float4(in.normal, 0.0)).xyz;  // shade() renormalises
    if (d.billboard != 0) {  // centre + corner offset in the camera plane; unlit (zero normal = full light)
        world.xyz += u.camRight.xyz * in.normal.x + u.camUp.xyz * in.normal.y;
        out.normal = float3(0.0);
    }
    out.position = u.viewProjection * world;
    out.world = world.xyz;
    out.uv2 = in.uv2;
    out.uv = in.uv + fract(d.scroll * u.time);             // textures repeat, so wrap to keep precision
    float4 c = u.lightSet == 1 ? in.color1 : u.lightSet == 2 ? in.color2 : in.color;
    out.color = c.a > 0.5 ? c : in.color;  // objects without that variant keep set 0
    return out;
}

// Static geometry: the engine's baked per-vertex radiosity (Gouraud), texture x light, up to ~2x.
// Persos (and the few IPOs without radiosity, alpha 0) keep the placeholder until sector lights are
// decoded: a fixed key light plus ambient, so geometry reads clearly in the viewer.
static float3 shade(VertexOut in, constant Uniforms &u) {
    if (in.color.a > 0.5) return in.color.rgb * u.bakedScale;
    float3 n = in.normal;
    float l = length(n) > 0.0 ? saturate(dot(normalize(n), u.lightDirection)) : 1.0;
    return float3(u.ambient + (1.0 - u.ambient) * l);
}

// Per-triangle lightmaps (bar, death, ...): the atlas texel replaces the baked vertex light, same scale.
constexpr sampler lightmapSampler(filter::linear, address::clamp_to_edge);
static float3 light(VertexOut in, constant Uniforms &u, texture2d<float> lightmap) {
    if (in.uv2.x >= 0.0) return lightmap.sample(lightmapSampler, in.uv2).rgb * u.bakedScale;
    return shade(in, u);
}

// The level's linear fog (visual environment), applied to the displayed colour.
static float3 fogged(float3 rgb, VertexOut in, constant Uniforms &u) {
    if (u.fog.z < 0.5 || u.fog.y <= u.fog.x) return rgb;
    float f = saturate((distance(in.world, u.eye.xyz) - u.fog.x) / (u.fog.y - u.fog.x));
    return mix(rgb, u.fogColor.rgb, f);
}

// Water sheen (TC_FLAG_WATER, docs/NOTES.md "Water surfaces"): on the upward faces of a water material, a Fresnel
// reflection of the sky and the light's highlight (the material's specular is 0.9), over slowly moving ripples.
// Returns rgb to add and the alpha the surface should at least have.
float4 water_sheen(VertexOut in, constant Uniforms &u) {
    float3 n = normalize(in.normal);
    if (n.z < 0.9) return float4(0.0);
    float2 p = in.world.xy;
    float t = u.time;
    float2 r = float2(sin(p.x * 0.9 + t * 1.3) + sin(p.y * 1.7 - t * 0.9) * 0.6,
                      cos(p.y * 1.1 + t * 1.1) + cos((p.x + p.y) * 0.7 - t * 1.6) * 0.6);
    n = normalize(float3(r * 0.06, 1.0));
    float3 v = normalize(u.eye.xyz - in.world);
    // Seen from below (the eye under the surface): no sheen and the texture's own alpha, so the world above shows.
    if (v.z <= 0.0) return float4(0.0);
    float fresnel = 0.04 + 0.96 * pow(1.0 - saturate(dot(n, v)), 5.0);
    float3 sky = float3(0.62, 0.74, 0.86);
    float3 h = normalize(v + normalize(u.lightDirection));
    float spec = pow(saturate(dot(n, h)), 80.0) * 0.9;
    return float4(sky * fresnel * 0.8 + float3(spec), saturate(0.35 + fresnel));
}

// f.x: TC_FLAG_*; f.y: drawn with alpha to coverage (the opaque pipeline with MSAA on, as the SDL build).
fragment float4 level_fragment_textured(VertexOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
                                        texture2d<float> lightmap [[texture(1)]],
                                        constant uint2 &f [[buffer(2)]]) {
    float4 c = tex.sample(smp, in.uv);
    float a = c.a;
    if (f.y != 0u) {
        // Alpha to coverage: the alpha becomes the share of covered samples, so it must be 1 on opaque surfaces.
        // Alpha-tested cutouts keep the 0.5 threshold, sharpened to about a pixel wide around it: a crisp edge as
        // with discard, but antialiased.
        a = 1.0;
        if (f.x & 1u) {
            a = saturate((c.a - 0.5) / max(fwidth(c.a), 1e-4) + 0.5);
            if (a <= 0.0) discard_fragment();
        }
    } else if ((f.x & 1u) && c.a < 0.5) {
        discard_fragment();
    }
    float3 rgb = c.rgb * light(in, u, lightmap);
    if (f.x & 8u) {
        float4 w = water_sheen(in, u);
        rgb += w.rgb;
        if (f.y == 0u) a = max(a, w.a);
    }
    return float4(fogged(pow(saturate(rgb), float3(1.0 / u.gamma)), in, u), a);
}

fragment float4 level_fragment_flat(VertexOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                                     texture2d<float> lightmap [[texture(1)]]) {
    return float4(fogged(pow(saturate(float3(0.7) * light(in, u, lightmap)), float3(1.0 / u.gamma)), in, u), 1.0);
}

// ---- Screen transitions (docs/PORTALS.md): 2D quads over the frame, alpha blended, no depth ----

struct OverlayVertex { float2 position; float2 uv; };  // position: viewport units, 0..1, y down
struct OverlayOut { float4 position [[position]]; float2 uv; };

vertex OverlayOut overlay_vertex(uint vid [[vertex_id]], constant OverlayVertex *v [[buffer(0)]]) {
    OverlayOut out;
    out.position = float4(v[vid].position.x * 2 - 1, 1 - v[vid].position.y * 2, 0, 1);
    out.uv = v[vid].uv;
    return out;
}

// mode 0: black where the mask's alpha <= threshold (effect A: Direct3D alpha test LESSEQUAL); 1: black with the
// texture's alpha (effect B, the spinning logo); 2: solid black.
struct OverlayParams { uint mode; float threshold; };

fragment float4 overlay_fragment(OverlayOut in [[stage_in]], constant OverlayParams &p [[buffer(0)]],
                                 texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    if (p.mode == 2) return float4(0, 0, 0, 1);
    float a = tex.sample(s, in.uv).a;
    if (p.mode == 0) {
        if (a > p.threshold) discard_fragment();
        return float4(0, 0, 0, 1);
    }
    return float4(0, 0, 0, a);
}

// ---- Ed's round shadow (tc_viewer_shadow; the SDL build's shaders/shadow.vert/.frag) ----
// A disc lying on the ground under him, 6 vertices, no vertex buffer; drawn after the opaque draws, alpha blended,
// depth-tested without writing, biased towards the camera. Must match ShadowParams in Renderer.swift.
struct ShadowParams {
    float4x4 viewProjection;
    float4 center;           // xyz on the ground, w radius
    float4 axisU;            // two axes along the ground (unit), w unused
    float4 axisV;
    float4 normal;           // the ground's normal, w the share of the radius at full darkness
    float4 fog;              // near, far, enabled (0/1), darkness (alpha)
    float4 eye;              // camera position
};
struct ShadowOut { float4 position [[position]]; float2 local; float3 world; };  // local: -1..1 across the disc

constant float2 shadowCorners[6] = {float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, -1), float2(1, 1), float2(-1, 1)};

vertex ShadowOut shadow_vertex(uint vid [[vertex_id]], constant ShadowParams &s [[buffer(3)]]) {
    float2 c = shadowCorners[vid];
    // a few centimetres off the ground (the depth bias does the rest)
    float3 p = s.center.xyz + (s.axisU.xyz * c.x + s.axisV.xyz * c.y) * s.center.w + s.normal.xyz * 0.04;
    ShadowOut out;
    out.position = s.viewProjection * float4(p, 1.0);
    out.local = c;
    out.world = p;
    return out;
}

// Black, darkest in the middle, soft towards the rim, faded by the level's fog.
fragment float4 shadow_fragment(ShadowOut in [[stage_in]], constant ShadowParams &s [[buffer(3)]]) {
    float r = length(in.local);
    if (r >= 1.0) discard_fragment();
    float a = s.fog.w * (1.0 - smoothstep(s.normal.w, 1.0, r));
    if (s.fog.z > 0.5 && s.fog.y > s.fog.x)
        a *= 1.0 - saturate((distance(in.world, s.eye.xyz) - s.fog.x) / (s.fog.y - s.fog.x));
    return float4(0.0, 0.0, 0.0, a);
}

// ---- Supersampling: the level's frame, drawn at scale x the drawable's pixels, filtered down into it ----
// One full-screen triangle; linear filtering at the destination's pixel centres (at 2x a 2 x 2 box filter), as the
// SDL build's linear blit.
struct DownsampleOut { float4 position [[position]]; float2 uv; };

vertex DownsampleOut downsample_vertex(uint vid [[vertex_id]]) {
    float2 uv = float2((vid << 1) & 2, vid & 2);
    DownsampleOut out;
    out.position = float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
    out.uv = uv;
    return out;
}

fragment float4 downsample_fragment(DownsampleOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    return tex.sample(s, in.uv);
}
