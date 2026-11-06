## Special Edition compatibility

// Response after asking Claude about similarities and differences between retail and SE. The goal is to get all SE levels working, within Retail Edition physics and parameters. Although we might end up having to incorporate dodgy SE physics. They were using the tank controls from Rayman 2 beta and Shenmue basically. Then Ocarina of Time came out and changed everything. Anyway... modify this as you see fit, Claude. Since you wanted to make this.

Mostly similarities. The SE is recognisably the same engine about a year earlier: the same formats, with a handful of version differences. That's why combining them looks feasible.

What's the same
- The snapshot block layout, relocation tables and global pointer table (.gpt) layout.
- The world structures: sectors, static objects (IPOs), geometry, materials, collision and baked lighting.
- The texture archive format (.cnt) and the RLE scheme inside its .gf textures.
- The character structures: 3D data, object lists, families and states.
- The AI script tree format (the same node layout).
- The same 11 level names: carota, cavedoc, cock01, land, lecanyon, marmite, north, outro, pyramide, sud0, totalski.

Once three small loader changes were in, all 11 SE levels walked with the existing parser: SE Totalski with 0 unresolved pointers, the others with 12 or fewer.

What's different
- Files: nothing is encrypted, snapshots have no header string, and each block lacks retail's extra byte. Level .gpt/.ptx files have their own relocation tables, which retail lacks. Level scripts are plain text (.dsc) instead of compiled .dsb.
- Textures: an older .gf header, 24/32-bit BGR instead of retail's 16-bit formats. All 3,159 decode.
- Animations: a completely different format. Instead of retail's compressed A3D, each frame stores its channels directly, with small compressed matrices and an occasional parent hierarchy. That's decoded now, and SE characters stand and animate in place.
- Sectors: one extra dword at the start shifts most fields by 4 bytes. Until I found that, every sector list read empty and the skier fell through the slope.
- Floors: SE snow has grip 0.001 and rebound 0, where retail's has grip 0 and rebound 0.8.
- AI: the primitives are numbered differently, so retail's opcode table doesn't apply. The SE exe does still contain the primitive names (Cond_PressedBut, Func_DistanceToPerso, ...), which retail stripped. That's a real lead for running SE scripts later, and maybe for naming retail's.
- The levels themselves: genuinely different maps under the same names. SE Land is small (3.3k triangles); SE Totalski is a heavier course (13k against retail's 4k) with no separate skier character. SE Ed is an earlier design, with a bow tie and floating hands and feet.

Where it stands, all headless (nothing seen in the app)
SE Totalski exports with your current Ed: the retail skier and his AI, on SE's own slope. These all pass:
- playcheck: Ed lands at the start.
- skicheck: 1.9% of 570 random rides stop, where retail stops 0.3% (the limit is 2%).
- Skier AI from the real start: pushes off and rides down through three sectors at up to 66 u/s.

Getting there needed workarounds: SE snow mapped to retail values, and a minimal cast entry so the ski camera engages. Of the other levels, only Land has been run through the full SE export and playcheck so far; it passes (Ed lands at the start). None of the remaining nine has gone through the core's checks yet.

// A first playthrough of an earlier Tonic Trouble is a lovely thing to aim for.

---

# Reference

Code: `tools/ttre/se.py` (disc, donor, glue), `sna.py` (`is_se`), `gf.py` (`decode_old`), `level.py` (`SeAnim`,
`read_se_anim`, `sectors` shift). Conversion: `tools/convert_files.sh` step 4b. Outputs: `files/out/se/{ttlv,Textures,
Vignette}`. Frontends: `LevelPicker.swift`, `sdl/src/picker.c`.

## The disc

- `Tt.bin`: raw MODE1/2352, one data track, plain ISO 9660 (`se.Disc` reads it, or a cooked `.iso`). Unlike the
  retail disc's tt1-4 archives, the SE disc stores its game files plain: `GAMEDATA/` (levels, `TEXTURES.CNT`,
  `VIGNETTE.CNT`, sound banks), the game exe `MAID3DVR.EXE` (1.5 MB), `VIDEOS/EDPROL.AVI`, DirectX/Indeo installers.
- `se.extract` copies `GAMEDATA/` and the exe to `files/sources/se`. `se.find_disc` finds the image under
  `files/sources` by its `GAMEDATA/GAME.DSC`.
- Levels ship as text scripts (`*.DSC`, "Scripts Parser Version 2.0.1, (c) Ubi Simulations 1997"). Retail ships them
  compiled (`game.dsb`).
- **The exe still has its AI primitive names** (`Cond_PressedBut`, `Func_GetPersoAbsolutePosition`, 187 `Proc_`, 88
  `Func_`, 92 `Cond_`...). Retail stripped them. This is the key to mapping SE scripts later, and the names may also
  help name retail primitives where the handlers match.

## Formats: retail vs SE (verified on all 11 levels)

| | Retail | SE |
|---|---|---|
| .sna/.rtb/.gpt/.ptx/.rt* | XOR-encrypted (rolling key) | plain |
| .sna header | u8 length + obfuscated path | none |
| .sna block header | base, 3 x u32, size, **u8 unk** | base, 3 x u32, size |
| level .gpt/.ptx relocation | none (resolved by address) | `.rtp` / `.rtt`, keyed (4, 1), like the fix ones |
| .gf texture header | u32 format (565/1555/4444/88), w, h, ch, rle | w, h, ch (3 BGR / 4 BGRA, 8 bit), rle |
| animation | A3D (compressed keyframes, AnimRef +0x28) | per-frame channel table (below) |
| sector record | portal +0x00 ... activity +0x64 | an extra dword at +0 (always 0): all of those +4, the water-surface list too; virtual +0x88 and name +0x94 unchanged |
| .cnt archives | same | same |

Relocation is clean: 0 unresolved pointers on lecanyon, north and totalski, and 12 on the other eight (not traced:
the same 12 everywhere, probably fix data). Everything else the converter walks has the same layout as retail:
GPT perso-in-fix, SuperObjects, IPOs, geometry, materials (but for their tail: "Moving textures"), radiosity, PTX, persos,
families, states, object lists.

