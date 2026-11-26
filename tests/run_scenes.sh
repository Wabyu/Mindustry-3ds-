#!/bin/sh
# Visual control scenes: records the citro2d calls of fixed scenes, composes them with the
# real atlas (tex3ds output, same UVs as the console) and runs the automatic checks.
# Needs tex3ds (devkitPro), python3 with Pillow and numpy. Output: build/scenes/*.png
set -e
cd "$(dirname "$0")/.."
TEX3DS=${TEX3DS:-tex3ds} sh tools/atlas_debug.sh
mkdir -p build/host build/scenes build/scenes-nosnap
${CXX:-g++} -std=gnu++17 -O2 -Isource -Ibuild/gen source/world.cpp source/sim.cpp source/units.cpp source/save.cpp \
  source/app.cpp build/gen/content_gen.cpp tests/gfx_host.cpp tests/control_scenes.cpp -o build/host/control_scenes
./build/host/control_scenes romfs build/scenes
./build/host/control_scenes romfs build/scenes-nosnap nosnap
python3 tools/render_shots.py build/scenes build/gfx build/gen >/dev/null
echo "== snapped (game)"; python3 tools/check_scenes.py build/scenes build/gfx build/gen
echo "== without snapping (reference)"; python3 tools/check_scenes.py build/scenes-nosnap build/gfx build/gen
