// Shared by level.vert and level.frag. Port of Sources/OpenTonic/Shaders.metal: keep the two in sync by hand.
// std140; must match struct gpu_uniforms in src/renderer.c (checked there with _Static_assert).
// SDL_GPU SPIR-V sets: vertex uniforms in set 1, fragment textures in set 2, fragment uniforms in set 3.
layout(std140, set = UNIFORM_SET, binding = 0) uniform Uniforms {
    mat4 viewProjection;
    vec3 lightDirection;   // world space, normalised, pointing towards the light
    float ambient;
    float time;            // animation clock in seconds (paused with P)
    float bakedScale;      // vertex colour -> light factor (255/128: radiosity 128 = texture unchanged)
    uint lightSet;         // baked set shown (0-2), blended towards lightNext by lightBlend
    float gamma;           // display gamma, 1 = unchanged
    vec4 fog;              // near, far (view depth), enabled (0/1), unused
    vec4 fogColor;         // rgb, unused
    vec4 eye;              // camera position (world), unused
    vec4 camRight;         // camera axes (world), for billboards
    vec4 camUp;
    uint lightNext;        // the set a change blends towards (= lightSet when none runs)
    float lightBlend;      // 0..1
} u;