**SE animation** (`level.SeAnim`): AnimRef = {ptr frames, u8 n_frames, u8 speed, u8 n_channels, ...}. A frame is 16
bytes {ptr channels, ptr f32 matrix, ptr f32 vector, ptr hierarchy}; the matrix and vector look like root motion and
are not read (see "Animation"). channels = ptr[n] -> {ptr compressed matrix, u32 info}, where the object-list index is the low byte
(0xFF = none) and the channel id is byte 1. Compressed matrix (`level.se_matrix`, POS_fn_vDecompressPosition 0x4808b0):
u16 type & 0xF (the high nibble repeats which parts are set; 0x80 and up: already decompressed), one of 1 position, 2
rotation, 3 both, 7 both + zoom, 0xB both + axial scales, 0xF both + a scale matrix. Position i16[3] / 512; rotation i16
w, x, y, z / 32767 (the engine's conjugate convention, as retail); zoom and axial scales **u16** / 256; the scale matrix
six **i16** / 256, symmetric: [s0 s1 s2; s1 s3 s4; s2 s4 s5] (POS_fn_vDecompressScale), the matrix being T R S
(POS_fn_vSetScaleMatrix). Hierarchy = {u32 n, ptr {u16 child, u16 parent}[n]}, channel indices as in retail TT (always valid as indices,
only sometimes as ids). Checks: props rest exactly on z = 0 at /512, and SE Ed's frame 0 renders upright (head, bow
tie, floating hands and feet).

**SE floors.** Ski snow is mechanics material {1, 0, 0.001, 1, 1}: grip 0.001, rebound 0. Retail's is {1, 0.8, 0, 1,
1}. SE cock01 ice is {0.6, 0.82, 0.7, 1, 0.5}.

## Playing them: the retail Ed (port choice, workarounds)

- **Donor:** each SE level borrows the player from the retail level of the same name: its `.ttlv` EDPL (model, 305
  clips, textures, LITE records) and its `.ttai`. On Totalski that player is the skier. Ed is placed at the SE level's
  own start (its GPT perso-in-fix Ed matrix). Output files take the donor's spelling (`Totalski.ttlv`), so the
  core's name-keyed behaviour applies as on retail: the ski auto-start, Ed's level-name checks (COND 62), and the
  music table.
- **The donor .ttai** has its level-bound chunks emptied (PORT, TRIG, PLAC, ZONE: retail coordinates), and the
  TRAN texture paths re-rooted. Its FSND doesn't match the SE collision, so the core drops it (LINK): no per-floor
  footstep sounds.
- **Ski snow:** with the skier as donor, SE floors under grip 0.01 get retail snow's (grip 0, rebound 0.8). The core
  switches a level to sliding when some floor has grip <= 0, and the ski physics was tuned on retail snow. On the
  other levels, grips are clamped to >= 0.001 so no walking level turns into a sliding one.
- **Minimal CAST** on Totalski (no cast persos): main actor and main alias in the donor's numbering, so the ski
  camera and the fix-Ed alias work as on retail.
- **SE persos** are props: posed and looping their initial animation (PRSO). None runs its AI (`cast.select` and
  `ai.light_persos` return nothing for SE), except on the outro since 2026-11-05 ("The outro" below).
- **Loading screens:** the disc's are named cock, ski, sud...; the converter copies them under the level names. The
  disc has none for outro (the exe picks them by level name, not the scripts), so retail's Outro.png stands in.
- **Not there:** level changes (no portals), SE sounds and music of its own (the retail audio folder is used, by
  level name), lightmaps, sprites checked only in passing, the SE intro/credits.

## Checks (headless, 2026-11-04, on the real files/out after tools/convert_files.sh)

- **Retail, no regressions:** playcheck, castcheck and sectorcheck all pass; skicheck Totalski is unchanged (0.3 %
  stopped, mean 24.1), skymap 1.4 %.
- **SE playcheck, 11 levels:** Ed lands at the start on all of them. cavedoc: he lands 1.2 below the start (no floor
  right under the placement), and the synthetic walk-into-the-nearest-wall test fails there because that "wall" is a
  steep slope (normal z 0.28) he slides 1 unit down. No tunnelling anywhere.
- **SE skicheck Totalski:** 570 random rides, 1.9 % stop after riding (limit 2 %; retail 0.3 %), mean speed 21.6
  (retail 24.1). The SE slope has more dead spots; skicheck lists them with positions.
- **Skier from the real start** (`AI_SCRIPT=ski aicheck files/out/se/ttlv Totalski`): pushes off (B2 -> B3), sectors
  01a -> 01b -> 02, up to 66 u/s, about 240 units of descent in the 11 s script, board on the snow.
- **Ed's AI on SE Land** (`aicheck files/out/se/ttlv Land`): walks, runs, turns, crosses into sctsector01, action and
  shoot states play. The same on cavedoc and sud0 at the start.
- **SE sectorcheck:** 9 of 11 pass. marmite and north have 49 and 51 lights in one sector, over the renderer's 48
  (TC_VIEW_MAX_LIGHTS): the viewer drops the extra 1-3 there.
- Before the sector shift was found, the skier fell through the slope at the first sector border (only his own sector
  was solid).
- **Not seen in the app:** everything here is headless. Lighting, fog, sky, the persos' looks and the pickers are
  unverified on screen (the Swift app builds; the SDL picker is syntax-checked only, no glslang here).

## Sectors (workaround, 2026-11-05)

Retail's sector record has the virtual flag at +0x88 (skies, foliage, gates: never a point's sector). In SE that
byte is the count of an extra list ({head, tail, n} at +0x80; retail has zeros there), so SE has no virtual flag where
retail keeps one. The dword at +0x90 looks like a **priority**: 3 = skies, Univers and backdrops (sud0's waterfalls
too), 4 = most sectors, 5-6 = doors, "virt" sectors and other small ones. Read as a flag, the count made most of sud0's
playable sectors virtual. Ed was then never found in them and fell back to Univers, which collides with nothing, so he
fell through the world (your playtest).

Port choice until SE's own lookup and collision code is read (`level.LevelData.sectors`):
- No SE sector is virtual: any sector with floor faces can be a point's sector.
- Every SE sector collides with every solid one. Solid = on some collide list, or not a backdrop (priority != 3). SE's
  own lists leave out sectors Ed stands on (Totalski's sctdepartvirt*, Land's sctvsector*).
- Measured, 150 random walkable floor points per level: the looked-up sector collides with that floor at 147-150 of
  150 on all 11 levels (sud0 was 47, with 100 finding no sector at all).
