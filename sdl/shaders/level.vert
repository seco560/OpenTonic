#version 450
#extension GL_GOOGLE_include_directive : require
// Port of level_vertex in Sources/TonicViewer/Shaders.metal.
#define UNIFORM_SET 1
#include "level_common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;   // rgba8 normalised: baked radiosity set 0; a = 0: none (persos)
layout(location = 4) in vec4 inColor1;  // alternative baked sets (day/night variants); a = 0: absent
layout(location = 5) in vec4 inColor2;
layout(location = 6) in vec2 inUV2;     // lightmap atlas coordinates (.ttlv v8); -1 = no lightmap

// Must match struct gpu_draw_params in src/renderer.c.
layout(std140, set = 1, binding = 1) uniform DrawParams {
    mat4 model;            // object -> world (identity for static meshes; perso world * channel)
    vec2 scroll;           // UV per second (material scrolling)
    uint billboard;        // 1: sprite quad (TC_FLAG_BILLBOARD), expanded towards the camera
} d;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vColor;
layout(location = 3) out vec3 vWorld;   // world position, for fog
layout(location = 4) out vec2 vUV2;     // lightmap atlas coordinates; x < 0: none

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
    vec4 c = u.lightSet == 1u ? inColor1 : u.lightSet == 2u ? inColor2 : inColor;
    vColor = c.a > 0.5 ? c : inColor;  // objects without that variant keep set 0
}
