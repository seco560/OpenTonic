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

### Still to do (updated 2026-11-04, end of the session)
- ~~Wire the SE steps into convert_files.sh.~~ Done: step 4b (disc image found under files/sources, extracted to
  files/sources/se, outputs in files/out/se).
- ~~Add the Retail Edition / Special Edition sections to both pickers.~~ Done in LevelPicker.swift and sdl/src/picker.c
  (Retail Edition, Special Edition, Cutscenes, Imported). Built (Swift) and syntax-checked (SDL); not seen running.
- ~~Sort out dying in SE levels.~~ Not needed: no "death" level is loaded. The core catches ChangeLevel('death') and
  restarts Ed at his start or last save point (tc_ed_ai.c), whatever the folder.
- ~~Re-check the other levels after the sector fix.~~ Done: all 11 through playcheck and sectorcheck, see "Checks".
- **Playtest SE Totalski in the app** (and the two pickers): nothing has been seen on screen yet.
- ~~Write docs/SE.md.~~ This file.

A first playthrough of an earlier Tonic Trouble is a lovely thing to aim for.

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
GPT perso-in-fix, SuperObjects, IPOs, geometry, materials, radiosity, PTX, persos, families, states, object lists.

**SE animation** (`level.SeAnim`): AnimRef = {ptr frames, u8 n_frames, u8 speed, u8 n_channels, ...}. A frame is 16
bytes {ptr channels, ptr f32 matrix, ptr f32 vector, ptr hierarchy}; the matrix and vector look like root motion and
are not read. channels = ptr[n] -> {ptr compressed matrix, u32 info}, where the object-list index is the low byte
(0xFF = none) and the channel id is byte 1. Compressed matrix: u16 flags; & 1: i16 pos[3] / 512; & 2: i16 quaternion
w, x, y, z / 32767 (the engine's conjugate convention, as retail); & 4: i16 uniform scale / 256; & 8: i16 scale[3] /
256. Hierarchy = {u32 n, ptr {u16 child, u16 parent}[n]}, channel indices as in retail TT (always valid as indices,
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
  `ai.light_persos` return nothing for SE).
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
- Cost: SE skicheck went from 1.9 % to 2.4 % stopped (over the 2 % limit), mean speed 21.6 -> 19.3: more of the course
  is solid now. The descent from the real start is unchanged, position for position.

## Converting with one game or both (2026-11-05)

`tools/convert_files.sh` detects each game in `files/sources`: retail (`GAMEDATA` complete; a partial copy is an
error), the Special Edition (its disc image, or `files/sources/se` from an earlier run). It converts whichever it
finds and fails only when there is neither. Special Edition alone: its levels have no Ed (no donor) and open in the
Tonic Viewer only. Their file names use the retail spellings anyway (`se.RETAIL_SPELLING`), and there's no music,
sound or intro. Checked in scratch sandboxes: SE-only converts in 88 s, and all 11 Ed-less levels load with valid draw
lists in viewcheck. viewcheck's demo now skips a level without Ed: it crashed in the test, not the core.

## Lighting

SE levels start in the port's own ("Modern") lighting. Its original data shows many issues under the game's
("Classic") formula, probably engine changes before release, like the animation format. The level carries an
optional EDIT chunk (1 = Special Edition), and the core's default follows it (`tc_viewer.c`). The toggle still offers
Classic. Retail levels are unchanged.

## Crash found in the first playtest (fixed 2026-11-04)

The app crashed on loading SE levels: `water_plane` (tc_viewer.c) read a sector's water-surface list that was
missing. The converter read the SE surface list without the sector shift, so the six SE levels with water
(cavedoc, cock01, marmite, pyramide, sud0, Totalski) had water environments but no surfaces. Two fixes: the list is
read at the shifted offset (16 surfaces, every water env paired), and the core treats a missing surface as "no water
plane" instead of reading past the list. Reproduced headless: viewcheck on SE cavedoc segfaulted (exit 139) without
the guard. viewcheck's other lines are scripted for specific retail levels (bar, sud0, north), so they fail on SE maps
of the same names. Its generic line, "every frame's draw list is valid", passes.

## Next

The long-term goal these steps lead to (user): retail, SE, or both interwoven as alternate universes with portals
between the two versions of a level. See docs/DIRECTION.md, "Final goal: two editions, one game".


1. Playtest SE Totalski in the app (camera, speed, the slope's end: there is no finish trigger).
2. SE AI: read the primitive name tables from `MAID3DVR.EXE`, map them by name and handler to the retail numbering
   (tools/exe/ai_names.tsv). Then SE persos can run their own scripts (the cast), and later SE Ed himself.
3. SE mechanics (ID cards) and Ed's SE model and animations: SE Ed has 6 channels (head, body, hands, feet).
4. The 12 unresolved relocations: all fix -> block (5, 2) (the level's own block, empty in the fix); the SUD0 material
   with an empty texture name; outro has no loading screen on the disc.