- A scratch probe walks Ed (his AI) from random floors for 4 s with random steering, and counts walks whose body
  centre passes down through a floor face. Old export: sud0 58 %, Land 21 %. Now: 0-0.7 % on every SE level (retail
  sud0 7.5 %, Land 4.7 %, cock01 1.3 %; not looked into). Walks ending far below with no floor (walking off an edge)
  are high on SE carota, cock01 and north (36-53 %): those maps have big open drops (retail north 49 %).
- Cost (it has since reached 3.4 %, 20 of 596 rides, on 2026-11-05: over the 2 % limit, not looked into): SE skicheck went from 1.9 % to 2.4 % stopped (over the 2 % limit), mean speed 21.6 -> 19.3: more of the course
  is solid now. The descent from the real start is unchanged, position for position.

## Converting with one game or both (2026-11-05)

`tools/convert_files.sh` detects each game in `files/sources`: retail (`GAMEDATA` complete; a partial copy is an
error), the Special Edition (its disc image, or `files/sources/se` from an earlier run). It converts whichever it
finds and fails only when there is neither. Special Edition alone: its levels have no Ed (no donor) and open in the
Tonic Viewer only. Their file names use the retail spellings anyway (`se.RETAIL_SPELLING`), and there's no music,
sound or intro. Checked in scratch sandboxes: SE-only converts in 88 s, and all 11 Ed-less levels load with valid draw
lists in viewcheck. viewcheck's demo now skips a level without Ed: it crashed in the test, not the core.

## Demo

Removed 2026-11-06, Ed's autopilot with it: the level select's Replays menu took its place (docs/RECORDING.md).

## Lighting (read and ported 2026-11-05)

SE levels now start in the game's ("Classic") lighting, as retail does. The two engines turned out closer than the first
survey said: **the vertex formula is the same**, only which lights reach what differs. Nothing was wrong with the
"Classic" formula on SE data. The exporter was dropping the SE's radiosity.

Read from `MAID3DVR.EXE`, which keeps its function names (`GLI_*`, `ISI_*`, `SCT_*`). Full decompile:
`files/out/_decomp_artifacts/se_all` (docs/EXE.md).

**The same as retail:**
- Per vertex: `byte = clamp(510 x (acc x diffuse + ambient + G x diffuse), 0, 255)` (FUN_00466530, FUN_00465380), with acc
  the radiosity / 256 (signed) plus the lights, G the ambient lights. Retail has 512 where the SE has 510.
- Material offsets (+4 ambient, +0x14 diffuse), the light's offsets up to +0xa0, the parallel and spherical terms, the fog.

**Different, and what the converter does about each:**

| | retail | SE | conversion |
|---|---|---|---|
| radiosity | IPO +8 -> ISI {A, B, C} -> {u32 n_lod, ptr lods}, 3 sets | IPO +4 -> ISI itself: {i16 n_lod, ptr lods}, lod {i16 n, ptr colours}, 1 set | `level.se_radiosity`, into the same vertex colours |
| light record | 0x100 bytes, paint +0xa6, alpha mode +0xa7 | 0xa4 bytes, neither | both 0 (they were read from the next record) |
| lights off (+0) | few | most: 129 of 144 on carota. They only served the radiosity tool | left out of the lists |
| sector light lists | one frame-wide list | dynamic (+0x38) and static (+0x2C), below | `level.se_lights`: `lights` and `char_lights` |
| per-object light test | spherical light culled by the object's centre | none (`GLI_vSendObjectToLights`: the call there is a stub returning 1) | no centre runs in LITE: each vertex is its own centre, which makes the test the per-vertex one |
| first element's material | each element's own | an object's vertex colours are computed once, through its first element's material (FUN_0045c2a0) | `Geometric.lit`: that material's coefficients, a mesh split where they differ |

**The SE's rule** (`SCT_fn_vSendSectorWhereIAmToViewport`, FUN_00499a70, FUN_00499db0):
1. The frame pushes the dynamic lights of the sector it is drawn from and of every sector on its list, for all of the
   frame. Carota's candles and Land's parallel lights are these. In the data as `tc_sector.lights`, the union written
   out, so the core's frame code works on them as it does on retail's.
2. Each drawn sector pushes its static lights only *after* its static geometry, for its water surfaces and characters.
   Static geometry has them in its radiosity already. That is draw flag 0x8000: the display mask comes from each level's
   .gpt (at +0x110 + 0x478, in the 0x2868-byte block FUN_004a5900 reads) and is 0xffffffff on all 11. Without an .RLI
   section the game would clear 0x8000 and 0x1000 (no radiosity, static lights on the geometry), but that is
   FUN_00420d60's text-script path: these levels load from the .sna.

**One model for both.** A sector has `lights` (the whole frame) and `char_lights` (.ttlv CHLT: its characters and water).
Retail has no CHLT, so its second list is empty. `tc_view_draw.light_first / light_extra` name a draw's own lights after
the frame's, in the same 48; the shaders loop over both ranges (`DrawParams.water.zw`, spare until now). A sector's
ambient lights are added to the draw's `ambient` on the CPU, since `ambient + G x diffuse` is one sum: this is what keeps
COCK01's (2.0, 0.3, 0.2) off its static geometry. Port choices: a character's lights switch at once, as the original's
(the frame's still fade); a light on two sectors' lists counts once (the exe would add it twice); the sectors with most
claim to the 48 are Ed's, the camera's, then those with a perso. Measured over the demo: never over budget.

**Not ported:** hotspots (marmite's 5 that are on), the zbuffer lights (`GLI_lCreateZBufferForLight`: the record's +0x8c,
0 in every level), persos' own MS lights.

**Water.** The same structure as retail's, other numbers (`GLI_vInitWaterplaneModule`; no level sets its own, visual
environment +0 is null on all 11). `tc_view_frame.special_water` picks them in the shaders.

| | retail | SE |
|---|---|---|
| tint | + (-0.6, -0.3, -0.4), full from 7 deep | x (0.3, 0.8, 0.2), full from 2 deep, after the lights (`GLI_vFogWaterPlaneEffect`) |
| lift | 0.7 | 0.6 |
| wobble | ramps to 1.4 by 5 deep, on a cosine | 2 from 1 deep at once, on half a sine (64 entries of 2 pi / 128) |

