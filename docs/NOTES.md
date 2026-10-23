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
- `EDPROL.AVI`: Indeo 4 (IV41), **400×240**, 25 fps, 5:41 (the story prologue: spaceship, tonic, Grögh, "The
  Ultimate Mission", Ed in the snow; the only video on the disc). **The file has no audio track.**
- **Soundtrack found (2026-10-23):** menu action LaunchIntro (GAM_00412a40 table -> FUN_00412860 -> GAM_00410ca0)
  stops the 3D sound and runs the player thread (0x4863f0), which loads binary sound bank [0x4fe9f0] = **200**
  (_SND_fn_bLoadBinaryBank), plays its event 0 (_SND_fn_pGetBinEvent(0xC80000) -> SND_sound3d_00404db0) and picks
  each video frame from the sound's play position (0x405080), so the audio is the master clock; the end of the sound
  ends the movie. Bank 200 = `SOUND/English/BNK_200.BNM` (168 bytes, upper-case, so tools/audio_inventory.py missed
  it until its glob was made case-insensitive) -> external stream **USDOLBY.APM**, stereo 22050 Hz, 5:42.3.
  LaunchCredits (FUN_00412870) is a separate action (not a video).
- **In the viewer (2026-10-23):** `tools/make_intro.sh` -> files/out/video/intro.mp4 (deblock + light denoise +
  Lanczos to 1800x1080, HEVC, soundtrack as AAC; classical filters, no ML upscaler installed); V on the level select
  screen plays it at 5:3, 80% of the largest fit, centred on black (user: it is 240p underneath; IntroView.screenFraction) (Sources/TonicViewer/IntroPlayer.swift), Esc / Space / Return skip.
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
**State (end of session 5, 2026-10-22).** Ed is driven by **his own AI scripts** in the viewer (default when a level has
a .ttai; I switches to the simple controller). The user tested it: "playing pretend Tonic Trouble". Idle with random
idles, run (default on keyboard) / walk (Shift), start/stop transitions, jump/air/land, turning, the stick attack
(E) all come from the game's behaviours; only the physics is ours (tc_player) with hand-tuned speeds.
- **Pipeline:** docs/BUILD.md (textures, vignettes, then per level `level-ttlv` + `level-ttai`, icon, music, app via
  `tools/make_app.sh`). The .ttlv and .ttai of a level must come from the same exporter run.
- **Code map:** core/ = portable C11 (tc_level loader, tc_collision, tc_player physics, tc_anim, **tc_ai** interpreter,
  **tc_ed_ai** AI + physics driver); Sources/TonicViewer = macOS layer. Tests: `make -C core check` (playcheck, must
  stay output-identical) and `make -C core aicheck` (Ed's AI headless on bar's collision; AI_TRACE=<frame>).
  Python: tools/ttre (ai.py = scripts, buttons, .ttai export; level-ai dumps scripts as text). Primitive names:
  tools/exe/ai_names.tsv. Ghidra helpers: tools/ghidra (MkDis/Raw/RefsIn). Details: "AI interpreter" below.
- **Rules:** the user runs the viewer/game and handles commits; game logic goes in core/ only (no platform code);
  never commit game data; clean room (no leaked source).

**Queue, in order (agreed 2026-10-22):**
1. **Decompile the MEC physics** (the big next step; expect a long session): the mechanics handler table of
   GAM_0042a7e0 (**corrected 2026-10-23: 13 pairs indexed by ID card type, the AI mode is an argument; see "MEC
   physics" at the end**; Ed uses modes 3 turn, 12 move, 10, 9 ballistic jump,
   1/2/4/7/21), how the dynamics proposal (dynamics +0x314: [0] |v|, [1..3] wanted speed vector from ProposalVecA,
   [4..6] steered by ProposalVecD, [7..9]+[10] SetMoveDirection, [0xB], [0x18]) becomes velocity, Ed's ID card
   (gravity per state, ground/slope/wall angles; State +0x2C read as null, the offset is probably wrong) and the
   report bits the AI reads (dynamics +0x18: ground 1, slope 2, wall 4 ...). Goal: replace tc_ed_ai's
   approximations and hand tuning (run_scale 2.0 -> 9.0 u/s, walk 3.5 u/s, jump impulse with tc_player's gravity
   gives ~5 units, likely too high) with the game's own rules. Check whether walking really is
   animation-driven (no move direction is set for it) or comes from the ID card.
2. ~~**Skiing (Totalski, skymap)**~~ done 2026-10-23: a separate skier perso + ID card type 2 (see "Skiing" at the
   end); flight (card type 5) too ("Flight").
3. **Rest of the moveset through the AI:** Shoot (Q) / Lateral (C) / pogo / fly need their effects: channel
   hide/show (PROC_120/121, props like the stick), meta-actions other than ChangeStateAndWait (11, 12, 19, 21, 40
   finish at once now), collision probes (PathBlocked? is always false), touched objects, zones. Unimplemented
   primitives are counted in tc_ai_perso.unknown (aicheck prints them): FUNC_95/164, PROC_16/35/212-214/290, META_6.
4. **Gamepad:** tc_ed_ai_step takes a stick (-1..1); the viewer passes 0 (keyboard path in the scripts).
5. Other persos' AI (doors, platforms, enemies, bonuses) once the shared primitives exist; then sound events.
6. Older parked items: the 0.25 animation time factor for non-Ed persos (Ed's AI path uses the exe's 1x clock and
   looks right), the invisible sud0 river, Vita/3DS backends, CD-archive pipeline.

**Project direction (agreed with the user, 2026-10-22):**
- **Two apps, one repo for now.** TonicViewer stays a self-contained, macOS-native app: the inspection and debug tool
  (free-fly, AI traces, collision/contact views, later physics debug views). The game ("Tonic Trouble Recomp") is
  a separate app in the same repo, named **OpenTonic** (user, 2026-10-22): an engine reimplementation running the
  original data and AI scripts (OpenMW/ScummVM style), not a static recompilation or a matching decomp. Our code is
  ours (clean room: study the exe, write fresh code); the game data stays Ubisoft's and is never shipped (the player
  supplies the disc). README line: "runs the original game's data; requires your own copy of the game". Split into separate
  repos only once the core API is stable (after the MEC physics). Planned split (user, 2026-10-22): **TonicCore**
  (the C core plus the tools: converter, exporters, Ghidra helpers), **TonicViewer**, **OpenTonic**; the two apps
  consume TonicCore. A move to a layout
  like `core/`, `viewer/`, `game/`, `tools/` is cheap and can happen any time.
