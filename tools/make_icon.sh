#!/bin/sh
# Builds out/icon/TonicViewer.png: the game's own icon (icon_ED.ICO from the install) over a tiled
# crop of sud0's psychedelic water texture. Generated, never committed (game artwork).
set -e
ICO="files/sources/icon_ED.ICO"
WATER="files/out/Textures/Sud0/o_zarbi_auntxy.png"
mkdir -p out/icon
ffmpeg -v error -y -i "$ICO" -frames:v 1 out/icon/ed.png
# ffmpeg drops the .ico transparency mask (it decodes as black), so key black out again.
ffmpeg -v error -y -i "$WATER" -i out/icon/ed.png -filter_complex \
 "[0]scale=1024:1024:flags=bicubic,format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='255*lt(hypot(X-512,Y-512),470)'[bg];\
  [1]format=rgba,colorkey=black:0.05:0,scale=640:640:flags=neighbor[ed];[bg][ed]overlay=(W-w)/2:(H-h)/2" -frames:v 1 out/icon/TonicViewer.png
echo "wrote out/icon/TonicViewer.png"