**Measured, the formula on every static vertex (radiosity only):**

| level | before: mean, black | after: mean, black | retail's level |
|---|---|---|---|
| carota | 0.00, 99.5 % | 0.21, 45.7 % | 0.46 |
| cavedoc | 0.02, 93.2 % | 0.31, 0.5 % | 0.40 |
| cock01 | 0.03, 95.1 % | 0.35, 24.2 % | 0.60 |
| Land | 0.20, 0.1 % | 0.40, 0.2 % | 0.45 |
| LeCanyon | 0.00, 99.9 % | 0.46, 13.8 % | 0.45 |
| marmite | 0.00, 99.9 % | 0.45, 26.9 % | 0.39 |
| north | 0.22, 2.1 % | 0.44, 2.1 % | 0.41 |
| pyramide | 0.01, 98.4 % | 0.48, 0.8 % | 0.58 |
| sud0 | 0.29, 0.3 % | 0.56, 1.6 % | 0.33 |
| Totalski | 0.24, 14.6 % | 0.63, 0.0 % | 0.41 |

The outro's stage stays black: ambient and diffuse 0 in the data. Carota's dark vertices are its `sctrepair*` sectors,
candle-lit rooms with radiosity exactly 0, lit by the dynamic lights (with them, over the demo, 38.7 % of what is drawn is
black). Marmite's have real low radiosity. Both read as intended, but **nothing here has been seen on screen**.

**Retail unchanged, checked:** every chunk but the stamp of all 42 retail .ttlv / .ttai identical to the old exporter's
(691 chunks; the 6 lightmaps too), and 30 s of the demo on all 21 levels hashes the same through the old and new core:
frames, 4,066,970 draws, no draw with lights of its own. The shaders' retail path has `first = extra = 0`. Shaders.metal compiles
on the Mac the way the app does it (`MTLDevice.makeLibrary(source:)`, from a small headless program; it rejects the name
clash an earlier draft had). The GLSL was only type-checked as C++: the Linux install compiles it.

## Animation (read 2026-11-05)

The player is `GAM\\PlayAnim` (FUN_00423080 poses a frame, FUN_004239b0 and FUN_00423730 run the clock and the state
changes; no exported names, found through `POS_fn_vDecompressPosition`). Against the port's decoder, which was already
close:
- **Fixed:** a compressed matrix pointer of 1 means identity. The port took it for "no channel" and lost the object: 139
  channel-frames of real parts in the levels' initial animations. Zooms and axial scales are unsigned.
- **Fixed 2026-11-06, the shards:** type 0xF is not a uniform scale and three axes but a symmetric scale matrix, signed.
  The port read its off-diagonals as axis scales, and reading them unsigned (the line above, right for types 7 and 0xB
  only) turned small negatives into 255: parts drawn up to 256 times too big, the yellow and red shards across sud0 (the
  user's playtest). 3,277 channel-frames across the initial states; six levels drew it (carota, cavedoc, Land, outro,
  pyramide, sud0), and the outro's and marmite's state clips carry it too (CAST). Over 30 s with every sector drawn: 49,534
  perso draws past 16 times their size before, none after, the largest 3.15. `viewcheck` keeps it (under 16 times). Not
  seen in the app yet. Retail doesn't come through here: bar and sud0 re-exported with only TEXS different (paths).
- **Same as the port:** `frame = ms x state speed (+0x3B) / 1000 % frames`, stepped (the port blends between frames: its
  choice); hierarchy pairs index channel slots; slots stay put within an animation (1501 of 1501). `rec` byte 7 = 0 an
  object, 2 none, always with object 0xFF.
- **No effect in the data:** the half-rate mode (AnimRef +0x68: odd frames interpolated at run time) and the AnimRef
  origin (+0x10: a state change moves the perso by the difference) are unset on all 291 initial-state animations; +0x6C too.
  Only initial states were scanned.
- **Not ported:** frame +4 (a 3x3) and +8, accumulated by FUN_00424fd0 / FUN_00425720: root motion, by the look. Only
  matters once SE persos walk by their own AI.
- **Open: the speed of SE props.** ttlv.ANIM_TIME_SCALE (0.25) was matched by eye on retail props in Wine and is unexplained:
  the exe's formula says 1. It is applied to SE props too. Retail and SE props carry the same speeds (15, 25, 30), so the
  data doesn't say the SE differs, and nobody has compared SE props with the original. The state clips (Ed, the cast) run
  at 1.

## Moving textures (read 2026-11-06)

The SE VisualMaterial has 12 bytes fewer between the texture (+0x48) and the scroll fields, so the exporter read neither
its scrolls nor its flipbooks (`level.SE_VM_SHIFT`). `GLI_xRefreshAnimatedTexture` (0x45b9e0) is retail's FUN_004457d0
with every field 0xC lower: scroll_cur +0x4C, speed +0x54, scroll_on +0x5C, last update +0x60, flipbook first +0x64,
current +0x68, count +0x6C, start time +0x70, lock byte +0x74. Same clock as retail: ms x 0.06 (caller 0x428122; menus
pass 1.0). On all 11 levels: 12 scrolling materials, the rest flipbooks (torches, lava, neons, Doc's Kyou, explosions,
arches). LeCanyon's moving floor is RC_Canyon_21/23/24TX, speed (0, -0.006) a tick; its clouds cl_canyon02txy.

**Sign (read, both games):** the offset is added, u + offset and v + offset. SE: the rasterizer at 0x46787b and 0x467aac;
retail: GliDX6vr.dll 0x10003100 (from GLI_DRV_xSendElementTIToClip_TRIANGLES). ttlv.py emits the opposite, a 2026-10-19
inference from sud0's rainbow. Physical check under the exe's rule, world space: the SE's falls all run down (sud0
z -28/s, pyr_ochute -5.9/s, pyr_odeborde -1.5/s), so the SE reader negates its speeds.

