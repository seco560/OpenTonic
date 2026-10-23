#!/bin/sh
# Intro movie for the viewer (V on the level select screen): files/out/video/intro.mp4.
# Video: the disc's Videos/EDPROL.AVI (400x240 Indeo 4, 25 fps, 5:41, no audio track), deblocked, lightly denoised,
# Lanczos-upscaled to 1800x1080 (5:3, ~1080p) and encoded as HEVC. Classical filters only; an ML upscaler
# (Real-ESRGAN, VideoToolbox super resolution) could replace the scale step later.
# Audio: the soundtrack the exe starts with the video: sound bank 200 event 0 (English/BNK_200.BNM, a 168-byte
# header around the external stream USDOLBY.APM, 5:42 stereo 22 kHz). The exe syncs the frames to its position.
# Needs: ffmpeg, vgmstream-cli; files/sources/Videos/EDPROL.AVI and files/sources/GAMEDATA/WORLD/SOUND/English.
# Usage (from the repo root): tools/make_intro.sh
set -e
OUT=files/out/video
mkdir -p "$OUT"
vgmstream-cli -i -o "$OUT/intro_audio.wav" files/sources/GAMEDATA/WORLD/SOUND/English/BNK_200.BNM
ffmpeg -v error -y -i files/sources/Videos/EDPROL.AVI -i "$OUT/intro_audio.wav" -map 0:v -map 1:a \
  -vf "deblock=filter=strong:block=4,hqdn3d=2:1.5:3:3,scale=1800:1080:flags=lanczos,unsharp=5:5:0.5:5:5:0.0,format=yuv420p" \
  -c:v hevc_videotoolbox -q:v 65 -tag:v hvc1 -c:a aac_at -b:a 192k -movflags +faststart "$OUT/intro.mp4"
rm "$OUT/intro_audio.wav"
echo "wrote $OUT/intro.mp4"
