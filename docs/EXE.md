# TonicTrouble.exe – reverse-engineering notes

## Which binary
| File | sha256 | Use |
|------|--------|-----|
| Disc `TonicTrouble.exe` (= installed `.bak`) | `37631f2f…` | Original retail build, 27 Oct 1999. **Packed/encrypted** |
| Installed `TonicTrouble.exe` | `fea1140e…` | **Unpacked memory dump: our analysis target** |

**Original (packed):**
- Section names are zeroed. Code and data sections have entropy 8.0, i.e. fully encrypted.
- It imports only `GetModuleHandleA`.
- Entry is `call $+5; add [esp],0x1008; ret` into a stub full of `cmp eax,eax; jnz/jz` junk-instruction obfuscation.
- This is a commercial protector. The exact product hasn't been identified.

**Dump:**
- OEP is 0x45EF20. The import table was rebuilt as `.idata` (9 DLLs, 212 imports).
- Section names are generic (`.data` for code). The MZ header is non-standard (a dumper artefact); Ghidra loads it fine.
- Provenance unknown, probably from the earlier no-CD attempt. Treat any code we depend on as **to be verified**: later, emulate the original's unpacker and diff the result against this dump.
- Internal name from the export table: `MaiD3DFr.exe`. It exports 103 `SND_*` functions (sound API used by the sound DLLs).

## Copy protection
- **Layer 1, packer:** decrypts the image at startup. Not present in the dump.
- **Layer 2, CD module** (`CD_fn_*` in `tools/exe/names.tsv`, 0x48ED40–0x48F4B0):
  - Finds the CD drive (GetDriveTypeA / GetVolumeInformationA).
  - Opens it as an MCI `cdaudio` device and reads the disc TOC: track count, per-track type (audio = 2352 B/sector, data = 2048) and length. The total disc size goes into globals at 0x4E4F78–0x4E58F5.
  - Called at startup (`CD_fn_vCheckAtStartup`) and fix load (`CD_fn_vCheckAtFixLoad`). **Both ignore the return value**, so the comparison happens elsewhere, probably in the packer stub (unconfirmed).
- **Port impact: none.** The port reads data from disk and contains no protection code.

## Source layout (from assert/`__FILE__` strings)
Built from Ubisoft's CPA tree (`X:\Cpa\Tempgrp\<MODULE>\...`, `D:\cpa\Main2_0\Src\EngMain.c`). Modules seen:

| Module | Meaning | Example files |
|--------|---------|---------------|
| GAM | game engine: main loop, objects, actions, anims, demos | Engine.c, Initeng.c, Basic.c, Actions\Dynam.c, PlayAnim\PlayAnim.c, TTrouble\P5\GamExtra.c |
| GLI (Gliglou) | renderer front-end over Gli*.dll drivers | MultiDRV\Src\Init_Gli.c, Tex.c, Camera.c, Light.c, Material.c, Gld\DevVpt.c |
| SNA | snapshot (.sna/.rtb) loader | snaglob.c, snammg.c, snarlt.c, snasyn.c |
| SND | sound engine (exported API) | sound3d.c, sndseq.c, Win95\sndthrd.c |
| AI | AI script interpreter | AIBase\AI_Proto.c, StrIntel.c, AIGame\Keyword.c |
| MEC | dynamics/collision ("mechanics") | hidcard.c, hMecMatC.c, HMecEnvi.c |
| GEO | geometry | GeoObj.c, Element.c, GeoMem.c |
| IPT | input (DirectInput) | IPT_Main.c, IPT_Kwan.c, IPT_Bin.c, P5\MouseDI.c, InoMain.c |
| MNU | menus | mnuitem.c, mnubase.c |
| FON | fonts/text | fonfont.c, fontext.c, fonload.c |
| FIL | file formats | FIL_GF.c, FIL_Bmp.c, FIL_Tga.c, FIL_Conc.c (CNT) |
| SCR | script (.dsb/text) parser | SCR_Link.c, SCR_Hash.c, P5\Sectload.c |
| SAI | save games | Sai_main.c, P5\SAI_Save.c |
| CMP | compression | PC\CMP_Main.c, CMP_DIFF.c |
| SPO / POS | hierarchy (super objects) / positions | HieSpObj.c, Position.c |
| VIG | loading screens (Vignette.cnt) | Vignette.c |
| others | CPA (memory/errors), TMP, TMR (timer), RND, PO (DrawPO.c) | |

## Tooling
1. `tools/ghidra/ExportIndex.java` writes `out/exe/index.json`: functions, callees, referenced strings.
2. `tools/exe/modmap.py` assigns functions to modules.
   - Coverage: 2,493 / 3,718 functions (67%).
   - Evidence: direct `__FILE__` refs, exports, or lying between two functions of the same module (MSVC links each .obj contiguously).
3. `tools/exe/names.tsv` holds hand-assigned names and is committed. `tools/ghidra/ApplyNames.java` applies manual and auto names to the persistent project in `out/ghidra/`.
4. Types: `tools/exe/types.h`, `signatures.tsv` and `globals.tsv`, applied by `tools/ghidra/ApplyTypes.java`. `tools/ghidra/ArgCheck.java` reads argument counts from the asm. See docs/TYPES.md.