**Retail, open, left as it was:** retail's four scrolling surfaces still move opposite to the driver. PYR_OCHUTEANUTXY
runs uphill in the port today (the exe's rule: z -6.65/s); O_ZARBI_AUNTXY would run uphill under the exe's rule (z
+13.7/s; its UVs differ from the SE's), which only the running original can settle. The fix, if wanted: drop the SE
negation and emit `su, -sv` in ttlv.surface().

## Crash found in the first playtest (fixed 2026-11-04)

The app crashed on loading SE levels: `water_plane` (tc_viewer.c) read a sector's water-surface list that was
missing. The converter read the SE surface list without the sector shift, so the six SE levels with water
(cavedoc, cock01, marmite, pyramide, sud0, Totalski) had water environments but no surfaces. Two fixes: the list is
read at the shifted offset (16 surfaces, every water env paired), and the core treats a missing surface as "no water
plane" instead of reading past the list. Reproduced headless: viewcheck on SE cavedoc segfaulted (exit 139) without
the guard. viewcheck's other lines are scripted for specific retail levels (bar, sud0, north), so they fail on SE maps
of the same names. Its generic line, "every frame's draw list is valid", passes.

## AI primitive names (2026-11-05)

The SE exe (`MAID3DVR.EXE`) keeps what retail stripped: its AI primitive tables with their script names (e.g.
`Cond_PressedBut`, `Proc_ChangeComport`, `Action_GoToWP`), and an export table of **2593 named engine functions**
(`fn_eGetNbCond`, `szFindConditionScriptNameFromId`, `GLI_*`, `GEO_*`, `COL_*`, `DNM_*`, `SECT_*`...).
`tools/exe/se_names.py` maps the primitives onto retail's numbering (method and table layouts in its docstring) and
writes `tools/exe/se_ai_map.tsv`.

| kind | SE | retail | matched by code (>= 0.85 / >= 0.5) | by order or position | retail-only |
|---|---|---|---|---|---|
| keywords | 9 | 10 | 9 / 0 | 0 | 1 |
| operators | 28 | 28 | 12 / 12 | 4 | 0 |
| fields | 15 | 15 | 9 / 5 | 1 | 0 |
| conditions | 98 | 111 | 88 / 10 | 0 | 13 |
| functions | 121 | 165 | 52 / 47 | 22 | 44 |
| procedures | 230 | 294 | 142 / 72 | 16 | 64 |
| meta-actions | 42 | 43 | 20 / 10 | 12 | 1 |

Every SE primitive has a retail counterpart. Where this project had already named a retail primitive by reading its
code, the two agree, e.g. ButtonHeld = `Cond_PressedBut`, InZone = `Cond_CollidePersoZDDWithPerso`, ForceBehaviour =
`Proc_ChangeComport`, ChangeState = `Proc_ChangeAction`, TurnBy = `Proc_Turn`, SetHeading =
`Proc_TurnAbsoluteDirection`, RemovePerso = `Proc_KillPerso`. Several guesses get their real names: Overhang? =
`Cond_CollideWithAttic`, Counter34? = `Func_GetHitPoints`, TimeSince? = `Func_ElapsedTime`, StandingOnPerso =
`Func_GetFather`, the swim conditions 70-74 (`Cond_CanSwim`, `Cond_IsNotOutOfDepth`...). The AI listings now use these
names for retail primitives this project hadn't named (`ai.load_names`): 595 of retail's 666 primitives have a name; the
71 without are retail additions with no SE counterpart.

To check: retail conditions 44-47, named GroundMaterialIs... here, are SE's `Cond_ZDMCollideWithGround`... (0.97 code
similarity). The pairs marked `order` (e.g. procedure 244, JumpBallistic in the port, is `Proc_Accelerate` at 0.62)
are the least certain.

**What retail added after the SE** (retail-only, with this project's names where it has them):
- Conditions: MainActorWithin, LevelIs, two game-state tests, IsValid, and 8 others.
- Functions: an angle function (2), Lives and two neighbours (40-42), SaveValue (93), the vector set (VecNormalize,
  GlobalToLocal..., 99-103), and a block of 32 at the end (133-164).
- Procedures: the SetActivity family and the activity radius (17-22), perso flags, StopMotion, lives (SetLives /
  AddLife / TakeLife), save values (IncSaveValue / SetSaveValue), a second global sound, SetLightSet (283, the day /
  night cycle), and other blocks (224-233, 277-293).
- One meta-action (29), one keyword (9).

Next with these: translating SE scripts into retail numbering (SE index -> retail index from this map), so the SE
levels' own characters and triggers can run in the core's interpreter. That is also the way to find and read the SE
level scripts that switch Ed into sliding mid-level (the SE has `Proc_SkiTurnLeft` / `Proc_SkiTurnRight`, retail
255 / 256), which the SE uses where retail has skymap.

## Sliding in the Special Edition (read 2026-11-05)

With the scripts translated into retail numbering (`ai.se_translate`: primitives through se_ai_map.tsv, the SE's
channel reference node type 18 -> retail's 33), the SE levels' scripts read as real logic. The SE has no separate
skier character and no skymap. **Skiing is a mode of Ed himself**: his behaviour **B52** with reflex **R24**. B52
sets the camera for skiing, among other things; Ed's own scripts never enter it. Level trigger characters force it on
him (`ForceBehaviour` / `ForceReflex` = `Proc_ChangeComport` / `Proc_ChangeComportReflex`) when he is in one of their
zones (`InZone` = `Cond_CollidePersoZDDWithPerso`). Only SE Totalski does this, and it mixes walking and sliding
within the level:

| perso (model, instance) | at | forces on Ed | when |
|---|---|---|---|
| LOL_Actor_Model1, STH_Actor8 | (-416, 17, 525), below the start | B52 + R24: ski | Ed in its zone |
| STH_genski_petitepiste, STH_finpiste | (213, 607, -1148), the big run's end | B3 + R2: walk | in its zone |
| STH_remonte_pentes / _pente2, STH_cabine1 / 2 | (170, 539, -1149), (430, 608, -1065) | B82 / B84: ride the ski lift | a button, a power (`Cond_TestPower`), in its zone |
| STH_genski_meca_glisse, STH_genski_glisse1 | (395, 658, -1082) | B52 + R24: ski again (a small run) | in its zone |
| STH_glisse_marche, STH_meca_vers_saut | (458, 526, -1047) | B0 + R0: walk | in its zone |
| STH_genreinit, STH_reinitski | (107, 534, -1149) | B52 + R24 | in its zone (a restart point) |

So SE Totalski is a hub: Ed walks from the start, skis the long run, walks at the bottom, rides ski lifts up to
shorter runs, and leaves through an arch to Sud0 (STH_vers_Sud0). Its bottom area also has ice targets
(STH_skicible), beehives, silver and life pickups. The other ten SE levels force B52 nowhere.

**Retail can't do this with one AI:** retail Ed's own AI never enters a ski state (no reference to states 115-125,
217, 220, 278 in the fix Ed's scripts). Only the separate skier character skis.

**Built (2026-11-05): the switching, with the two retail AIs.**
- Exporter (`se.mode_zones`, `se.export_level`): each SE trigger above becomes a mode zone. It has the trigger's ZDD
  box, the mode it sets, the mode its guard on SE Ed's dsg7 requires, whether it fires once, and a teleport (to
  another perso's position, or by a vector).
- SE Totalski's `.ttai` = walking Ed (retail Land's, `WALK_DONOR`) + `MODZ` (5 zones) + `ALTM` (the whole retail
  Totalski skier `.ttai`). Both share the NewEd family, so one EDPL serves both (retail Land's and Totalski's are
  byte-identical).
- Core: `tc_ai_model` reads `MODZ` / `ALTM`; `tc_viewer` loads the skier as a second model and, each tick before Ed's
  step, swaps the driver when he enters a zone (`play_mode_zones`), keeping his position and speed.
  `tc_viewer_play_mode` tells which model drives him. Fire-once zones re-arm after a death, as the SE reloads the
  level.
- Walking on SE snow: the walk mechanics treat a zero-grip floor as grip 1 while walking (`tc_mec_walk.firm_floors`,
  port choice). The skier keeps the slide.
- Checked headless (scratch probe through the viewer):
  - start: walking, then the start zone hands Ed to the skier on the first tick; auto-start pushes off, 30 u/s.
  - `STH_finpiste`: walking; he runs on the snow at 9 u/s (top 12).
  - `STH_genski_glisse1`: skiing again, 0.6 lift.
  - `STH_meca_vers_saut`: walking.
  - `STH_reinitski`: skiing, teleported to the start.
  - skicheck uses the skier from ALTM (unchanged 2.4 %).
- Not modelled yet: the ski lifts (`STH_cabine1 / 2`, B82 / B84 with a power and a button).
- **Regression, fixed 2026-11-05 (user: "he no longer gets his plate out on totalski"):** `play_mode_zones` skipped
  the switch on any level with a cast, because the cast holds links into the driver's AI that the switch frees. SE
  Totalski had no cast until the pickups (below) gave it one: 27 items and World. So the switch never fired, and Ed
  stayed walking on the start slope. Now the switch hands the cast its new driver: `tc_cast_detach_main` before the old
  driver is freed (ghosts that shared his memory keep their own copy), then `tc_cast_set_main`. viewcheck runs SE
  Totalski when `files/out/se/ttlv` is there: skier on the first tick, the ride down, every zone's swap with the cast
  linked. That check fails on the old code.

## Portals (2026-11-05)

SE arches are different from retail's: an arch fires as soon as SE Ed is in its small ZDD box in the opening, at
once. sud0's eight hub arches `ChangeLevel` by their own dsg0 (totalski, cavedoc, carota, north, lecanyon, cock01,
pyramide, marmite; the first time, a story behaviour runs first). Every world's entrance and exit arch does
`ChangeMapAtPosition('sud0', 'zor_ed<world>', 'zor_kamera<world>')`.

**Port: SE arches play out the retail way.** `se.se_portals` finds them (a ZDD zone on Ed and one destination decided
by the arch's own dsgvars; reloads of the same level, like Doc's barge in sud0, are left out). It writes retail-format
`PORT` records: capture in the SE zone with the retail arches' ops (dsg134 == 0, dsg99 = the arch, dsg100 = 0, dsg136
= the arrival heading, Ed's walk-in B66 + R4), fire on Ed.dsg103 == 1 with the SE destination. The arrival heading is
the destination SE level's start facing, or its named place's. The SE level's own places go in `PLAC`, for arrivals at
`zor_ed...`. Portal level changes stay in the current level's folder (both frontends), so SE arches lead to SE levels.

Checked headless:

| run | result |
|---|---|
| sud0's Totalski arch | Ed walks to 4.5 in front, plays state 228 (stepping in), cover at 5.9 s, `totalski` requested at 10.5 s |
| the same, with Jump | cover at 0.35 s, change at 5.0 s |
| carota's exit | `sud0` at `zor_edcarota`; arriving, Ed stands at that place |

**Both sides:** SE arches are walked into from either side, while retail's walk-in takes Ed 4.5 in front of the
arch's local -Y. Entering from +Y, that point lies through the arch, and he walked at it for good (seen on SE
Totalski's exit). SE portals carry a two-sided flag (PORT's flags byte, `TC_AI_PORTAL_TWO_SIDED`): at capture the
arch's axes turn half round about Z when Ed is on its +Y side. Checked: both sides of Totalski's exit reach sud0 in
10.6 s; the old export was stuck from +Y.

**Stopping in front (2026-11-05, your report: "Ed walks up close to them, then walks away"):** every SE arch's zone
is the same gen_arche box, 2.5 across the opening and only +-1.2 through it, so Ed was caught already in the opening.
The retail walk-in (B68: to 4.5 in front, `ChangeStateByDirection(dsg99, (0, -4.5, 0))`) then walked him about 3
units back out before he stepped in. Retail's hub arches capture within radius 4 instead (PORT, no zone). Port choice
(`se.arch_capture`, `SE_ARCH_REACH`): the SE box keeps its width and height and reaches +-5 along the arch's axis, so
Ed is caught about 5 out, on either side. Headless, Ed running at each of sud0's 8 arches from both sides (a scratch
probe): before, caught at 0.9-1.2, walked back 2.9-3.3, level change at 6.5-7.2 s; now, caught at 4.4-4.8, no step
back, he stops there and steps in, change at 4.9-5.0 s. Retail portals are unchanged (their own records).

