#!/bin/sh
# Converts your own copy of the game into everything Open Tonic needs in files/out/ (docs/BUILD.md steps 3-6):
# textures and loading screens, levels (.ttlv + .ttai), music, sound effects, the intro movie and the app icon.
# macOS and Linux alike; only the intro's encoding differs:
#   macOS:  video/intro.mp4 (HEVC, for the Swift app's AVFoundation)
#   other:  video/intro.mpg (MPEG-1, for the SDL build's pl_mpeg)
# A re-run only redoes what is missing or was made by code that has changed since: levels by the converter stamp they
# carry, the other steps by the stamp of their code recorded in files/out/.stamps (comments don't count, so editing
# them redoes nothing). --force redoes everything. Builds nothing: tools/install_macos.sh and tools/install_linux.sh
# run this, then build and install the app (or run it from the repo: swift run -c release OpenTonic / make -C sdl).
# Usage (from the repo root): tools/convert_files.sh [--force] [--check]
#   --check     also run the core's headless checks afterwards (make -C core check mechcheck)
# Needs python3; music and sound effects need vgmstream-cli (PATH or tools/bin/; macOS: brew install vgmstream; Linux:
# the official prebuilt, see docs/SDL3.md), the icon ffmpeg, the intro both. All optional.
# Never commit or share files/out/: it holds converted Ubisoft assets.
set -e
FORCE=0
CHECK=0
for a in "$@"; do
  case "$a" in
    --force) FORCE=1 ;;
    --check) CHECK=1 ;;
    *) echo "usage: tools/convert_files.sh [--force] [--check]" >&2; exit 2 ;;
  esac
done

SRC=files/sources
OUT=files/out
LEVELS="$SRC/GAMEDATA/WORLD/LEVELS"
SOUND="$SRC/GAMEDATA/WORLD/SOUND"
PATH="$PATH:$(pwd)/tools/bin"
if [ "$(uname)" = Darwin ]; then INTRO=mp4; VGM_HINT="brew install vgmstream"
else INTRO=mpg; VGM_HINT="see docs/SDL3.md"; fi

# ---- prerequisites ----
fail=0
[ -e tools/ttre/__main__.py ] || { echo "run this from the repository root" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "missing: python3 (see docs/BUILD.md, step 1)" >&2; fail=1; }
for f in "$LEVELS" "$SOUND" "$SRC/GAMEDATA/Textures.cnt" "$SRC/GAMEDATA/Vignette.cnt"; do
  [ -e "$f" ] || { echo "missing: $f (copy the game's GAMEDATA folder to $SRC/GAMEDATA, see docs/BUILD.md step 2)" >&2; fail=1; }
done
[ "$fail" = 0 ] || exit 1
has() { command -v "$1" >/dev/null 2>&1; }

need() { [ "$FORCE" = 1 ] || [ ! -e "$1" ]; }  # true when $1 has to be (re)made

# The steps other than the levels record the stamp of the code that made their output (tools/ttre/chunks.py
# `stamp`: tools/ttre modules and their imports, scripts) in files/out/.stamps/<step>.
#   outdated STEP OUTPUT INPUT...  true when OUTPUT is missing or its code changed (then `record STEP` once it is made).
# An output made before these records existed is taken as current, as this script always did: its record is written.
STAMPS="$OUT/.stamps"
outdated() {
  step=$1 target=$2
  shift 2
  STEP_STAMP=$(python3 -m tools.ttre.chunks --inputs "$@")
  need "$target" && return 0
  if [ ! -e "$STAMPS/$step" ]; then record "$step"; return 1; fi
  [ "$(cat "$STAMPS/$step")" != "$STEP_STAMP" ]
}
record() { mkdir -p "$STAMPS"; echo "$STEP_STAMP" > "$STAMPS/$1"; }

# A .cnt archive into a folder: extracted next to it and swapped in when complete, so an interrupted run never leaves
# a half folder that later runs take as done.
extract() {
  rm -rf "$2.partial"
  python3 -m tools.ttre cnt-extract "$1" "$2.partial" --png
  rm -rf "$2"
  mv "$2.partial" "$2"
}

