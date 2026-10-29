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
- **sud0 sets = lighting configurations:** A = dusk (red sky), B = day (blue sky, (-43, 184, 393)), C = night (dark blue). Previews: `out/scratch/light_sud0_set{1,2}.png`. Land's B is biased around 25600, so not plain colours: it is the white flash of Land's lightning.
  - Which set shows: traced and ported 2026-10-29 (DECOMP.md "sud0's day / night cycle"): level persos call
    SetLightSet (sud0's day / night cycle, Land's lightning); the core runs their scripts (.ttlv LAIS) and the shaders
    blend two sets (the exe blends them into IPO+4). Every other level stays on A.
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
  screen plays it at 5:3, 80% of the largest fit, centred on black (user: it is 240p underneath; IntroView.screenFraction) (Sources/OpenTonic/IntroPlayer.swift), Esc / Space / Return skip.
- **Intro -> Totalski (2026-10-24, user):** the original game goes from the intro, watched or skipped, straight into
  the ski slope; both frontends do the same (`startAfterIntro` / `finish_intro`; the level select when Totalski is not
  converted). The level select has a big "Play Intro Video" button at the top (and V), Controls top right.
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
**2026-10-28: see docs/DECOMP.md.** It has the coverage ledger, where the port is not the exe, and the collision
library, read and ported (tc_mec_sphere_tri = FUN_00465b60, proved under Unicorn by tools/exe/oracle.py). The level
files are chunk containers now (docs/BUILD.md "Formats"). The text below is older.

**State (end of session 5, 2026-10-22).** Ed is driven by **his own AI scripts** in the viewer (default when a level has
a .ttai; I switches to the simple controller). The user tested it: "playing pretend Tonic Trouble". Idle with random
idles, run (default on keyboard) / walk (Shift), start/stop transitions, jump/air/land, turning, the stick attack
(E) all come from the game's behaviours; only the physics is ours (tc_player) with hand-tuned speeds.
- **Pipeline:** docs/BUILD.md (textures, vignettes, then per level `level-ttlv` + `level-ttai`, icon, music, app via
  `tools/make_app.sh`). The .ttlv and .ttai of a level must come from the same exporter run.
