#!/bin/sh
# This is a fairly dodgy self-contained build script Claude whipped up that only uses bash commands to produce
# a .app file. It forces a code sign that circumvents all the usual Apple privacy restrictions. For those reasons
# this can NOT be considered a "production release" and must thus remain on the user's machine at all times.
# This on top of the fact that you'd be redistributing copyrighted Ubisoft assets (textures and music mainly)
# if you were posting this anywhere.

# Needs: files/out/ttlv (.ttlv + .ttai), files/out/Textures (incl. Effects/: the screen transitions), files/out/Vignette
# (picker thumbnails, portal loading screens), files/out/audio (music + music.tsv; sfx/ from tools/ttre/sfx.py),
# files/out/icon (tools/make_icon.sh); optional: files/out/video/intro.mp4 (tools/make_intro.sh), files/out/Imported
# (tools/ttre/import_gltf.py).
# Usage (from the repo root): tools/make_app.sh [install dir, default ~/Applications]
set -e
DEST="${1:-$HOME/Applications}"
APP="files/out/TonicViewer.app"
swift build -c release
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/data"
cp .build/release/TonicViewer "$APP/Contents/MacOS/TonicViewer"
cp Sources/TonicViewer/Shaders.metal "$APP/Contents/Resources/"

# Data keeps the files/out/ layout, so the .ttlv files' relative texture paths (../Textures/...) still resolve.
R="$APP/Contents/Resources/data"
cp -R files/out/ttlv files/out/Textures "$R/"
rm -f "$R"/ttlv/*_ai.txt  # the AI listings level-ttai writes next to the .ttai: debugging only
# Imported models (tools/ttre/import_gltf.py): their textures, and the folder that files them under "Imported".
[ -d files/out/Imported ] && cp -R files/out/Imported "$R/"
mkdir -p "$R/Vignette" "$R/audio/music" "$R/audio/sfx" "$R/icon"
# Sound effects (tools/ttre/sfx.py): the global sound events' files and their table.
[ -d files/out/audio/sfx ] && cp files/out/audio/sfx/*.wav files/out/audio/sfx/events.tsv "$R/audio/sfx/"
cp -R files/out/Vignette/Level "$R/Vignette/"
cp files/out/icon/TonicViewer.png "$R/icon/"
# Music: WAV -> AAC (about 10x smaller), tracks already in .m4a are copied; the table is rewritten to the .m4a names.
for w in files/out/audio/music/*.wav; do
  [ -e "$w" ] || continue
  ffmpeg -v error -y -i "$w" -c:a aac -b:a 128k "$R/audio/music/$(basename "$w" .wav).m4a"
done
for m in files/out/audio/music/*.m4a; do
  [ -e "$m" ] && cp "$m" "$R/audio/music/"
done
sed 's/\.wav/.m4a/g' files/out/audio/music.tsv > "$R/audio/music.tsv"
# Intro movie (tools/make_intro.sh), if built.
if [ -e files/out/video/intro.mp4 ]; then mkdir -p "$R/video"; cp files/out/video/intro.mp4 "$R/video/"; fi

# Icon: PNG -> .icns
ICONSET="files/out/icon/TonicViewer.iconset"; rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s files/out/icon/TonicViewer.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) files/out/icon/TonicViewer.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/TonicViewer.icns"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>TonicViewer</string>
  <key>CFBundleDisplayName</key><string>Tonic Trouble Viewer</string>
  <key>CFBundleIdentifier</key><string>local.retonictrouble.viewer</string>
  <key>CFBundleExecutable</key><string>TonicViewer</string>
  <key>CFBundleIconFile</key><string>TonicViewer</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
</dict></plist>
PLIST

codesign --force --deep -s - "$APP"
mkdir -p "$DEST"
rm -rf "$DEST/TonicViewer.app"
cp -R "$APP" "$DEST/"
echo "Installed $DEST/TonicViewer.app ($(du -sh "$DEST/TonicViewer.app" | cut -f1))"
