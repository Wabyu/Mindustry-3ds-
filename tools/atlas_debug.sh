#!/bin/sh
# Uncompressed atlas + preview for tools/render_shots.py (needs tex3ds, e.g. devkitpro/devkitarm).
set -e
cd "$(dirname "$0")/../build/gfx"
for t in sheet*.t3s; do n=${t%.t3s}; ${TEX3DS:-tex3ds} -i "$t" -z none -o "$n.raw.t3x" -p "$n.preview.png"; done
