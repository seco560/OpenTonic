#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 color    [[attribute(3)]];  // rgba8 normalised: baked radiosity set 0; a = 0: none (persos)
    float4 color1   [[attribute(4)]];  // alternative baked sets (day/night variants); a = 0: absent
    float4 color2   [[attribute(5)]];
    float2 uv2      [[attribute(6)]];  // lightmap atlas coordinates (.ttlv v8); -1 = no lightmap
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

fragment float4 level_fragment_textured(VertexOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
                                        texture2d<float> lightmap [[texture(1)]],
                                        constant uint &flags [[buffer(2)]]) {
    float4 c = tex.sample(smp, in.uv);
    if ((flags & 1u) && c.a < 0.5) discard_fragment();
    return float4(fogged(pow(saturate(c.rgb * light(in, u, lightmap)), float3(1.0 / u.gamma)), in, u), c.a);
}

fragment float4 level_fragment_flat(VertexOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                                     texture2d<float> lightmap [[texture(1)]]) {
    return float4(fogged(pow(saturate(float3(0.7) * light(in, u, lightmap)), float3(1.0 / u.gamma)), in, u), 1.0);
}