- **Layering (the "higher-level approach" = this layering, not another language; all C11):**
  1. core (shared): loaders, collision, AI interpreter, animation sampling, physics. No platform code.
  2. game layer (game only, portable): persos/sectors world model, fixed-timestep loop, camera, sound events,
     level transitions, menus/HUD. Talks to the platform through a small interface: input state in; a **render
     list**, audio events and file reads out. The core never calls a graphics API.
  3. platform backends (thin, per target): Metal (macOS), **vitaGL + SceCtrl + SceAudio on Vita (first non-Mac
     target, the user's main interest)**, later GL desktop, citro3d (3DS). SDL may cover desktop windowing/input/
     audio; its Vita/3DS ports need checking. SDL_GPU/bgfx/sokol do not reach Vita/3DS, so render list + thin
     backends.
- **Shared rendering:** move "what to draw" (render list, material/fog parameters, Ed's animation sampling) into the
  core so TonicViewer's Metal renderer and the game consume the same list; shaders stay per backend.
- **Order:** MEC physics first (it shapes the game layer: who owns velocity, how mechanics modes plug in, the frame
  update); then design the game layer + render list (its own session); then the Vita backend (core/README.md has
  the Vita sketch).

**Budget note:** the user funds this personally and is budget-limited. Prefer one focused pass, verify by data or
headless tests, and ask before large explorations.

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


## Earlier plan (2026-10-22, session 3; superseded by "Next session: start here")
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
  Handler table filled by GAM_0042a7e0 (26 slots). **Correction (session 4):** it is indexed by the dynamics *mode* that
  the AI sets (PROC_51..65, see tools/exe/ai_names.tsv), not by the card type; slots 0 and 8 share FUN_004aa6af.
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

## Level re-export: .ttlv v6, floor materials, rainbow lighting (2026-10-22, session 3)
- **.ttlv v6** (spec in tools/ttre/ttlv.py): the play section adds a floor table {grip, rebound} and a u16 index per
  collision triangle. C: `tc_level.collision_floors` (per triangle), `tc_tri.floor`, `tc_collision_init(..., floors)`.
  All 21 levels re-exported (0 textures missing). Totalski: 3028/3356 triangles grip 0 (rebound 0.8); skymap: same
  material; cock01: 736/1900 triangles grip 0.4/0.5/0.6 (rebound 1); everything else: none.
- **Controller:** `tc_player_apply_floors` replaces the level-name switch: any zero-grip floor → the user's sliding
  profile (walk 10.1, run 20.2, jump 22.3, gravity 11.2, air scales 1, max_slope 10°; per-triangle grip ignored).
  Elsewhere, while grounded on grip < 1 the ground acceleration (42) is scaled by grip^4, as the engine scales steering
  (cock01 ice: ×0.03..0.13, so momentum carries). Rebound is exported but not applied yet (cock01's 1.0 would make
  ice walls fully elastic on head-on hits; unverified, so left out).
- **Exporter texture root** now defaults to files/out/Textures (the repo moved out/ → files/out/; it was still
  out/Textures, which exports 0 textures). Other docs still say out/... in places.
- **"Patch" visual sets do not exist in sud0** (the water was a sector surface list, see below) (all 508 visual sets are type 0; bar and Land too). The only skipped
  geometry is element type 3 (sprites): 102 in sud0, all on perso objects (incl. Ed's family), billboards 0.1–0.7
  units, not water. TT sprite layout: element {u32 n, ptr sprites, u32}; sprite (0x10) {u32, ptr info, f32 size[2]};
  info {u32 1, ptr, ptr, ptr scale f32[2] (+0xC), ptr f32[2] (+0x10), ptr → ptr GameMaterial (+0x14)}. Not rendered yet.
- **sud0 rainbow water / falls black in light sets 0 and 2:** its baked radiosity is exactly (0,0,0) on every vertex
  in sets 0 and 2 (set 1: ~95). Material flag bit 0x400 is clear on it, but also on normally lit shadows/paintings/Land
  terrain, so it is not an "unlit" flag. Fix (heuristic, level.fill_unbaked): an element all-zero in one set but lit in
  another takes the first lit set's values. Only these two elements match in the levels checked. north has 25
  elements zero in its only set (nothing to copy; check whether they look black in the viewer).

### Water surfaces found (2026-10-22, session 3, continued)
- **The missing water is a sector-level "surface" list**, not a patch visual set: sector data +0x10 = {ptr head,
  ptr tail, u32 n} of nodes {ptr surface, ptr next, ...}; surface = {ptr GeometricObject, Matrix 0x58 (world),
  Matrix (copy), ... (rest not decoded; maybe wave parameters)}. Several sectors share one surface. Found by tracing
  pointers back from the unused texture SUD0\OO_MER01ANU.TGA. Probably the engine's SRF (water surface) module.
- 34 surfaces in 12 levels, all liquids: sud0 6 (rainbow river at z -5.2, pools at the top (5.2) and **foot of the
  falls (-77.5, the "hole")**, two OO_MER01 sea quads), cock01 2 (O8TXY pools), pyramide 9 (PYR_O*, incl. a waterfall
  texture), skymap 4 (ACID1), cavedoc 1 (OO_EAU), marmite 3, BigBoss 4 / Land 2 / Transit 1 (LAND_MORT1, likely a death
  liquid). `LevelData.surfaces()`; `meshes()` yields them as static meshes, so all exports include them.
- No baked radiosity (no ISI), so they use the viewer's placeholder key light (alpha 0) in every light set. Not collision.
- Viewer data path: the viewer now looks in the app bundle, ./files/out/ttlv, then the source checkout's
  files/out/ttlv (via #filePath, so Xcode builds running from DerivedData read the repo directly), then ./out/ttlv.
  tools/make_app.sh updated to the files/out layout (not run).

## Skies and fog (2026-10-22, autonomous session)
- **Sky sectors are named "...ciel..."** (sector data +0x94 = NUL-terminated name: "sctciel", "sctciel02", north's
  "sct3ciel02" window skies, BigBoss "ciel"; "fond"/"plafond" sectors are background walls/ceilings, not sky). Object
  flags (SO +0x2C/+0x30) are identical for sky and non-sky IPOs.
- **Why skies were black:** their baked radiosity is unusable (sud0 dome mean -13, Totalski 0, death 2, many north
  panels 0; Land 135, north main 204, Transit 223). `LevelData.mark_sky` gives sky-sector IPOs neutral light
  (RADIOSITY_ONE, texture unchanged) in every set. **Inferred** from the data and the in-game look, not the exe.
- Sky geometry encloses the playable area in BigBoss, death, north, skymap, Totalski; sud0/Land/Transit domes stop
  above the deepest pits (below the horizon), cock01 = two backdrop panels, pyramide = a ceiling opening. So no
  camera-follow needed for now.
- **Material flag bit 0x400 = backface culling** (raymap: flags_backfaceCulling = 1 << 10); clear on double-sided
  materials (sky domes, water, shadows, paintings). Not "unlit". The viewer draws everything without culling anyway.
- **Fog:** sector data +4 environment entries = {ptr mechanics env, ptr ?, ptr visual env}; visual env = {u32, ptr,
  f32 near, f32 far, f32 third distance (40000 / 600 / 128; unused), f32 rgb}. Engine default 20000/30000/40000 light
  blue (0.736, 0.768, 0.961) = no visible fog. Real fog: north black 80..120 (night), Totalski light blue 200..400,
  Land 68 sectors grey at 5000. `LevelData.fog` takes the most common one per level (fog is per sector in the game).
  **.ttlv v7** stores it after play_offset; the viewer applies linear fog by eye distance after gamma, uses the fog
  colour as the background, and G toggles it. Unverified against the exe (GLI vComputeFogEffect lives in the driver
  DLL); north's 120-unit black fog in particular needs an in-game comparison.
- Other unused-texture finds: bar loads 676 lightmaps (BAR\LIGHTMAP\00000..00675.BMP; lighting stage 4), north has
  unused comet (NORD_COMETE*) and torch (North\torche01..07) flipbooks, probably sprites/particles.

## Sprites / billboards (2026-10-22, autonomous session)
- **Element type 3 (TT):** {u32 n, ptr sprites, u32}; sprite (0x10) {u32, ptr info, f32 size[2] (1, 1 seen)}; info
  {u32 1, ptr, ptr, ptr scale f32[2] (+0xC), ptr f32[2] (+0x10, zeros), ptr (+0x14) -> {ptr GameMaterial, f32 x2, ...}}.
  No centre-point field in TT; the sprite objects have one vertex per sprite, so sprite k sits on vertex k.
  Half extents = info scale x size (raymap's convention; a shield effect comes out 2.4 wide around Ed, which fits).
- They are effects and pickups on persos: bonus collectibles (BONUS1 flipbook + BOULE1-3 orbs), sparks, chrome glints,
  ELEC1 arcs, mines (MINE/_MINETTE), smoke (Fum06-09), north's torch flames (TORCHE01-07, one object per frame,
  switched by the animation), shields, hearts, stars, death's letter sprites. sud0: 524 billboard parts, north 505.
- **Export (v7, no extra fields):** each sprite = a 4-vertex quad in a mesh flagged TC_FLAG_BILLBOARD (1 << 2); every
  vertex holds the centre, normal.xy = the corner offset (world units, object scale applied), uv corners, no baked
  colour. The vertex shader adds camRight/camUp x offset after the model transform and zeroes the normal (unlit).
- Not done: additive blending (many effect textures are *_ANU, probably additive in the game; the viewer only
  alpha-tests), mirror flags, per-sprite sizes from the (+0x10) pair. Sizes unverified in game (bonus 2.2 wide, mines
  3.5-4.2, smoke 5.4).

## Housekeeping (2026-10-22, autonomous session)
- **playcheck mammyon "failure" was a test assumption, not a controller bug:** Ed walks up a ramp, hits the wall and
  slides along it; the wall recedes slightly, so his progress along the run direction ended 0.02 beyond the distance
  measured at the start. The tunnelling detector (exact path segments) never fired. The check now requires "no
  tunnelling and still facing a wall within 2 units" instead. All checks pass.
- Paths moved to the files/out layout: core/Makefile `check`, tools/make_icon.sh, core/README.md, Package.swift comment,
  CLI help. **docs/BUILD.md** = the full reproducible pipeline (textures, vignettes, levels, music via vgmstream,
  icon, run, checks); the music loop was tested on one track.
- The top-level README's paragraph about copying files into DerivedData is outdated (the viewer finds files/out/ttlv
  itself now); left for the user to edit (personal write-up).

## Lightmaps (2026-10-22, autonomous session) — lighting stage 4 done for the lightmapped levels
- **Triangle element +0x18 = per-triangle lightmap array** (our layout notes called it "vertex_indices"; the vertex
  index list is +0x20 with its count at +0x1C): ptr per triangle -> {ptr TextureInfo, u32 1}. Each lightmap is a
  32x32 BMP (`<LEVEL>\LIGHTMAP\00000.BMP`...), one per triangle. bar 677/688 triangles, death 545, PharmMap 291,
  pyr_fou 700, SpyCarota 362, spynorth 356 (all of their static triangles); radiosity levels (sud0 ...) have none.
- **Mapping:** the triangle is drawn in the upper-left half (right angle top-left); vertex a -> (0, 0), b -> (1, 0),
  c -> (0, 1) in image coordinates (top-left origin). Chosen by edge continuity over shared edges: mean |diff| 14.6
  (bar) / 17.9 (death) vs >= 25.5 for the other five assignments; top-down lightmap-only rasters of bar show
  continuous shading and cast shadows across triangles. The lightmapper fades the last ~3 texels before the
  hypotenuse to black (pixels with x + y >= 31 are black), so the exporter maps the legs to texel 0.5 .. w - 4.5.
- **.ttlv v8:** per level, `<level>_lightmap.png` next to the .ttlv (32x32 tiles in a square power of two, each tile's
  empty half mirrored from the triangle so filtering never reaches black), its path after the fog, and 52-byte
  vertices with uv2 (atlas coords, -1 = none; lightmapped triangles get their own vertices). tc_vertex is 52 bytes.
- **Viewer:** where uv2 >= 0 the atlas sample (linear, clamped) x 255/128 replaces the baked vertex light (same scale as
  radiosity: 128 = texture unchanged; inferred, like RADIOSITY_ONE). Levels without an atlas bind a 1x1 white texture.

## Transparency (2026-10-22, autonomous session)
- raymap pre-R3 rule: material flag 0x4000000 and texture flags & 0x10A = transparent (texture flag 0x20 = "light",
  unused in the levels checked). In TT, texture flag 0x8 alone = blended (effects, *_ANU/*AUN, OMBRE shadows, the sud0
  rainbow water, glass/vitrine, water, LAND_MORT liquid); 0x8|0x2 = hard cut-outs (*_nz teeth, cloth). Exporter sets
  TC_FLAG_TRANSPARENT (and drops ALPHA_TEST) for the former when the PNG has alpha; the viewer draws those after all
  opaque geometry with source-alpha blending and no depth writes (no back-to-front sorting yet).

## AI scripts: structure and plan (2026-10-22, session 4)
- **Path:** perso +0xC brain -> +0 mind -> +0 AI model = {ptr normal behaviours list, ptr reflex list, ptr dsgVar
  defs, ...}; mind +0xC = dsgMem. Behaviour list = {ptr array, u32 n, ...}; behaviour = 12 bytes {ptr scripts (ptr[n]),
  ptr schedule script (may be null), u32 n_scripts}. Ed (NewEd): 109 normal behaviours + 38 reflexes, 532 scripts,
  18,028 nodes.
- **Script = array of 8-byte nodes** {u32 param, u8 0, u8 0, u8 depth, u8 type}, a pre-order tree by depth; depth 0 ends
  it. Types seen: 0 keyword (If/Then/Else...), 1 condition, 2 operator, 3 function, 4 procedure, 5 meta-action,
  9 field, 11 dsgvar ref, 12 int constant, 13 float constant (bits), 14 pointer (likely input button; 515 uses),
  16 vector, 17 mask, 20/24/25/31/32/37 pointers (24 likely state/action ref, 25/32 other refs), 30, 33, 34.
- **Exe dispatch tables** (accessors AI_004923a0..00492837; index * stride + base):
  procedures 0x4E5A48 (8 bytes {fn, u8 flag}, 294); keywords 0x4E6378 (4 bytes, 10); conditions 0x4E63A0 (8 bytes,
  111); functions 0x4E6718 (8 bytes, 165); operators 0x4E6C40 (4 bytes, 28); 0x4E6CB0 (16 bytes {fn, fn, size, 0},
  probably fields); 0x4E5910/14/18 (small 4-byte tables, maybe meta-actions). Ed's max index per type fits each table
  (conditions 90 < 111, functions 164 < 165, procedures 292 < 294, operators 23 < 28). Many implementations live in
  0x49B000+ that Ghidra never made into functions (create them in the scratch project before decompiling).
  AI_00490b80 reads the node type byte and is itself an operator-table entry.
- **Plan:**
  A. `tools/ttre/ai.py`: disassemble any perso's behaviours into readable pseudo-code (types, dsgvar names by index,
     state/anim refs resolved), with per-primitive usage counts. Export of Ed's scripts as the working reference.
  B. Name the primitives Ed uses (about 157: 40 conditions, 31 functions, 56 procedures, 16 operators, 6 meta-actions,
     8 fields), most-used first: decompile each table entry, record in tools/exe/names.tsv + an AI table in NOTES.
  C. Core runtime (C, core/src/tc_ai.c): tree evaluator, dsgvars, per-frame normal/reflex scheduling, state changes
     (-> animation + mechanics card), input buttons, the MEC calls (speeds, impulses). Drive Ed by his real AI:
     walk/run/jump, fly, stick attack/shoot/pogo, random idles, and Totalski skiing (check whether skiing is Ed's own
     AI or a separate ski perso/family with its own models).
  D. Other persos (doors, platforms, enemies, bonuses) once the shared primitives exist.
- **Phase B progress (session 4):** tools/exe/ai_names.tsv names ~125 primitives (keywords, all comparisons and logic,
  arithmetic, vectors, math incl. RandomInt/RandomReal, time and TimeElapsed, input ButtonHeld/JustPressed/NotHeld/
  JustReleased/Analog, ChangeState (honours transitions) / InState / CanChangeState, ChangeBehaviour/Reflex, contact
  tests OnGround/AgainstWall/NoContact/OnSteepSlope + material tests, the dynamics proposal setters (speed, direction,
  turn) and **SetMechMode*: the AI picks the mechanics handler slot**, channels hide/show, transparency, sounds).
  `python3 -m tools.ttre level-ai LEVEL OUT.txt` prints them by name (files/out/ai/ed_bar.txt).
  Left for Ed: ~35 low-use primitives (PROC_0/2/4-26/27/66-71/79/84-86/149-155/212-216/236/244/290-292, FUNC_2/4/13/
  34/37/40/54/91/92/95/96/162/164, OP_5/6/9/16-18/23, the FIELDs, META_* whose table is not found yet, COND_59/61/63/
  69-74), button names (input entries are 0x1C-byte records in fix, no names; map them via OPTIONS/IPT.bin or usage),
  dsgvar meaning, and node types 17/30/33/34/37 (33 = channel index).
- **Phase B result (session 4):** 142 of Ed's 161 primitives named (9,773 of his 9,878 primitive uses, 99%); see
  tools/exe/ai_names.tsv (with implementations and how each was identified; "?" = inferred). Key tables besides the five
  dispatch tables: **meta-actions** 0x4E6CB0 (16 B {start, per-frame continuation, packed signature, 0}; multi-frame
  actions, continuation returns 4 while running), **fields** 0x4E6FA0 (12 B {value type, getter, setter}), **dsgvar
  types** 0x4E5910 (12 B {value type, getter, setter}). Value types: 6 int, 7 uint (time), 8 float, 0xC perso,
  0x16 vector (getters write type 0x16), 0x19/0x23 references. Node types relabelled in ai.py: 14 button, 20 string,
  24 perso, 32 behaviour, 33 channel. Still unknown: a dozen 1-15-use primitives (FUNC_2/4/13/95/96/162/164, PROC_35/
  212-214/290, OP_23, COND_41), button names, dsgvar meanings, and the exact meaning of mechanics modes 1-23.
- **TODO (later, user decision):** the 19 rare primitives (FUNC_2/4/13/40/54/95/96/102/162/164, PROC_35/149/153/212-214/
  290, OP_23, COND_41), probably cutscene/special-case code; name them when a script needs them.
- **Tooling for this work** (tools/ghidra): MkDis.java (creates missing functions, then decompiles a list of addresses;
  run without -readOnly on a *copy* of the project), Raw.java (dump data words with symbol hints), RefsIn.java (code
  references into an address range). Example: copy files/out/_decomp_artifacts/ghidra to a scratch dir, then
  `analyzeHeadless <copy> TonicTrouble -process -noanalysis -scriptPath tools/ghidra -postScript MkDis.java out.txt 00497970`.
  Table entry addresses: base + index * stride (tables listed above).


## AI interpreter (phase C, 2026-10-22, session 5)
- **Phase A bug fixed:** a behaviour's schedule pointer is ptr -> {ptr nodes} (one more indirection than the rule
  scripts); ai.py read it as nodes, so all 71 schedules of Ed came out empty (behaviour 0, his init, looked empty).
  Ed now has 23,462 nodes and 177 distinct primitives (31 unnamed, 174 of 12,637 uses). FUNC_33 is Abs, not
  NormalizeAngle.
- **Engine loop (exe):** GAM_0042b260 runs the reflex intelligence (mind +8) then, unless a meta-action ran in it,
  the normal one (mind +4); mind +0x10 = "running normal". AI_0048eb20 per intelligence: every rule script of the
  behaviour in order (each from its start, or from its running meta-action), then the schedule (AI_0048ec60), which
  resumes where it stopped (intelligence +4; restarts after its end). Intelligence = {model, schedule pos (+4),
  behaviour (+8), previous (+0xC), action table (+0x10: 0x50-byte entries {.., +0x48 node, +0x4C active,
  +0x4D script (0 = schedule, k + 1 = rule k)})}.
- **Nodes run linearly** (handler table 0x4E43F8 by type; each returns the next node): If evaluates its child and
  continues into Then's children (true) or past Then, into Else's children (false); an Else reached from a Then
  branch skips itself. End node = type 8. Types 10/11 dsgvar, 12 int, 13 real, 14 button, 15 constant vector
  (pointer), 16 vector from 3 children, 17 mask (u16), 20 string, 24 perso, 25 state, 32 behaviour, 33 channel.
- **Result flags** (AI_0048e970/eab0): 1 stop the intelligence this frame, 2 stop this script (refused ChangeState;
  a schedule retries that statement next frame), 4 meta-action done, 8 behaviour changed (start the new one now),
  0x10 end. Stop beats restart. **Proc9(v)**: v replaces the flags of the statement after the next one; scripts use
  `Proc9(1); ChangeBehaviour(X)` (switch, run X next frame) and `Proc9(1); Nop` (wait one frame).
  ChangeBehaviour/ChangeReflex restart only when they change the intelligence that is running.
- **Meta-actions** start once, then a continuation runs every frame (1 running, 4 done); the script blocks on it.
  ChangeStateAndWait: done when the requested state changed or the animation "ended".
- **States** (Perso3dData): +4 playing, +8 requested (InState; only ChangeState sets it), +0x15C next to play,
  +0x160 change pending, +0x161 ended (animation finished, no repeats left, no next state), +0x163 frame rate
  (State +0x3B), +0x78 repeats left (State +0x3A). ChangeState(t) (GAM_00437260): no-op if t is the requested
  state and not ended; else look t up in the transitions of the state to play (play its intermediate), refuse if
  prohibited or if the state has flag +0x3C = 1; at the end of an animation: repeat, else next state (InState keeps
  the requested one), so a looping state never "ends".
- **dsgvars:** mind +0xC dsgMem {ptr -> ptr def, ptr initial buffer, ptr current}; def {ptr default buffer, ptr
  infos (16 B {offset, type, ..}), size, count}. Types index table 0x4E5910: 0/1 int8, 2 uint8, 3/4 16-bit, 5/6 int,
  7 float, 10 perso, 11 perso list (count in byte 0, capacity byte 1; CollectNearby fills it), 12 vector.
  Ed reads/writes the camera perso's dsgvars (perso-in-fix 1) all the time: `Dot(camera, dsg20)` gates the idle.
- **Inputs:** the 23 button entries Ed uses are formulas over key/pad entries (0xF001 AND, 0xF002 OR, 0xF00B NOT,
  0xF00A ref, 0xF006 DIK key, 0xF01A pad button, 0xF010 axis range, 0xF011/12 stick angle/magnitude). Names (string
  before each list): Left/Right/Up/Down (arrows, pad axes), SpeedUp (LSHIFT, pad 16), Jump (Z, pad 10), Lateral
  (LALT, 13), Action (LCTRL, 11), Shoot (SPACE, 12), Camera (NUM0, 14). Keyboard: Ed runs by default, SpeedUp makes
  him **walk** (magnitude 0.625 instead of 1). Stick angle: degrees, 0 = right, -90 = up; scripts add 90.
- **Movement:** TurnBy(deg) = mechanics mode 3 at deg*pi/180 rad/s; TurnBySpeed divides by the frame time (turn
  by that much this frame). FUNC_2(perso) = angle to that perso in Ed's frame (inferred: degrees, CCW, 0 = facing),
  with the camera it makes the stick camera-relative. Ground walking/running sets **no move direction**: speed
  comes from the animation (Proc10(int(dsg75 * dsg79 * 100 or 222)) looks like an animation-rate %), so the
  mechanics must apply the state animation's root motion. SetMoveDirection + SetMechMode12 only on special ground
  (TouchingObject(0x20)).
- **Code:** `level-ttai LEVEL OUT.ttai` (format in tools/ttre/ai.py; also writes OUT_ai.txt, the node-indexed
  listing); core/include/tonic/tc_ai.h + core/src/tc_ai.c (loader, interpreter; the host fills inputs, stick,
  position, contacts, the camera position, reads the requested motion and state); core/tests/aicheck.c
  (`make aicheck`, AI_TRACE=<frame> prints executed node indices). Ed's AI is identical in every level (fix).
- **Status (bar, scripted keys, fake ground contact):** init B63 -> B0 -> idle B87; up -> B88 via transition state
  158 into run (10); release -> idle via 159; SpeedUp + up -> walk (9 then 1); jump -> B85 -> B89 (state 43 air)
  -> land (41) -> idle; left -> turn; stick forward -> turn back; Action -> stick attack B72/B69 (128/129).
  Stubbed (constant answers): collision probes (PathBlocked false), materials, zones (ZoneTest true), touched
  objects, GlobalFlag (abilities: all on), sounds, channel effects. Not implemented (counted in
  tc_ai_perso.unknown): FUNC_95/164, PROC_16/35/41/212-214/290, META_6.
- **C2, viewer integration (same day):**
  - **Animation clock = the exe's:** frames advance by dt x frame rate (+0x163 = State +0x3B, reset on every new
    animation); **Proc10(pct) = frame rate x pct / 100** (GAM_0041ec90, stored as a byte). Ed's AI path therefore
    uses no ANIM_TIME_SCALE; the unexplained 0.25 for other persos stays an open question.
  - **.ttlv:** after the 7 named clips, every distinct state animation (298 A3Ds for 369 states) as "s<k>" (k = the
    first state using it), one sample per frame (bar.ttlv 3.6 MB; all levels 125 MB). **.ttai** states gain the
    animation key and `ground` (units per frame).
  - **Ground speed:** walking/running set no move direction, so speed = the state animation's forward foot stride
    (level.forward_stride: feet sweep >= 0.6 mostly along Y; looping states only: 1, 2, 10, 25, 30, 96, 213, 215,
    235, 247, 320) x the current frame rate. Walk: 0.086 x 30 x Proc10 139 % = 3.5 u/s (hand-tuned earlier 3.3).
    **Run: 0.100 x 45 x Proc10 100 % = 4.5 u/s (hand-tuned 9.9): a run has a flight phase, so the stride
    underestimates it; the stance-phase foot speed was too noisy to use.** Backward and sideways cycles (2, 30,
    101 ...) move forward or not at all. The real answer is in the MEC handlers / ID cards.
  - **Jump = ProposalVecA** (AI_00488070: proposal [1..3] = wanted speed vector, local; [0] its length): B85 sets
    (8 x stick x sin/cos(turn), 1.3 x dsg62 = 15.6 up). Applied as an impulse; with tc_player's gravity (x1 for the
    take-off 1/6 s, then x3.5 rising) Ed rises ~5 units (the old hand-tuned jump: ~3). ProposalVecD (AI_004881d0)
    rotates proposal [4..6] towards a direction keeping its length (air control, not applied yet).
  - **core/src/tc_ed_ai.c** (tc_ed_ai): AI + tc_player. Contacts from tc_player (grounded, new `against_wall`),
    turn = heading += turn_speed x dt, impulse, Velocity field, StopMotion, SetMoveDirection under mode 12, ground
    speed above, airborne: momentum kept. tc_player_drive = tc_player_step's physics with a target velocity
    (playcheck output unchanged). aicheck now runs on the level's collision.
  - **Viewer:** loads `<level>.ttai` when present; Ed's AI is the default, I toggles the simple controller.
    WASD = direction buttons (camera-relative through the scripts, which get the camera eye), Shift = SpeedUp
    (walk), Space jump, E Action (stick), Q Shoot, C Lateral. Title bar: behaviour, reflex, state (4 Hz).
  - **Hand tuning (user request, until the MEC is decoded):** tc_ed_ai.run_scale = 2.0 on run cycles (> 4 u/s at
    their own rate: 10, 25, 30, 96): run 9.0 u/s (user: a tad under the old 9.9); walk_scale = 1 (3.5 u/s).
    Start-up and transition states without a cycle (walk start 9, run start 158, run -> walk 272) borrow the
    speed of the state they lead into; before that Ed stopped ~0.5 s when Shift switched him to walking.
  - **Known gaps:** Totalski/skymap: the sliding profile's 10° ground limit makes the slopes count as air, so the
    AI falls (skiing needs its own pass); no gamepad stick yet (stick inputs are 0: keyboard behaviour); channel
    hide/show, transparency, sounds not shown; camera perso's own AI does not run.

## MEC physics (session 6, 2026-10-23)
- **Raw decompiles from sessions 2-4 saved** (they only lived in /tmp) to `files/out/_decomp_artifacts/decomp/`
  (git-ignored): mec_module.txt (all 106 MEC funcs 0x4b92fd-0x4c11e7), dnm_handlers.txt (0x4a71e0-0x4ae392: the
  mode-switch base, 12 of the 26 table entries), mec_hidcard.txt, collide_0043bff0.txt, dnm_helpers_c41.txt
  (forces/grip 0x4c41c5, 0x4c39f0, 0x4c37af), mec_env.txt, gam_dispatch.txt + gam_dyn_helpers.txt (this session).
  Coverage of the DNM+MEC range 0x4a7000-0x4c1300: 149 of 201 functions, 68 of 106 KB. Ghidra signatures are often
  wrong (param counts differ between caller and callee): check the asm before trusting argument order.
- **Handler table corrected:** GAM_0042a7e0 fills 0x8C2240 with **13 pairs {fn0, fn1}, indexed by the ID card type**
  (MEC_004b9466 = card type), not 26 mode slots: 0 {4aa6af, 4ae392}, 1 {4abbb8, 4af8f0}, 2 {4ab91d, 4b00bf},
  3 {4b2900, 4b3810}, 4 {4aa6af, 4b0f2a}, 5 {4ac0da, 4aea7c}, 6 {4aca45, 4b11ea}, 7 {4acb49, 4b17d1},
  8 {4ac62b, 4af04e}, 9 {4ad386, 4b239c}, 10 {4adfc3, 4b252e}, 11 {4adb4d, 4b2793}, 12 {4a9d20, 4ae392}.
  Every fn0 is decompiled; every fn1 except 4ae392 is not. Ed only needs types 0 and 4 (TODO: 4b0f2a, 657 B).
- **Per-frame driver** (GAM_0042b520 -> GAM_0042b2b0, per perso): dynamics +8 = a **request queue** (u8 count, max 8;
  modes at +4 + 4k, ID cards at +0x24 + 4k). The AI's SetMechMode* (AI_004920a0) pushes {mode, the current
  state's card (State +0x30)}; with an empty queue the engine pushes a default entry for the state's card. A card
  type change runs FUN_004a7459 (mode switch init; current type kept at +0x11C). Then for each entry:
  GAM_0042aff0 -> fn1(card type)(..., mode): **fn1 switches on the AI mode** and turns the proposal (+0x314) into the
  wanted motion at +0x6C of its output (4ae392: mode 3/4 turn clamped by card +0x18, 5/0x12 turn toward angle,
  0xC move direction x speed clamped by card +0x14, rotated by FUN_004b7c56, slope-projected when grounded; default
  sets +0xB8 = 0). Finally GAM_0042b0e0 -> fn0(last card's type) = the integrator; it then copies report bits
  +0xC8 = +0xC0 | +0xC4 and the speed/position snapshots (+0x25C, +0x294). Dynamics +0x10 = the activation radius
  (GAM_0042a720: skip if no camera/active perso within it).
- **Type-0/4 integrator 4aa6af (structure):** mass from the card (+0x98); forces + grip into F (FUN_004c41c5);
  v = v_old + F dt / m + wanted; FUN_004c39f0(v, mass); FUN_004c37af integrates the new position; collide-and-move
  MEC_004be1bd (translating) or MEC_004bedd6 (rotation only); on losing ground contact, the ground re-snap
  MEC_004bf9f0 with card +0x20 / +0x34; carried by a parent perso (platforms) via SPO_00482ed0/FUN_004a8388; writes
  the position (+0x54..+0x6C: xyz + yaw angle about an axis) into the SuperObject matrix.
- **Open:** where walking speed comes from when the AI pushes no move direction (root motion: candidates are the
  A3D frame root vector that raymap ignores, or dynamics +0x2B0..+0x2CC that GAM_0042ab20 sets).

### Flight = ID card type 5, decoded and ported (2026-10-23)
- **Trigger (Ed's scripts):** in the air (B89) Jump -> B26 + reflex R15 (flight); Action -> B23 + R22 (pogo). B26 ->
  state 54, B46 -> 185 -> 183, B45 = the flight loop. R15 ends it: OnGround -> B62 (landing), not moving 0.2 in 2 s ->
  B62, wall/steep slope -> bump state 268. Flight states 54/183/185/268 (and 346) use **type 5 cards** (Ed's main one
  0x3c379c: mass 80, polar set 2, lags 0.1/0.3/2 s, thrust 2000, wing area 5.5, attack -4..12 deg, bank +-85 deg, bank
  factor 0, top speed 20, launch speed 10, roll sign byte 1).
- **B45's controls:** Up/Down = SetMechMode1/2 with proposal [0x18] (4 per request, or 0.06 x stick), Left/Right =
  SetMechMode3/4 with [0xB]; without input it trims: modes 3/4 bring Report2C0 (lagged bank) to 0, modes 1/2 hold
  Report2BC (lagged attack, rad) at 0.05 = ~3 deg, the best-glide angle of the polar. Up dives, Down flares.
- **Request handler FUN_004aea7c** (per queued request): attack (+0x2B0, deg) -/+ [0x18] for modes 1/2, bank (+0x2B4)
  -/+ [0xB] for 3/4, other modes: bank x card +0x3C; mode 10 thrust target = card +0x24; mode 21 adds a force
  [1..3] x mass / dt (velocity change [1..3]); clamps from the card; then first-order lags (FUN_004c9afb:
  x += (target - x) / tau x dt) into +0x2BC (attack, rad), +0x2C0 (bank, rad), +0x2C4 (thrust); builds the frame
  along the velocity (FUN_004a9993: x = velocity, z = up in the vertical plane, y = left) and rolls it about x by
  the lagged bank (FUN_004a9588 axis-angle; M x Rx).
- **Integrator FUN_004ac0da:** forces (FUN_004c478d): gravity (0, 0, -mass x env g); lift = rho |v|^2 S CL(attack)
  along the rolled z; drag = lift / (L/D)(attack) against the velocity; no lift beyond the vertical (|v.z| > 0.9).
  rho = mechanical environment word 5 (1). v += sum F / mass x dt (FUN_004c9560), capped at card +0x44, position
  += v dt, collide (MEC_004be1bd flags 0x10), orientation to the SuperObject (FUN_004c8dfc). The thrust value is not
  used by any force decoded so far.
- **Polars** (FUN_004cac3c by card +8): set 2 (Ed) = CL FUN_004caa9b = 0.0693 a + 0.18 for a in -10..15 deg (else 0),
  L/D FUN_004caae9 = 11 outside 0..15, 11 a + 11 to 1 deg, sqrt(4 - (3 - a)^2) + 22 to 3 deg (24), then
  -1.08 a + 27.24. Set 1 = same CL, L/D x 0.1; sets 0 and 3 = constants DAT_004e7834..40 (not read).
- **Switching cards (GAM_0042ab20):** into type 5: velocity = card +0x48 along the facing (-Y), controls reset;
  out of type 5: velocity 0, heading from the flight frame.
- **Port:** core/src/tc_mec.c (tc_mec_fly_*), driven by tc_ed_ai when the playing state's card is type 5;
  tc_ai_motion.modes[] = this frame's request queue (each SetMechMode* appends, as the exe's dynamics +8);
  tc_player_move = collision-only move (contacts accumulate over the frame). **.ttai v2** carries the ID cards
  (per-state index + card table). `AI_SCRIPT=fly ./core/build/aicheck files/out/ttlv Land`: launch 10 u/s, trims,
  dives, banks left/right, flares to ~19 u/s, lands on terrain (R15 -> B62).
- Not done: Ed's model is not pitched/rolled with the flight frame (the viewer only takes the heading; the script's
  DynAxis0..2 remap of the model axes is also unused), the camera does not follow flight specially, type-5 states
  other than Ed's not checked.
- `level-ai` listings now name behaviours (B<k>/R<k>) and buttons (btn(jump), btn(up y-110..-30) ...).

### Skiing (Totalski, skymap): card type 2 + the skier perso, ported (2026-10-23)
- **Not a level flag:** each level holds a second perso of Ed's family (NewEd, same 369 states) with a
  **level-specific AI model**: bar 15 + 4 behaviours (probably a cutscene double), Totalski / skymap 23 + 5 (the
  skier; Totalski dyn perso 0x275f3, model 0x19e93d; skymap 0x30c27, 0x145ded). It reads Left/Right (and the stick
  x axis), Jump, Action; its init writes the fix Ed's counter 66 and dsgvars (probably to park him). Listing:
  files/out/ai/ed2_Totalski.txt (scratch script: dump the non-fix NewEd perso's AI).
- **Physics = ID card type 2** on the ski states (115, 124, 217/220, 278; 119 = type 6, 206 = type 8, 321 = type 7,
  299 = the fall card): mass 150, then 0/1 flags, 50..85, 6000|50, 1000, 90, 0.2, 350|260, 100|6000|0, 4|1, 0.6, 0.5,
  2.5|6, 2|0, 0.798635 (cos 37 deg), 0.001. Handler pair {0x4ab91d integrator (decompiled, dnm_handlers.txt),
  0x4b00bf request handler (3.6 KB, not yet)}. The floor material grip 0 / rebound 0.8 of the ski slopes is
  probably read here.
- **Takeover (found 2026-10-23):** the skier's own initial behaviour B0 is empty. A level trigger perso deactivates
  the fix Ed (PROC_6, same code as PROC_5 ActivatePerso?) and sets the skier's behaviours with **PROC_237 / PROC_238**
  (set another perso's normal / reflex behaviour, 0x48b730 / 0x48b800): Totalski 0x60ae7 (first schedule), skymap
  0x6bc43 (B2) -> skier B2 + R0. B2 waits for Jump (push-off state 117), then B3 = riding (states 124/123; Action
  -> 115 = card with top speed 50; Jump -> 125). Not the main actor (0x8C1744 is the level's MS_World perso),
  not GameAction2 (GAM_00429070 = timer reset). Exporter: `LevelData.player()` = the skier when a level perso of
  Ed's family has AI reading buttons (only Totalski / skymap; the other doubles in bar, SpyCarota, mammyon,
  pharmmap, pyr_fou, spynorth read none), `ai.started_by_others()` = the start behaviours; the .ttlv's Ed is the
  skier there (start = his level matrix) and the unplaced fix Ed is left out of the persos.
- **Type 2 request handler FUN_004b00bf** (per request): steering = proposal [0xD] for modes 3/4 (TurnBy writes it:
  AI_00492140 stores +0x34 and queues mode 3); mode 10: push = y x [2] + move direction [7..9] x [1], jump = z x [3]
  (and the report is cleared); mode 20 turn toward [7..9] (unused by the skier). In the air (no contact, no probe
  hit): steering x card +0x3C (0.5), weathervane off; on the ground steering and edge grip x the floor's mat[0].
  Angular velocity = z x steering x card +0x24 + card +0x28 x (v_planar cross y) (weathervane: the front -y swings
  into the travel direction), set fresh each frame (not integrated from a torque). Edge grip force = -card +0x2C x
  (v_planar . x) x. On faces steeper than 60 deg the forces are flattened against them.
- **Forces FUN_004c4ecd:** gravity m g x card +0x34 (4), x card +0x40 (2.5) more in the air; on contact the surface
  takes the normal part of the weight, friction -m grip^2 / dt x v (grip 1 stops dead in one frame, snow 0 none);
  without contact a probe along -z (card +0x44 = 2, within card +0x48 = 37 deg) aligns the board to the ground below
  and, when riding upside down (z . down > 0.65), holds it on with card +0x30. z turns towards the surface normal by
  card +0x38 (0.6) of the angle per frame (0.05 in the air). Steep (normal z < cos 70 deg): forces projected.
- **Then** v += F / m dt, |v| capped at card +0x14 (FUN_004c3af0), orientation by omega, move with collisions
  (MEC_004bedd6), re-snap 0.7 when ground was lost (MEC_004bf9f0), full orientation to the SuperObject
  (FUN_004c8860).
- **Port:** core/src/tc_mec.c tc_mec_ski_*, driven by tc_ed_ai for type 2 states; tc_player.slide_on_ground (ground
  contacts push along the normal and keep the tangential speed), ground_normal / wall_normal.
  `AI_SCRIPT=ski ./core/build/aicheck files/out/ttlv Totalski`: waits, pushes off, ~20 u/s after 3 s on the first
  slope, carves left / right, jumps, 40-64 u/s further down; skymap similar.
- **Ground clipping fixed (user report):** the riding poses reach 0.56-0.67 below the skier's origin (the board;
  standing poses 0), so with the origin on the snow the board sank in, more at the downhill end with the board
  drawn level. .ttai v3 stores each state's drop (lowest point of its first frame below the origin;
  ai.pose_drops); tc_ed_ai_model_matrix draws type 2 states with the board's full orientation, lifted by the drop
  along its up axis (the viewer uses it). Flight still draws with the heading only.
- Known (user, 2026-10-23): it rides but feels janky and unstable next to the original; Ed once got stuck on a
  slope and stopped sliding (collision: our capsule vs the exe's collide spheres / MEC_004bedd6 not decoded).
- Not done: floor mat[0] is not exported (taken as 1, its value on the snow), contact type 3 and mode 20, rebound (mat[1]) on
  impacts, the camera's ski behaviour. The simple controller's sliding profile (I key) is unchanged.
