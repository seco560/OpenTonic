#!/bin/sh
# This is a fairly dodgy self-contained build script Claude whipped up that only uses bash commands to produce
# a .app file. It forces a code sign that circumvents all the usual Apple privacy restrictions. For those reasons
# this can NOT be considered a "production release" and must thus remain on the user's machine at all times.
# This on top of the fact that you'd be redistributing copyrighted Ubisoft assets (textures and music mainly)
# if you were posting this anywhere.
#
# macOS counterpart of tools/install_linux.sh: brings files/out up to date (tools/convert_files.sh: converts what is
# missing, re-exports levels a changed converter made stale), builds the Swift app and installs OpenTonic.app with its
# data. Re-running converts, rebuilds and updates the app (music is only re-encoded when its WAV changed; files gone
# from files/out are removed from the bundle).
#
# Needs: the game's files in files/sources/GAMEDATA (docs/BUILD.md step 2) and what tools/convert_files.sh needs, or an
# already converted files/out (without the sources the conversion is skipped); the Swift toolchain; ffmpeg when
# there is WAV music (encoded to AAC for the bundle). Bundles, from files/out: ttlv (.ttlv + .ttai), Textures (incl.
# Effects/: the screen transitions), Vignette/Level (picker thumbnails, portal loading screens), audio (music +
# music.tsv, sfx/), icon; optional: video/intro.mp4 (tools/make_intro.sh), Imported (tools/ttre/import_gltf.py).
# Usage (from the repo root): tools/install_macos.sh [--force] [--no-convert] [install dir, default ~/Applications]
#                              tools/install_macos.sh --uninstall [install dir]
#   --force       reconvert everything (tools/convert_files.sh --force)
#   --no-convert  bundle files/out as it is
set -e
[ -e tools/ttre/__main__.py ] || { echo "run this from the repository root" >&2; exit 1; }

FORCE=""
CONVERT=1
UNINSTALL=0
DEST=""
for a in "$@"; do
  case "$a" in
    --uninstall) UNINSTALL=1 ;;
    --force) FORCE=--force ;;
    --no-convert) CONVERT=0 ;;
    -*) echo "usage: tools/install_macos.sh [--force] [--no-convert] [dest] | --uninstall [dest]" >&2; exit 2 ;;
    *) DEST="$a" ;;
  esac
done
DEST="${DEST:-$HOME/Applications}"

if [ "$UNINSTALL" = 1 ]; then
  rm -rf "$DEST/OpenTonic.app"
  echo "Removed $DEST/OpenTonic.app"
  exit 0
fi

OUT=files/out
CONVERTED=0
if [ "$CONVERT" = 1 ] && [ -d files/sources/GAMEDATA ]; then
  echo "== convert (tools/convert_files.sh)"
  tools/convert_files.sh $FORCE
  CONVERTED=1
elif [ "$CONVERT" = 1 ]; then
  echo "== convert: no files/sources/GAMEDATA, bundling $OUT as it is"
