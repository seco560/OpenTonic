# Moving the shared viewer logic into TonicCore (plan)

Status: steps 1-4 done (2026-10-24): `core/include/tonic/tc_viewer.h` + `core/src/tc_viewer.c`, `make -C core
viewcheck`, both frontends switched (`Camera.swift` and `sdl/src/camera.c` removed). The Swift app builds; the SDL
sources compile against SDL3 on macOS (not linked: no glslang there). Left: step 5 (parity, run by the user: Swift on
the Mac, SDL on Linux). Differences from the sketch below: `tc_viewer` is a public struct (read its fields; change
them through the functions); `fly_speed` instead of base speed; `tc_viewer_status(v, extra, buf, size)` takes the
frontend's extra settings (SDL: `", AA 4x"`) to put after the FOV; `ai_result` reports a `.ttai` that didn't load;
`tc_viewer_player`, `tc_viewer_can_play`, `tc_viewer_has_ai`, `tc_viewer_ai_active` (for the title refresh). The AI
status uses `→` in both builds now (SDL had `->`).

## Why
Both frontends carry the same non-platform logic, ported line by line from Swift to C: the free-fly and orbit
cameras, the key -> action -> Ed input mapping, the play session (collision, player, AI), choosing Ed's clip and its
playback rate, the viewer settings and title readout, the level-load bookkeeping, and the per-frame walk over
statics, persos and Ed that decides what to draw with which matrix and texture. Every physics/animation fix touches
`Renderer.swift` and `sdl/src/view.c` alike, and the copies have to be kept identical by hand. A third platform
(Vita, 3DS) would mean a third copy.

