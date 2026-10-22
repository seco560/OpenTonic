# RETonicTrouble – working notes

## Source data
- **Reference data: `Installed Files Linux/`.** It holds the complete game (WORLD, Textures.cnt, Vignette.cnt, Videos).
- **Reference exe:** `Original Disc Contents/TonicTrouble.exe` (27 Oct 1999, sha1 `e8472e67…`).
  - `Installed Files Linux/TonicTrouble.exe.bak` is identical.
  - The installed `TonicTrouble.exe` was modified by an earlier patch attempt. Don't use it.
- **Disc-only files:** `GAMEDATA/tt1–tt4.cnt` (about 1 GB, unknown non-CNT format, all four share the same first 32 bytes) and `InstalTT.exe`.
  - The installed game doesn't need them. Possibly installer payload or minimal-install data. Low priority.
  - `tt4.cnt` is damaged: everything from offset 318,697,474 to the end (about 43 MB) is zeros, from a bad copy.
- Both source trees are read-only (`chmod a-w`). Game data and anything extracted from it stay out of git (`out/`).

## Formats
| Ext | Status | Notes |
|-----|--------|-------|
| .cnt | done (`tools/ttre/cnt.py`) | All 6910 + 30 entries verified |
| .gf | done (`tools/ttre/gf.py`) | 565/1555/4444/88, planar RLE; all decode exactly. Rows are stored **bottom-up** (TGA origin); `decode` flips them (fixed 2026-10-19, PNGs regenerated: before that every PNG was upside down) |
| .sna/.rtb | done (`tools/ttre/sna.py`) | Rolling 10-byte XOR key; block header has **no** unk byte (unlike R2) but a trailing u8 after size; RTB entries are 6 bytes, no u32 after count. `sna.Level` merges fix + level + FixLvl.rtb: 112,289 pointers resolve in `bar`. Pointers to (255,255) are intentionally unrelocated. The 37 (bar) / 41 (sud0) unresolved fix→level (5,2) pointers are runtime links of the 3 fix persos, rewritten at load; see "Level structure" |
| .gpt/.ptx/.rtp/.rtt | done (`sna.py`) | Same encryption, no header. fix.rtp/.rtt: same format as RTB, one list keyed (4,0), but `address` = the pointer *value*, matched against the file's dwords in order (all 1086/450 match). Levels have no .rtp/.rtt: their GPT/PTX pointers are plain addresses in the level's memory view (bases from level.sna's block table; fix blocks keep their fix base there), resolved on demand, as raymap does. `fix.rts` (relocation for fix.snd?) not looked at |
| level structures | static geometry + animated persos + scrolling/flipbook materials + collision + Ed (`level.py`, `export.py`, `ttlv.py`) | See "Level structure", "Animation", "Play as Ed". `level-export` writes OBJ/MTL (static only) + previews (persos at frame 0); `level-ttlv` writes .ttlv v5 (baked vertex colours, collision, Ed's anims) with persos unless `--no-persos`. All 21 levels parse with no warnings |
| .bnm/.apm | later | vgmstream `ubi_sb.c` (BNM, TT-aware) and `ubi_apm.c`; FFmpeg has an APM decoder |
| .lsy/.bls/.dsb/.cst/.bpg | todo | dsb = game script/config |
| EDPROL.AVI | identified | See intro video below |

## Level structure (TT layouts; details in `tools/ttre/level.py` docstring)
- **Level GPT** = root: `u32 n` + n × 0x60 "perso in fix" records (ptr fix perso, 0x58 matrix, ptr new level SuperObject), then actual world, dynamic world, **father sector**, then always/object-type tables and 4 × 0x104 level-name strings. Two fields raymap reads as u32 ("soundEventIndex", "0x6F") are pointers in our data.
- **SuperObject** (0x38): type, data, children {head, tail, n}, next, prev, parent, matrix @0x20, matrix @0x24. TT types: 0 world, 4 perso, 8 sector, 0xD/0x15 IPO. @0x20 is **local**, @0x24 is the precomputed **global**: composing locals down the tree reproduces @0x24 for every SuperObject in all 21 levels.
- **Matrix** (0x58): type, pos, 3×3 rotation (columns), 3×3 **complete transform** R·S (columns); M = T·transform. Type bits seen: 0x80 identity, 0x10 translation, 0x20 rotation, 0x40 scale.
  - **Fixed 2026-10-19:** the second 3×3 was read as a pure scale, giving T·R·(R·S), so every rotated perso had its yaw doubled. Statics are all type 0x80, so they were unaffected. Evidence: in every rotated perso matrix (type 0x1B0, Land/bar/sud0/north) the second 3×3 equals the rotation exactly; in 0x1F0 the rotation is I and the second 3×3 holds the scale. This was the user-reported "portals face the wrong way" bug: Land's arch at (51.1, -6.1) went from -28° to 166°. Before/after: `out/lighting/Land_portal_{before,after}.png`. Characters' relative poses are unchanged (bar re-checked).
- Hierarchy: father sector → sectors → IPOs (nested IPOs allowed) → PhysicalObject (TT: 3 ptrs, no collide BV) → VisualSet (LODs; every level has 1 LOD, type 0) → GeometricObject → elements (type 1 = indexed triangles; one type 3 sprite element in north is skipped) → GameMaterial → VisualMaterial → TextureInfo (name @0x54 = Textures.cnt path with .tga, matches our PNGs case-insensitively: 0 missing).
- **PTX**: capacity 5120 slots; fix lists 450 TextureInfos; level lists `5120 - 450` slots (used ones first), then per-slot memory channel (1 fix, 2 level, 0xC0DE0005 free).
- Geometry is small: 117 (outro) to 5.3k (pyramide) static triangles per level. Engine space is right-handed Z-up; front faces are CCW (~95% agree with the vertex normals; the rest are probably double-sided). UV v=0 = GF row 0 = bottom of the upright image.
- TextureInfo `flags_byte` bit 0/1 match the name suffixes `_TX`/`_TY`/`_TXY` (tiling); raymap calls bit 1 "repeat U". Names ending `nz` have flags 0x1A (colour-keyed/transparent?).
- **Persos** (dynamic world = GPT `dynamic_world`, a type-0 SO whose children are type-4 SOs; data = Perso). Layouts in the `level.py` docstring, all checked on bar/sud0:
  - Perso → Perso3dData (+0) → initial state (+0), object list (+0xC, falls back to +0x10 / family default list +0x1C), family (+0x14), **inline 0x58 matrix at +0x18** (the SO's matrix pointer points there).
  - State +0xC → AnimationReference (TT 0x2C) → A3D (+0x28): 0x34 header + packed arrays, data lives in the SNA (no separate anim bank). Default pose = **frame 0 of the initial state**: per channel, keyframe `frames_kf[ch.frames_kf]` gives T(kf pos) · R(quat) · S(scale vector); TT hierarchies use channel *indices*; channel object = `ntto[num_ntto[ch + frame]]` → object-list entry (scale ptr, PhysicalObject). Quaternions (|q| = 0x4000) need the **conjugate** of the textbook matrix (chosen visually: textbook form scrambles Ed's limbs).
  - Fix persos (Ed and two others, 3 in every level): the GPT perso-in-fix matrix replaces the stale fix p3d matrix (bar: Ed at (73, 49, -0.4), fix copy says (100, -332, 130)).
  - Validated with software previews (`out/persos/*.png`): bar shows Ed ×2 and a third character behind the counter, all standing on the floor; sud0's four 15×15 stone platforms exactly fill the lava pit. Counts (dynamic-world persos / with geometry / parts): bar 10/3/20, sud0 326/197/622, north 212/149/450, Land 226/139/333, LeCanyon 200/150/820, pyramide 339/235/657. Persos without object lists are cameras/generators/etc.
  - Not done: other states (only the initial state's animation loops; no state transitions/AI), morphs, the A3D `frame` root vector/quaternion (raymap ignores it too), perso sprites (geometry element type 3), `level-export` OBJ persos.
- **Unresolved fix→level pointers (answered):** all belong to the 3 fix persos' runtime state and are rewritten at load. In bar: std_game+0xC ×3 (= the new level SuperObject, as raymap patches it), Perso3dData +0x70, +0x94 (Ed only) and +0x164/+0x168/+0x16C ×3, plus 6 triplets of consecutive pointers and 3 single pointers elsewhere in fix (probably linked-list next/prev/header nodes into level data; not traced). None are needed for rendering.
- Not exported yet: sprites, sector data (lights, fog, neighbours, sky material). Collision: see "Play as Ed".

## Lighting (stage 1: baked radiosity, 2026-10-19)
- **IPO** (0xC) = {ptr PhysicalObject, ptr radiosity, ptr ISI}.
  - IPO+4 radiosity: all zero in every level file. Probably the runtime "current" buffer.
  - ISI = {ptr radiosity A, ptr B, ptr C}. A is present in all 21 levels. B and C are radiosities only in **sud0 and Land**; elsewhere B is null and C is some other struct.
- **Radiosity** = {u32 n_lod, ptr lods}; lod = {u32 n_colors, ptr colors}; colour = **i16 r, g, b, 0** (8 bytes). raymap reads the same Radiosity/RadiosityLOD shape but never loads it.
  - n_colors = the LOD-0 vertex count for every IPO, except 8 (bar, cock01, marmite, pyramide) whose ISI +0 is not a radiosity. Those keep the fallback lighting.
  - Values are signed. Medians are 20–100, typical range 0–400, outliers -5100..2400. Negative = darker.
- **sud0 sets = lighting configurations:** A = dusk (red sky), B = day (blue sky, (-43, 184, 393)), C = night (dark blue). Previews: `out/scratch/light_sud0_set{1,2}.png`. Land's B is biased around 25600, so not plain colours.
  - Which set the game selects (and whether it blends them into IPO+4) is **not traced**. The export uses A everywhere.
- **Combine (guessed visually, not from the exe):** light = clamp(rgb, 0, 255) / 128, texture × light, saturated (so up to 2× overbright).
  - At /255, bar and Land are too dark, but sector lights and ambient (stage 3) may add to this. Re-check the scale once those exist.
  - Tunable: `level.RADIOSITY_ONE` (Python preview) and `bakedScale` (viewer uniform).
- **Material coefficients:** diffuse is always 0.7; ambient varies per material (0 / 0.1 / 0.36 / 0.87); colour is (1, 0, 1, 0). They are **not** applied to radiosity: bar's ambient-0 materials aren't black under radiosity. They most likely scale the dynamic sector ambient and lights (stage 3).
- **Persos** have no radiosity. They keep the placeholder key light plus ambient (sector ambient not looked up).
- **.ttlv v3**: 36-byte vertex = pos3, normal3, uv2, u8 rgba. rgb = clamped radiosity; a = 255 means baked, 0 means fallback (perso parts and the 8 IPOs). The viewer reads v1–v3. The shader computes texture × rgb × 255/128 when a > 0.5.
- Previews: `out/lighting/{bar,sud0}_{before,baked}.png`, `out/scratch/light_{bar,sud0,Land}_*` (scale and set experiments).
- **Fog (stage 4):** not located. Not in GameMaterial/VisualMaterial (no fog fields seen). In this engine family, fog is usually a sector light/LightInfo of type fog, so look for it with the sector lights (stage 3).

## Animation and moving textures (2026-10-19)
- **A3D playback** (`level.Anim`, follows raymap's TT path): integer frame f at `fps` = State byte @0x3B (raymap's `state.speed`), falling back to A3D header u16 @0 (they agree for most persos: 15/20/25/30). Per channel, keyframe `kf = keyframes[frames_kf[ch.frames_kf + f]]`; next = `kf + 1`, or the channel's first keyframe if `kf.flags & 0x80` (end; then diff = n_frames - 1 + next.frame - kf.frame). Lerp position vector and multiplier separately, quaternion nlerp (sign-corrected), scale vector lerp; `u16 interp` (raymap: × 1/8192) is unused there and by us. Hierarchy {child, parent} channel indices and NTTO (object switch; flag 2 = hidden) are per frame via the frame record. Many persos switch objects per frame (bar 20 frame-0 parts → 45 distinct parts).
- **Quaternion direction re-verified with motion**: conjugate (transpose of textbook) keeps sud0 villager 0x1a1b37's arms and head attached through the loop; textbook detaches the arm and head (`out/anim/sud0_1a1b37_close.png` vs `out/scratch/sud0_1a1b37_textbook.png`). raymap's `AnimQuaternion.ToMatrix` is also the transpose.
- **Engine time unit = 1/60 s** (confirmed in the exe): GAM_0041cc90 stores the TMR timer delta in ms at 0x8BD098; the game loop calls FUN_00445710(ms × 0.06), which sets frame delta 0x90BA84, accumulated time 0x4EE454 and a frame counter 0x4EE458 (menus pass 1.0).
- **VisualMaterial is 8 bytes off in raymap's TT reader, and TT does have animated textures.** GLI FUN_004457d0 (per-material per frame): if scroll_on @0x68, `cur(@0x58) += speed(@0x60) × frame_delta`, wrapped to [0, 1), copied into TextureInfo +0x14/+0x18 (+0x1C = scroll flag). Then a flipbook: nodes {ptr texture, f32 duration, ptr next} from @0x70 (current @0x74, count @0x78, start time @0x7C), stepped against 0x4EE454. Durations are in 1/60 s: lava `LAVAGAIN1..16` = 3.0 each (20 fps, 0.8 s loop).
- **sud0 rainbow water** = material `SUD0\O_ZARBI_AUNTXY.TGA` on two static IPOs (0x1D105D: pool + front face at (24..61, 40..77, -5..22); 0x1D0DCD: the falls, z -78..5). Plain UV scroll, speed (0, 0.01)/frame = 0.6 UV/s; no flipbook, no AI. Scroll sign is **inferred**: subtracting the offset makes the pool flow over its edge and down the falls (v increases downward on both); adding would run uphill. Lava (cavedoc, LeCanyon, sud0) = the 16-frame `LAVAGAIN` flipbook; cavedoc's lava also scrolls (0, 0.001). Totals over 21 levels: 5 scrolling, 28 flipbook static meshes (and GEN_ARCH* flipbooks on persos).
- **.ttlv v2** (spec in `tools/ttre/ttlv.py`): surface record gains scroll (Metal UV/s) + flipbook list (texture, seconds); static meshes no longer include persos; a perso section holds world 3x4, parent table (informational), parts per (channel, object, material) in channel space, and baked channel→perso 3x4 per sample plus the shown object per channel per sample. Sampling: k = round(60 / fps) samples per animation frame (smooth at 60 Hz; the original may show integer frames only, unverified). The viewer still reads v1.
- Previews: `export.render_preview(..., t=seconds, eye=...)` poses persos, scrolls UVs and picks flipbook frames. Outputs in `out/anim/` (bar_ed, sud0_1a1b37[_close], sud0_rainbow, sud0_lava).
- **Portal flipbooks** (GEN_ARCH01–04, *R; Land, carota, Totalski; 4 frames × 5.4 ticks): the user saw them play too fast, while lava flipbooks and scrolls looked right. `ttlv.TEXTURE_TIME_SCALE = {'GEN_ARCH': 0.25}` (file-name prefix) makes each frame 0.36 s. Empirical; maybe the same unexplained 4× as ANIM_TIME_SCALE.
- Open: the engine's sampling between integer frames; scroll sign from the renderer (where TextureInfo +0x14 is consumed); scroll_on values other than 0/1; morphs; state transitions.

## Intro video (user priority: later, but keep in mind)
- `EDPROL.AVI`: Indeo 4 (IV41), **400×240**, 25 fps, 5:41. **The file has no audio track**, so the soundtrack must come from the game's sound banks.
- Disc and installed copies are byte-identical.
- FFmpeg decodes it natively. TonicTroubleFix ships an Indeo-free intro path, worth checking how it handles sound.
- Upscaling plan:
  1. Decode losslessly with FFmpeg.
  2. Upscale with an ML super-resolution model, e.g. Real-ESRGAN (anime/cartoon models suit the CG style), or Apple VideoToolbox super-resolution.
  3. Encode to HEVC/ProRes.
  4. Play through AVFoundation in the port, muxed with the extracted audio.
  - Expect Indeo block artefacts. Deblock before upscaling.

## Prior art to lean on (verified 2026-10)
- **BinarySerializer.OpenSpace** (MIT, C#): CNT, GF, TT SNA decoder, TT save files. Safe to port logic from.
- **raymap** (no license, so reference only; don't copy code): the authority on TT level loading (SNA/RTB/GPT/PTX, objects, meshes, AI).
- **raytools/ACP_Ray2** (MIT): R2 exe headers and struct/function names, to seed Ghidra.
- **raytools/OpenSpaceCodeGen**: AI-script-to-code decompiler.
- **RibShark/TonicTroubleFix** (GPL-3): maps the TT exe's renderer, input and intro entry points.
- **vgmstream / FFmpeg**: audio.
- No public R2 or TT code decompilation exists.
- **Avoid (clean room):** the 2022 Rayman 2 DS source leak.

## Next session: start here
**State (end of session 1, 2026-10-19).** The user tested every feature; the portable-core move changed nothing visible.
- **Data:** CNT/GF textures, encrypted SNA/RTB/GPT/PTX level snapshots, static geometry, persos with animations,
  baked radiosity (3 sets in sud0/Land), texture scroll and flipbooks, collision (= visual mesh), Ed's 7 animations,
  music (vgmstream; level table in tools/music.tsv). Exe: unpacked dump analysed, module map, CD check understood.
- **Runtime:** `port/core` (C11, portable: loader, collision, Ed controller, anim sampling) plus a thin macOS layer
  `port/Sources/TonicViewer` (Swift/AppKit/Metal/AVFoundation): level picker with music, fly/play modes, lighting
  sets, gamma, FOV, animation speed, help (⌘/).
- **Rebuild from scratch:**
  1. `python3 -m tools.ttre cnt-extract …Textures.cnt out/Textures --png` (and Vignette.cnt → out/Vignette)
  2. level-ttlv for every level (loop in the "Commands" history; see tools/ttre/__main__.py)
  3. `tools/make_icon.sh`; extract the music (see Audio); `cp tools/music.tsv out/audio/`
  4. `cd port && swift build -c release`
- **Rules:** the user runs the viewer/game (never launch them); game logic goes in port/core only (C11, no platform
  code); never commit game data; clean room (no leaked R2 source).

**Queue, in order:**
1. **Ed's mechanical ID card** (MEC `hidcard.c`): real gravity, speeds and jump in tc_player. May also explain the
   4x animation time factor (ANIM_TIME_SCALE = 0.25, TEXTURE_TIME_SCALE GEN_ARCH = 0.5 are empirical).
2. **Vita backend**: VitaSDK build of port/core, vitaGL renderer (GLSL port of Shaders.metal), SceCtrl input, data on
   ux0:data/TonicTrouble/ (see port/core/README.md). Then 3DS.
3. MEC physics engine reverse-engineering, then the **AI script interpreter** (player behaviours, sound events →
   automatic music, day/night logic, the invisible sud0 river, portals' runtime state).
4. Parked: lighting stages 2–4 (set selection, sector lights + fog, lightmaps), water/death collision materials,
   steps > 0.15, morphs/sprites, the CD-archive asset pipeline (tt1–tt4.cnt via InstalTT.exe), intro video upscale,
   disc demos.

**Budget note:** the user funds this personally and is budget-limited. Prefer one focused agent with a tight brief,
verify by data or headless tests, and ask before large explorations.

## Roadmap
1. ~~Repo + CNT/GF tools~~
2. Level data: SNA/RTB decryption → pointer relocation → geometry, materials, objects (follow raymap's TT path). **Static geometry, perso animation, moving textures done**; next: lighting, collision, AI
3. Audio extraction (BNM/APM)
4. Exe RE: Ghidra + REA (morluto/rea) over MCP; seed with ACP_Ray2 names; map the engine modules
5. macOS/Metal port: reimplement engine subsystems against the extracted data; Metal replaces GliDX6
   - Resolution-independent from day one. The target is native Retina: the dev machine (M1 Pro, Metal 4) has a 3024×1964 panel, ~16:10, at up to 120 Hz.
   - Display rules: render 3D at the drawable's pixel size; widescreen through a wider field of view (Hor+), never stretching; lay out 640×480 menus and HUD in a 4:3 safe area; frame-rate-independent presentation with the original game-logic timing preserved
   - Controls: the original hard-codes camera controls to the numpad (MacBooks have none) and they can't be rebound. Port defaults: **WASD = movement, arrow keys = camera**. Every action should be rebindable, with controller support too. Find the original input mapping in the exe (DirectInput layer; TonicTroubleFix's SDL3 input replacement shows where it lives)
   - Fixed-resolution content that needs special handling: 640×480 loading screens/menus (Vignette.cnt), and low-res textures (most are 16–64 px). An optional upscaled texture pack can come later.
6. Redistributable asset pipeline: the port must ship no game data. The user supplies their own disc, and a converter builds everything from the **original CD install archives (tt1–tt4.cnt + GAMEDATA)**, not from an existing install.
   - tt*.cnt is an installer format, not yet decoded. It has a different header and probably comes from InstalTT.exe. Reverse InstalTT.exe to read it.
   - Must tolerate damaged rips. The user's tt4.cnt lost its last ~43 MB: macOS Finder aborted the copy, Linux skipped the bad sectors. Map which entries are missing, fall back to an existing install for those, and report clearly what couldn't be recovered.
6b. Extras: upscaled intro, disc demos (`Demos/` = cock01, lecanyon, pyramide, sud0, TotalSki, transit)

## Open questions (2026-10-19, after viewer feedback)
- **Animation speed**: the user matched the original (Wine) at 0.25x of our playback. The exe says frames advance by
  `dt_ms/1000 * FrameRate` (FUN_00415fb0; FrameRate = state byte @0x3B copied in FUN_00437170; DAT_008bd098 = frame ms,
  clamped to 64 in FUN_0041cc70). The exact 4x factor points to a unit we misread: keyframe frame indices, n_frames, or
  the ms source. `ttlv.ANIM_TIME_SCALE = 0.25` until resolved. The viewer blends between samples, so motion stays smooth.
- **sud0 river does not render at all.** Ed can swim in it (unlike lava), so it's probably a special water object.
  Suspect VisualSet type 1 ("patch"), which is unhandled, or a perso/zone-driven surface. Find it via the river's
  sector/IPO list.
- **Portals** (swirls): orientation fixed (matrix decoding) and flipbook slowed 4× (TEXTURE_TIME_SCALE). The user needs to re-check both in the viewer.

## Play as Ed (2026-10-19, ttlv v5)
- **Collision layout** (level.py docstring): PhysicalObject +4 → CollSet {u32 zdm, zdd, zde (0 in all statics), ptr zdr} → GeometricObjectCollide (TT: n_vertices, vertices, normals, ?, i32, n_elements @0x14, types @0x18, elements @0x1C, ...); element type 1 = {game_material, u16 n_tris, u16 n_mapping, ptr tris i16[3], ...}.
  - **Confirmed:** for every static IPO in bar and sud0 the collide object *shares* the visual LOD 0's vertex and triangle arrays (same pointers), and GameMaterial +0xC (collide material) is null. So TT world collision = the visual mesh, including foliage, the sky dome and lava surfaces (no "no collision"/water/death flags available this way). Previews: `out/collision/{bar,sud0}_top*.png` (visual left, collision right; `export.render_collision_compare`).
  - Perso collide meshes (13 persos in sud0, e.g. the 4 lava-pit platforms, fam 46) are added at frame 0 of their initial state (treated as static). Non-triangle collide elements (sud0: 4 × type 8 aligned boxes, 1 × type 7 spheres) are **ignored**. Ed's family has no zdr.
- **Ed** = GPT perso-in-fix #0, family "NewEd" (GPT object-type table 0 gives family names: NewEd, Camera, gen_cheatCode, ...) in all 21 levels, 369 states, same fix data everywhere. No state names in TT data, so the anims were picked from filmstrips (`out/scratch/filmstrip.py`, `out/scratch/ed_fs{1,2}.png`; Ed faces local **-Y**; channels 0/1 = feet, 2 body, 3 head, 4/5 hands):
  idle = state 0 (initial), walk = 1 (start state 9 → 1), run = 10 (158 → 10), jump = 39 (crouch and take-off → 43), jump_air = 43 (tucked loop), fall = 44 (arms-up loop, 40 → 44), land = 41 (→ 0). Mapping in `level.ED_ANIMS`. Visually confident for idle/walk/run/jump/fall; jump_air/land less so. Anims are in place (no root motion).
- **.ttlv v5** (spec in ttlv.py): header gains u32 play_offset; play section = world-space collision triangles (f32[9] each), then Ed: start 3x4, index of his static perso entry, parts keyed by object (channel numbering differs per anim), and 7 named anims (n_channels, samples, rate with ANIM_TIME_SCALE, loop, ground_speed). The viewer reads v1–v5.
- **Controller** (now `port/core/src/tc_player.c`, formerly Swift `TonicPlay`; not the MEC physics): two stacked spheres (r 0.4, height 1.3), uniform XY grid over the triangles, 1/120 s substeps each moving ≤ r/2 (no tunnelling), ground = contact normal within 50° of up (pushes straight up, so no sliding on slopes), walls push along the normal and slide, downward snap ray (0.45) while grounded. Walk 2.9 / run 6 u/s, jump 7 u/s, gravity 22 (guesses). Walk/run clips play at move speed / ground_speed so the feet don't slide (that is ~3.9× the 0.25-scaled walk rate, i.e. about the exe's raw rate: one more hint that ANIM_TIME_SCALE is wrong for Ed). Third-person orbit camera with a pull-in ray. Play mode is the default on level load when Ed is present; Tab toggles free-fly; Ed's static copy is hidden while playing. `,`/`.` change the vertical FOV (30–100°).
- **Headless checks:** `cd port && swift run -c release CorePlayCheck ../out/ttlv` (or `make -C port/core check`) (synthetic floor/wall/30° ramp + every level: drop at Ed's start, walk/run into the nearest head-on wall, tunnelling detector = no body-centre path segment crosses a triangle). All pass. Totalski's start is 9 units above the ground (its GPT matrix = the stale fix one), outro/skymap 2 units.
- Open: the real collide flags (zdm/zdd/zde for persos, type 7/8 elements, collide materials of persos), water/lava/death surfaces, Ed's real speeds and jump arc (MEC IDCard; State +0x2C read as null for all of Ed's states, so the offset is probably wrong), state transitions instead of hand-picked clips, slopes > 50° and ledge grabbing.

## Plan update (2026-10-19, user decision)
- Lighting stages 2 (which baked set the game picks / day-night transitions) and 3 (sector lights, fog) are **parked**:
  the transition logic is expected to be tricky and the budget is limited. The viewer exposes all baked sets instead
  (ttlv v4: 3 rgba8 sets per vertex; number keys pick the set, -/= adjust gamma).
- **Next: basic playable Ed in the viewer** (an optional mode): decode static collision geometry (PhysicalObject
  collide set), export it, and add a simple third-person controller (walk/run/jump, gravity, collision) using Ed's
  idle/walk/run/jump animations. This is not the original physics (MEC module); faithful dynamics come later.
- Portal flipbooks: user re-matched to 0.5x (`TEXTURE_TIME_SCALE`).

## Queue (2026-10-19)
1. **Ed's mechanical ID card** (`hidcard.c`, MEC module): read Ed's real dynamics parameters (gravity, speeds, jump,
   slopes) from the level data and feed them to the Play-mode controller in place of the guessed constants.
   Also check whether it explains the 4x animation-time question (the matched walk rate came out near the raw rate).
2. Then, budget permitting: the MEC physics engine (~100-200 functions), then the AI script interpreter
   (player behaviours, the sud0 river, day/night logic).
3. Parked: lighting stages 2-4, the river, collision materials (water/death), steps higher than 0.15.

## Audio (2026-10-19)
- All audio lives in Ubisoft sound banks `WORLD/SOUND/Bnk_*.bnm` (+ `English/` voice banks). vgmstream reads them (brew `vgmstream`).
  `python3 -m tools.audio_inventory 20 > out/audio/inventory.tsv` lists streams ≥ 20 s.
- Music = stereo looping sequences (`0b-…/multi-loop|intro-loop|single-loop`): 12 long tracks + 9 ~30 s loops/jingles,
  extracted to `out/audio/music/Bnk_<bank>_<subsong>.wav` (`vgmstream-cli -l 1 -f 0 -s N`). Mono `A_*.apm` = speech/ambience.
- Level -> track is chosen at runtime (level `.snd` = sound pointer table, game.dsb has no mapping), so the viewer uses
  a user-edited table `out/audio/music.tsv`; M mutes. Decode the sound events later to make it automatic.
- Music matched to the official soundtrack (Eric Chevalier), by length and audio-envelope correlation (1.00 for Bnk_77_1,
  47_3, 21_2, 9_8, 89_1): 50_2 Doc's Cave, 74_1 Canyon, 42_5 Vegetable HQ, 77_1 Pressure Cooker, 47_3 Glacier Cocktail,
  21_2 North Plain, 9_8 South Plain, 8_6 Pyramid, 76_1 Grögh HQ, 80_1 Ski Slope, 89_1 Bonus (Crazy Town); 66_30 no album match.
  Level pairings inferred from names: bar/cock01 = Glacier Cocktail?, pyr_fou = Pyramid?, BigBoss = Grögh HQ?, rest by name.
- User-confirmed (2026-10-19): cock01 = Glacier Cocktail; the bar/pyr_fou/BigBoss guesses are correct. Levels without
  their own track reuse other levels' music in the game; until the sound events are decoded they play North Plain.
  The picker plays `menu` (Bnk_66_30, unconfirmed guess). Canonical copy of the table: `tools/music.tsv`
  (copy to out/audio/music.tsv after re-extracting).

## Portable core (2026-10-19)
- The game core is now C11 in `port/core` (SwiftPM target `TonicCore`; layering rules, backend contract and a Vita sketch in `port/core/README.md`): `.ttlv` v1-v5 loader from a memory buffer, collision grid/raycast/closest point, Ed's controller (same tunables, animation-state choice, teleport), perso/Ed animation sampling. Only libc + `<math.h>`, allocations via `tc_set_allocator`, clean at `-std=c11 -Wall -Wextra -Wpedantic`; `make` in `port/core` builds it with plain `cc`.
- Swift (`port/Sources/TonicViewer`) is only the platform layer: AppKit window/input/picker, Metal, AVFoundation music. `Core.swift` wraps the C structs. Removed: `TonicPlay`, `PlayCheck` (Swift), `LevelFile.swift`.
- Parity: `CorePlayCheck` prints the same lines as the Swift PlayCheck did, byte for byte on all 21 levels (only the wall-clock "sim time" lines differ). One subtlety: Apple's `simd_cross` is compiled with FMA contraction, which decides whether a few near-degenerate sliver triangles are kept (Transit, north: one each), so `tc_v3_cross` uses `fmaf` explicitly; all other core math is compiled without contraction.


## Next session plan (2026-10-22, agreed with user; NOT started)
Uncommitted: default-to-play-mode change (Renderer.swift load() → togglePlay(); main.swift title/help; Input.swift; this file).
1. **Ed's mechanical ID card** (MEC `hidcard.c`, 2 funcs in modmap; MEC ≈ 107 funcs total). Find hidcard.c funcs in the
   Ghidra project (files/out/_decomp_artifacts/ghidra), recover the struct layout, locate Ed's IDCard (State +0x2C read
   null for all 369 states → offset likely wrong), read real walk/run speed, jump, gravity, slope values, check in 2+
   levels, feed them into core/src/tc_player.c; verify with `make -C core check`. One focused pass, no broad sweeps.
2. **Sliding stages:** skislope and topsecret use different physics. The user's guessed values are already in
   tc_player.c; look for a separate ID card / MEC mode for these levels and replace the guesses if found.
3. **Shelved (user, 2026-10-22):** animation speed. Each animation has its own speed; they can't share one clock
   (so a single ANIM_TIME_SCALE is wrong). Large change, later.
4. Small items: optional `swiftLanguageModes: [.v5]` in Package.swift (Swift 6 warnings); fix old `port/...` paths in
   these notes (now `core/`, `Sources/`).

## Ed's mechanics ID cards (2026-10-22, session 2)
- **Where:** State **+0x30** is the MEC ID card pointer (TT; +0x2C is something else, which is why it read null). Ed's
  cards are in fix, identical in every level. Card = u32 type @0 + fields; type 0..12, sizes from the constructor
  `MEC_hidcard_004b7d04` (0/4/6: 0x40, 1: 0x48, 2/5: 0x50, 3: 0x10, 7: 0x28, 8: 8, 9: 0x1C, 10: 0x14, 11: 0x34, 12: 0x18).
  Handlers per type: table filled by GAM_0042a7e0 (type 0 and 4 share FUN_004aa6af).
- **Ed's states:** idle/walk/run/jump take-off (39, 5 frames at 30 fps: the first 1/6 s of a jump)/land = card type 0 @fix 0x3c33c8 (234 of 369 states use type 0);
  jump_air (43) = type 4 @0x3c34c0, fall (44) and fall start (40) = type 4 @0x3c3500 / 0x3c36e8.
- **Type 0/4 fields** (Ed ground / jump_air / fall): +4 = 120 (mass-like: stored in dynamics +0x98, force integration
  divides by it, gravity multiplies by it, so it cancels for gravity); +8 = 1 / 0 / 0 (passed to collision response,
  unknown); +0x14 = 50 / 100 / 100; +0x18 = angular speed (rad/s: 191.99 / 628.3); **+0x1C..+0x28 = cosines that
  classify contacts by normal·up** (MEC_004bc477): >= +0x1C ground (Ed: cos 50°), >= +0x20 steep slope (cos 70°),
  >= +0x24 wall (cos 120°), >= +0x28 overhang, else ceiling; +0x2C/+0x30 wall parameters; +0x34 = 0.4 / 0.3 / 0.3
  (ground re-snap, MEC_004bf9f0 with +0x20); **+0x38 = gravity factor 1 / 3.5 / 3.0**; byte +0x3C = 1.
- **Mechanical environment** (sector data +4 → list of {ptr mec env, ?, ptr visual env}): {9.81, dir (0,0,1), 1, 1, 1.3,
  ...} in bar and Totalski. Gravity = 9.81 × card +0x38.
- **Applied to tc_player:** gravity 9.81 ×1 grounded / ×3.5 rising / ×3.0 falling; max_slope 50°. Walk/run/jump speeds
  stay the user's guesses: they come from the AI (proposed speed, dynamics param +0x6c), not the cards. The
  take-off phase (1/6 s at ×1 before ×3.5, `takeoff_time`) matters: without it the apex was 1.44 units; with it ~2.6
  (jump_speed 10.1, ~0.85 s in the air); user asked for slightly higher, jump_speed 11 → ~3.0. Ed's DsgVars (mind +0xC → dsgMem) hold 8, 6, 4, 0.05, 1.1 …
  (unlabelled; need the AI interpreter).
- **Sliding:** not a card; a **mechanical material** on the floor (GameMaterial +4, 0x1C bytes, ctor in hMecMatC.c).
  Totalski (398 collide elements) and skymap (134) share 0x36ae40 = {1.0, 0.8, 0, 1, 1, 0, 0}; cock01 has
  {0, 1, 0.6|0.5|0.4, 1, 1, 0, 0}; bar/sud0 have none. Field meanings not decoded yet (read in the collision
  response, MEC_004bb904 and friends). skymap is presumably the user's "topsecret" (to confirm).
- Pre-existing playcheck failure (also before this change): mammyon, running at the nearest wall climbs 1.7 units.

### Floor mechanical material (sliding), decoded 2026-10-22
- Collision detection (FUN_0043bff0) copies 5 floats of each side's material into the contact record (0x7C bytes):
  obstacle at +0x30..+0x40, the moving object's own at +0x4C..+0x5C; a missing material reads as all 1.0
  (DAT_004dc9c4..d4, also MEC_004baa78's defaults). Ed has none, so only the floor's values matter.
- **mat[1] = rebound**: on a head-on impact (MEC_004bb904) v -= n·(2 − r)(v·n) with r = obstacle × own; r = 1 elastic, 0 dead stop.
- **mat[2] = adhesion/grip**: the ground contact is kept at dynamics +0xCC; FUN_004c41c5 takes g = obstacle × own
  (+0x104 × +0x120), clamped to 0..1. g >= 1: last frame's speed is discarded every frame (pure AI-driven speed, no
  inertia). g < 1: the speed carries over (momentum), the AI-proposed speed is scaled by g^4, and slope forces are added.
- mat[0], mat[3], mat[4]: not read by MEC/DNM code that has been decompiled (accessors exist for the AI: 0x49c140.., 0x48c6b3..).
- Values: Totalski/skymap {1, 0.8, **0**, 1, 1}: no grip, so momentum only and zero steering from the walk/run
  input (in the game Ed skis there using other states/AI). cock01 ice {0, 1, **0.6/0.5/0.4**, 1, 1}: steering
  scaled 0.13 / 0.06 / 0.03 with momentum. Everything else: no material (grip 1).
- **Sliding in the viewer (2026-10-22, user decision): per-level switch only.** `tc_player_apply_level` gives
  Totalski and skymap the user's sliding profile (walk 10.1, run 20.2, jump 22.3, gravity 11.2, air scales 1,
  max_slope 10°); no .ttlv change.
- **TODO (user request): re-export the levels with the mechanical data** — per-triangle floor material (grip mat[2],
  rebound mat[1]) in .ttlv, so sliding/ice (Totalski, skymap, cock01) comes from the data instead of the name switch.
  Needs a ttlv version bump (tools/ttre/ttlv.py + level.py, core/src/tc_level.c), then re-run level-ttlv for all levels.
