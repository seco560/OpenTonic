#version 450
#extension GL_GOOGLE_include_directive : require
// Port of level_vertex in Sources/OpenTonic/Shaders.metal.
#define UNIFORM_SET 1
#include "level_common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;   // rgba8 normalised: baked radiosity set 0; a = 0: none (persos), ~0.5: a sky's
layout(location = 4) in vec4 inColor1;  // alternative baked sets (day/night variants); a = 0: absent
layout(location = 5) in vec4 inColor2;
layout(location = 6) in vec2 inUV2;     // lightmap atlas coordinates (.ttlv LMAP); -1 = no lightmap
layout(location = 7) in vec3 inCentre;  // the object's bounding-sphere centre (model space): what lights are culled by

// Must match struct gpu_draw_params in src/renderer.c.
layout(std140, set = 1, binding = 1) uniform DrawParams {
    mat4 model;            // object -> world (identity for static meshes; perso world * channel)
    vec2 scroll;           // UV per second (material scrolling)
    uint billboard;        // 1: sprite quad (TC_FLAG_BILLBOARD), expanded towards the camera
    vec4 ambient;          // rgb: the material's ambient
    vec4 diffuse;          // rgb: the material's diffuse
} d;

// The frame's lights (tc_view_light; struct gpu_lights in src/view.c): spherical and parallel, world space.
struct Light {
    vec4 position;         // xyz; w: 2 spherical, 1 parallel
    vec4 direction;        // parallel: xyz the way it shines; w: near
    vec4 color;            // rgb (x its fade); w: far
};
layout(std140, set = 1, binding = 2) uniform Lights {
    uint count;
    Light light[48];       // TC_VIEW_MAX_LIGHTS
} lights;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vColor;
layout(location = 3) out vec3 vWorld;   // world position, for fog
layout(location = 4) out vec2 vUV2;     // lightmap atlas coordinates; x < 0: none

// A baked set's colour; objects without that set keep set 0.
vec4 baked(uint s) {
    vec4 c = s == 1u ? inColor1 : s == 2u ? inColor2 : inColor;
    return c.a > 0.25 ? c : inColor;
}

// The original's vertex light (game_light in Shaders.metal; docs/DECOMP.md "Vertex colour"): the baked radiosity (none
// on persos) plus the frame's lights, through the material: min(1, (acc x diffuse + ambient + G x diffuse) x 512 / 255).
vec3 game_light(vec4 bakedColor, vec3 world, vec3 n, vec3 centre) {
    vec3 acc = bakedColor.a > 0.25 ? bakedColor.rgb * (255.0 / 256.0) : vec3(0.0);
    for (uint i = 0u; i < min(lights.count, 48u); i++) {
        Light l = lights.light[i];
        if (l.position.w < 1.5) {  // parallel
            acc += l.color.rgb * max(0.0, -dot(n, l.direction.xyz));
            continue;
        }
        float far2 = l.color.w * l.color.w, near2 = l.direction.w * l.direction.w;
        vec3 c = centre - l.position.xyz;
        if (dot(c, c) > far2) continue;  // the object's centre beyond far: not lit by it (GLIGLOU_00443940)
        vec3 dv = world - l.position.xyz;
        float d2 = dot(dv, dv);
        if (d2 >= far2) continue;
        if (d2 <= 0.0) { acc += l.color.rgb; continue; }
        float nl = -dot(dv, n) * inversesqrt(d2);
        if (nl <= 0.0) continue;
        acc += l.color.rgb * (d2 < near2 ? nl : nl * (far2 - d2) / (far2 - near2));
    }
    vec3 add = d.ambient.rgb + u.ambientLight.rgb * d.diffuse.rgb;
    return clamp((acc * d.diffuse.rgb + add) * (512.0 / 255.0), 0.0, 1.0);
}

void main() {
    vec4 world = d.model * vec4(inPosition, 1.0);
    vNormal = (d.model * vec4(inNormal, 0.0)).xyz;  // shade() renormalises
    if (d.billboard != 0u) {  // centre + corner offset in the camera plane; unlit (zero normal = full light)
        world.xyz += u.camRight.xyz * inNormal.x + u.camUp.xyz * inNormal.y;
        vNormal = vec3(0.0);
    }
    gl_Position = u.viewProjection * world;
    vWorld = world.xyz;
    vUV2 = inUV2;
    vUV = inUV + fract(d.scroll * u.time);             // textures repeat, so wrap to keep precision
    vColor = mix(baked(u.lightSet), baked(u.lightNext), u.lightBlend);  // a change between sets: their lerp
    if (u.lighting == 0u) {  // the vertex's light itself (alpha 1); sprites unlit
        vec3 n = length(vNormal) > 0.0 ? normalize(vNormal) : vec3(0.0);
        vColor = d.billboard != 0u ? vec4(1.0)
               : vec4(game_light(vColor, world.xyz, n, (d.model * vec4(inCentre, 1.0)).xyz), 1.0);
    }
}
