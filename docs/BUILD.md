# Building Open Tonic from your own copy of Tonic Trouble

Everything below runs from the repository root. Nothing generated here may be committed or shared:
`files/out/` is git-ignored because it holds converted Ubisoft assets.

## 1. Requirements
- macOS 14+, Xcode or the Swift toolchain (Swift 6), Python 3.
- `brew install ffmpeg vgmstream` (music and the app icon).
- Linux (and other SDL3 platforms): the SDL3 frontend in `sdl/`, see docs/SDL3.md for its packages. The
  conversion steps below are the same, and so is the quick route (`tools/convert_files.sh` converts the same way on
  both; `tools/install_linux.sh` builds and installs the SDL build).

## 2. Game files
Copy the PC version's `GAMEDATA` folder (installed copy) to `files/sources/GAMEDATA`, and the game's
`icon_ED.ICO` to `files/sources/icon_ED.ICO`. The tools expect `files/sources/GAMEDATA/WORLD/LEVELS`,
`.../WORLD/SOUND`, `Textures.cnt` and `Vignette.cnt` there.

## Quick route
After steps 1 and 2:
```sh
tools/convert_files.sh        # steps 3-6: everything in files/out/ (only what is missing or stale; --force redoes all,
                              # --check also runs the core checks); builds nothing
tools/install_macos.sh        # the same, then builds and installs OpenTonic.app in ~/Applications (--force reconverts
                              # everything, --no-convert bundles files/out as it is, --uninstall removes the app)
tools/install_linux.sh        # Linux: the same, then builds and installs the SDL build (same flags; docs/SDL3.md)
```
The steps below are what these scripts run, for doing parts by hand.

## 3. Textures and loading screens
```sh
python3 -m tools.ttre cnt-extract files/sources/GAMEDATA/Textures.cnt files/out/Textures --png
python3 -m tools.ttre cnt-extract files/sources/GAMEDATA/Vignette.cnt files/out/Vignette --png
```
Textures must exist before the levels are converted (the level files reference them by relative path).

