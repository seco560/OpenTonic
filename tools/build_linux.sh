#!/bin/sh
# Linux counterpart of tools/build_all.sh: generates files/out/ from your own copy of the game (docs/BUILD.md steps 3-6)
# and builds the SDL3 frontend (docs/SDL3.md). Textures and loading screens, levels (.ttlv + .ttai), music, the intro
# movie (as intro.mpg for the SDL build, tools/make_intro.sh mpg), the icon.
# Outputs that already exist are skipped, so a re-run only does what is missing; --force redoes everything.
# Usage (from the repo root): tools/build_linux.sh [--force] [--check] [--no-build]
#   --check     also run the core's headless checks afterwards (make -C core check mechcheck)
#   --no-build  convert only, don't build sdl/
# Needs python3; music needs vgmstream-cli (PATH or tools/bin/, see docs/SDL3.md), the icon ffmpeg, the intro both.
# All three optional.
# Never commit or share files/out/: it holds converted Ubisoft assets.
set -e
FORCE=0
CHECK=0
BUILD=1
for a in "$@"; do
  case "$a" in
    --force) FORCE=1 ;;
    --check) CHECK=1 ;;
    --no-build) BUILD=0 ;;
    *) echo "usage: tools/build_linux.sh [--force] [--check] [--no-build]" >&2; exit 2 ;;
  esac
done

SRC=files/sources
OUT=files/out
LEVELS="$SRC/GAMEDATA/WORLD/LEVELS"
SOUND="$SRC/GAMEDATA/WORLD/SOUND"
PATH="$PATH:$(pwd)/tools/bin"

# ---- prerequisites ----
fail=0
[ -e tools/ttre/__main__.py ] || { echo "run this from the repository root" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "missing: python3" >&2; fail=1; }
for f in "$LEVELS" "$SOUND" "$SRC/GAMEDATA/Textures.cnt" "$SRC/GAMEDATA/Vignette.cnt"; do
  [ -e "$f" ] || { echo "missing: $f (copy the game's GAMEDATA folder to $SRC/GAMEDATA, see docs/BUILD.md step 2)" >&2; fail=1; }
done
[ "$fail" = 0 ] || exit 1

need() { [ "$FORCE" = 1 ] || [ ! -e "$1" ]; }  # true when $1 has to be (re)made

# ---- 3. textures and loading screens ----
if need "$OUT/Textures"; then
  echo "== textures"
  python3 -m tools.ttre cnt-extract "$SRC/GAMEDATA/Textures.cnt" "$OUT/Textures" --png
else echo "== textures: present (skip)"; fi
if need "$OUT/Vignette"; then
  echo "== loading screens"
  python3 -m tools.ttre cnt-extract "$SRC/GAMEDATA/Vignette.cnt" "$OUT/Vignette" --png
else echo "== loading screens: present (skip)"; fi

# ---- 4. levels (.ttlv + .ttai from the same run) ----
mkdir -p "$OUT/ttlv"
missing_tex=""
for l in $(ls "$LEVELS" | grep -v '\.'); do
  if need "$OUT/ttlv/$l.ttlv" || need "$OUT/ttlv/$l.ttai"; then
    line=$(python3 -m tools.ttre level-ttlv "$l" "$OUT/ttlv/$l.ttlv" 2>/dev/null | grep '^wrote' || true)
    case "$line" in
      *" 0 not found"*) ;;
      "") echo "level $l: export failed (run: python3 -m tools.ttre level-ttlv $l $OUT/ttlv/$l.ttlv)" >&2; exit 1 ;;
      *) missing_tex="$missing_tex $l" ;;
    esac
    python3 -m tools.ttre level-ttai "$l" "$OUT/ttlv/$l.ttai" >/dev/null 2>&1 || echo "level $l: no Ed AI (simple controller only)"
    echo "== level $l"
  fi
done
[ -z "$missing_tex" ] || echo "warning: textures not found in:$missing_tex (were the textures extracted first? letter case?)" >&2

# ---- 5. music ----
if ! command -v vgmstream-cli >/dev/null 2>&1; then
  echo "== music: vgmstream-cli not found (optional, skip; see docs/SDL3.md)"
elif need "$OUT/audio/music.tsv"; then
  echo "== music"
  mkdir -p "$OUT/audio/music"
  grep -v '^#' tools/music.tsv | cut -f2 | sort -u | grep . | while read -r t; do
    b=${t%.wav}; bank=${b%_*}; sub=${b##*_}
    # The table spells banks Bnk_<n>; the install mixes case (Bnk_0.bnm, BNK_200.BNM), so match case-insensitively.
    src=$(find "$SOUND" -maxdepth 1 -iname "$bank.bnm" | head -n 1)
    if [ -z "$src" ]; then echo "music: no $bank.bnm in $SOUND" >&2; continue; fi
    vgmstream-cli -l 1 -f 0 -s "$sub" -o "$OUT/audio/music/$t" "$src" >/dev/null
  done
  cp tools/music.tsv "$OUT/audio/"
else echo "== music: present (skip)"; fi

# ---- 5b. intro movie ----
if [ ! -e "$SRC/Videos/EDPROL.AVI" ]; then echo "== intro movie: $SRC/Videos/EDPROL.AVI not found (optional, skip)"
elif ! command -v ffmpeg >/dev/null 2>&1 || ! command -v vgmstream-cli >/dev/null 2>&1; then
  echo "== intro movie: needs ffmpeg and vgmstream-cli (optional, skip)"
elif need "$OUT/video/intro.mpg"; then echo "== intro movie (a few minutes)"; tools/make_intro.sh mpg
else echo "== intro movie: present (skip)"; fi

# ---- 6. app icon ----
if [ ! -e "$SRC/icon_ED.ICO" ]; then echo "== icon: $SRC/icon_ED.ICO not found (optional, skip)"
elif ! command -v ffmpeg >/dev/null 2>&1; then echo "== icon: ffmpeg not found (optional, skip)"
elif need "$OUT/icon/OpenTonic.png" && need "$OUT/icon/TonicViewer.png"; then echo "== icon"; tools/make_icon.sh
else echo "== icon: present (skip)"; fi

# ---- checks ----
if [ "$CHECK" = 1 ]; then
  echo "== core checks"
  make -C core check mechcheck
fi

# ---- 5a. sound effects (Ed's: the global sound events) ----
if need "$OUT/audio/sfx/events.tsv"; then
  echo "== sound effects"
  python3 -m tools.ttre.sfx "$OUT/audio/sfx" || echo "sound effects: skipped (vgmstream-cli?)"
fi

# ---- the SDL3 frontend ----
if [ "$BUILD" = 1 ]; then
  echo "== sdl"
  make -C sdl
  echo "done: run sdl/build/opentonic, or install it in the applications menu with tools/install_linux.sh"
else
  echo "done: build with 'make -C sdl', then run sdl/build/opentonic"
fi
