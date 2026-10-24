#!/bin/sh
# Everything in one go: generate files/out/ (tools/build_all.sh), then build and install OpenTonic.app
# (tools/make_app.sh). Personal use only: the app bundles converted Ubisoft assets.
# Usage (from the repo root): tools/build_and_package.sh [--force] [--check] [install dir, default ~/Applications]
set -e
GEN=""
DEST=""
for a in "$@"; do
  case "$a" in
    --force|--check) GEN="$GEN $a" ;;
    *) DEST="$a" ;;
  esac
done
# shellcheck disable=SC2086
tools/build_all.sh $GEN
if [ ! -e files/out/icon/OpenTonic.png ] && [ ! -e files/out/icon/TonicViewer.png ]; then
  echo "make_app.sh needs files/out/icon/OpenTonic.png (copy the game's icon_ED.ICO to files/sources/)" >&2
  exit 1
fi
if [ -n "$DEST" ]; then tools/make_app.sh "$DEST"; else tools/make_app.sh; fi