## Target
A new core module, `core/include/tonic/tc_viewer.h` + `core/src/tc_viewer.c`, under the core's layering rules
(core/README.md: C11, no platform headers, no file I/O, allocations through `tc_alloc`, clean with `-Wpedantic`).
The frontends keep only: window and events, key codes -> viewer actions, GPU resources and drawing, texture/PNG
loading, file reading, audio, the intro movie, the level picker, and their own extras (the SDL build's MSAA).

## API sketch
Names are suggestions; keep them consistent with the rest of the core (`tc_` prefix, snake_case).

### 1. Camera (replaces `Camera.swift`, `sdl/src/camera.c`)
```c
typedef struct tc_camera {
    tc_vec3 position;
    float yaw, pitch;          /* radians; yaw 0 = looking along +Y, pitch > 0 = up */
    float vertical_fov;        /* Hor+: widescreen widens the view, never stretches */
    float near_z, far_z;
} tc_camera;
void tc_camera_init(tc_camera *c);                 /* fov 80 degrees, near 0.05, far 2000 */
tc_vec3 tc_camera_forward(const tc_camera *c);     /* (sin yaw cos pitch, cos yaw cos pitch, sin pitch) */
tc_vec3 tc_camera_right(const tc_camera *c);       /* (cos yaw, -sin yaw, 0) */
tc_mat4 tc_camera_view_projection(const tc_camera *c, float aspect);  /* right-handed, 0..1 depth */
```
0..1 clip depth suits both Metal and SDL_GPU (whose Vulkan backend flips the viewport itself).

### 2. Input (replaces the logic in `Input.swift`, `sdl/src/input.c`)
Frontends map their key codes to actions and report held state plus mouse deltas; the bindings stay per platform.
```c
typedef enum tc_view_action {
    TC_VA_FORWARD, TC_VA_BACK, TC_VA_LEFT, TC_VA_RIGHT, TC_VA_UP, TC_VA_DOWN,
    TC_VA_LOOK_LEFT, TC_VA_LOOK_RIGHT, TC_VA_LOOK_UP, TC_VA_LOOK_DOWN, TC_VA_FAST, TC_VA_JUMP,
    TC_VA_ACTION, TC_VA_SHOOT, TC_VA_LATERAL, TC_VA_COUNT
} tc_view_action;
void tc_viewer_set_action(tc_viewer *v, tc_view_action a, bool down);
void tc_viewer_release_all(tc_viewer *v);            /* focus lost */
void tc_viewer_mouse_look(tc_viewer *v, float dx, float dy);  /* accumulated, 0.003 rad per unit */
```
Internally: look (turn speed 1.6 rad/s, pitch clamped to +-1.5), free-fly (W/S along the view incl. pitch, R/F world
up/down, Shift x4, base speed), camera-relative move direction for the simple controller (length <= 1), and the
action -> `1 << TC_AI_IN_*` map (WASD = directions, Shift = SpeedUp, Space = Jump, E = Action, Q = Shoot,
C = Lateral).

### 3. Play session (replaces `Core.swift`'s `PlaySession`, `sdl/src/play.c`)
Owned by the viewer, heap-allocated inside it (the `tc_player` points at its `tc_collision`, the AI driver at the
player, so their addresses must not move). The frontend reads the files and passes bytes:
```c
typedef struct tc_viewer_desc {
    const tc_level *level;          /* loaded by the frontend; must outlive the viewer */
    const void *ai_data;            /* the .ttai next to the .ttlv, or NULL */
    size_t ai_size;
    const char *level_name;         /* "bar": the AI's per-level settings */
} tc_viewer_desc;
tc_result tc_viewer_init(tc_viewer *v, const tc_viewer_desc *desc);
void tc_viewer_free(tc_viewer *v);
```
It covers: creating the session only when the level has Ed, collision and an `idle` clip; clip lengths of the
one-shot clips; starting in play mode; Tab (`tc_viewer_toggle_play`: the orbit camera behind Ed on entry; on
re-entry after flying more than 1 unit away, raycast down from the camera and drop Ed 0.3 above the ground, facing
the view); I (`tc_viewer_toggle_ai`); the per-frame step (AI with last frame's camera eye, or the simple controller);
the orbit camera (target 1.1 above Ed, pulled in to `max(0.4, hit - 0.3)` when the level is in the way, distance
1.5-25, default 4.5); Ed's clip (the AI's `"s<anim>"` and frame, or the movement state's clip with walk/run
playing at the rate that matches Ed's ground speed, `dt * rate / 4 * speed`); Ed's model matrix (the AI's, so
the skier's board tilts); and the AI status string for the title bar (`B<n> R<n> state <n>[-><n>]`).

### 4. Settings and the title readout
State in the viewer, changed through small functions so both frontends behave the same:
- `tc_viewer_step_speed(v, +1/-1)`: steps 1/4, 1/3, 1/2, 2/3, 3/4, 1, 1.5, 2; `tc_viewer_toggle_pause`.
- `tc_viewer_set_light_set(v, n)` (only below `light_set_count`; holds it), `tc_viewer_follow_light(v)` (key 0: the
  level's light persos again: sud0's day / night cycle, Land's lightning, run from their scripts in the .ttlv's
  LAIS; `tc_viewer_status_live` says when to refresh the title), `tc_viewer_step_gamma(v, up)` (x1.1, 0.5-2.5),
  `tc_viewer_step_fov(v, +1/-1)` (5 degree steps, 30-100), `tc_viewer_toggle_fog`.
- `tc_viewer_cycle_sectors(v)` (K, levels with a .ttlv SECT): the sectors drawn, from Ed's sector: his sector
  and its list (the default, sky included) / all of them / his sector alone; `tc_viewer_ed_sector` (name:
  `tc_sectors_name`). docs/NOTES.md "Sectors".
- `tc_viewer_wheel(v, y)`: orbit zoom while playing, fly speed (0.5-500) in free-fly, both x1.1 per notch.
- `tc_viewer_status(v, buf, size)`: the UTF-8 readout both title bars show today: `view: anim 1.00×, gamma 1.00,
  FOV 80°[, lighting 1/2][, fog on (G)][  ·  PLAYING as Ed (Tab: free-fly)][  ·  AI ... (I: simple controller)]`.
  Frontends add their own prefix (level name, stats) and extras (SDL: `AA 4x`).

### 5. Level setup at load
In `tc_viewer_init`: fog from the level, light-set count, stats (meshes, persos, parts, triangles; texture counts
stay with the frontend, which knows which PNGs are missing), and the start camera: the centre of
`tc_level_bounds`, yaw 0, pitch -0.2, far `max(2000, size * 3)`, base speed `max(2, size / 40)` (size = the bounds'
diagonal).

### 6. Per-frame output
**Mesh table, uploaded once.** The viewer numbers every drawable mesh: the level's statics, then each perso's parts,
then Ed's parts. The frontend uploads them in that order and keeps its GPU handles in an array indexed the same
way. Empty meshes are numbered too (keep the indices stable) but never drawn.
```c
uint32_t tc_viewer_mesh_count(const tc_viewer *v);
const tc_mesh *tc_viewer_mesh(const tc_viewer *v, uint32_t i);
```
**Draw list, every frame.**
```c
typedef struct tc_view_draw {
    uint32_t mesh;      /* tc_viewer_mesh index */
    int32_t texture;    /* level texture for this frame (animated textures stepped), -1 = untextured */
    uint32_t flags;     /* the surface's TC_FLAG_* (alpha test, transparent, billboard) */
    uint32_t first_index, index_count;  /* the part of the mesh's indices to draw */
    tc_mat4 model;
    float ambient[3], diffuse[3]; /* the material's, for the formula */
} tc_view_draw;
const tc_view_draw *tc_viewer_draws(tc_viewer *v, uint32_t *count, uint32_t *opaque_count);
```
Order: opaque statics, persos (each part's channel matrix at the current sample, skipping parts whose object isn't
shown; the static Ed perso hidden while playing), Ed's channels at his phase, then every `TC_FLAG_TRANSPARENT` draw
at the end (`[opaque_count, count)`). A frontend with a missing texture (its handle NULL for that index) falls back
to the flat pipeline itself. Storage is the viewer's, valid until the next call. Draw the indices
`[first_index, first_index + index_count)` of the mesh: all of them except in a sector mode, where a static mesh
(one per material for the whole level) is drawn as the runs of its shown sectors' index ranges, one draw each.

**Shading values, every frame.** Both uniform layouts differ (Swift's `SIMD3<Float>` is 16 bytes, std140 packs the
float after a vec3), so the core fills a plain struct and each frontend packs it:
```c
typedef struct tc_view_frame {
    tc_mat4 view_projection;
    tc_vec3 light_direction;    /* normalize(0.4, -0.3, 0.85) */
    float ambient;              /* 0.45 */
    float time, baked_scale;    /* animation clock; 255 / 128 */
    uint32_t light_set, light_next;  /* the shaders lerp the two baked sets */
    float light_blend;
    float gamma;
    float fog_near, fog_far;
    bool fog_on;
    float fog_color[3];
    tc_vec3 eye, cam_right, cam_up;
    float clear_color[3];       /* the fog colour with fog, else (0.08, 0.09, 0.14) */
    uint32_t lighting;          /* tc_view_lighting: the original's formula (default) / the port's old look (L) */
    float fog_strength;         /* 0..1 while the fog fades with Ed's sector */
    float ambient_light[3], sky_light[3];  /* the ambient lights' sum; the sky's light (water sheen) */
    uint32_t light_count;
    tc_view_light lights[TC_VIEW_MAX_LIGHTS];  /* Ed's sector's spherical and parallel lights, x their fade */
} tc_view_frame;
void tc_viewer_frame(const tc_viewer *v, float aspect, tc_view_frame *out);
void tc_viewer_update(tc_viewer *v, float dt);  /* input, Ed, cameras, animation clock */
```

## Steps
1. Write `tc_viewer.h/.c` from `sdl/src/view.c`, `play.c`, `camera.c`, `input.c` (the C ports, so mostly moving
   code), checking constants against `Renderer.swift`, `Core.swift`, `Input.swift`, `Camera.swift` and `main.swift`
   where they might have drifted. Add it to `tonic.h`; `make -C core` and the SwiftPM `TonicCore` target pick
   up new files by themselves.
2. A headless check, `core/tests/viewcheck.c` (+ `make viewcheck`): load a level, run `tc_viewer_update` for a
   while with scripted actions, and print draw counts and Ed's position, so the module is tested without a GPU.
3. Switch the SDL build: `view.c` keeps the GPU side (mesh upload by table index, pipelines, packing
   `tc_view_frame` into `gpu_uniforms`, drawing the list); `play.c` keeps only `tv_level` (file reading);
   `camera.c` goes; `input.c` shrinks to the scancode -> action table; `main.c`'s key handlers call the viewer.
4. Switch the Swift app the same way: `Renderer.swift` keeps Metal, `Core.swift` keeps `Level` (file and texture
   loading), `Input.swift` keeps the key-code table, `Camera.swift` goes, `main.swift`'s handlers call the viewer.
   Note how the C structs import (fixed `char[]` arrays become tuples, so prefer accessor functions).
5. Parity: both builds side by side on `bar` and one radiosity-lit level from the same camera, in play mode and
   free-fly; title readouts identical apart from the SDL extras. `make -C core check aicheck mechcheck crashcheck`
   unchanged.
6. Update core/README.md ("What a platform backend provides"), docs/SDL3.md (layout, porting notes) and this file
   (status: done, or remove it).

## Not in scope
Shaders stay per backend (`Shaders.metal`, `sdl/shaders/`, kept in sync by hand; generating MSL from the SPIR-V
with SPIRV-Cross is a separate step). The music table lookup, the level picker's listing and thumbnail matching are
small and file-system bound: they can follow later. The SDL build's alpha bleed on cutout textures could move into
the converter (`tools/ttre`) instead, so both builds get it at no runtime cost.
