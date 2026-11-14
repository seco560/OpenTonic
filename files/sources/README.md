# files/sources

Put the files from your own game copies here. `tools/convert_files.sh` reads them and writes everything the apps load
into `files/out`. Nothing in this folder is redistributed.

**Recommended:** Retail + Special Edition + the SSX Tricky disc image + the GTA III map and a GTA III car.
**Minimum:** the Retail Edition alone, or the Special Edition alone (with less to play, see below).

## Tonic Trouble (Retail Edition) — recommended

From an **installed** copy. On Windows that is `C:/UbiSoft/Ed` by default. A plain copy of the
CD's files won't work: the disc keeps them in the `tt1`-`tt4` archives, which only its installer unpacks.

| Path here | Needed for |
|---|---|
| `GAMEDATA/` (the whole folder) | Required. The levels, Ed, textures, loading screens, music and sounds. The conversion checks for `GAMEDATA/WORLD/LEVELS`, `GAMEDATA/WORLD/SOUND`, `GAMEDATA/Textures.cnt` and `GAMEDATA/Vignette.cnt`. |
| `icon_ED.ICO` | Optional: the app icon. |
| `Demos/` | Optional: the original demo recordings (the Replays button). |
| `Videos/EDPROL.AVI` | Optional: the intro movie. The disc's `Videos` folder. |

Tested with European version only, not guaranteeing full compatibility with American version; it could mostly work, but with things like the different Super Ed corporate sponsorship (popcorn instead of Nestle Crunch, the latter is way more iconic to me) there might be different names in the files which might mess things up.

## Tonic Trouble: Special Edition — optional (or on its own)

The 1998 OEM disc (build 8.5.2) as a disc image: the raw `Tt.bin` (with its `.cue`), an `.iso` or an `.img`. Any file
name, anywhere in this folder up to two folders deep. The first conversion copies its `GAMEDATA` and exe into `se/`
here; after that the image itself is no longer needed.

- **With Retail:** its levels borrow Ed (and the skier) from the retail levels of the same names, and are playable.
- **On its own:** its levels open in the Tonic Viewer but can't be played (no Ed), and there is no music, sound or
  intro: those come from the retail install.

Tested with the version hosted on Internet Archive.

## SSX Tricky — optional

Your own PS2 disc image as an `.iso` (any name, up to two folders deep). Its courses become levels ridden by the
retail skier, so this **needs the Retail Edition** (its Totalski level).

## GTA III: Liberty City and a car — optional

| Path here | What |
|---|---|
| `GLTF Imports/LCG3/LCG3.glb` | The GTA III map as one glTF binary, with its textures next to it. Becomes the level LibertyCity2001; Ed comes from the retail level `bar`, so this **needs the Retail Edition**. |
| `Vehicles/manana/manana.dff` + `manana.txd` | Ed's drivable car, placed around Liberty City. The Mazda Miata "Manana" replacement mod is the one used and tested. Any GTA III-era PC car (`.dff` + `.txd`) should work too, but **it has to be named `manana.dff` / `manana.txd`** in that folder. |

[Download link for map](https://sketchfab.com/3d-models/gtalc-2001-full-map-3dmodel-6695e98d6d534d90a23993289ee4ecbf) | [Download link for car.](https://gamemodding.com/en/gta-3/cars/92038-mazda-mx-5-miata-nb1-na-spec-1999.html)

Without the map, the car is left out too.

## Other optional extras

`tonic2_art/` - Tonic Adventure 2000's concept art. Only used by `python3 -m tools.concept_foyer`, which rebuilds Concept Art 3 as a walkable room (docs/BUILD.md). These are files I kept myself; this was a small experiment to test Claude's 3D modelling capabilities and they were fairly underwhelming, but I'm leaving them in regardless. The actual filename I used should be somewhere in the code.

## Tools the conversion needs

`python3`, plus `vgmstream-cli` (music and sounds) and `ffmpeg` (intro movie, icon). Without them, the steps that need
them are skipped. See docs/BUILD.md.
