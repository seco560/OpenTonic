#!/bin/sh
# Linux counterpart of tools/install_macos.sh: brings files/out up to date (tools/convert_files.sh: converts what is missing,
# re-exports levels a changed converter made stale), builds the SDL3 frontend and installs it for the current user
# with its data and an applications menu entry. Re-running converts, rebuilds and updates the install in place (only
# changed files are copied; files gone from files/out are removed). Like the .app, the install is full of converted
# Ubisoft assets: it must stay on this machine, never be redistributed.
#
# Needs: the game's files in files/sources/GAMEDATA (docs/BUILD.md step 2) and what tools/convert_files.sh needs, or an
# already converted files/out (without the sources the conversion is skipped); rsync; ffmpeg for the menu icon sizes
# (else the full-size icon is used).
# Installs:
#   <dest>/opentonic, <dest>/data/...   (default dest ~/.local/share/opentonic; data keeps the files/out layout, so
#                                        the .ttlv files' relative texture paths, ../Textures/..., still resolve)
#   ~/.local/share/applications/org.opentonic.sdl.desktop  (the name matches the app ID the binary gives SDL, so
#                                        the desktop pairs the window with the entry and its icon)
#   ~/.local/share/icons/hicolor/<size>/apps/org.opentonic.sdl.png
# Usage (from the repo root): tools/install_linux.sh [--force] [--no-convert] [dest]
#                              tools/install_linux.sh --uninstall [dest]
#   --force       reconvert everything (tools/convert_files.sh --force)
#   --no-convert  install files/out as it is
set -e
[ -e tools/ttre/__main__.py ] || { echo "run this from the repository root" >&2; exit 1; }
ID=org.opentonic.sdl
SHARE="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS="$SHARE/applications"
ICONS="$SHARE/icons/hicolor"
SIZES="48 64 128 256 512"

refresh_menu() {
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$APPS" || true
  command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -f -t "$ICONS" || true
}

FORCE=""
CONVERT=1
UNINSTALL=0
DEST=""
for a in "$@"; do
  case "$a" in
    --uninstall) UNINSTALL=1 ;;
    --force) FORCE=--force ;;
    --no-convert) CONVERT=0 ;;
    -*) echo "usage: tools/install_linux.sh [--force] [--no-convert] [dest] | --uninstall [dest]" >&2; exit 2 ;;
    *) DEST="$a" ;;
  esac
done
DEST="${DEST:-$SHARE/opentonic}"

if [ "$UNINSTALL" = 1 ]; then
  rm -rf "$DEST"
  rm -f "$APPS/$ID.desktop"
  for s in $SIZES; do rm -f "$ICONS/${s}x$s/apps/$ID.png"; done
  refresh_menu
  echo "Removed $DEST and the menu entry"
  exit 0
fi

OUT=files/out
if [ "$CONVERT" = 1 ] && [ -d files/sources/GAMEDATA ]; then
  echo "== convert (tools/convert_files.sh)"
  tools/convert_files.sh $FORCE
elif [ "$CONVERT" = 1 ]; then
  echo "== convert: no files/sources/GAMEDATA, installing $OUT as it is"
fi
[ -d "$OUT/ttlv" ] || { echo "no $OUT/ttlv: convert the game first (tools/convert_files.sh)" >&2; exit 1; }
command -v rsync >/dev/null 2>&1 || { echo "missing: rsync (sudo apt install rsync)" >&2; exit 1; }
# The icon: files/out/icon/OpenTonic.png (tools/make_icon.sh), or the one made before the rename.
ICON="$OUT/icon/OpenTonic.png"; [ -e "$ICON" ] || ICON="$OUT/icon/TonicViewer.png"

echo "== sdl"
make -C sdl

echo "== install to $DEST"
D="$DEST/data"
mkdir -p "$D/Vignette" "$D/audio" "$D/icon"
SYNC="rsync -a --delete"
install -m 755 sdl/build/opentonic "$DEST/opentonic"
$SYNC --exclude '*_ai.txt' "$OUT/ttlv/" "$D/ttlv/"  # the AI listings next to the .ttai: debugging only
$SYNC "$OUT/Textures/" "$D/Textures/"
[ -d "$OUT/Vignette/Level" ] && $SYNC "$OUT/Vignette/Level/" "$D/Vignette/Level/"
# Imported models (tools/ttre/import_gltf.py): their textures, and the folder that files them under "Imported".
if [ -d "$OUT/Imported" ]; then $SYNC "$OUT/Imported/" "$D/Imported/"; else rm -rf "$D/Imported"; fi
# Music stays WAV (the SDL build streams WAV only) with its table; sound effects (tools/ttre/sfx.py) as they are.
[ -d "$OUT/audio/music" ] && $SYNC "$OUT/audio/music/" "$D/audio/music/"
[ -e "$OUT/audio/music.tsv" ] && cp "$OUT/audio/music.tsv" "$D/audio/"
[ -d "$OUT/audio/sfx" ] && $SYNC "$OUT/audio/sfx/" "$D/audio/sfx/"
# Intro movie for the SDL build (tools/make_intro.sh mpg), if built.
if [ -e "$OUT/video/intro.mpg" ]; then
  mkdir -p "$D/video"
  rsync -a "$OUT/video/intro.mpg" "$D/video/"
fi
[ -e "$ICON" ] && cp "$ICON" "$D/icon/OpenTonic.png"

echo "== menu entry"
ICON_NAME="$ID"
if [ -e "$ICON" ] && command -v ffmpeg >/dev/null 2>&1; then
  for s in $SIZES; do
    mkdir -p "$ICONS/${s}x$s/apps"
    ffmpeg -v error -y -i "$ICON" -vf "scale=$s:$s:flags=lanczos" "$ICONS/${s}x$s/apps/$ID.png"
  done
elif [ -e "$ICON" ]; then
  ICON_NAME="$D/icon/OpenTonic.png"  # no ffmpeg: the full-size icon by path
fi
mkdir -p "$APPS"
cat > "$APPS/$ID.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Open Tonic
GenericName=Tonic Trouble
Comment=Open source engine reimplementation for Tonic Trouble
Exec="$DEST/opentonic"
Path=$DEST
Icon=$ICON_NAME
Terminal=false
Categories=Game;ActionGame;
StartupWMClass=$ID
DESKTOP
command -v desktop-file-validate >/dev/null 2>&1 && desktop-file-validate "$APPS/$ID.desktop" || true
refresh_menu
echo "Installed $DEST ($(du -sh "$DEST" | cut -f1)); launch Open Tonic from the applications menu"