**Loading screens:** a level change from an SE level shows the SE's picture of the destination (the purple swirl
ones, `out/se/Vignette/Level`). Both frontends take the vignettes next to the shown level's own folder.

**Skipping the walk-in** (both editions): the game's own rule is in Ed's B68: Shoot held or just released skips to the
cover. Port choice: Action and Jump do it too. While a portal has captured Ed and not yet fired, the core presents
them to his scripts as Shoot (`tc_ed_ai_step`).

**The arches' surface:** a flipbook in both games (retail GEN_ARCH01R..04R, SE gen_arch01..04, 5.4 ticks a frame, slowed
by ttlv.TEXTURE_TIME_SCALE). The SE's looked still only because its flipbook was misread ("Moving textures").

## The outro (2026-11-05)

SE outro is the end credits: a 12-triangle box (75 x 85 x 48, untextured, ambient and diffuse 0: black under the
game's lighting; the grey you saw was the port's "Modern" lighting, then the SE default) as a stage, 20 "cine" characters,
two directors and a credits keeper. Its scripts read cleanly in retail numbering (`ai.se_translate`), and they now run
in the core's cast: the first SE level whose own persos run (`cast.SE_CAST_LEVELS`).

**What the scripts do (read from the data):**
- Every character starts in a behaviour that does `DeactivatePerso(Me)` and changes to its "turn": ChangeStateAndWait
  through its states (its performance: they walk in, act, walk out), `ActivatePerso(next)`, `RemovePerso(Me)`. A
  relay: DS3_CoCo (the chicken) -> MIC_TomyBoy -> POC_Hg07Cine (states 1-6) -> MIC_Babar (the mammoth) -> CarCine ->
  Navet_Cine -> POC_C2Cine -> POC_RaisinCineAct -> POC_PirexPerso -> MIC_LaSuzy -> MIC_LeMalade -> MIC_LeCoq -> MIC_Hary
  -> MIC_Hary2 -> MIC_Spring2 -> MIC_Fiyaa -> MIC_Spring -> MIC_Waterloo (Napoleon, the last). MIC_gd02 (a second
  gd007) is never activated by anyone: it never appears (cut, probably).