# ---- 3. textures and loading screens ----
if outdated textures "$OUT/Textures" cnt gf; then
  echo "== textures"
  extract "$SRC/GAMEDATA/Textures.cnt" "$OUT/Textures"
  record textures
else echo "== textures: current (skip)"; fi
if outdated vignettes "$OUT/Vignette" cnt gf; then
  echo "== loading screens"
  extract "$SRC/GAMEDATA/Vignette.cnt" "$OUT/Vignette"
  record vignettes
else echo "== loading screens: current (skip)"; fi

# ---- 4. levels (.ttlv + .ttai from the same run) ----
# Re-exported when either file is missing or carries another converter stamp (a hash of the converter's code,
# tools/ttre/chunks.py): an older file may not load in the app ("re-run the conversion") or load with outdated data.
# A level the AI export fails for has no .ttai (an older one would not match the new .ttlv); that is recorded under
# the stamp as .stamps/noai-<level>, so it isn't retried until the converter changes.
STAMP=$(python3 -m tools.ttre.chunks)
stale() { [ "$(python3 -c 'import sys; from tools.ttre.chunks import file_stamp; print(file_stamp(sys.argv[1]))' "$1")" != "$STAMP" ]; }
ai_current() { { [ -e "$2" ] && ! stale "$2"; } || [ "$(cat "$STAMPS/noai-$1" 2>/dev/null)" = "$STAMP" ]; }
mkdir -p "$OUT/ttlv"
missing_tex=""
current=0
for l in $(ls "$LEVELS" | grep -v '\.'); do
  t="$OUT/ttlv/$l.ttlv" a="$OUT/ttlv/$l.ttai"
  if need "$t" || stale "$t" || ! ai_current "$l" "$a"; then
    line=$(python3 -m tools.ttre level-ttlv "$l" "$t" 2>/dev/null | grep '^wrote' || true)
    case "$line" in
      *" 0 not found"*) ;;
      "") echo "level $l: export failed (run: python3 -m tools.ttre level-ttlv $l $t)" >&2; exit 1 ;;
      *) missing_tex="$missing_tex $l" ;;
    esac
    if python3 -m tools.ttre level-ttai "$l" "$a" >/dev/null 2>&1; then
      rm -f "$STAMPS/noai-$l"
    else
      rm -f "$a"
      mkdir -p "$STAMPS"
      echo "$STAMP" > "$STAMPS/noai-$l"
      echo "level $l: no Ed AI (simple controller only; run: python3 -m tools.ttre level-ttai $l $a)"
    fi
    if stale "$t" || { [ -e "$a" ] && stale "$a"; }; then
      echo "level $l: still not current after its export" >&2; exit 1
    fi
    echo "== level $l"
  else current=$((current + 1)); fi
done
[ "$current" = 0 ] || echo "== levels: $current current (skip)"
[ -z "$missing_tex" ] || echo "warning: textures not found in:$missing_tex (were the textures extracted first? letter case?)" >&2

# ---- 5. music ----
# files/out/audio/music.tsv is yours to edit (both apps read it). It is replaced by tools/music.tsv only when that
# changed (comment lines aside) or with --force, the previous one kept as music.tsv.bak. Every track it names is
# converted when missing (as .wav; a .m4a of it counts as present, and the table names whichever file there is).
M="$OUT/audio/music"
TABLE="$OUT/audio/music.tsv"
if ! has vgmstream-cli; then
  echo "== music: vgmstream-cli not found (optional, skip; $VGM_HINT)"
