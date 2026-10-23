#!/bin/sh
# Intro movie for the viewer (V on the level select screen), in one of two encodings:
#   mp4 (default on macOS): files/out/video/intro.mp4 for the Swift app (AVFoundation). HEVC + AAC through
#       VideoToolbox / AudioToolbox, upscaled to 1800x1080 (5:3, ~1080p).
#   mpg (default elsewhere): files/out/video/intro.mpg for the SDL build, which decodes it with pl_mpeg
#       (sdl/third_party). MPEG-1 video + MP2 audio in an MPEG program stream, upscaled to 800x480: MPEG-1 decodes
#       cheaply on slow CPUs, and the GPU does the rest of the upscale when drawing it.
# Video: the disc's Videos/EDPROL.AVI (400x240 Indeo 4, 25 fps, 5:41, no audio track), deblocked, lightly denoised,
# Lanczos-upscaled. Classical filters only; an ML upscaler (Real-ESRGAN, VideoToolbox super resolution) could
# replace the scale step later.
# Audio: the soundtrack the exe starts with the video: sound bank 200 event 0 (English/BNK_200.BNM, a 168-byte
# header around the external stream USDOLBY.APM, 5:42 stereo 22 kHz). The exe syncs the frames to its position.
# Needs: ffmpeg, vgmstream-cli; files/sources/Videos/EDPROL.AVI and files/sources/GAMEDATA/WORLD/SOUND/English.
# Usage (from the repo root): tools/make_intro.sh [mp4|mpg]
set -e
FORMAT=${1:-$([ "$(uname)" = Darwin ] && echo mp4 || echo mpg)}
case "$FORMAT" in
  mp4|mpg) ;;
  *) echo "usage: tools/make_intro.sh [mp4|mpg]" >&2; exit 2 ;;
esac
OUT=files/out/video
FILTERS="deblock=filter=strong:block=4,hqdn3d=2:1.5:3:3"
SND=files/sources/GAMEDATA/WORLD/SOUND/English
mkdir -p "$OUT"
# The bank names its stream "Usdolby.apm" and the install has USDOLBY.APM: on a case-sensitive filesystem vgmstream
# can't find it (and renders a second of silence), so stage both under the names the bank expects.
BANK=$(find "$SND" -maxdepth 1 -iname bnk_200.bnm | head -n 1)
APM=$(find "$SND" -maxdepth 1 -iname usdolby.apm | head -n 1)
[ -n "$BANK" ] && [ -n "$APM" ] || { echo "missing: $SND/BNK_200.BNM or USDOLBY.APM" >&2; exit 1; }
STAGE="$OUT/intro_stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BANK" "$STAGE/BNK_200.BNM"
ln -s "$(cd "$(dirname "$APM")" && pwd)/$(basename "$APM")" "$STAGE/Usdolby.apm"
vgmstream-cli -i -o "$OUT/intro_audio.wav" "$STAGE/BNK_200.BNM" >/dev/null
rm -rf "$STAGE"
if [ "$FORMAT" = mp4 ]; then
  ffmpeg -v error -y -i files/sources/Videos/EDPROL.AVI -i "$OUT/intro_audio.wav" -map 0:v -map 1:a \
    -vf "$FILTERS,scale=1800:1080:flags=lanczos,unsharp=5:5:0.5:5:5:0.0,format=yuv420p" \
    -c:v hevc_videotoolbox -q:v 65 -tag:v hvc1 -c:a aac_at -b:a 192k -movflags +faststart "$OUT/intro.mp4"
else
  # pl_mpeg decodes MPEG-1 Layer II at 32/44.1/48 kHz, so the 22 kHz soundtrack is resampled.
  ffmpeg -v error -y -i files/sources/Videos/EDPROL.AVI -i "$OUT/intro_audio.wav" -map 0:v -map 1:a \
    -vf "$FILTERS,scale=800:480:flags=lanczos,unsharp=5:5:0.4:5:5:0.0,format=yuv420p" \
    -c:v mpeg1video -q:v 3 -bf 2 -g 25 -c:a mp2 -b:a 192k -ar 44100 -ac 2 -f mpeg "$OUT/intro.mpg"
fi
rm "$OUT/intro_audio.wav"
echo "wrote $OUT/intro.$FORMAT"