- **POC_Actor1** (the director): `SetCameraTarget(camera, POC_GenVisee)`, `SetCameraMode(camera, 6)`, then takes
  itself out. **POC_GenVisee** ("aim") is the point the camera looks at, and its schedule keeps
  `ChangeFocal(camera, 0, 1.35)`: a 1.35 rad (77 degree) horizontal field of view.
- **DS3_CoCo** parks Ed (`ForceBehaviour(Ed, B21)`: SE Ed's B21 = stand in state 0, release the modules; no
  reflex), hides the HUD (`Proc_SetDisplayFixFlag(0)`), then `Proc9(1); Proc_DisplayVignetteDuring(
  'Credits\\Credit0N.bmp', 5000)` for 06, 04, 03, 02, 05, then `Proc_StartMenuWithoutPauseGame('StartCredits')`,
  then its own turn (state 0) and wakes TomyBoy.

**What the SE exe does (read, MAID3DVR.EXE):**
- `Proc_DisplayVignetteDuring` (PROC table SE 175, handler 0x4d5550 -> 0x40f9e0) only queues: a flag (0x5bf3e0), the
  name, the time. The main loop's 0x40fa20 then loads the picture, shows it and **busy-waits** for the time: the game
  is held. One queue slot, so `Proc9(1)` before each call (SE `Proc_SetActionReturn`: 0x4c3a50 / 0x4c3a70 set the
  action-return globals, as retail PROC 9) stops the schedule after it: one picture per frame, 25 s in all.
- `Proc_ActivateObject` (0x4d1ff7 -> 0x41b140): if not dead, 0x41b0a0 puts it back in its sector by its position and
  sets it active; it also clears a hidden bit (0x200000 at SO +0x194). `Proc_DesactivateObject` (0x41b160): inactive,
  out of the world. No re-init: the perso goes on in the behaviour it was in, which is how the relay works.
- Camera mode 6, in the retail exe (`FUN_004a3130` case 6): the camera's displacement and four more fields zeroed,
  request flags 0x514. It stays where it is and turns to its target (docs/CAMERA.md).
- The SE std game (perso +4) ends before retail's +0x2A: no mechanics / animation flags and **no activation radius**
  (retail added those with SetActivity, PROC 17-22). Port: SE cast persos are always awake, animation on.