else
  mkdir -p "$M"
  if outdated music "$TABLE" tools/music.tsv; then
    if [ -e "$TABLE" ] && ! cmp -s tools/music.tsv "$TABLE"; then cp "$TABLE" "$TABLE.bak"; echo "== music: table updated (yours: $TABLE.bak)"; fi
    cp tools/music.tsv "$TABLE"
    record music
  fi
  made=0
  for t in $(grep -v '^#' "$TABLE" | cut -f2 | sort -u | grep .); do
    b=${t%.*}
    if [ "$FORCE" = 0 ] && { [ -e "$M/$b.wav" ] || [ -e "$M/$b.m4a" ]; }; then continue; fi
    case "$b" in
      Bnk_*_*) ;;
      *) echo "music: $M/$t missing (not a Bnk_<bank>_<subsong> track to convert)" >&2; continue ;;
    esac
    bank=${b%_*}; sub=${b##*_}
    # The table spells banks Bnk_<n>; the install mixes case (Bnk_0.bnm, BNK_200.BNM), so match case-insensitively.
    src=$(find "$SOUND" -maxdepth 1 -iname "$bank.bnm" | head -n 1)
    if [ -z "$src" ]; then echo "music: no $bank.bnm in $SOUND" >&2; continue; fi
    echo "== music $b.wav"
    vgmstream-cli -l 1 -f 0 -s "$sub" -o "$M/$b.wav" "$src" >/dev/null
    made=$((made + 1))
  done
  # Each entry names the file that is there (.wav, else a .m4a made from it).
  awk -F'\t' -v OFS='\t' -v dir="$M" '
    /^#/ || NF < 2 || $2 == "" { print; next }
    { b = $2; sub(/\.[^.]*$/, "", b)
      if (system("test -e \"" dir "/" $2 "\"") != 0) {
        if (system("test -e \"" dir "/" b ".wav\"") == 0) $2 = b ".wav"
        else if (system("test -e \"" dir "/" b ".m4a\"") == 0) $2 = b ".m4a"
      }
      print }' "$TABLE" > "$TABLE.new"
  if cmp -s "$TABLE.new" "$TABLE"; then rm -f "$TABLE.new"; else mv "$TABLE.new" "$TABLE"; fi
  [ "$made" != 0 ] || echo "== music: current (skip)"
fi

# ---- 5a. sound effects (Ed's: the global sound events) ----
if ! has vgmstream-cli; then
  echo "== sound effects: vgmstream-cli not found (optional, skip)"
elif outdated sfx "$OUT/audio/sfx/events.tsv" sfx; then
  echo "== sound effects"
  if python3 -m tools.ttre.sfx "$OUT/audio/sfx"; then record sfx; else echo "sound effects: failed (skipped)" >&2; fi
else echo "== sound effects: current (skip)"; fi

# ---- 5b. intro movie (needs the disc's Videos folder) ----
if [ ! -e "$SRC/Videos/EDPROL.AVI" ]; then echo "== intro movie: $SRC/Videos/EDPROL.AVI not found (optional, skip)"
elif ! has ffmpeg || ! has vgmstream-cli; then echo "== intro movie: needs ffmpeg and vgmstream-cli (optional, skip)"
elif outdated "intro-$INTRO" "$OUT/video/intro.$INTRO" tools/make_intro.sh; then
  echo "== intro movie (intro.$INTRO, a few minutes)"
  tools/make_intro.sh "$INTRO"
  record "intro-$INTRO"
else echo "== intro movie: current (skip)"; fi

# ---- 6. app icon ----
if [ ! -e "$SRC/icon_ED.ICO" ]; then echo "== icon: $SRC/icon_ED.ICO not found (optional, skip)"
elif ! has ffmpeg; then echo "== icon: ffmpeg not found (optional, skip)"
elif [ "$FORCE" = 0 ] && [ ! -e "$OUT/icon/OpenTonic.png" ] && [ -e "$OUT/icon/TonicViewer.png" ]; then
  echo "== icon: TonicViewer.png present (skip; the apps use it)"
elif outdated icon "$OUT/icon/OpenTonic.png" tools/make_icon.sh; then
  echo "== icon"
  tools/make_icon.sh
  record icon
else echo "== icon: current (skip)"; fi

# ---- checks ----
if [ "$CHECK" = 1 ]; then
  echo "== core checks"
  make -C core check mechcheck
fi
if [ "$(uname)" = Darwin ]; then
  echo "done: run with 'swift run -c release OpenTonic', or install the app with tools/install_macos.sh"
else
  echo "done: build with 'make -C sdl' and run sdl/build/opentonic, or install it with tools/install_linux.sh"
fi
