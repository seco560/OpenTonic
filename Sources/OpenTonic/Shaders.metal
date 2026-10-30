#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 color    [[attribute(3)]];  // rgba8 normalised: baked radiosity set 0; a = 0: none (persos), ~0.5: a sky's
    float4 color1   [[attribute(4)]];  // alternative baked sets (day/night variants); a = 0: absent
    float4 color2   [[attribute(5)]];
    float2 uv2      [[attribute(6)]];  // lightmap atlas coordinates (.ttlv LMAP); -1 = no lightmap
    float3 centre   [[attribute(7)]];  // the object's bounding-sphere centre (model space): what lights are culled by
};

struct Uniforms {
    float4x4 viewProjection;
    float3 lightDirection;   // world space, normalised, pointing towards the light
    float ambient;
    float time;              // animation clock in seconds (paused with P)
    float bakedScale;        // vertex colour -> light factor (255/128: radiosity 128 = texture unchanged)
    uint lightSet;           // baked set shown (0-2), blended towards lightNext by lightBlend
    float gamma;             // display gamma, 1 = unchanged
    float4 fog;              // near, far (view depth), enabled (0/1), strength (0..1: fading with a sector change)
    float4 fogColor;         // rgb, unused
    float4 eye;              // camera position (world): fog depth, the water sheen, the water effect
    float4 camRight;         // camera axes (world), for billboards
    float4 camUp;
    uint lightNext;          // the set a change blends towards (= lightSet when none runs)
    float lightBlend;        // 0..1
    uint lighting;           // 0: the original's formula (tc_view_lighting TC_VL_GAME), 1: the port's earlier look
    float4 ambientLight;     // rgb: the ambient lights' sum (G)
    float4 skyLight;         // rgb: the sky's light (day / night), for the water's sheen
};

struct DrawParams {
    float4x4 model;          // object -> world (identity for static meshes; perso world * channel)
    float2 scroll;           // UV per second (material scrolling)
    uint billboard;          // 1: sprite quad (TC_FLAG_BILLBOARD), expanded towards the camera
    float4 ambient;          // rgb: the material's ambient
    float4 diffuse;          // rgb: the material's diffuse
    float4 water;            // x: the water plane's height, y: 1 drawn under the water effect (tc_view_draw.underwater)
};

// The frame's lights (tc_view_light): spherical and parallel, world space.
struct Light {
    float4 position;         // xyz; w: 2 spherical, 1 parallel
    float4 direction;        // parallel: xyz the way it shines; w: near
    float4 color;            // rgb (x its fade); w: far
};
// The lights at offset 16, as Renderer.swift packs them. Not `uint3 pad`: a uint3 is 16-aligned in Metal, which put
// the array at 32 and read every light one float4 late (colour = the next light's position: Totalski red, pyramide green).
struct Lights {
    uint count;
    uint pad0, pad1, pad2;
    Light light[48];         // TC_VIEW_MAX_LIGHTS
};
static_assert(__builtin_offsetof(Lights, light) == 16, "Lights must match Renderer.swift's packing");

struct VertexOut {
    float4 position [[position]];
    float3 normal;
    float2 uv;
    float4 color;
    float3 world;            // world position, for fog
    float2 uv2;              // lightmap atlas coordinates; x < 0: none
};

// A baked set's colour; objects without that set keep set 0.
static float4 baked(VertexIn in, uint s) {
    float4 c = s == 1 ? in.color1 : s == 2 ? in.color2 : in.color;
    return c.a > 0.25 ? c : in.color;
}