Rebuild:
```
export GHIDRA_INSTALL_DIR=/opt/homebrew/opt/ghidra/libexec
$GHIDRA_INSTALL_DIR/support/analyzeHeadless out/ghidra TonicTrouble -import "Source Files/Installed Files Linux/TonicTrouble.exe" -overwrite \
  -scriptPath tools/ghidra -postScript ExportIndex.java $PWD/out/exe/index.json
python3 -m tools.exe.modmap out/exe/index.json out/exe/exports.txt out/exe/modmap.json
```

## Full dump (2026-10-29)
Every function decompiled, with the project's types, and indexed. Made read-only from the typed project (nothing in
it changes; ~2 minutes on the M1 Pro, all cores):
```
export GHIDRA_INSTALL_DIR=/opt/homebrew/opt/ghidra/libexec
$GHIDRA_INSTALL_DIR/support/analyzeHeadless files/out/_decomp_artifacts/ghidra TonicTrouble -process -noanalysis -readOnly \
  -scriptPath tools/ghidra -postScript DumpAll.java files/out/_decomp_artifacts/all
```
Re-run it after ApplyTypes (docs/TYPES.md) so the dump shows the new types and names.
- `files/out/_decomp_artifacts/all/c/<addr>_<name>.c`: 3,971 functions, none failed (3,718 from Ghidra's own
  analysis plus 253 the typing passes created at dispatch-table targets, the AI primitives among them).
- `all/index.json`, per function: signature, callees, globals read / written / address used, struct fields read /
  written (typed accesses only: the field written is the last one before an assignment's `=`; conditions count as
  reads), functions whose address it takes, the data that holds its own address (873 aligned pointers to 387
  functions: dispatch tables, callbacks), strings; plus the 212 imports by name.
- Query it with `python3 -m tools.exe.xref` (fn, show, callers, callees, path, taken, field, global, string, grep;
  `-h` for the list). Untyped accesses (`*(float *)(param_1 + 0xd8)`) are reached with `grep`; indirect calls are
  not callers (see `taken`).
- Checked against REA (independent Ghidra 12.1.4 default analysis of the same dump, `fea1140e…`): all 3,718 of its
  procedures are in the index; callers of GAM_PlayAnim_Update (0x415c00, 6) and MEC_CollideMove (0x4be1bd, 7) and the
  references to 0x4ec6ac match exactly (evidence `ev_ce5ba4ce…`, `ev_d023d678…`, `ev_9ae218e3…`, `ev_93993f39…`).
  REA stays the tool for single questions with an evidence trail; the index is for sweeps over the whole exe.

## Driver DLLs = the platform boundary
The engine keeps hardware access in swappable DLLs (`DLL/`), selected via Ubi.ini. These are what the port replaces.
- **Renderer, `GliDX6vr.dll`:** 48 `GLI_DRV_*` exports over DDRAW/D3D6.
  - Device/mode: `xInitDriver`, `hChangeMode`, `lEnumerateDisplayMode`, `vFlipDevice`, `vClearDevice`.
  - Textures: `vDownLoadTextures`, `vChangeOneTexture`, `lGetHardwareMaxTextureSize`.
  - Drawing: `xSendElementTIToClip_TRIANGLES`, `xSendListToViewport`, `vSendSpriteToClip(WithUV)`, `vSendSingleLineToClip`, `vAddBlackPolygon`, `vComputeFogEffect`, `vSetZClip`, `vSetClipWindow`, Z-buffer enable/disable, and `vWrite16b/24bBitmapToBackBuffer` (loading screens).
  - The engine passes shared state in through `lSetCommonData` / `lSetCommonFct`.
  - **This is the surface the Metal backend must reproduce.**
- **Sound:** per-format backends sharing one `SND_fn_*Sxd*` interface.
  - `WAV*` samples (variants: d3 = DirectSound3D, MW/PW = MMX/Pentium wave mixers).
  - `APM*` Ubisoft ADPCM.
  - `MPG*` **MPEG music**. Music is streamed MPEG, *not* Red Book CD audio, so the CD-audio code is only the protection check.
  - `MnuSND*` sound-options menu.
  - `SNDdbgV` debug "supervisor".
  - B/S VR variants are likely builds with/without extra features (TBD).

## Anchors found so far
- `GAM_fn_vStartUp` 0x40EE40
- `GAM_fn_vLoadFix` 0x40D9E0 (fix.ptx / Fix.snd / Fix.vgm)
- `GAM_fn_vReadGameDsb` 0x45B6D0

## Unassigned regions (no asserts)
- 0x4971D0–0x4B7CF6 (201 funcs, 105 KB): after AI; probably AI conditions/procedures/functions tables.
- 0x45FD70–0x471F40 (197): between CMP and FIL.
- 0x4C12D6–0x4CD3F0 (177): before FON; unaligned starts suggest CRT/hand-written asm, but it references "Normal", "Parachute".
- 0x4010B0–0x4021A0 (47): threads/events (DRAWSEM, EXEFRAMEEVT, EXITEVT): the main-loop threading scaffolding.
