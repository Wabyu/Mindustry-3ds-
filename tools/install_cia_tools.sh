#!/usr/bin/env bash
# Build host tools only. This does not install or execute anything on the 3DS.
set -euo pipefail
cd "$(dirname "$0")/.."
project_dir="$PWD"
tool_dir="${CIA_TOOLS_DIR:-$project_dir/.cia-tools}"
cc_tool="${CC:-cc}"
cxx_tool="${CXX:-c++}"
ar_tool="${AR:-ar}"
for tool in git make "$cc_tool" "$cxx_tool" "$ar_tool"; do command -v "$tool" >/dev/null || { echo "Missing tool : $tool" >&2; exit 1; }; done
mkdir -p "$tool_dir"
if [[ ! -d "$tool_dir/Project_CTR/.git" ]]; then git clone https://github.com/3DSGuy/Project_CTR.git "$tool_dir/Project_CTR"; fi
if [[ ! -d "$tool_dir/bannertool/.git" ]]; then git clone https://github.com/carstene1ns/3ds-bannertool.git "$tool_dir/bannertool"; fi
git -C "$tool_dir/Project_CTR" checkout --detach d52c2b0e788c2d085763d7c5dcc4b2edd9ffb7d8
git -C "$tool_dir/bannertool" checkout --detach 734d33be79fd3f8c29c6296158f06ac7c5ca9dcb
# Avoid tool-version macros shadowing title-version enum indices (minor index 19).
sed -i 's/^#define VER_MAJOR/#define MAKEROM_VER_MAJOR/;s/^#define VER_MINOR/#define MAKEROM_VER_MINOR/;s/^#define VER_PATCH/#define MAKEROM_VER_PATCH/' "$tool_dir/Project_CTR/makerom/src/version.h"
sed -i 's/APP_NAME, VER_MAJOR, VER_MINOR, VER_PATCH, AUTHORS/APP_NAME, MAKEROM_VER_MAJOR, MAKEROM_VER_MINOR, MAKEROM_VER_PATCH, AUTHORS/' "$tool_dir/Project_CTR/makerom/src/user_settings.c"
if [[ -n "${TERMUX_VERSION:-}" ]]; then
 # Android Bionic does not expose glibc iconv. Use the Termux libiconv package.
 sed -i '/^#define LIBICONV_PLUG$/d' "$tool_dir/Project_CTR/makerom/src/oschar.c"
 LIB=-liconv make -C "$tool_dir/Project_CTR/makerom" -j2 CC="$cc_tool" CXX="$cxx_tool" AR="$ar_tool"
else
 make -C "$tool_dir/Project_CTR/makerom" -j2 CC="$cc_tool" CXX="$cxx_tool" AR="$ar_tool"
fi
repo="$tool_dir/bannertool"
"$cxx_tool" -std=c++17 -O2 '-DVERSION="1.2.3"' -DSTBI_ONLY_PNG -DSTBI_NO_LINEAR -DSTBI_NO_STDIO -DSTB_VORBIS_NO_PUSHDATA_API -DSTB_VORBIS_NO_STDIO -DDR_WAV_NO_STDIO \
 -I"$repo/source" -I"$repo/source/pc" "$repo"/source/*.cpp "$repo"/source/3ds/*.cpp \
 "$repo/source/pc/stb_image.cpp" "$repo/source/pc/stb_vorbis.cpp" "$repo/source/pc/dr_wav.cpp" -o "$tool_dir/bannertool-host"
printf 'export MAKEROM=%q\nexport BANNERTOOL=%q\n' "$tool_dir/Project_CTR/makerom/bin/makerom" "$tool_dir/bannertool-host" > "$tool_dir/env.sh"
printf '%s\n' "Tools ready. Load : source \"$tool_dir/env.sh\""