// The original's vertex light (docs/DECOMP.md "Vertex colour"; tc_view_light): the baked radiosity (none on persos)
// plus the frame's lights, through the material: min(1, (acc x diffuse + ambient + G x diffuse) x 512 / 255).
// Spherical (GLIGLOU_00443a10): only when the object's centre is within far (GLIGLOU_00443940), then per vertex
// nearer than far and facing it: N.L, x (far^2 - d^2) / (far^2 - near^2) beyond near. Parallel (GLIGLOU_00444fc0):
// -N.direction where positive.
// Under the water effect (GLIGLOU_00442ce0), before the material: + the engine's default tint (-0.6, -0.3, -0.4) x the
// depth below the plane / 7, full from 7 deep (no level sets its own: visual environment +0 is null in all of them).
static float3 game_light(float4 bakedColor, float3 world, float3 n, float3 centre, constant DrawParams &d,
                         constant Uniforms &u, constant Lights &lights) {
    float3 acc = bakedColor.a > 0.25 ? bakedColor.rgb * (255.0 / 256.0) : float3(0.0);
    if (d.water.y > 0.5) acc += float3(-0.6, -0.3, -0.4) * saturate((d.water.x - world.z) / 7.0);
    for (uint i = 0; i < min(lights.count, 48u); i++) {
        Light l = lights.light[i];
        if (l.position.w < 1.5) {  // parallel
            acc += l.color.rgb * max(0.0, -dot(n, l.direction.xyz));
            continue;
        }
        float far2 = l.color.w * l.color.w, near2 = l.direction.w * l.direction.w;
        float3 c = centre - l.position.xyz;
        if (dot(c, c) > far2) continue;
        float3 dv = world - l.position.xyz;
        float d2 = dot(dv, dv);
        if (d2 >= far2) continue;
        if (d2 <= 0.0) { acc += l.color.rgb; continue; }
        float nl = -dot(dv, n) * rsqrt(d2);
        if (nl <= 0.0) continue;
        acc += l.color.rgb * (d2 < near2 ? nl : nl * (far2 - d2) / (far2 - near2));
    }
    float3 add = d.ambient.rgb + u.ambientLight.rgb * d.diffuse.rgb;
    return saturate((acc * d.diffuse.rgb + add) * (512.0 / 255.0));
}

// The water effect's wobble (GLIGLOU_00442ae0), on the projected vertex in the original's 640 x 480 pixels: x and y
// each move by 2 cos((pixel + frame) x 2 pi / 64) (y a quarter turn on) x an amplitude; the frame is the rendering
// counter (DAT_004ec8ac; here 60 per second of the animation clock). The camera above the water: what is below it
// wobbles, 1.4 from 5 deep, ramping from 0 at the surface; the camera below it (flag 8): only what is more than 5
// above the water. Only in front of the camera.
static float4 underwater_wobble(float4 clip, float depth, float eyeDepth, constant Uniforms &u) {
    float amp = eyeDepth < 0.0 ? (depth > 5.0 ? 1.4 : 0.0) : (depth >= 0.0 ? 0.0 : 1.4 * saturate(-depth / 5.0));
    if (amp <= 0.0 || clip.w <= 0.25) return clip;
    float2 px = (clip.xy / clip.w * float2(0.5, -0.5) + 0.5) * float2(640.0, 480.0);
    float frame = floor(u.time * 60.0), k = 2.0 * M_PI_F / 64.0;
    px += 2.0 * float2(cos((floor(px.x + 0.5) + frame) * k), cos((floor(px.y + 0.5) + 16.0 + frame) * k)) * amp;
    clip.xy = ((px / float2(640.0, 480.0) - 0.5) / float2(0.5, -0.5)) * clip.w;
    return clip;
}

