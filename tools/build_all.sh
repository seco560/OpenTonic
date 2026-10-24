#!/bin/sh
# Generates everything TonicViewer needs in files/out/ from your own copy of the game (docs/BUILD.md steps 3-6):
# textures and loading screens, levels (.ttlv + .ttai), music, the intro movie and the app icon.
# Outputs that already exist are skipped, so a re-run only does what is missing; --force redoes everything.
# Usage (from the repo root): tools/build_all.sh [--force] [--check]
#   --check   also run the core's headless checks afterwards (make -C core check mechcheck)
# Never commit or share files/out/: it holds converted Ubisoft assets.
set -e
FORCE=0
CHECK=0
for a in "$@"; do
  case "$a" in
    --force) FORCE=1 ;;
    --check) CHECK=1 ;;
    *) echo "usage: tools/build_all.sh [--force] [--check]" >&2; exit 2 ;;
  esac
done

SRC=files/sources
OUT=files/out
LEVELS="$SRC/GAMEDATA/WORLD/LEVELS"

# ---- prerequisites ----
fail=0
for t in python3 swift ffmpeg vgmstream-cli; do
  command -v "$t" >/dev/null 2>&1 || { echo "missing: $t (see docs/BUILD.md, step 1)" >&2; fail=1; }
done
for f in "$LEVELS" "$SRC/GAMEDATA/WORLD/SOUND" "$SRC/GAMEDATA/Textures.cnt" "$SRC/GAMEDATA/Vignette.cnt"; do
  [ -e "$f" ] || { echo "missing: $f (copy the game's GAMEDATA folder to $SRC/GAMEDATA, see docs/BUILD.md step 2)" >&2; fail=1; }
done
[ -e tools/ttre/__main__.py ] || { echo "run this from the repository root" >&2; fail=1; }
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
      "") echo "level $l: export failed" >&2; exit 1 ;;
      *) missing_tex="$missing_tex $l" ;;
    esac
    python3 -m tools.ttre level-ttai "$l" "$OUT/ttlv/$l.ttai" >/dev/null 2>&1 || echo "level $l: no Ed AI (simple controller only)"
    echo "== level $l"
  fi
done
[ -z "$missing_tex" ] || echo "warning: textures not found in:$missing_tex (were the textures extracted first?)" >&2

# ---- 5. music ----
if need "$OUT/audio/music.tsv"; then
  echo "== music"
  mkdir -p "$OUT/audio/music"
  grep -v '^#' tools/music.tsv | cut -f2 | sort -u | grep . | while read -r t; do
    b=${t%.wav}; bank=${b%_*}; sub=${b##*_}
    vgmstream-cli -l 1 -f 0 -s "$sub" -o "$OUT/audio/music/$t" "$SRC/GAMEDATA/WORLD/SOUND/$bank.bnm" >/dev/null
  done
  cp tools/music.tsv "$OUT/audio/"
else echo "== music: present (skip)"; fi

# ---- 5b. intro movie (needs the disc's Videos folder) ----
if [ -d "$SRC/Videos" ]; then
  if need "$OUT/video/intro.mp4"; then echo "== intro movie"; tools/make_intro.sh; else echo "== intro movie: present (skip)"; fi
else echo "== intro movie: $SRC/Videos not found (optional, skip)"; fi

# ---- 6. app icon ----
if [ -e "$SRC/icon_ED.ICO" ]; then
  if need "$OUT/icon/TonicViewer.png"; then echo "== icon"; tools/make_icon.sh; else echo "== icon: present (skip)"; fi
else echo "== icon: $SRC/icon_ED.ICO not found (optional, skip)"; fi

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
echo "done: run with 'swift run -c release TonicViewer', or package with tools/make_app.sh"
