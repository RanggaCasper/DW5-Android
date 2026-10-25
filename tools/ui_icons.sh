#!/usr/bin/env bash
# The touch controls' icons: native/ui/*.svg drawn to the raw RGBA the build reads. The Android build carries no image
# decoder, so an icon travels as bytes in ui_icons.cpp (CMakeLists.txt). Edit an .svg, run this, commit both.
set -e
cd "$(dirname "$0")/.."
for svg in native/ui/*.svg; do
    name=$(basename "$svg" .svg)
    rsvg-convert -w 96 -h 96 "$svg" -o "/tmp/$name.png"
    magick "/tmp/$name.png" -depth 8 "rgba:native/ui/$name.rgba"
    echo "native/ui/$name.rgba  $(stat -c%s "native/ui/$name.rgba") bytes"
done