vertex VertexOut level_vertex(VertexIn in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                              constant DrawParams &d [[buffer(2)]], constant Lights &lights [[buffer(4)]]) {
    VertexOut out;
    float4 world = d.model * float4(in.position, 1.0);
    out.normal = (d.model * float4(in.normal, 0.0)).xyz;  // shade() renormalises
    if (d.billboard != 0) {  // centre + corner offset in the camera plane; unlit (zero normal = full light)
        world.xyz += u.camRight.xyz * in.normal.x + u.camUp.xyz * in.normal.y;
        out.normal = float3(0.0);
    }
    // The water effect's lift (GLIGLOU_00442da0): with the camera not below the water, vertices below it are drawn
    // 0.7 x their depth higher (seen from above, the pool looks 0.3 as deep; everything under it alike, so Ed keeps
    // above its floor). Its test is in camera space (the plane's offset from the camera: flag 8 = the camera under the
    // water), not per object. The tint and the wobble keep the real depth, stored before the move.
    float3 real = world.xyz;
    bool water = d.water.y > 0.5 && d.billboard == 0;
    float eyeDepth = u.eye.z - d.water.x;
    if (water && eyeDepth >= 0.0) world.z += 0.7 * max(0.0, d.water.x - world.z);
    out.position = u.viewProjection * world;
    out.world = world.xyz;
    out.uv2 = in.uv2;
    out.uv = in.uv + fract(d.scroll * u.time);             // textures repeat, so wrap to keep precision
    out.color = mix(baked(in, u.lightSet), baked(in, u.lightNext), u.lightBlend);  // a change between sets: their lerp
    if (u.lighting == 0) {  // the vertex's light itself (alpha 1); sprites unlit
        float3 n = length(out.normal) > 0.0 ? normalize(out.normal) : float3(0.0);
        out.color = d.billboard != 0 ? float4(1.0)
                  : float4(game_light(out.color, real, n, (d.model * float4(in.centre, 1.0)).xyz, d, u, lights), 1.0);
        if (water) out.position = underwater_wobble(out.position, real.z - d.water.x, eyeDepth, u);
    }
    return out;
}

// The original's lighting: the vertex's (level_vertex). The port's earlier look: the baked radiosity x 255/128 (up to
// ~2x), skies neutral, persos (and the few IPOs without radiosity, alpha 0) a fixed key light plus ambient.
static float3 shade(VertexOut in, constant Uniforms &u) {
    if (u.lighting == 0) return in.color.rgb;
    if (in.color.a > 0.25 && in.color.a < 0.75) return float3(1.0);  // a sky (TC_SKY_ALPHA)
    if (in.color.a > 0.25) return in.color.rgb * u.bakedScale;
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

// The level's linear fog (visual environment), applied to the displayed colour. By view depth, as the original's
// driver (GLI_DRV_vComputeFogEffect: the projected vertex's z; DECOMP.md "Read from GliDX6vr.dll"): none at near,
// full from far. The camera's forward axis is up x right (tc_viewer_frame: up = right x forward).
static float3 fogged(float3 rgb, VertexOut in, constant Uniforms &u) {
    if (u.fog.z < 0.5 || u.fog.y <= u.fog.x) return rgb;
    float depth = dot(in.world - u.eye.xyz, cross(u.camUp.xyz, u.camRight.xyz));
    float f = saturate((depth - u.fog.x) / (u.fog.y - u.fog.x)) * u.fog.w;  // w: faded in or out (sector change)
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
    // with the sky's light: dim at night (sud0's day / night cycle). Kept faint (0.35; user, 2026-10-30: the original's
    // blue water is not this reflective): the texture's own colour and alpha stay the look.
    const float strength = 0.35;
    return float4((sky * fresnel * 0.8 + float3(spec)) * u.skyLight.rgb * strength, saturate(0.1 + fresnel * 0.5));
}

// The original's alpha test for cut-outs: GREATEREQUAL 225 (TC_ALPHA_REF, GliDX6vr.dll; DECOMP.md).
constant float alphaRef = 225.0 / 255.0;

// f.x: TC_FLAG_*; f.y: drawn with alpha to coverage (the opaque pipeline with MSAA on, as the SDL build).
fragment float4 level_fragment_textured(VertexOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
                                        texture2d<float> lightmap [[texture(1)]],
                                        constant uint2 &f [[buffer(2)]]) {
    float4 c = tex.sample(smp, in.uv);
    float a = c.a;
    if (f.y != 0u) {
        // Alpha to coverage: the alpha becomes the share of covered samples, so it must be 1 on opaque surfaces.
        // Alpha-tested cutouts keep the original's threshold, sharpened to about a pixel wide around it: a crisp
        // edge as with discard, but antialiased.
        a = 1.0;
        if (f.x & 1u) {
            a = saturate((c.a - alphaRef) / max(fwidth(c.a), 1e-4) + 0.5);
            if (a <= 0.0) discard_fragment();
        }
    } else if ((f.x & 1u) && c.a < alphaRef) {
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
