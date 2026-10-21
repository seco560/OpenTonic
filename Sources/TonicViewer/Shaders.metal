#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 color    [[attribute(3)]];  // rgba8 normalised: baked radiosity set 0; a = 0: none (persos)
    float4 color1   [[attribute(4)]];  // alternative baked sets (day/night variants); a = 0: absent
    float4 color2   [[attribute(5)]];
};

struct Uniforms {
    float4x4 viewProjection;
    float3 lightDirection;   // world space, normalised, pointing towards the light
    float ambient;
    float time;              // animation clock in seconds (paused with P)
    float bakedScale;        // vertex colour -> light factor (255/128: radiosity 128 = texture unchanged)
    uint lightSet;           // baked set to show (0-2)
    float gamma;             // display gamma, 1 = unchanged
};

struct DrawParams {
    float4x4 model;          // object -> world (identity for static meshes; perso world * channel)
    float2 scroll;           // UV per second (material scrolling)
};

struct VertexOut {
    float4 position [[position]];
    float3 normal;
    float2 uv;
    float4 color;
};

vertex VertexOut level_vertex(VertexIn in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                              constant DrawParams &d [[buffer(2)]]) {
    VertexOut out;
    out.position = u.viewProjection * (d.model * float4(in.position, 1.0));
    out.normal = (d.model * float4(in.normal, 0.0)).xyz;  // shade() renormalises
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

fragment float4 level_fragment_textured(VertexOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
                                        constant uint &flags [[buffer(2)]]) {
    float4 c = tex.sample(smp, in.uv);
    if ((flags & 1u) && c.a < 0.5) discard_fragment();
    return float4(pow(saturate(c.rgb * shade(in, u)), float3(1.0 / u.gamma)), c.a);
}

fragment float4 level_fragment_flat(VertexOut in [[stage_in]], constant Uniforms &u [[buffer(1)]]) {
    return float4(pow(saturate(float3(0.7) * shade(in, u)), float3(1.0 / u.gamma)), 1.0);
}
