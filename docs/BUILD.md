# Building TonicViewer from your own copy of Tonic Trouble

Everything below runs from the repository root. Nothing generated here may be committed or shared:
`files/out/` is git-ignored because it holds converted Ubisoft assets.

## 1. Requirements
- macOS 14+, Xcode or the Swift toolchain (Swift 6), Python 3.
- `brew install ffmpeg vgmstream` (music and the app icon).

## 2. Game files
Copy the PC version's `GAMEDATA` folder (installed copy) to `files/sources/GAMEDATA`, and the game's
`icon_ED.ICO` to `files/sources/icon_ED.ICO`. The tools expect `files/sources/GAMEDATA/WORLD/LEVELS`,
`.../WORLD/SOUND`, `Textures.cnt` and `Vignette.cnt` there.

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
Each line should end in `0 not found` (all textures resolved). Lightmapped levels (bar, death, PharmMap, pyr_fou,
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

## 5b. Intro movie (optional)
Copy the disc's `Videos` folder to `files/sources/Videos`, then `tools/make_intro.sh` (about a minute): the
400x240 `EDPROL.AVI`, deblocked and upscaled to 1800x1080, with its soundtrack from sound bank 200
(`English/BNK_200.BNM` -> `USDOLBY.APM`) muxed in, as `files/out/video/intro.mp4`. Press V on the level select
screen to play it (Esc skips).

## 6. App icon (optional)
`tools/make_icon.sh` (writes `files/out/icon/TonicViewer.png`).

## 7. Run
- `swift run -c release TonicViewer`, or open `Package.swift` in Xcode and run the TonicViewer scheme.
  The viewer finds `files/out/ttlv` by itself (working directory, or the checkout it was built from), so no
  copying into DerivedData is needed. A folder or .ttlv can also be passed as the first argument.
- `tools/make_app.sh` builds a self-contained `TonicViewer.app` into `~/Applications` (personal use only:
  it bundles the converted assets).

## 8. Checks
`make -C core check` (or `swift run -c release CorePlayCheck files/out/ttlv`): headless controller tests on
every level (landing, wall runs without tunnelling, jump).
