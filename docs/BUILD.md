# Building Open Tonic from your own copy of Tonic Trouble

Everything below runs from the repository root. Nothing generated here may be committed or shared:
`files/out/` is git-ignored because it holds converted Ubisoft assets.

## 1. Requirements
- macOS 14+, Xcode or the Swift toolchain (Swift 6), Python 3.
- `brew install ffmpeg vgmstream` (music and the app icon).
- Linux (and other SDL3 platforms): the SDL3 frontend in `sdl/`, see docs/SDL3.md for its packages. The
  conversion steps below are the same; `tools/build_linux.sh` is the Linux quick route (`tools/build_all.sh` is macOS-only).

## 2. Game files
Copy the PC version's `GAMEDATA` folder (installed copy) to `files/sources/GAMEDATA`, and the game's
`icon_ED.ICO` to `files/sources/icon_ED.ICO`. The tools expect `files/sources/GAMEDATA/WORLD/LEVELS`,
`.../WORLD/SOUND`, `Textures.cnt` and `Vignette.cnt` there.

## Quick route
After steps 1 and 2:
```sh
tools/build_all.sh            # steps 3-6: everything in files/out/ (existing outputs are skipped; --force redoes all,
                              # --check also runs the core checks)
tools/build_and_package.sh    # the same, then tools/make_app.sh: OpenTonic.app in ~/Applications
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
Each line should end in `0 not found` (all textures resolved; build_all.sh checks this). Lightmapped levels (bar, death, PharmMap, pyr_fou,
SpyCarota, spynorth) also get a `<level>_lightmap.png` atlas next to their .ttlv. The format is documented at the top of
`tools/ttre/ttlv.py`; re-run this step whenever the exporter changes. The .ttai (format in `tools/ttre/ai.py`) also
writes `<level>_ai.txt` (node-indexed scripts, for debugging); .ttlv and .ttai of a
level must come from the same exporter run (the .ttai points at the .ttlv's `s<k>` state animations).

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
- `tools/make_app.sh` builds a self-contained `OpenTonic.app` into `~/Applications` (personal use only:
  it bundles the converted assets).

## 8. Checks
- `make -C core check` (or `swift run -c release CorePlayCheck files/out/ttlv`): the simple controller on every level
  (landing, wall runs without tunnelling, jump).
- `make -C core mechcheck`: the game's ground/air mechanics on hand-made geometry (no game data needed).
- `make -C core aicheck` (Ed's AI on bar; `AI_SCRIPT=fly|ski|wander`, see tests/aicheck.c) and
  `make -C core crashcheck` (Totalski: the skier crashes into a tree).

## Importing other models (optional)
Any glTF 2.0 model (`.gltf` + `.bin`, or `.glb`) becomes a level to walk around as Ed:
```sh
python3 -m tools.ttre.import_gltf path/to/Sponza.gltf Sponza [--scale 1] [--spawn x,y,z] [--ed bar]
```
It writes `files/out/ttlv/<name>.ttlv` (+ `.ttai`: Ed's AI) and the textures as PNG in `files/out/Imported/<name>/`
(JPEG and embedded images are converted with ffmpeg or, on macOS, sips). Y-up glTF becomes TT's Z-up; materials map
alphaMode MASK to alpha test and BLEND to transparent; every non-blended triangle is collision; there is no baked
lighting (the viewer's sun lights it). Ed (meshes, animations, AI) comes from `--ed`'s level, by default bar (a
cutscene level, so no portals or triggers). He starts on the highest upward-facing surface at the model's centre
(sky domes and ceilings face down, so they are skipped), or at `--spawn` (glTF coordinates: Y is up). Re-importing
under the same name overwrites the level; delete `files/out/Imported/<name>/` first if the model changed a lot, or
old PNGs stay behind. Good test levels: Sponza from Khronos' glTF sample assets, CC-licensed downloadable scenes on
Sketchfab (glTF export), Kenney / Quaternius kits (CC0); game rips only from your own copies, never redistributed.