fi
[ -d "$OUT/ttlv" ] || { echo "no $OUT/ttlv: convert the game first (tools/convert_files.sh)" >&2; exit 1; }
# Levels from another converter version may not load ("re-run the conversion") or load outdated data: without a
# conversion, say which.
if [ "$CONVERTED" = 0 ]; then
  STAMP=$(python3 -m tools.ttre.chunks)
  stale=""
  for f in "$OUT"/ttlv/*.ttlv "$OUT"/ttlv/*.ttai; do
    [ -e "$f" ] || continue
    s=$(python3 -c 'import sys; from tools.ttre.chunks import file_stamp; print(file_stamp(sys.argv[1]))' "$f")
    [ "$s" = "$STAMP" ] || stale="$stale $(basename "$f")"
  done
  [ -z "$stale" ] || echo "warning: made by another converter version (may not load, or show outdated data):$stale" >&2
fi
if ls "$OUT"/audio/music/*.wav >/dev/null 2>&1 && ! command -v ffmpeg >/dev/null 2>&1; then
  echo "missing: ffmpeg (brew install ffmpeg), to encode the music" >&2; exit 1
fi
# The icon: files/out/icon/OpenTonic.png (tools/make_icon.sh), or the one made before the rename.
ICON="$OUT/icon/OpenTonic.png"; [ -e "$ICON" ] || ICON="$OUT/icon/TonicViewer.png"

echo "== swift"
swift build -c release

# The bundle is staged in files/out and kept between runs, so only what changed is copied or re-encoded.
echo "== bundle"
APP="$OUT/OpenTonic.app"
R="$APP/Contents/Resources/data"
SYNC="rsync -a --delete"
mkdir -p "$APP/Contents/MacOS" "$R/Vignette" "$R/audio/music" "$R/icon"
cp .build/release/OpenTonic "$APP/Contents/MacOS/OpenTonic"
cp Sources/OpenTonic/Shaders.metal "$APP/Contents/Resources/"
# Data keeps the files/out/ layout, so the .ttlv files' relative texture paths (../Textures/...) still resolve.
$SYNC --exclude '*_ai.txt' "$OUT/ttlv/" "$R/ttlv/"  # the AI listings level-ttai writes next to the .ttai: debugging only
$SYNC "$OUT/Textures/" "$R/Textures/"
if [ -d "$OUT/Vignette/Level" ]; then $SYNC "$OUT/Vignette/Level/" "$R/Vignette/Level/"; else rm -rf "$R/Vignette/Level"; fi
# Imported models (tools/ttre/import_gltf.py): their textures, and the folder that files them under "Imported".
if [ -d "$OUT/Imported" ]; then $SYNC "$OUT/Imported/" "$R/Imported/"; else rm -rf "$R/Imported"; fi
# Sound effects (tools/ttre/sfx.py): the global sound events' files and their table.
if [ -d "$OUT/audio/sfx" ]; then $SYNC "$OUT/audio/sfx/" "$R/audio/sfx/"; else rm -rf "$R/audio/sfx"; fi
[ -e "$ICON" ] && cp "$ICON" "$R/icon/OpenTonic.png"
# Music: WAV -> AAC (about 10x smaller), only when the WAV is newer than the bundle's .m4a; tracks already in .m4a are
# copied; .m4a files whose source is gone are removed. The table is rewritten to the .m4a names.
for w in "$OUT"/audio/music/*.wav; do
  [ -e "$w" ] || continue
  m="$R/audio/music/$(basename "$w" .wav).m4a"
  if [ ! -e "$m" ] || [ "$w" -nt "$m" ]; then
    echo "   music $(basename "$w")"
    ffmpeg -v error -y -i "$w" -c:a aac -b:a 128k "$m"
  fi
done
for m in "$OUT"/audio/music/*.m4a; do
  [ -e "$m" ] && cp "$m" "$R/audio/music/"
done
for m in "$R"/audio/music/*.m4a; do
  [ -e "$m" ] || continue
  b=$(basename "$m" .m4a)
  [ -e "$OUT/audio/music/$b.wav" ] || [ -e "$OUT/audio/music/$b.m4a" ] || rm -f "$m"
done
if [ -e "$OUT/audio/music.tsv" ]; then sed 's/\.wav/.m4a/g' "$OUT/audio/music.tsv" > "$R/audio/music.tsv"; else rm -f "$R/audio/music.tsv"; fi
# Intro movie (tools/make_intro.sh), if built.
if [ -e "$OUT/video/intro.mp4" ]; then mkdir -p "$R/video"; cp "$OUT/video/intro.mp4" "$R/video/"; else rm -rf "$R/video"; fi

# Icon: PNG -> .icns
if [ -e "$ICON" ]; then
  ICONSET="$OUT/icon/OpenTonic.iconset"; rm -rf "$ICONSET"; mkdir -p "$ICONSET"
  for s in 16 32 128 256 512; do
    sips -z $s $s "$ICON" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) "$ICON" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/OpenTonic.icns"
else
  echo "== icon: none (tools/make_icon.sh), the app gets the generic one"
  rm -f "$APP/Contents/Resources/OpenTonic.icns"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Open Tonic</string>
  <key>CFBundleDisplayName</key><string>Open Tonic</string>
  <key>CFBundleIdentifier</key><string>local.opentonic</string>
  <key>CFBundleExecutable</key><string>OpenTonic</string>
  <key>CFBundleIconFile</key><string>OpenTonic</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
</dict></plist>
PLIST

codesign --force --deep -s - "$APP"

echo "== install to $DEST"
mkdir -p "$DEST"
# A fresh copy rather than an update in place: macOS caches a signed binary's signature by file, so overwriting the
# installed one can get the new build killed at launch.
rm -rf "$DEST/OpenTonic.app"
ditto "$APP" "$DEST/OpenTonic.app"
echo "Installed $DEST/OpenTonic.app ($(du -sh "$DEST/OpenTonic.app" | cut -f1))"