- **Code map:** core/ = portable C11 (tc_level loader, tc_collision, tc_player physics, tc_anim, **tc_ai** interpreter,
  **tc_ed_ai** AI + physics driver); Sources/OpenTonic = macOS layer. Tests: `make -C core check` (playcheck, must
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
   **Status 2026-10-23 (session 7):** types 0/4 + the collide-and-move + root motion + Ed's ZDM spheres are ported
   ("Ground and air (card types 0/4)" at the end); open: the jump's path, grip < 1 forces, the skier's sphere.
2. ~~**Skiing (Totalski, skymap)**~~ done 2026-10-23: a separate skier perso + ID card type 2 (see "Skiing" at the
   end); flight (card type 5) too ("Flight").
3. **Rest of the moveset through the AI:** Shoot (Q) / Lateral (C) / pogo / fly need their effects: channel
   hide/show (PROC_120/121, props like the stick), meta-actions other than ChangeStateAndWait (11, 12, 19, 21, 40
   finish at once now), collision probes (PathBlocked? is always false), touched objects, zones. Unimplemented
   primitives are counted in tc_ai_perso.unknown (aicheck prints them): FUNC_95/164, PROC_16/35/212-214/290, META_6.
4. **Gamepad (done 2026-10-25):** tc_viewer_set_move_stick feeds the left stick to tc_ed_ai_step as the game's own
   stick (the scripts' thresholds and walk-to-run pace; headless: a 0.35 push 2.9 u/s, 0.5 4.1, 0.85 10, full 12 =
   the keyboard run, directions camera-relative); tc_viewer_set_look_stick turns the camera. `viewcheck` covers it.
5. Other persos' AI (doors, platforms, enemies, bonuses) once the shared primitives exist; then sound events.
6. Older parked items: the 0.25 animation time factor for non-Ed persos (Ed's AI path uses the exe's 1x clock and
   looks right), the invisible sud0 river, Vita/3DS backends, CD-archive pipeline.

**Project direction (agreed with the user, 2026-10-22):**
- **Renamed 2026-10-25 (user):** the project and the app are **Open Tonic** (repo OpenTonic, `Sources/OpenTonic`,
  `OpenTonic.app`, `sdl/build/opentonic`); TonicCore keeps its name; free-roam (Tab) stays in the app. The plan below
  (a separate viewer and game) is history.
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
- Lighting stages 2 (which baked set the game picks / day-night transitions; done 2026-10-29) and 3 (sector lights, fog) are **parked**:
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

## Sound events (2026-10-25): Ed's sound effects
- **Global sound events = Bnk_0's events** (84; the fix bank, always loaded). The snapshot stores a sound reference as
  a pointer 0x9bdfe0c + 0x20 x index (where Bnk_0's event 0 lived); **fix.snd** lists them as { u32 index, u32 address }
  pairs (fix.rts = their relocation table). Levels have no .snd (their own sounds: not looked at).
- **Bank** (.bnm, plain): { u32 version 0, events offset, n_events, resources offset, n_resources, data offset, ... };
  event 0x20 { u32 id, u32 kind (1 play, 2 stop), u32 (index-like, unreliable), ... }; resource 0x5c { u32 id, u32 type
  (1 audio = the vgmstream streams, `bnm/<resource index>/...`; 8 / 10 tiny raw waveforms), u32, u32 size, u32 data
  offset, u32 volume (0x7f), ..., file name }. An event plays the resource of the **same id**; the 6 stop events share a
  resource with a play event (loops: flight, sliding). Event 49 (0x00390000) has no resource: the **footstep switch** over
  0x0039xxxx (EsCarpet, EsIce, EsSnow, EsGrassS/H, ... Edpasgen = generic) by ground material, probably resource r2e
  (type 8) + GameMaterial +8 "sound"; not decoded, the generic step plays. **Event word 2 (fixed 2026-10-25)**: play
  events = the resource index (agrees with the id link wherever both exist; the id link was wrong for 45: edboof01,
  the portal's "boof", not edpafo01), stop events = the index of the event they stop (0 -> 1, 13 -> 12, 19 -> 18,
  33 -> 34 edgliss = sliding, 37 -> 38, 76 -> 77 edplane1 = flight): those targets are the loops. Sfx.swift: a loop has
  one voice (the animations retrigger it every cycle; stacking copies made the flight / sliding echo), one-shot sounds
  overlap to their end (the portal step-in's six boofs, user-confirmed). The engine's voice sharing: not decoded yet.
- **Triggers**: animation events, AnimationReference (TT) +8 ptr events, +0xC u32 n (the A3D header's n_events agrees),
  20-byte entries { u32 type, u32 value, u32 frame, u32, u32 }: type 0 = sound (value = event pointer), type 3 = foot
  contact (value 14 / 15; not used yet), types 1 / 2 rare; 232 of Ed's 369 states have some (523 sounds). Scripts:
  PlaySound3D / ...Other / Global / Global2 (PROC 145-148) with a T34 node (the event pointer).
- **Port**: tools/ttre/sfx.py extracts the events' files (72, 5.3 MB) to out/audio/sfx with events.tsv (play / loop /
  stop, volume); .ttai v7 adds each state's animation sound rows and turns T34 nodes into event indices; tc_ai queues
  the events whose frame the animation clock passes and PlaySound3D's (tc_ai_perso.sounds), tc_viewer_sounds hands them
  to the frontend; the Mac app plays them (Sfx.swift, AVAudioPlayer, muted with M). Not positional yet (3D), no
  per-material footsteps (done 2026-10-24: "Footsteps by floor material"), other persos' sounds not played.

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
- User-confirmed (2026-10-24): **cutscene-only levels** = bar, death, mammyon, outro, pharmmap, pyr_fou, SpyCarota,
  spynorth (`tc_level_is_cutscene`; both level pickers list them last under "Cutscenes", still explorable). marmite
  (Pressure Cooker) is playable.
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
  opaque geometry with source-alpha blending and no depth writes. Sorted back to front since 2026-10-29 (core only:
  tc_viewer_draws, by each mesh's centre; the per-material transparent statics are cut into 16-unit pieces at init).

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
  Added 2026-10-23: InsnGrep.java (out.txt pattern...: every instruction whose text contains a pattern; absolute
  addresses print zero-padded, e.g. "0050e166") and CallArgs.java (out.txt target [n]: each call to target with
  the n instructions before it, to read pushed constants). Session 7's raw decompiles: decomp/s7_*.txt.


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
  MEC_004be1bd when the step turns (velocity angle != 0), else MEC_004bedd6 (no turn; corrected 2026-10-24 from the
  typed decompile: before this line had them the other way round); on losing ground contact, the ground re-snap
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
  **Update 2026-10-24 (typed decompiles): the model pose is now ported, see "Flight, read with types" at the end.**
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
- **Slope props (2026-10-24):** the speed arrows (SYM_GenHurryUp; skymap JET_Speed*) and spring stars
  (SYM_Gen_Ressort; JET_Etoile*) are trigger persos: within PROC_18(Me, r) they write the skier's dsgvars and sleep
  (META_29 = Wait(ms), FUN_004a0b00 / 0x4a0b30, now ported). Arrow: dsg0 0, dsg22 1, dsg24 duration (ms), dsg27 the
  proposal (2000, -500, 0), dsg46 its facing (local -Y, downhill) -> skier B3 rule 1: state 278, B12 = each frame
  ProposalVecB(dsg27, dsg46) (AI_004891a0: [7..9] = direction, then ProposalVecA) + SetMechMode10Value -> push
  -500 along the board + 2000 along the arrow, until TimeElapsed dsg24. Spring: dsg27 = (own dsg0, dsg1, dsg2), e.g.
  (0, -100000, 300000) -> B3 rule 2: ProposalVecA(dsg27), state 217, Wait 1000, R2 (-> 220), B8. Exported as
  capture-only portals (ai.ski_portals). ProposalVecA queues no request; on a type 2 card the port takes a freshly
  written non-zero vector as one 1/30 s frame of mode 10 (the skier's own jump button uses the same layout,
  (0, -70000, 10000)): spring ressort1 ~45 u/s up, 30-40 units high. The exe's routing of it is not decoded.
  Their flat decal quads (2 triangles facing up, also the red direction arrows further down) were collision and
  stopped or slowed the skier: `LevelData.collision(skip_decals=True)` for skier levels drops them.
- Not done: floor mat[0] is not exported (taken as 1, its value on the snow), contact type 3 and mode 20, rebound (mat[1]) on
  impacts, the camera's ski behaviour. The simple controller's sliding profile (I key) is unchanged.

### Ground and air (card types 0/4), collisions, root motion: decoded and ported (session 7, 2026-10-23)
- **Walking speed = root motion.** Each A3D frame's own vector (frame +2 -> vectors; level.py ignored it) is the
  perso's displacement per animation frame in its axes (front = -Y): walk (1) (0, -0.08, 0), run (10) (0, -0.2, 0),
  run (25) -0.26, sprint (96) -0.4, backward walk (2) +0.08, run start (158) 0 / -0.2 / -0.26 / -0.23 / -0.4, walk
  start (9) ramps -0.04 .. -0.125; idle, take-off (39), landing (41): none; jump/fall (43/44): 0. x the frame rate:
  walk 0.08 x 30 x Proc10 139 % = 3.34 u/s, run 0.2 x 45 = 9.0 u/s: exactly the speeds hand-tuned earlier, so
  run_scale / walk_scale are gone (kept only as the fallback for .ttai v3). The frame quaternion beside it is the
  identity for all of Ed's states. The code applying it was not found (not in PlayAnim.c 0x415500-0x416390; no
  writer of the wanted motion +0x6C outside the handlers): the port feeds it as the request's wanted velocity.
- **Ed's collision body = the ZDM spheres of his collide set** (perso +0x14 = {ZDD, ZDE, ZDM, ZDR lists, then
  their activation lists}; ZDR is null for Ed). Zone list head {first, last, n}, entries {next, prev, head, ptr
  GeometricObjectCollide}; sphere element (type 7): vertex = centre, radius at element +0xC. Activation lists have
  one entry per state (+0x10 state index, +0xC -> list of {.., +0xC zone index}). Ed standing/walking: r 0.25 at
  (0, 0, 0.25), 0.375 at (0, -0.1, 0.75), 0.53 at (0, -0.15, 1.28), 0.3 at (0, -0.3, 1.95) (model 2.17 tall; our old
  capsule was 1.4); running/jumping add zone 9 (element type 5, not a sphere, skipped); 9 distinct sets; ski states
  none. ZDD = one r 3 sphere 3 ahead, ZDE = 18 boxes (zone 5 = body box). **The skier (Totalski / skymap) has no
  ZDM**: its collide set has a flag (GAM_00431720) and a radius at +0x30 (1.3) and the exe sweeps a single sphere
  (FUN_0043d480 path, GAM_0042bc34); not ported (the skier still uses tc_player's capsule).
- **Collide-and-move MEC_004be1bd** (flags 1 for Ed): up to 6 passes of {sweep the body from the start position
  (dyn +0x294, fixed during the loop) to the target; no hit: done; else respond}. Still blocked after the 6th
  response: back to the start with zero velocity and last frame's contacts (MEC_004bdeee / be074). Sweep FUN_0043bff0
  -> contact record (0x7C): [0] 1 static / 2 perso, [1] own zone type (FUN_0043bf70: 1 for mechanics zones), [2] t
  0..1, [3..5] normal, [6..8] point, +0x30 obstacle material, +0x4C own material, +0x78 moving shape, +0x7A contact
  kind (corrected 2026-10-28, docs/DECOMP.md "The collision library": for a sphere 1 face, 2 edge, 0 corner). Two sweeps with zone masks 1 and ~1: a mask-1 hit counts only on floors with normal z >= 0.93
  (DAT_004dc9bc); not ported (one sweep of all spheres).
- **Classification MEC_004bc477** by n . up with the card (+0x1C/+0x20/+0x24/+0x28; no card: cos 45/60/120/135):
  ground (1) / steep slope (2) -> MEC_004bb904; wall (4) -> MEC_004bbc4c; overhang (8) / ceiling (0x10) ->
  MEC_004bc20a. Then the target moves out 0.01 along the normal (DAT_004d3010). Each response: impact = start +
  (target - start) t, velocity v at the contact point (v + w x r, FUN_004c9c90), rest = (1 - t) dt, new target =
  impact + v' rest. Ground/slope: if v . up <= 0.35 or the hit is not head-on (v . n / |v| >= -0.7071): slide (v
  minus its normal part; on a steep slope also x f(|v|, 6), f(a, b) = (2ab - b^2) / a^2 for a > b (FUN_004c99af):
  speeds along steep slopes tend to 12); head-on and rising: rebound v -= n (2 - r)(v . n), r = obstacle x own
  material mat[1]. Ceiling: slide, or head-on: bounce with r = 0. Wall: step-up (below), else slide with x/y x
  f(2 vx^2, 36) (the exe uses vx twice; z untouched), or head-on: rebound.
- **Step-up MEC_004bb06e** (walls only): when last frame had ground (report & 3), card +8 (step height: Ed's ground
  card 1, air cards 0) > 0, |n . up| <= card +0x2C (sin 7 deg) (or contact kind 2): put the body 0.15 beyond the
  contact at the feet's height, card +8 higher, sweep it down; a floor within card +0x30 of flat (cos 20 deg) ->
  the loop continues from there raised (start := top - offset + 0.01 n, target := top, v := offset / dt), report
  0x41. Ed steps onto ledges up to 1 unit (mechcheck: 0.5 and 0.9 yes, 1.5 blocked).
- **Integrator FUN_004aa6af** (types 0 and 4): grounded = (report | last frame's report) & 3. Forces (FUN_004c41c5):
  gravity m g card +0x38; on the ground with grip >= 1 (floor mat[2] x own): no force and last frame's velocity is
  dropped, so v = wanted (the request's motion, projected along the ground normal, FUN_004c1541); grip < 1 (decoded
  2026-10-24 from the listing, the decompile drops this inline code): F loses its part along the contact normal
  (+0xCC +0xC: the ground takes it, the slope part stays), wanted x grip^4, and friction F += -m grip^4 / dt x v (v =
  dynamics +0x38, last frame's), so v = (1 - grip^4) v_old + slope dt + grip^4 wanted: Ed eases towards the wanted
  speed per frame. Before this the port added grip^4 x wanted on
  top of v_old every frame with no brake: on cock01's ice Ed gained ~70 u/s per second up to the 100 cap.
- **Per-frame fractions -> any frame rate** (2026-10-24): the exe applies some shares once per mechanics frame (ice
  friction grip^4, the skier's friction grip^2, the board turning 0.6 / 0.05 of the way to the ground), so at a
  higher frame rate they act more often. Its mechanics frame is taken as 1/30 s (the re-snap's 30 dt, the 1/30 and
  30 constants), and `tc_mec_per_frame(k, dt)` = 1 - (1 - k)^(dt x 30) gives the same share per 1/30 s while the
  port still steps every displayed frame (120 fps on the Mac). Ice at grip 0.4 after 1 s of running: 4.9 / 4.8 / 4.8
  u/s at 30 / 60 / 120 fps (was 4.9 / 7.1 / 8.6). Use it for any new per-frame share found in the exe. In the air the wanted motion is dropped unless the handler set +0xB8 = 8
  (type 4 does). v = v_old + F dt / m + wanted; kinetic energy m v^2 / 2 < 0.025 -> 0, |v| capped at 100
  (FUN_004c39f0). v = 0: no move (contacts kept). Then collide-and-move; ground lost this frame and the last but
  present the one before -> re-snap (MEC_004bf9f0): sweep down card +0x34 (x 30 dt beyond 1/30 s; Ed 0.4 / 0.3),
  normal within card +0x20 (cos 70) of up -> stand 0.01 / (n . up) above it if the way is free, report |= 1. On flat
  ground the report alternates (no contact while walking, re-snap every other frame); the AI reads report | last
  report (+0xC8), so OnGround stays true.
- **Type 4 request handler FUN_004b0f2a**: +0xB8 = 8, clears the contact reports (an air card never counts as
  grounded, the AI sees only this frame's contacts); modes 3/4 turn by proposal [0xD]; 7: velocity := 0, wanted :=
  R(heading) x proposal [1..3] (ProposalVecA); 8: the same without the reset; 6/21: as 7 plus GAM_0042b5b0(0.1)
  (probably the carrier's velocity); 9: R x proposal [4..6] (ProposalVecD). Default mode 0: nothing. The engine's
  default request is mode 0 (GAM_0042a8a0 -> the state's card, State +0x30), and Ed's normal jump (B85:
  ProposalVecA then ChangeState(43)) pushes no mode, so how the jump velocity reaches the integrator is still open;
  the port applies ProposalVecA as mode 7 on the frame it is set (apex ~3.4 at 60 fps: 15.6 u/s against 9.81 x 3.5).
  An impulse away from the ground also clears the contact reports (as type 4 does), under any card: the pogo sets
  its launch vector in state 59, a **type 0** card, and with last frame's ground report still counting, full grip
  dropped the velocity the next frame and Ed hovered (regression from session 7, fixed 2026-10-23;
  `AI_SCRIPT=pogo aicheck files/out/ttlv bar`: hops ~1.2 high at 5 u/s). Whether the exe does the same is part of
  the open jump question.
- Constants: DAT_004d3000.. = 0.7071, -0.7071, 1, 0, 0.01, 2, -1, 0.35; 004d3050.. = 0.5, 0.025, 10000; 004d2ee8
  1/30, 004d2eec 30; 004d30cc.. = 0, 1, 2. The exe file on disk is not readable as plain PE data at these
  addresses (packed/protected); read constants through the Ghidra project (tools/ghidra/Raw.java).
- **Port:** core/src/tc_mec_walk.c (tc_mec_sweep: swept spheres vs triangles, faces from both sides, edges and
  corners as capsules, a touching sphere hits at t = 0 only when moving in; tc_mec_walk_step = the integrator, the
  6-pass loop, responses, step-up, re-snap; a small depenetration before each move, not in the exe, for sphere sets
  changing with the state). tc_ed_ai uses it for type 0/4 states that have spheres. **.ttai v4** appends the root
  rows, per-state {root rows, sphere set} and the sphere sets (ai.root_motion, ai.zdm_sets); v4 = v3 + appended
  tables, so the .ttlv pairing is unchanged. Tests: `make mechcheck` (no game data), `AI_SCRIPT=wander aicheck`
  (60 s stress: all levels: no teleports; stalls only against walls / in corners; pyr_fou and spynorth start
  facing a gap narrower than Ed's 1.06-wide torso sphere: blocked frames, as the exe would; BigBoss: a corner with
  a wall leaning back (n.z 0.26) makes Ed slide up, lose ground and fall in a loop). 30/60/120/144 fps agree (run
  9.0, walk 3.28, apex 2.7-3.0; 30 fps differs at walls).
- **Open:** the jump's path (above); the zone
  masks of FUN_0043bff0; the skier's single-sphere collide set; dynamics +0x38C (when set, standing still keeps the
  reports; the port always keeps them); air control (mode 9, ProposalVecD) and the AI's Velocity field under type 4;
  whether the exe projects root motion onto the ground (the port does, as mode 12 does).

### The skier's collision sphere, and the animation clock (session 8, 2026-10-23)
- **Skier body:** collide set byte +0x36 (GAM_00431720; skier 1, Ed 0) switches the mechanics to one sphere
  (GAM_0042bc34 -> FUN_0043d480 -> FUN_0043d400 -> FUN_0043d2f0 -> FUN_00464b80 -> FUN_004717d0): centred on the
  perso's origin (the sweep runs between the two matrices' translations), radius = collide set +0x30 x 0.5
  (DAT_004d2928; the skier's 1.3 is a diameter -> 0.65). His riding poses reach 0.611-0.646 below the origin over
  all their frames, so the board rides 0.005-0.04 above the snow with no visual lift. The old port put the origin
  on the snow (tc_player's upright capsule) and lifted the drawn model by the frame-0 drop along the tilted board
  axis; on slopes the two disagreed and the board sank (user: "slightly clipping").
- The sphere path's responses MEC_004bd297 / bc8cd / bcb80 / bd0b8 are bc477's family without the contact point's
  angular velocity term (v + w x r): the same math as the port. The type 2 integrator (FUN_004ab91d) calls
  MEC_004bedd6 with **no card** (default classes cos 45/60/120/135, no step-up, no softening) and flags 0x10, then,
  when that move touched nothing and last frame had ground, MEC_004bf9f0(distance 0.7, min n . up 0.7).
- **Port:** ai.zdm_sets exports {(0, 0, 0), 0.65} for every state of a perso with the flag (.ttai v4 unchanged in
  layout; Totalski / skymap regenerated). tc_ed_ai's ski path: tc_mec_collide_move(card NULL) + tc_mec_resnap(0.7,
  0.7) (both split out of tc_mec_walk_step), contacts from tc_mec_walk; tc_ed_ai_model_matrix draws at the origin
  when the state has spheres. States with spheres and an undecoded card (the skier's 118/119 type 6 parked, 206 type
  8 crash, 321 type 7 finish) run the type 0/4 path without a card so the body stays the same. aicheck prints the
  board's clearance (snow along the board's down axis minus the pose's reach): ski script and 60 s wander on both
  levels, 30-144 fps: min 0.015 (the designed gap), never into the snow; the old Totalski wander stall is gone; hard
  head-on hits now stop the skier (floor rebound 0.8 slows slope impacts).
- **Animation clock (user: animations "way too fast"):** GAM_PlayAnim_00415fb0 adds per engine frame
  (DAT_008bd098 / 1000) x frame rate (+0x163) frames (exactly 1 frame instead when DAT_004ec6b8 is set; nothing
  writes it directly). GAM_0041cc90: DAT_008bd098 = timer delta (TMR_Timer_00484fa0), DAT_008bd0dc = the same
  clamped to 1..64 = the mechanics' milliseconds. So the exe's clock = real seconds x frame rate, as the port's
  (tc_ai advance_state). Checks: the timeline is identical at 30/60/120/240 fps; state 43 (14 frames at 30) lasts
  0.47 s against the 0.45 s rise of the data's jump (15.6 u/s against 9.81 x 3.5); walk/run cycles match their own
  root motion (no foot sliding). Rates measured: idle 20 fps, walk 41 (30 x 139 %), run 45. The unexplained 0.25
  for non-Ed persos (ANIM_TIME_SCALE) contradicts this and is worth revisiting.
- **Walk/run rate, traced (user: Ed's walk and run look too fast).** B2 (node 872) / B88 (13226, 13260): below the
  threshold dsg79 < dsg82 + dsg78 Ed walks with Proc10(dsg75 x dsg79 x 222), else runs with Proc10(dsg75 x dsg79 x
  100); dsg79 = stick magnitude / 100, or with keys 1 (run) / 0.625 (SpeedUp held: walk); dsg75 = 1 out of zones (in
  a zone: 1 - EnvHeight x 0.2 below 2, else 0.6: wading). So keyboard walk = 30 x 139 % = 41 fps, run = 45 (state
  10's own +0x3B) x 100 %. GAM_0041ec90 (Proc10) multiplies the playing state's +0x3B; GAM_00437170 resets +0x163 to
  it on a state change: as the port. DAT_004ec6b8 (1 frame per tick) is never written (dead switch). The timer
  (FUN_004855a0) reads timeGetTime (ms) or a performance counter; the mechanics use the same delta as ms (clamped
  1..64). Conclusion: Ed's rates equal the exe's for the same inputs; a remaining mismatch can only be checked
  against footage of the original (ours: run 2.5 cycles = 5 steps per second, keyboard walk 1.7 cycles per second).
- **Ski air gravity, re-checked (session 9; user: the skier "doesn't fly off properly").** FUN_004c4ecd (now in
  decomp/s9_ski_forces.txt): gravity = mass x env g x card +0x34; x card +0x40 more when last frame's report is 0 and
  the probe flag dyn +0x2F0 is clear; the probe (report 0 or 0x20, |v| > card +0x4C) casts from the origin along -z
  and sets the flag on a hit within card +0x44 (2) at more than card +0x48 (cos 37 deg). Skier cards (124: top speed
  70; the rest 50): +0x34 = 4, +0x40 = 2.5, so 9.81 x 4 = 39 u/s^2 near the snow and 98 u/s^2 more than 2 units above
  it, the same as the port. FUN_004c3550 appends the force to a list, FUN_004c969c -> FUN_004c9560 integrates
  v += sum F / m dt (as flight), FUN_004ca140 returns its argument (the state's card), the cap FUN_004c3af0 is 3D and
  unconditional. No change: the difference has to be found against footage.

### Prop collision: box and sphere collide elements (session 9, 2026-10-23)
- **Object names:** the GPT's 3 object type name tables (family, model, instance; lists of {next, prev, head, ptr
  name}) are indexed by the perso's std game block (+0 family, +4 model, +8 instance); the fix part links to the
  level part through an unresolved pointer, found as the level entry whose prev points back (LevelData.object_names,
  perso_names). Land: kitkatland (SDO_KKChemin, paths), landpont (bridges), gen_pierres (stones), grille (gates),
  sud_ponlevi (drawbridge), escargot (Sluggy) ...
- **Where props collide:** every static IPO already had its collide mesh (type 1 triangles; no IPO uses anything
  else). Props are persos; their collision is the collide set of their physical objects (as IPOs), mostly **type 8
  (axis-aligned box)** and **type 7 (sphere)** elements, which the exporter skipped. Element = {u32 n, ptr entries}
  with the entries inline after it: box {u16 min vertex, u16 max vertex, ptr game material}, sphere {u16 centre
  vertex, u16, f32 radius, ptr game material}; the collide object's vertices hold the corners / centre. Users: Land
  kitkatland x16 (7.6 x 2.4 x 0.4 path pieces over a gap: before, the ground there was 11 units lower), landpont x15,
  grille, leg_grille, sud_ponlevi, distrib, gen_pierres (r 1.9); sud0 sud_biblio, ponlevi, pierres; carota
  leg_machpor; north grille, nord_pfmonaie, gen_cage. Land: persos with collision 7 -> 50.
- **Export:** LevelData.collide_primitives turns them into triangles (box: 12; sphere: 80-face icosphere) in the same
  collision list (.ttlv format unchanged; all 21 levels re-exported, the .ttai files still match). Prop collision is
  posed at frame 0 of the perso's initial state, like the existing perso meshes: gates, drawbridges and moving
  platforms are frozen there (closed gates stay closed).
- **Not done:** persos' own collide sets (ZDR lists: gates, distrib, dalfou, leg_pale have ZDR boxes; the mover's
  ZDM is swept against other persos in FUN_0043c830 -> FUN_0043c640, list types 1 and 3 from GAM_ZdxStuff); moving
  props (the collision should follow the perso's animation / mechanics); collide element type 3 (2 in carota, 1 in
  north, unknown).

### Ski obstacles: trees, cacti, icicles crash the skier (session 10, 2026-10-23)
- **No collision geometry:** Totalski's Tree (x41, BEN_newtree / SYM_sapin / SYM_Cactus), stalag (x22), Barrierre /
  Barriere2 (x17) have no collide sets in their physical objects and no ZDx zones: the mechanics pass through them in
  the original too. The crash is scripted by the obstacle.
- **AI activation radius:** PROC_18 / 19 / 20 / 21 (shared handler FUN_00488580, switch on the procedure number;
  case 10 = Proc10 confirms the numbering) call GAM_00424280(perso, a, b, r): std game +0x2A = a, +0x2B = b, +0x2C =
  r^2 (GAM_004242d0; -1 stays -1 = always). PROC_18 (0, 0, r), 19 (0, 1, -1), 20 (0, 1, r), 21 (1, 1, -1), 22 sets
  r only. GAM_0042b520 runs a perso's AI only when +0x2C is -1 or GAM_0042a680's squared distance between its origin
  and the main actor's (DAT_008c1720's first perso; KW_5 = FUN_00497270 = the same) is below it. +0x2A/+0x2B not
  traced.
- **Tree schedule (B0):** Proc9(1); PROC_18(Me, dsg0 = 2, cacti 3, one sapin 4); If Equal(Dot(KW_5, dsg17), 0)
  Then PROC_237(KW_5, skier B9); META_29(2000). Standing icicles (17, B3): the same with radius dsg2 = 2, then they
  break (PROC_19, ChangeStateAndWait(2), PROC_257 re-init); hanging ones (stalagup, 5, B0) fall when the skier is
  within dsg3 130-150 and shatter on contact (no crash). Barriers: PROC_20(Me, 5) then a break animation, no crash.
- **The crash = skier B9:** if Counter34 (hit points) is 0, game over (315 / 299); else dsg19 = time, state 115, a
  sound, ChangeStateAndWait(274) (55 frames at 30 = 1.8 s; its type 2 card has top speed +0x14 = 15, so the cap
  brakes the skier), dsg17 = 1 (blink, SetTransparency 127), back to B3. Reflex R1 script 2 clears dsg17 after
  TimeElapsed(dsg19, 3000).
- **Port (.ttai v5):** ai.proximity_triggers finds that pattern in every level perso's starting schedule (radius
  from PROC_18/19/20(Me, x), guard from If Equal(Dot(KW_5, dsg), 0), behaviour from PROC_237(KW_5, ...) mapped to the
  player's model, sleep from the META_29 after it) and exports {position, radius, behaviour, guard, sleep}; Totalski
  58 (41 trees and cacti, 17 icicles), every other level 0. tc_ed_ai checks them before the AI step (3D distance
  between origins; sleep 0 = fire once, re-arm when the skier leaves the radius) and calls tc_ai_force_behaviour
  (PROC_237). `make crashcheck`: start 12 uphill of a tree, crash at 1.6 s, 15 u/s in state 274, riding at 3.4 s,
  blinking until 4.5 s.
- **Not done:** the obstacles' own reactions (barriers breaking, icicles falling and shattering, the tree's state)
  need their AI and animations running (persos); skymap's obstacles are a different design (mines on META_28 paths
  forcing the skier's behaviour by direct perso reference, pillars without AI, fire jets): later, with the persos'
  AI in general.
- **skymap (parked, session 10 short pass):** the skier's AI is Totalski's (same B9 crash, flags). Its obstacles are
  not the trigger pattern: mines (glac_mine x14) move on META_28 paths, and B4 does PROC_20(Me, 3) + PROC_237 with a
  direct PERSO ref to the skier (plus reflex R0: explode when FUNC_14(skier) < 1.5); pillars (x17) have no AI, only
  2 ZDE zones; fire jets / lightning cycle by script. The skier's reflex R1 script 2 (also in Totalski) crashes on
  `CollectNearby(list, 6, MASK(0x10 | 0x40))` non-empty: CollectNearby = GAM_ZdxStuff_0041fa70 (decomp/
  s10_collectnearby.txt) -> GAM_0041efe0(perso, 2, 6 - 1) = the skier's own zone #6 of zone list 2, tested against
  other persos' zones by GAM_ZdxStuff_0041fb00 with the mask. So pillar / fire hits are zone-vs-zone (ZDx event
  zones, per-state activation, mask bits): needs that subsystem (export every perso's zones + the list/mask
  semantics of 0041fb00 / 0041efe0) - with the persos' AI work.

### Level portals (red arches), first look - parked (session 10)
- **2026-10-24: decoded further, see docs/PORTALS.md** (the plan). Corrections to the guesses below: Ed.dsg136 is the
  arrival heading (PROC_242), Land's arches teleport within Land (PROC_8), real level changes are sud0's arches
  (ChangeLevel) and the exits back to sud0 (PROC_29 with named places); the transition screens are fix data.
- Portals = family **gen_arche** (Land: SDO_Arche1/2/5/6, SDO_ArcheRetourSUD; sud0 x20 HEG_Arche...; Totalski
  GIZ_ArcheRetourneDansSud0; skymap NBD_ArcheGagneSkymap); **sprite_arche** = their effect sprites. No collide set.
- The arch's script does not change level itself. B0: if Dot(info, dsg134) == 0: info.dsg99 = Me, info.dsg100 = 0,
  info.dsg136 = own dsg5 (an int8 index: Land arches 5, 3, 1, 2), then PROC_237 / PROC_238 force the **game-info
  perso's** (PERSO@0x948da, a perso-in-fix; the offset alone is ambiguous across files: resolve it through the node's
  ref, not loc[1]) normal / reflex behaviours (0x68fff8 / 0x6b36c0) -> that perso's script does the transition
  (likely PROC_29(level, n, 'position name', 'camera name'), as the skier's B22: PROC_29('sud0', 6,
  'heg_positionedskymap', 'heg_positioncamskymap'), or ChangeLevel). Arch dsg0..2 are pointers (waypoints?), dsg5
  index, dsg6 a float (73 / 237 / 78 / 257: maybe a heading).
- **Blue portals (in-level):** B1 script 1 of the same model: when info.dsg103 == 1 ->
  PROC_8(info, PositionOf(dsg0), Ed, PositionOf(dsg1), dsg3) = teleport Ed between the arch's two waypoints.
- Next: dump the game-info perso's AI (find it via the PERSO node's resolved ref), read behaviours 0x68fff8 /
  0x6b36c0 for the dsg136 -> (level, entry point) table and how Ed is detected (ZDx zones or distance), then: core
  trigger "enter arch -> request level change (name, entry waypoint)", viewer: vignette 3 s + loading bar + "loading
  <level>", load the level and place Ed at the named waypoint.
