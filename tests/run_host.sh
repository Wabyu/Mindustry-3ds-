#!/bin/sh
# Builds and runs the host regression tests (no devkitPro needed).
set -e
cd "$(dirname "$0")/.."
python3 tools/build_assets.py --skip-sprites --gen build/gen-host --romfs build/romfs-host >/dev/null
mkdir -p build/host
${CXX:-g++} -std=gnu++17 -O2 -g -Wall -Wextra -fsanitize=address,undefined -Isource -Ibuild/gen-host \
  source/world.cpp source/sim.cpp source/units.cpp source/save.cpp build/gen-host/content_gen.cpp tests/host_test.cpp \
  -o build/host/host_test
./build/host/host_test build/romfs-host
