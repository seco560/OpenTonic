#version 450
// The loading bar (main.swift's RainbowBar), drawn on a ui.vert quad over the bar's bounds (uv 0..1): a capsule of
// translucent black, filled up to the progress with sud0's rainbow pool texture (scrolling at the pool's 0.6 UV/s, in
// 3-point strips that wave), a soft gloss on the fill's upper half and a thin white outline. Plain white without the
// texture (the white 1x1 is bound then). Works in points (pixels / scale) so it looks as the Mac app's at any scale.
// Must match struct bar_params in src/gpu.c.
layout(set = 2, binding = 0) uniform sampler2D tex;  // repeat sampler
layout(std140, set = 3, binding = 0) uniform Params {
    vec4 size;  // bar width, height in pixels; pixels per point; progress 0..1
    vec4 time;  // seconds since the loading screen appeared
} p;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// Signed distance (points) to a horizontal capsule from x0 to x1, centred on cy, of radius r.
float capsule(vec2 q, float x0, float x1, float cy, float r) {
    float cx = clamp(q.x, x0 + r, max(x0 + r, x1 - r));
    return length(vec2(q.x - cx, q.y - cy)) - r;
}

// Premultiplied "over".
vec4 over(vec4 dst, vec3 rgb, float a) { return vec4(rgb * a, a) + dst * (1.0 - a); }

void main() {
    float k = p.size.z, progress = clamp(p.size.w, 0.0, 1.0), t = p.time.x;
    vec2 bounds = p.size.xy / k, q = vUV * bounds;
    // The bar inside a 1-point margin (the outline's room), y down.
    float x0 = 1.0, x1 = bounds.x - 1.0, y0 = 1.0, y1 = bounds.y - 1.0;
    float h = y1 - y0, r = h / 2.0, cy = bounds.y / 2.0;

    float d = capsule(q, x0, x1, cy, r);
    vec4 c = over(vec4(0.0), vec3(0.0), 0.55 * clamp(0.5 - d * k, 0.0, 1.0));

    float width = (x1 - x0) * progress;
    if (width > 0.0) {
        float df = capsule(q, x0, x0 + max(width, h), cy, r);
        float cover = clamp(0.5 - df * k, 0.0, 1.0);
        if (cover > 0.0) {
            float tile = h * 3.0, strip = 3.0;
            float sx = x0 + floor((q.x - x0) / strip) * strip;  // this strip's left edge
            float wave = sin(sx / 16.0 + t * 4.0) * h * 0.3;
            float scroll = fract(t * 0.6) * tile;
            vec2 origin = vec2(x0 - tile + sx * 0.15, y0 + scroll - wave);
            // Explicit gradients: the strips' jumps in uv would pick the wrong mip level at their edges.
            float g = 1.0 / (tile * k);
            vec4 texel = textureGrad(tex, (q - origin) / tile, vec2(g, 0.0), vec2(0.0, g));
            c = over(c, texel.rgb, texel.a * cover);
            c = over(c, vec3(1.0), 0.35 * clamp(1.0 - (q.y - y0) / (cy - y0), 0.0, 1.0) * cover);
        }
    }

    c = over(c, vec3(1.0), 0.6 * clamp((0.5 - abs(d)) * k + 0.5, 0.0, 1.0));
    outColor = c.a > 0.0 ? vec4(c.rgb / c.a, c.a) : vec4(0.0);
}