- **Correction + the portal animation (session 10, end):** PERSO@0x948da in the arch's script is **Ed** (the fix
  Ed; also the "hit points" Counter34 owner), not a separate game-info perso. The arch (B3: Proc9(1); PROC_18(Me, 20)
  ... PROC_18(Me, 4) -> its AI wakes when Ed is within 4, the same activation-radius mechanism as Totalski's trees)
  runs B0: Ed.dsg99 = the arch, Ed.dsg100 = 0, Ed.dsg136 = arch dsg5 (destination index), then PROC_237(Ed, **B66**)
  and PROC_238(Ed, **R4**) (refs resolved through the nodes against Ed's model). Ed's B66 plays a lead-in by his
  current state (ChangeStateAndWait 29 / 182 / 38 / 127, then state 0) and goes to **B68**: on the ground,
  ChangeStateByDirection(dsg99 = the arch, offset (0, -4.5, 0), state 9 walk-start / 2 backward) - he walks to 4.5
  in front of the arch -, TurnTowardOverFrames(arch, 1), dsg0 = 20 (the arch's B2 waits for Ed.dsg0 == 20 and then
  drives the camera perso 0x225266: dsg44/89/90 = 1, dsg91 = 0, dsg92 = arch waypoint dsg2, dsg93 = position, dsg39
  = 6), **ChangeStateAndWait(state 228) = the step-into-the-portal animation**, then B106 -> B102 (empty in Ed's
  script: Ed waits). Holding Shoot skips to B106. So the level load itself is done elsewhere: next, find which perso
  reads Ed.dsg136 / dsg0 == 20 and calls ChangeLevel / PROC_29 (candidates: the level's genLand POC_GenMap x34,
  DFM_Save x23, World WLand). Plan: run Ed's B66 -> B68 for real (needs ChangeStateByDirection, TurnTowardOverFrames,
  forced behaviour/reflex - tc_ai_force_behaviour exists), let state 228 play, then the loading screen
  (vignette 3 s + bar + "loading <level>") and the new level with Ed at the entry waypoint.

### Typing pass, step 1 (2026-10-24): the Ghidra project is typed (docs/TYPES.md)
- **Done:** tools/exe/types.h (46 types: the perso chain, Perso3dData, StdGame, State / transitions / AnimRef,
  Mind / Intelligence / DsgMem, Dynam / Dynamics / DynamWork / RequestQueue, ContactRecord, MecMaterial, MecEnv,
  Proposal, the type 0/4, 2 and 5 ID cards, the handler table), checked by tools/exe/types_check.c (clang i386) and
  again inside Ghidra before anything is applied; 80 prototypes (tools/exe/signatures.tsv, every argument count read
  from the asm with tools/ghidra/ArgCheck.java), 17 globals (tools/exe/globals.tsv, float constants only labelled when
  memory holds the expected value: all of the notes' constants did). Typed decompiles:
  files/out/_decomp_artifacts/typed/<addr>_<name>.c. Backup of the untyped project: ghidra_backup_2026-10-24_pre_types.
- **Function names now carry their meaning and keep the address** (MEC_004be1bd -> MEC_CollideMove_004be1bd,
  FUN_004aa6af -> DNM_IntegrateWalk_004aa6af ...): grep by address still finds them. The raw decompiles in decomp/
  keep the old names.
- **Corrections found while typing:**
  - perso +8 is a wrapper, not the dynamics: Dynam {+0 Dynamics *, +4 DynamWork * (per-frame workspace: +0x6C the
    wanted motion, +0x12C the force list of types 2 / 5), +8 RequestQueue *, +0x10 activation radius, +0x11C current
    card type}. "Dynamics +8 = request queue", "+0x10 activation radius" and "+0x11C" above are dynam offsets.
  - The handler call sites (asm): integrator(dyn, so, carrier, sector SuperObject, work, card, dt), request
    handler(dyn, so, work, card, dt, mode). The integrators' 4th argument is the perso's current sector (sector_info
    +4); the environment is looked up in it.
  - MEC_004be1bd = the move with a turn, MEC_004bedd6 = without (see the corrected line above).
  - Dynamics +0x08 = a Vec3, +0x14 = a 3x3 frame (identity at init; the flight integrator uses it), not three
    quaternions. Dynamics +0xB0..+0x2AF is one block (init FUN_004c1c00): handler modes, reports, three contact
    records (+0xCC ground, +0x148 wall, +0x1C4 perso contacts: report bit 0x10000), the velocity / position
    snapshots. Contact record +0x68 = the obstacle's SuperObject, +0x6C its dynamics, +0x70 / +0x74 the two game
    materials, +0x24 an edge vector (kind 2 only).
  - The type 5 and type 2 code never reads its card directly: FUN_004c9a20 / FUN_004ca140 return their argument and are
    called only by the type 5 / type 2 code (the source's casts; now MEC_AsFlyCard / MEC_AsSkiCard). The flight
    integrator collides with card NULL, flags 0x10.
  - AnimationReference +0xC is u32 n_events (level.py's raymap f32[3] there is wrong; ai.py already read it right).
  - Dynamics +0x2B0 (flight controls) is shared by card types 1 and 5.
  - The dynam keeps 8-entry histories of the step's velocity, position and dt (GAM_0042aee0 / af40 / afa0).
- **Not typed yet (step 2):** the step-up MEC_004bb06e (14 arguments, its call site in the decompile shows 13), the
  card accessors MEC_004b9530..004baa14, the AI dispatch handlers, collide sets and zones, sound, transitions, the
  engine globals.

### Typing pass, step 2 (2026-10-24): AI interpreter, zones, sweeps, card layouts
- **AI interpreter typed** (types.h AINode / AIValue / AIBehaviour / AIAction / DsgMem ...; tools/exe/tables.tsv gives
  every dispatch table one prototype; names from tools/exe/ai_names.tsv). Asm-checked call shapes:
  AI_EvalNode_0048e930(me, node, result) = g_ai_node_handlers[node->type] (41 node types; 247 callers: every
  primitive evaluates its arguments with it); keyword / condition / operator / function primitives
  fn(me, node + 1, result) (conditions get an int *), procedures and meta-action starts fn(me, node + 1), meta
  continuations cont(me) (1 running, 4 done), field getters get(me, out). `me` is the perso's SuperObject. Tables
  (counts = gaps): 294 procedures, 10 keywords, 111 conditions, 165 functions, 28 operators, **43** meta-actions (a
  string follows, not 47), 15 fields, value kinds at 0x4E7058 {value type, after_get, before_set}. 295 distinct
  implementations typed and named <KIND>_<Name>_<addr> (shared ones ...Etc_; unnamed <KIND>_<index>_), in
  files/out/_decomp_artifacts/typed/ai/. AINode +4 (u16) caches the offset to the next sibling; a behaviour's
  schedule is one more indirection (as ai.py found); the action table is {entries (0x50), u8 capacity, u8 used};
  AI_GetDsgVar_0048fa20 reads mind->dsg->current + def[0]->infos[i].offset. New names: PROC 237 ForceBehaviour,
  238 ForceReflex, 242 SetHeading (ai.py matches primitives by number, not name).
- **Card accessors MEC_004b9530..004baa14 are dead code**: no caller and no pointer to them anywhere in memory, except
  MEC_004ba9c2 (launch speed, GAM_0042ab20). They still give the cross-type layout (types.h, ID cards comment): ski
  +0x10 / +0x18 = min / max turn as walk's; gravity factor 0/4 +0x38 = type 7 +0x1C = type 10 +8.
- **Step-up MEC_StepUp_004bb06e** (14 args from the asm): tried only when last frame had ground, the hit's own zone
  type is 1 (mechanics zone) and step_height > 0 (or collide flag 0x40), and the wall is near vertical or the contact
  kind is 2.
- **Sweeps:** MEC_SweepZones_0043bff0 = two MEC_SweepPass_0043c830(sector, so, start, end, mask) passes (masks 1 and
  ~1), each read with MEC_HitsNearest_004602c0 into a 0x30-byte SweepHit; a floor-pass hit counts only with normal.z
  >= 0.93 and, on a perso, only when that perso's collide set byte +0x37 (floor_ok) is set.
- **Zones typed:** collide set = 4 zone lists (ZDD, ZDE, ZDM, ZDR) + 4 activation lists, zone entries -> GeoCollide,
  activation {zones, state}; collide elements (triangles / spheres / boxes) as the exporter reads them.
- **Matrix type bits** (asm): 0x80 = the full layout (else a compact form at +2), 0x10 translation, 0x40 scale.
- **Platforms:** DNM_FollowCarrier_004a83d2 moves the perso with its carrier and collides that move;
  DNM_LeftCarrier_004a8388 tells when it left it (0.2); SPO_SetParent_00483430 re-parents.
- Backup of the step-1 project: ghidra_backup_2026-10-24_step1.

### Flight, read with types (2026-10-24, typing step 3)
- **The physics equals the port.** Checked line by line in DNM_RequestFly_004aea7c, DNM_IntegrateFly_004ac0da and
  DNM_FlyForces_004c478d: modes 1-4 / 10 / 21 / default, the clamps, the lags (deg -> rad, DAT_004d2f20), the thrust
  cut at 0.7 x card thrust, lift rho |v|^2 S CL(attack) with the commanded attack (degrees, not the lagged one; the
  lagged attack and bank are only reported to the scripts and roll the frame), drag lift / (L/D), no lift beyond
  |x.z| 0.9, v += sum F / m dt, then the top speed. The frame FUN_004a9993 = Rz(heading + pi / 2) Rx(acos v.z) with
  its columns reordered = (v, horizontal left, up in the vertical plane): exactly the port's.
- **Differences found and fixed in the port (core/src/tc_mec.c):** the forces act in the frame the request handler
  builds and stores (dynamics +0x14, `dyn->rotation`), not one rebuilt from the integrated velocity; mode 21's velocity
  change goes through the force list (applied in the integrator), so it no longer tilts the frame built in the same
  request. Below |v|^2 = 1e-4 the frame is kept (the exe rebuilds it from the model's axes and rolls it again).
  Flight trajectories in `AI_SCRIPT=fly aicheck` are unchanged (that script does not use mode 21).
- **The model pose in flight (FUN_004c8dfc, the integrator's last step), now ported:** with the card's roll byte
  (+0x4C; Ed's card 1) the SuperObject's rotation columns = (frame y, frame z, frame x): model z (the head) along the
  velocity, model y (Ed's back; his front is -Y) along the rolled frame's up, i.e. a head-first, belly-down glide
  that banks with the turn. Without the roll byte the same from the frame levelled (its z turned onto world up,
  FUN_004b7a22; not used by Ed). Ed's flight start B26 agrees: it sets DynAxis0 (the sight axis, dynam +0xF8, read by
  GAM_00431ef0 for the AI / camera) to +Z, from (0, -1, 0) on foot. The DNM 3x3 helpers are row-major with vectors as
  columns (FUN_004a8aed set, FUN_004a8a74 get; asm). tc_ed_ai_model_matrix draws it (the viewer uses it); aicheck's
  fly script prints the check: head within 1.6 / 0.8 / 0.4 deg of the velocity at 30 / 60 / 144 fps in the air, axes
  an exact rotation. **Needs a look in the app**: if the flight animations were authored lying down, this would stand
  Ed upright instead (the exe's code says they are not).
- Still not done: the flight collision body (the exe sweeps the state's ZDM spheres in this tilted pose, card NULL,
  flags 0x10; the port uses tc_player's upright capsule), FUN_004c3dc3 after a hit (the post-collision velocity when
  before . after > DAT_004d3070, else the velocity from before the collision), the camera in flight.

### Swimming and water, decoded with types (2026-10-24, typing step 3)
- **Water = a second mechanical environment below a sector's water surfaces.** Sector data +4 lists the sector's
  environments, +0x10 its water surfaces (session 3's "surface list"). FUN_00451b90 (the env lookup, also inside the
  swim forces) walks the surfaces in order while the point is below them: FUN_0045d880(point, surface) = surface
  +0x5C matrix's z - point z (a horizontal plane at the surface height, unbounded within the sectors that list it),
  > 0 = below; the env is the sector's environment number (surfaces passed), the last depth goes to dynamics +0x2E0
  (= the scripts' EnvHeight, FUNC 51; EnvHeightAbove FUNC 52 = it + own z). Values: the fix's air 0x36b51c {g 9.81, up
  (0, 0, 1), +0x10 drag coefficient 1, +0x14 density 1.3} in every level; water (sud0 0x36b60c, Land 0x36b714, ...)
  the same with density 1000 and +0x28 = 1. sud0: 21 sectors with a surface (river -5.2, falls foot -77.53, top pool
  5.25, 0.32 / 0.38).
- **Correction: air density is 1.3, not 1** (the notes counted the words wrong): the flight lift and drag are 1.3 x the
  port's until now. Fixed (tc_mec_env_default); `AI_SCRIPT=fly aicheck`: the glide sinks slower (vz -4.3 instead of
  -4.7 u/s at 2 s) and lands 0.05 s later.
- **Swimming = ID card type 1** (Ed's states 6, 7, 8, 16, 17, 20-24, 36, 37, 151, 152, 211-214; masses 900-1100
  against water's 1000: buoyancy decides). Card (0x48): +4 mass, +0x10 / +0x14 / +0x18 lags (polar angle, heading,
  thrust), +0x28 body size s (1), +0x2C volume factor c (1), +0x34 depth d over which the volume ramps (10 / 3), +0x38
  polar-angle decay in mode 0 (0.99), +0x3C / +0x40 polar angle max / min (deg; main card 160 / 1), +0x44 polar offset
  (deg). FUN_004c9a10 is the type 1 cast (returns its argument; only the type 1 code calls it).
- **Request handler DNM_RequestType1_004af8f0** (handler mode 7): the control slots hold a direction in spherical
  angles: "attack" = polar angle from +Z in degrees (0 up, 90 level, 180 down), "bank" = heading in degrees. Mode 0:
  polar x card +0x38; 1 / 2: polar +/- proposal [0x18]; 3 / 4: heading +/- [0xB]; 10: thrust += proposal [0]; 0x14:
  polar and heading from proposal [7..9] (normalised; acos(z), the xy angle), thrust += proposal [0]. Clamp polar to
  [card +0x40, +0x3C]; lag polar (+ the offset) / heading / thrust (FUN_004c9afb, card +0x10 / +0x14 / +0x18);
  thrust := 0 when its lagged value passes 0.7 x proposal [0]. Frame: dyn->rotation = Rz(heading_lagged + pi / 2)
  Rx(polar_lagged) (FUN_004b6d50). Force: thrust_lagged x (cos h sin p, sin h sin p, cos p).
- **Forces FUN_004c4af0** (from the integrator DNM_IntegrateType1_004abbb8): the env lookup at (snapshot x, y,
  position z - 0.5 s) gives the environment and depth D (dynamics +0x2E0 := D), D' = D - 0.5 s. Weight -m g; buoyancy
  +rho g V with V = 0 for D' <= -0.5 s, (0.5 s + D') s^2 for |D'| < 0.5 s, then s^3 rising linearly to s^3 c at D' =
  card +0x34 and s^3 c beyond; drag -rho Cd s^2 min(|v|, 10) v while D' >= -0.5 s (Cd = env +0x10). Then v += sum F /
  m dt (with the thrust), the position, the orientation from dyn->rotation, collide-and-move with no card and no env
  (flags 0x10); after a hit the angles are re-read from the new frame (FUN_004a98a0) and written back to the control
  slots; FUN_004c91d2 writes the pose (not read yet).
- So with s = c = 1 Ed floats with net (1000 - m) g / m: the 990 card rises at ~0.1 m/s^2 from depth, settles where
  V = m / rho (about 0.49 above the probe), i.e. bobbing at the surface; the 900 cards lift him out faster, the 1100
  one (state 151) sinks.
- **The scripts' "in water" is the water zones, not the depth.** GAM_UpdateWaterZones_00437900 (from
  MEC_CombineZones_004c0fb5, at the end of every integrator) stores the environment's water flag (MecEnv +0x28) at the
  origin (zone kind 0, Perso3dData +0x1A8) and at each **sample point of the playing state** (State +0x34: entries
  {.., +0xC point in the perso's axes, +0x18 kind 1-6, +0x1C value}; Ed standing: z 2.0 / 1.7 / 0.8 / 0.55 / 0.35 /
  0.1, swimming 1.4 .. -0.2, flying along the tilted body); dynamics +0x2EC = the value kinds 1..6 share, 3 when they
  differ, kind 0's without points. COND 70 InWater = combined 1, or 3 with point 3 or 4 in water; 71 = in water with
  point 1 (the head) out; 72 = in water with point 1 in; 73 ZoneTest = not in water, or point 2 out (without one: not
  72); 74 NotInAnyZone = no kind in water; 69 = falling (card type 4 / 10, no contacts, velocity z <= 0). Ed's water
  reflex R3 / R14 and behaviours B2-B10 run on these. EnvHeightAbove only places the splash perso.
- **Card type 7 (surface swimming 5 / 18 / 19, also the portal walk-in 228, the skier's finish 321): kinematic.**
  Request handler DNM_RequestType7_004b17d1 = the walk handler (card +0xC / +0x14 speed range, +0x10 / +0x18 turn
  range); the integrator sub-steps (5 ms with dynamics flag 0x20000) DNM_Type7Forces_004c831a: with the card's gravity
  factor (+0x1C) 0, as on all of Ed's type 7 cards, the velocity is kept and the request's motion added (constant
  height); dynamics +0x70 >= 0 (no move request) pulls towards a plane (+0x74 normal, +0x8C stiffness, +0x90 offset);
  other factors use a height spring (FUN_004c32f6, card +0x20 / +0x24: 630 / 981 on Ed's cards, not read).
- **Ported (2026-10-24):** .ttai **v8** appends the water planes (ai.water_planes: z, the environment below, the
  bounding boxes of the sectors listing the surface) and the per-state water sample points (ai.water_points); all 21
  game levels regenerated (the glTF test levels stay v7). Core: tc_mec_env_at (the plane among those whose boxes hold
  the point that is nearest in height: stands in for the engine's sector choice where boxes overlap, e.g. sud0's top
  pool above the falls' foot), tc_mec_swim_* (type 1: handler, forces, pose), the kinematic type 7 path in tc_ed_ai,
  the zones before every AI step (tc_ed_ai water_zones), EnvHeight / EnvHeightAbove (FUNC 51 / 52), COND 69-74; the
  swim and flight poses draw head first. Air density 1.3 everywhere (was 1).
- **Checks:** `make mechcheck` (no data): the 990 card floats up and bobs at the surface (z -0.60 .. -0.44), 1100 sinks
  at the drag's terminal 0.99 u/s, a thrust along +x swims there head first; the same at 30 and 144 fps.
  `AI_START=-8.6,24.6,-6 AI_SCRIPT=swim aicheck files/out/ttlv sud0` (the river, 6 deep): Ed drops in, his water
  reflex takes over (B4 / R14, state 7, type 1), he comes up and treads water at the surface; Up: state 5 (type 7),
  swims at ~4 u/s along the surface, stops at the bank, turns with Left; Jump leaps out and back in. wander: bar /
  Totalski unchanged, Land / north within noise, sud0 different (Ed now enters the river).
- **Not done / approximations:** the type 1 post-collision angle re-read; type 7's carried velocity (dropped) and plane
  spring; the water flag of every exported plane is taken as 1 (true for all seen); Land's LAND_MORT liquid is also a
  water environment (+0x2C 255.0 there: meaning unknown, nothing in the AI reads it); diving / surfacing transitions
  are whatever Ed's scripts do with the zones (checked only by the aicheck run above); the swim collision body (the
  exe sweeps no card, flags 0x10 / 4; the port uses tc_player's capsule).

### Water at a pool's floor (2026-10-29, Linux session; BUGS.md "Water Jump Buoyancy")
- **Symptom (cavedoc, the pool in front of Ed's arrival from sud0: surface -48.25, floor -51.14, box (-25.4, -136.7,
  -51.1)-(18.0, -96.2, -38.5)):** a running jump in reached the bottom, Ed then "surface swam" (B2, state 5) on the
  floor, walked along it (B7 / B9, ground) and jumped off it (state 43).
- **Cause: the port's sector boxes, not the physics.** The exe looks the water up in the perso's sector (the
  integrators get the sector), so a sector's water does not end at its floor. The port stands in for sectors with
  their bounding boxes, which stop at the geometry (cavedoc's 0.04 above the collision floor), and the swim forces
  probe 0.5 below Ed's origin (FUN_004c4af0). Within 0.5 of the floor the probe left the box, so he had no buoyancy,
  no drag and depth 0, which his scripts read as the surface. **Fix:** `box_holds` (tc_mec.c) counts points up to one
  body size (1.0) below a box's bottom as inside it, for water and dry boxes alike.
- **The deep plunge is the original's.** The air card has no water forces (DNM_WalkForces: weight only); R25 hands
  over to the water reflexes only when ZoneTest fails, i.e. with sample point 2 (1.7 above the origin) under; the card
  change keeps the velocity (DNM_CardTypeInit_004a7459 converts it between a carrier's frame and the world, nothing
  else). So Ed enters the swim card ~2 below the surface at ~12 u/s and the drag stops him ~0.7 further down.
- Check: `AI_SCRIPT=hang AI_START=-19.94,-136.57,-47.63,172.5 aicheck files/out/ttlv cavedoc` (run and jump into the
  pool): state 5 at z -48.19 (was -51.14, then on the floor). wander: 20 levels identical, cavedoc 0 stalls (was 1).

### Ghost water (2026-10-29, Linux session; BUGS.md "Ghost Water Bodies")
- **Cause:** the port bounds each water plane by the boxes of the sectors listing it (the exe asks the perso's sector,
  which the port lacks), so dry places inside or under those boxes counted as water. Found with a scan of every level's
  walkable floor (wet by the lookup, no drawn water straight above). The exe's water really is an unbounded plane
  within a sector: FUN_00451b90 walks the sector's surfaces, FUN_0045d880 is a pure height difference.
- **Fix 1 (core, user's observation: the ghosts lie under actual water):** `tc_mec_env_at` takes the collision; a
  point with a ceiling (a face turned down) straight above it, below the surface, is dry. sud0: the cave 16 under the
  river, the cave under the well, the frozen pond (ice slab over the water, so Ed walks on it); pyramide's lower
  chambers.
- **Fix 2 (exporter, ai.dry_boxes):** a dry sector wholly inside a water box stays dry unless a water surface's
  triangle overlaps it seen from above (the old rule kept every such sector wet for the rocks in the river). sud0: the
  tunnel under Doc's house (sctsud17, inside the river's sector sctsud22b; its ceiling, -3.3, is above the river's
  -5.2, so fix 1 cannot see it). Needs a re-export.
- **Result** (sud0 / pyramide / skymap ghost floor samples): 3519 -> 854, 793 -> 335, 449 -> 61; every visible water
  triangle keeps its water except the frozen pond's; wander identical on all water levels but cavedoc (the pool-floor
  fix). **Left:** the pit 100 below sud0's falls and under the well shaft (open to the water above); marmite's sctV01a
  (the sector lists the surface over all of it); BigBoss. cock01's water depends on gameplay (Ed changes its level).
- A drawn-water footprint test (wet only under drawn water) was tried and dropped: it dried large parts of Land.

### ZDx zones, read with types (2026-10-24, typing step 3; COND_13 for portals ported, see the last bullet)
- **List numbering** (GAM_ZoneList_00431520): 0 = ZDD (collide set +0), 1 = ZDM (+8), 2 = ZDE (+4), 3 = ZDR (+0xC);
  ai.py's order of the pointers is right, the exe's index differs. GAM_ActiveZones_004316d0(list, collset) = the list's
  activation entry for the playing state; its zone index entries (+0xC) pick zones (GAM_ZoneGeo_0041f5a0 -> the zone
  entry's GeoCollide). The skier's CollectNearby(list, 6, mask) tests his **ZDE** zone 6 (not ZDM).
- **COND 13 / 14 / 15 (the zone tests: ski holes SYM_GenTrou, GenTransitionSubMap teleports, ...):** handler
  COND_13Etc_00497d70 (13..17 by the node's own index). 13 (a, zone k, b): b's origin inside zone k (1-based) of a's
  **ZDD** list (GAM_ZoneHolds_00420280 mode 0); 14: the same with a zone carried by one of a's channel objects (p3d
  +0x164 list, kinds 0xB / 0x14); 15 (a, b): inside any active ZDD zone of a or of its channel objects
  (GAM_AnyZoneHolds_004202c0); 16 / 17: AI_00490250 (not read). The test (GAM_ZoneContains_0041f210): b's origin
  through the inverse of the zone's matrix (a's world matrix), then GEO_PointInZone_00462f60: triangle zones by
  counting crossings of a ray along +z (odd = inside, FUN_00468070), sphere zones (type 7) and boxes directly.
- **Ported for portals (2026-10-24): .ttai v9.** ai.portal_zones: a portal whose perso's scripts test the player with
  COND_13(Me, k, player) carries its ZDD zone k (spheres / boxes in the perso's axes; triangle zones are not exported)
  and the inverse of its world matrix; tc_ed_ai's portal_inside() captures (and re-arms) on "the player's origin inside
  the zone" instead of the radius, which stays the fallback. Totalski: 13 of 22 portals (the 11 holes, the 2 finish
  arches); Land / sud0 / north: none, unchanged. The holes' real zones are boxes up to 290 x 130, from below the opening
  to as much as 23 above it (7 of the 11 hold their own origin), not the radius-8 stand-in; the hole script also sets
  the skier's dsg31 := its dsg1 (a second point beside the respawn spot; now an op) and dsg32 := its dsg2 (a perso: not
  expressible as an op). `make portalcheck`: every hole captures at its box centre and not 5 above it.
  Not ported: COND 14 / 15 (zones on channel objects, any active zone), moving zone persos (posed at frame 0),
  GenTransitionSubMap (its scripts are still skipped by portals()).

### Footsteps by floor material (2026-10-24, typing step 3): decoded from the data, ported
- **The mapping: step sound id = 0x390002 + the floor's sound type** (GameMaterial +8; -1 = none). The footstep switch
  is Bnk_0 event 49 (id 0x390000, resource 46 of type 8 whose record holds 0x18 = 24: the switch's entries); the step
  resources 0x390003..0x390019 are 23 sounds (EsEarthS / H, EsGrassS / H, EsLeafS / H, EsSandS / H, EsStoneS / H,
  EsTileS / H, EsWaterS / H, EsWoodS / G, EsGrate, EsMetal, EsMud, EsSnow, EsIce, EsFire, EsCarpet) plus the generic
  Edpasgen (0x39001C). No constant of them is in the code (the switch is data); the offset was settled against the
  textures of the floors carrying each value: Totalski 20 on its NG_* snow = EsSnow and 3 on NG_HERB grass = EsGrassS,
  north 23 on its TAPIS carpets = EsCarpet, bar 21 on COKICE ice = EsIce and 19 on orange pulp = EsMud, north 10 on
  flagstones = EsStoneH. The S / H variants are separate values (the material picks one). Ed's walk / run animations
  queue event 49 at each foot contact (right after the type 3 events 14 / 15). Where the engine reads the switch value
  (the ground contact's material, presumably) is not located; the port uses the floor triangle under the origin.
- **Port:** level.py collision(sounds=True) (each triangle's GameMaterial +8); **.ttai v10** appends it per collision
  triangle (the .ttlv's order; tc_tri.source keeps each triangle's input index, since degenerate ones are dropped);
  tc_ed_ai footsteps(): a queued event 49 becomes TC_AI_STEP_SOUND (256) + the floor's type 1..23, else stays the
  generic step; sfx.py extracts the 23 steps and writes events.tsv rows 257..279 (the frontends need no change: they
  play by row). `AI_SCRIPT=wander aicheck`: Land 153 steps on type 1 (EsEarthS) ..., north on 2 / 4 / 9, sud0 1 / 3 / 8;
  bar's main floor has no type (generic, as in the game presumably).

### The camera, first look (2026-10-24; next)
- The camera is the fix perso StdCam (perso-in-fix 1): 35 behaviours + 25 reflexes, 3555 primitive uses of 86
  primitives, 37 of them unnamed (mostly camera procedures PROC 156-199, e.g. PROC 184 x 187, 173 x 114, OP 24, FUNC
  58 / 81 / 76); its 2 states use **card type 3** = the camera mechanics: DNM_IntegrateType3_004b2900 (only when a
  request set a handler mode) -> FUN_004b3d33(.., dyn, work, so, &work->unk_50, .., &work->wanted, dt) then
  FUN_004b533c(dyn, so, .., proposal [0x11] + 4, work, dt); request handler DNM_RequestType3_004b3810 (140 lines).
  Ed reads and writes its dsgvars all the time (NOTES "AI interpreter": Dot(camera, dsg20) gates the idle; the arches
  drive dsg44 / 89-93 / 39).
- **Plan:** (1) name the camera primitives from typed/ai/ (their typed decompiles exist); (2) read FUN_004b3d33 /
  004b533c with types (target, distance, angles, collision) into a tc_mec_camera; (3) let the core run a second perso's
  AI (tc_ai already holds the camera's dsgvars as an "other"): the camera's scripts + type 3 mechanics each frame,
  the frontends taking the camera matrix from it; (4) compare with the viewer's current follow camera.

### Swimming refined (2026-10-24): diving fixed, the water sheen
- **Diving was broken by a missing primitive.** Ed's underwater reflex R8 leaves the water (-> B5, the jump) when
  `VecZ(FUNC_102((0, 1, -1))) > EnvHeightAbove + 0.55`; FUNC 102 / 103 (FUNC_VecLengthEtc_0049ab10 cases 0x66 / 0x67)
  are point LocalToGlobal / GlobalToLocal through the perso's **world matrix** (FUN_0047b2f0, with translation), which
  the port lacked: the result's z of 0 threw Ed out of the water the frame he dived. Ported with the drawn axes
  (tc_ai_perso.has_axes / axes, host-fed from tc_ed_ai_model_matrix: tilted while swimming / flying).
  `AI_START=-8.6,24.6,-6 AI_SCRIPT=swim aicheck files/out/ttlv sud0`: Action + Up dives (B7 / R19, state 24, then B9 /
  R8 state 6), Ed swims down to the bed (-11.3) and along it at ~3 u/s; idle he floats back up, upright.
- **Water surfaces** (user: they should have a sheen over the texture). Their material (sud0's rainbow
  O_ZARBI_AUNTXY): specular 0.9 (the shine), UV scrolling, ambient 0.2; the words after the surface's two matrices
  are 0 (no wave parameters). The .ttlv groups static geometry by material, so tc_viewer marks a mesh a water material
  (tc_viewer.mesh_water, TC_FLAG_WATER on its draws) when one of its triangles lies flat at a .ttai v8 water plane's
  height inside its sector boxes (sud0: 2); Shaders.metal / sdl level.frag water_sheen() add, on upward faces only (the
  falls share the material), a Fresnel sky reflection and the light's highlight over moving procedural ripples, and
  raise the alpha of blended water. The look is a stand-in for the engine's (its water rendering, the SRF module, is
  not decoded); **not seen yet** (the Metal shader compiles; the SDL GLSL builds on Linux).
  Fixed the same day (user: from underwater the whole surface was one solid colour): seen from below, the view vector
  points down, so the Fresnel term was 1 (full sky) and the alpha forced opaque; now no sheen from below (eye under the
  fragment), the texture's own alpha, the world above showing through.

### Dry sectors inside water boxes (.ttai v11, 2026-10-24)
- User: sud0's way into Doc's cave counted as water (Ed swam in the air). Cause: water_planes bounds a surface by the
  geometry boxes of the sectors listing it; the river's box [-169..-87, -18..75, -23..16] (surface z -5.2) overlaps the
  cave entrance's own sector [-181..-134, 11..39, -16..-4], which lists no surface. The exe asks the perso's sector.
- Port: v11 appends the boxes of sectors without surfaces that are not wholly inside a water box (ai.dry_boxes);
  tc_mec_env_at: a dry box holding the point and smaller than every water box holding it -> no water. A first version
  (any dry sector, smallest box decides the plane too) lost 11 of sud0's 63 visible water triangles (sector 9, inside
  the river box) and one in Land: rejected. Checked: every visible water triangle keeps water below it (sud0 63/63,
  cock01 4/4, cavedoc 4/4; Land 2/3 as before), the cave points dry, aicheck swim / wander on sud0 / Land / cock01
  identical to v10. Proper fix: the exe's sector choice (who sets SectorInfo +4), not read yet.

### Sliding stages: wrong voices, the wall stumble (2026-10-24, open; start here next)
- **The user, from the original:** at the speed-up arrow Ed goes "YIPEEE!"; at the jump (spring star) "WOAH!"; after
  falling into a hole, at the respawn, a gasp. Riding into a wall fast: he stumbles (crash animation) and keeps
  going, all through Totalski and skymap; skymap's pillars and fire kill him (not a stumble).
- **Sound references are patched at load (the cause of the wrong voices).** Every level has a `<level>.snd` (sna.decrypt;
  the notes' "levels have no .snd" was wrong): u32 pairs {event, snapshot address of the reference}. The engine
  writes the event into the reference, so the pointer values in the snapshot (0x9bdfe0c + 0x20 x index, what
  sfx.event_index reads) are stale outside the fix. Values < 0x100 = Bnk_0 indices; larger = event ids in other banks.
  TotalSki.snd: 25 entries; the skier's references (level block 0xd3b0d0c..: 0xd3dd194, 0xd3defb4, 0xd3e514c,
  0xd3fefbc, 0xd3fefd0) are ids 0x350000 / 0x350000 / 0x330000 / 0x330000 / 0x340000, i.e. not Bnk_0's 65 ed15a.wav
  that the port plays on the spring. 0x330000 is Bnk_68's only event; 0x340000 / 0x350000 / 0x450000 were not found by
  id in any bank: the level's bank list is probably TotalSki.bls (4 bytes, encrypted?: not read). vEdyoupi.wav
  ("youpi" = the YIPEEE) is in Bnk_0 and Bnk_66. **Fix:** resolve T34 nodes and animation sound events through the
  level's .snd by the reference's address, extract the referenced level banks (sfx.py), number their events after
  Bnk_0's in events.tsv; check against the three cues above. Applies to every level (all have a .snd).
- **The wall stumble is not found statically.** Scripts: the skier's crash B9 (state 274, a hit point, then riding) has
  no wall / speed-loss trigger (only obstacle zones, trees via PROC_237, dsg25 written by NBD_PRESSZTOSTART); the ski
  mechanics change no state; card +0x18 / +0x1C (6000 / 1000 on the riding card) are read by none of the decoded ski
  code. **Next step: observe the original.** GAM_PlayAnim_00415c00 prints every state change with OutputDebugStringA
  ("change state trame", "Next state trame") when the byte at 0x4EC6AC is non-zero: patch it (or poke it at run time)
  and run under Wine with WINEDEBUG=+debugstr, ride into a wall, read which state plays; then trace its trigger.
- **2026-10-28: all three stopgaps below are removed** (user: he slides on anything in the original; the exe's
  collision, docs/DECOMP.md, no longer pins him). Kept for the history.
- **Stopgap in the port (2026-10-24, not from the exe):** tc_ed_ai ski_unstick(): a wall hit from above 8 u/s that
  leaves under 30 % of the speed keeps half of it along the wall (a dead-on hit: along the wall towards the drift) and
  puts the skier 5 cm off it, so he slides on instead of sticking. No animation (forcing B9 would cost a hit point).
  Replace it once the original's reaction is traced. Not exercised by the scripted aicheck runs (they never hit a wall).
- **Second stopgap (same day; user: stuck at a specific slope angle, e.g. Totalski's end ramp to the star platform):**
  without a card the collide classes are ground <= 45 deg, steep slope <= 60, wall beyond, while the ski forces only
  call a face steep beyond 70: a 60-70 deg face was a wall (the wall response's x/y damping f(2 vx^2, 36) ate the speed
  each contact) yet not steep enough for the forces to slide him: pinned. tc_mec_walk.slope_cos (set to cos 70 by the
  skier's ski_move) makes such faces slopes (no softening without a card). Totalski's end area: 28 faces at 60-70 deg,
  all snow (grip 0: friction is not the cause). skymap's 60 s wander now travels 1847 instead of 756. Whether the exe
  classifies them so for the skier is not checked (its sphere path passes no card either: defaults); revisit with the
  trace of the original.
- **Third stopgap, reworked (same day; user: still trapped at sharp angles, e.g. the house exit near the start):**
  ski_unpin() is now a no-progress detector (under 0.75 u in 0.4 s with a wall touched within 0.5 s, not on flat ground):
  corners flicker between wall and no contact, which reset the first version's timer, and a board still facing the
  corner rode straight back in. The rescue turns the board to the most open of 16 directions (rays at 0.2 / 0.6 / 1.4)
  and sends him off at 7 u/s; repeated rescues within 2 u escalate (moved up to 2 u out; the third lifts 1.2 first).
  Measured with a scratch trap finder (random snow drops, random headings, 5 s of random steering; stuck = under 2 u in
  the last 1.5 s): Totalski wall traps 90 -> 15 of 798 rides, skymap 2 -> 0 of 53; the scripted Totalski wander now
  travels 1001 instead of 702. Left: closed pockets (e.g. a 4 x 5 u walled room at (121, -315, 123) beside the start, all
  static geometry), stops on the flat finish (not traps). Next pass on the sliding mechanics: start from here.
- **Footsteps (same day, user):** in the original Ed's own step (event 49) plays with the floor's step sound on top, so
  footsteps() now adds TC_AI_STEP_SOUND + v next to event 49 instead of replacing it.
- Also found: FUNC 104 Report234 = dynamics +0x2E4 = the environment's +0x2C: 255 = a deadly environment (Land's
  LAND_MORT liquid): the skier loses a life (state 206), Ed underwater goes to B61.

### Hanging from the hang spheres (.ttai v12, 2026-10-27; built, aicheck only, not yet run by the user)
**Superseded 2026-10-28 (docs/DECOMP.md "Ledge hang, found and ported"): wrong trigger.** GroundMaterialIs tests Ed's
*own* part's material (record +0x70), not the floor's: the hang is his zone 9 hand point (flag 8) landing on a ledge
top. The spheres' obstacle id 0x8 is read by nothing; the CLID chunk and the per-triangle ids are gone. Kept for the
B82 / R10 script notes below.
- **Trigger:** reflexes R0 / R25 / R26 `GroundMaterialIs(0x8)` -> B19 (velocity 0, state 46) -> **B82 + R10** (the hang).
  B82: stick sideways -> states 48 / 49 (shimmy, Proc10 rate), stick back (> 140 deg) -> state 44 + B90 (drop), stick
  forward -> FUNC 162 probe from (0, 0, 4.4) along (0, -1, 0), clear beyond 2.25 -> **B43** (climb up, states 153/154).
  R10 lets go (state 44, B90) once GroundMaterialIs(0x8) has failed for 200 ms.
- **Material test** FUN_00497890 (hand-disassembled, 64 bytes): GameMaterial valid -> its collide material (+0xC, -1 =
  none) -> u16 at +2 (the identifier) & mask. COND 44 / 45 / 46 / 47 (FUN_004985e0) read the ground (+0x8C) or wall
  (+0x108) contact's material when the report has that contact. Identifier 0x8 is only on **type 7 collide spheres**
  (r ~1.7, 80 faces once triangulated): sud0 1 (under the rock overhang at (-54.7, 96.8, -2.4): Ed's head meets the
  overhang before any of him reaches the sphere's top, so it does not trigger here), Land 5, cavedoc 3. Other ids seen:
  0x400, 0x480 (sud0 floors), 0x1000 (Land).
- **Port:** level.collide_id -> .ttai v12 (per collision triangle u16, after the v11 dry boxes); tc_mec_walk records
  the contacts' triangles (ground_tri / wall_tri); tc_ai_perso.ground_material / wall_material; COND 44-47.
  Card type 11 (states 46-49, 197, 238-245; FUN_004adb4d / FUN_004b2793): root motion (or mode 12 along the contact
  plane), collide-and-move, no grip; FUN_004c6993's forces are not decoded, a 0.1 ground re-snap stands in so the
  sphere stays a ground contact every frame. FUNC 162 (FUN_0049c690, not decoded): ray in the perso's axes, unbounded
  (B82 compares a unit direction with 2.25), hit point in those axes. FUNC 13 = vector length (as used).
- `AI_SCRIPT=hang AI_START=38.5,29.4,-19.4,90 aicheck files/out/ttlv Land` (AI_START takes an optional heading in
  degrees): grabs at 0.28 s (B82, states 197/47 hold steadily), climb and shimmy run.
- **2026-10-28:** found: the hand point (above). An "edge riding" candidate from the first pass was the port's error
  (grip 1 on the body spheres) and is gone.
- **Not found: the generic hop onto a ledge just above Ed** (user: he shimmies over any surface a bit higher than him).
  Not in Ed's scripts (no air behaviour or reflex reacts to a wall that way; R6's AgainstWall -> state 134 is the
  climb out of water; R25's dsg64 only scales air steering); the exe's step-up (MEC_004bbc4c -> MEC_004bb06e) needs
  last frame's report & 3 (grounded) and the air card's request clears it. Candidates: the type 4 integrator path
  around the step-up, or a ZDM sphere's own_zone_type (hit->own_zone_type == 1 gates the step-up).