## 4. Levels
```sh
mkdir -p files/out/ttlv
for l in $(ls files/sources/GAMEDATA/WORLD/LEVELS | grep -v '\.'); do
  python3 -m tools.ttre level-ttlv "$l" "files/out/ttlv/$l.ttlv"
  python3 -m tools.ttre level-ttai "$l" "files/out/ttlv/$l.ttai"   # Ed's AI (optional: without it, the simple controller)
done
```
Each line should end in `0 not found` (all textures resolved; convert_files.sh checks this). Lightmapped levels (bar, death, PharmMap, pyr_fou,
SpyCarota, spynorth) also get a `<level>_lightmap.png` atlas next to their .ttlv. The .ttai also writes
`<level>_ai.txt` (node-indexed scripts, for debugging). .ttlv and .ttai of a level must come from the same exporter
run (the .ttai points at the .ttlv's `s<k>` state animations).

**Formats.** Both files are chunk containers (`tools/ttre/chunks.py`; chunk lists at the top of `tools/ttre/ttlv.py` and
`tools/ttre/ai.py`). There are no version numbers. The files are build outputs, so a reader asks for the chunks it needs
and refuses a file that lacks one or whose layout differs: "made by another version of the converter: re-run the
conversion". Every file carries the converter's stamp, a hash of the converter's code (`tools/ttre/__main__.py` and the
modules it imports, comments and docstrings aside, so editing those changes nothing). The other outputs' stamps are
recorded in `files/out/.stamps`: textures and loading screens (cnt.py, gf.py), sound effects (sfx.py), the intro and
the icon (their scripts) are redone when that code changes; music tracks are converted when missing, and
`files/out/audio/music.tsv` (yours to edit) is replaced only when `tools/music.tsv` changes, the old one kept as
`music.tsv.bak`. The build scripts re-export the
levels whose stamp differs from the current sources, so after pulling exporter changes a plain `tools/convert_files.sh`
(or either install script) is enough. `python3 -m tools.ttre.chunks files/out/ttlv/*.ttlv` lists each file's stamp
(current or stale) and chunks. The app bundle carries its own copy of `files/out/ttlv`:
`tools/install_macos.sh` re-exports stale levels itself before bundling (and warns about stale ones with `--no-convert`
or without the game's files).

## 5. Music (optional)
`tools/music.tsv` maps levels to tracks named `Bnk_<bank>_<subsong>.wav` (one loop each):
```sh
mkdir -p files/out/audio/music
grep -v '^#' tools/music.tsv | cut -f2 | sort -u | grep . | while read t; do
  b=${t%.wav}; bank=${b%_*}; sub=${b##*_}
  vgmstream-cli -l 1 -f 0 -s "$sub" -o "files/out/audio/music/$t" "files/sources/GAMEDATA/WORLD/SOUND/$bank.bnm"
done
cp tools/music.tsv files/out/audio/
```
(Converting to .m4a is optional; if you do, rename the entries in `files/out/audio/music.tsv` too.)

Sound effects (Ed's, the game's global sound events; docs/NOTES.md "Sound events"): `python3 -m tools.ttre.sfx`
writes `files/out/audio/sfx/*.wav` and `events.tsv` (about 15 s, needs vgmstream-cli). The .ttai files (step 4) carry
the animation sound events.

## 5b. Intro movie (optional)
Copy the disc's `Videos` folder to `files/sources/Videos`, then `tools/make_intro.sh` (about a minute): the
400x240 `EDPROL.AVI`, deblocked and upscaled to 1800x1080, with its soundtrack from sound bank 200
(`English/BNK_200.BNM` -> `USDOLBY.APM`) muxed in, as `files/out/video/intro.mp4`. Press V on the level select
screen to play it (Esc skips). The SDL build plays `files/out/video/intro.mpg` instead (MPEG-1 + MP2 at 800x480, for
pl_mpeg): `tools/make_intro.sh mpg`, the default off macOS.

## 6. App icon (optional)
`tools/make_icon.sh` (writes `files/out/icon/OpenTonic.png`).

## 7. Run
- Linux: `make -C sdl`, then `sdl/build/opentonic` (optionally a levels folder or a .ttlv; docs/SDL3.md).
- `swift run -c release OpenTonic`, or open `Package.swift` in Xcode and run the OpenTonic scheme.
  The viewer finds `files/out/ttlv` by itself (working directory, or the checkout it was built from), so no
  copying into DerivedData is needed. A folder or .ttlv can also be passed as the first argument.
- `tools/install_macos.sh` brings files/out up to date and builds a self-contained `OpenTonic.app` into
  `~/Applications` (personal use only: it bundles the converted assets). Re-running only re-encodes changed music.

## 8. Checks
- `make -C core check` (or `swift run -c release CorePlayCheck files/out/ttlv`): the simple controller on every level
  (landing, wall runs without tunnelling, jump).
- `make -C core mechcheck`: the game's ground/air mechanics on hand-made geometry (no game data needed).
- `make -C core aicheck` (Ed's AI on bar; `AI_SCRIPT=fly|ski|wander`, see tests/aicheck.c) and
  `make -C core crashcheck` (Totalski: the skier crashes into a tree). `AI_PATH=<file>` logs every frame's position.
- Oracle tests (the exe's own functions under an emulator against the core's ports; needs the unpacked exe, docs/EXE.md):
  `python3 -m venv tools/bin/oracle-venv && tools/bin/oracle-venv/bin/pip install unicorn` once, then
  `tools/bin/oracle-venv/bin/python -m tools.exe.oracle` (docs/DECOMP.md).

## Importing other models (optional)
Any glTF 2.0 model (`.gltf` + `.bin`, or `.glb`) becomes a level to walk around as Ed:
```sh
python3 -m tools.ttre.import_gltf path/to/Sponza.gltf Sponza [--scale 1] [--spawn x,y,z] [--ed bar]
```
It writes `files/out/ttlv/<name>.ttlv` (+ `.ttai`: Ed's AI) and the textures as PNG in `files/out/Imported/<name>/`
(JPEG and embedded images are converted with ffmpeg or, on macOS, sips). Y-up glTF becomes TT's Z-up; materials map
alphaMode MASK to alpha test and BLEND to transparent; every non-blended triangle is collision. Light: `COLOR_0`
becomes baked light (1.0 = the texture unchanged), `KHR_materials_unlit` without it = 1.0 everywhere; without either
the viewer's sun lights it. Ed (meshes, animations, AI) comes from `--ed`'s level, by default bar (a
cutscene level, so no portals or triggers). He starts on the highest upward-facing surface at the model's centre
(sky domes and ceilings face down, so they are skipped), or at `--spawn` (glTF coordinates: Y is up). Re-importing
under the same name overwrites the level; delete `files/out/Imported/<name>/` first if the model changed a lot, or
old PNGs stay behind. Imported levels are not part of the game data, so the build scripts never re-export them; after
a format change, re-run their import. Good test levels: Sponza from Khronos' glTF sample assets, CC-licensed downloadable scenes on
Sketchfab (glTF export), Kenney / Quaternius kits (CC0); game rips only from your own copies, never redistributed.

`python3 -m tools.concept_foyer` (needs numpy) rebuilds Tonic Adventure 2000's Concept Art 3 (the hall with the
grandfather clock and the curved staircase, from `files/sources/tonic2_art/`) as a walkable room: a camera fitted
to the painting's floor tiles, geometry laid out from it, textures projected from the painting itself. It writes
`files/out/gltf/ConceptFoyer/` and imports it as the level ConceptFoyer (Imported). How it works: the script's
docstring.