**The port:**
- Exporter: `cast.select` runs the SE outro's persos (22: the 20 characters, the two directors, World; the camera is a
  ghost). SE animations go into the cast families (`level.state_anim`, `anim_ref_frames`); SE sounds are not read; no
  SE perso may force or write the retail Ed (their behaviour references are SE Ed's); the SE .gpt's engine lists are
  not read (another layout). Entries carry flag `TC_CAST_SCENE`: their removals take wherever Ed is (no removal guard).
- **Cast entries now carry their strings and constant vectors** (`TC_CAST_TABLES`; .ttai STRS and VECS bodies). The
  cast never had them: every string read as nothing. That's why the vignette names were missing at first. It also
  fixes **LevelIs (COND 62) in the retail casts** (HEG_Inventaire and sud0's HEG_RichMine use it: always false
  before; cast actors now get the level's name too). Fun data bug: HEG_Inventaire tests `'totalki'`, so its Totalski
  branch never fires, in the game either. No retail cast uses constant vectors.
- Core: PROC 177 / 178 / 180 (`SetCameraMode`, `ChangeFocal`, `SetCameraTarget`) and 210 (`DisplayVignetteDuring`)
  write to the cast's shared `tc_ai_globals`. A camera write counts when it changes something (GenVisee repeats its
  focal every frame).
- Viewer: **the director's shot.** Mode 6 with a target: the eye at the camera perso's place in the level data
  ((-4.0, -40.7, 3.1); port choice, inferred: the shot is set on the first frame, before the camera moves), turned to
  the target, the scripts' focal as the field of view. Ed's inputs are held back meanwhile (as SE's B21 parks him).
  **Pictures:** a request holds the game for its time (Ed, the cast, the clock), over black, laid out as the loading
  screen's vignette (`TC_VO_PICTURE`; both frontends: `PictureScreen` in main.swift, `picture_prepare` /
  `draw_picture` in sdl/src/main.c). Port choices: a fresh Jump / Action / Shoot skips a picture, and ends the shot
  (the follow camera and your field of view come back; the level stays explorable). The status line shows "scripted
  camera" while the shot is on.
- Not ported: the `StartCredits` menu (the SE's scrolling credits, by the look of TEXTS/CDTSTD.TXT and CDTNME.TXT: its
  layout is in the exe's menu code), `Proc_SetDisplayFixFlag`, SE sounds and music.

**Checked headless** (a scratch probe through the viewer, 200 s; castcheck): the pictures come in the scripts' order
(Credit06, 04, 03, 02, 05, 5 s each), then CoCo's turn, then the whole relay to Waterloo at 176-182 s, each turn
handing on as above. Per turn the share of the character's parts inside the shot (4:3) goes from low to 100 % and back
(they walk in and out of frame: the framing reads as intended). Ed is out of shot (about 75 degrees off its axis).
castcheck on SE outro: ok, with "14 scene actors took themselves out" and "5 Jump taps to get past" the pictures and
the shot, then Ed runs. Retail: castcheck on fresh sud0 / north / Land / cavedoc exports prints the same as before
(timing aside), and every core check passes on the existing exports. **Not seen in the app:** the shot, the
pictures, the characters' looks.

## Pickups (2026-11-05)

The SE's own collectibles now run their scripts on every SE level: the cast there is the items and World
(`cast.SE_ITEM_FAMILIES`, `SE_SAVE_MODELS`; the outro keeps its whole scene). Read from the scripts:

| item (family / model) | per level | when Ed is near (its dsg0 radius) |
|---|---|---|
| silver spade icon (`gen_icon_argen` / ARG_LifeAG) | 10 | `World.dsg1++`, World's hit points + 1, fades out; at World's target (dsg0 = 10) World's B3: dsg4 = 1 and `AddHitPointsMax(Ed, 1)`: +1 max hit point, filled |
| gold icon (`gen_icon_or` / ARG_LifeExt) | 0-3 | its B4: refills Ed, 1 hit point every 200 ms until full, then leaves |
| red heart (`gen_icon3` / ARG_Life) | 6-13 | + 1 hit point if he is hurt (else it stays) |
| yellow beehive (`gen_ruche_j`) | 1-5 | into the inventory |
| Doc's machine parts (`doc_domino`, `doc_elicon`, `doc_pig`, `doc_plum`, `doc_ressor`, `doc_kyou`) | 6 on one level each | into the inventory |
| save point (`genplainenord` / ARG_Sauvegarde, JOB_Sauvegarde, POC_POC) | 1-9 | `Proc_SaveGame`, then leaves; port: Ed's checkpoint (as retail's DFM_Save) |

**Made to work for this (all general, not SE-only):**
- **World keyword** (KW 6, `Dot(World, dsg1)`): was unknown in the core (345 uses skipped on SE sud0; retail casts use
  it 10-16 times per level too). The CAST header's spare u16 now names the world perso (the engine's 0x8c1744 via
  `engine_lists`, else the level's World family perso); `tc_ai_perso.world_id`.
- **The max hit points, PROC 72-76**, read from retail `PROC_72Etc_004897f0` (std game +0x14 hit points, +0x15 max,
  +0x16 the max's ceiling): 72 max := n, 73 := its start, 74 := the ceiling, **75 max += n up to the ceiling, then the
  hit points := the max**, 76 max -= n, the hit points clamped. Retail's Worlds and HEG_Inventaire use 72 / 75 for the
  same collect-N reward, so it works on retail now too. The ceiling isn't exported: no cap (255). PROC 70
  (SubHitPoints, Ed's own scripts 255 uses) and 71 stay no-ops: that is Ed's damage, a subsystem of its own.
- **A mapping error fixed:** SE `Proc_SetHitPointsMax` is retail 72, not 71 (their wrappers trace alike, the calls
  being skipped); `tools/exe/se_names.py` READ_PAIRS keeps the corrected pair (confidence `read`). Other pairs of
  wrappers that differ only in the function they call could hide the same kind of error.
- **Models of reflexes only** (the silver icons have no normal behaviour): the core accepted none and would have run
  reflex 0 as the normal one.
- **Separate id ranges:** the borrowed retail Ed's .ttai names persos in his donor level's numbering (2 = the camera
  on most levels), the SE cast in the SE's: they collided (on SE sud0, SE Ed is perso 2). SE persos are now numbered
  from `cast.SE_ID_BASE` (0x2000) and SE Ed's references go to the main actor (on Totalski the skier, from the donor's
  CAST). The outro only worked before because its numbers happened to line up.
- **SE LeCanyon's names:** its fix -> level link in the name tables resolves into unrelated data, so every LeCanyon
  perso read as nameless; the tables are now walked back from their last entry when the forward walk fails
  (`level._names_backward`): 201 instances named (10 silver icons, 5 hives, 5 saves, Doc's `doc_kyou`).

**Checked headless** (a scratch probe: a fresh viewer per item, Ed at 1 hit point put on it for 1.5 s): taken 32 / 32
carota, 21 / 21 cavedoc, 25 / 25 cock01, 13 / 14 Land, 32 / 32 LeCanyon, 23 / 30 marmite, 30 / 31 north, 43 / 44
pyramide, 27 / 29 sud0, 26 / 26 Totalski. Hearts heal 1 -> 2, the silver icons fill World's count and its reward
fires (sud0: dsg1 = 10, dsg4 = 1, Ed 4 -> 5 / 5), save points set the checkpoint. Not taken:
- marmite's 7 pigs: caught with `Cond_CollidePersoTypeZDEWithPersoTypeZDE(Me, 1, Ed, 2)` (zone types: his attack,
  probably), and they run between waypoints, which the cast doesn't move: they wait for the collision work.
- Save points with radius 0 in the data (sud0 ZOR_Save1, Land POC_SauvAvantPf, pyramide Gun_Actor24): never fire in the
  game either. sud0's Doc feather sits at z -75 (Ed falls past it there: reached otherwise, probably). north's
  ARG_LifeAG1 starts hidden (R3) until something spawns it.
castcheck passes on all 11 SE levels; retail checks unchanged (castcheck on fresh sud0 / north / Land / cavedoc exports:
only PROC 72 no longer listed as skipped). Not ported: the inventory itself (`Proc_AddObjectInInventory`: the item
just leaves), the SE's sounds, `Proc_DisplayChrono` (SE Totalski's World, every frame: a race timer?). **Not seen in
the app.**

## Ultimate Goal

The long-term goal these steps lead to (user, 2026-11-05): Open Tonic as one merged Definitive Edition, Ed moving
between both editions' levels and collecting from both. See docs/DIRECTION.md, "Future direction: Open Tonic, the
Definitive Edition".
