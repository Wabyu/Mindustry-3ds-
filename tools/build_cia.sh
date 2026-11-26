#!/usr/bin/env bash
# Package mindustry-3ds.elf + romfs into an installable CIA (run `make` first).
set -euo pipefail
cd "$(dirname "$0")/.."
: "${MAKEROM:=makerom}"
: "${BANNERTOOL:=bannertool}"
command -v "$MAKEROM" >/dev/null || { echo "makerom not found: run tools/install_cia_tools.sh and source .cia-tools/env.sh" >&2; exit 1; }
command -v "$BANNERTOOL" >/dev/null || { echo "bannertool not found: run tools/install_cia_tools.sh and source .cia-tools/env.sh" >&2; exit 1; }
[[ -f mindustry-3ds.elf && -f mindustry-3ds.smdh && -d romfs/gfx ]] || { echo "Run make before building the CIA." >&2; exit 1; }
mkdir -p build
"$BANNERTOOL" makebanner -i cia/banner.png -a cia/silence.wav -o build/banner.bnr
"$MAKEROM" -f cia -o mindustry-3ds.cia -target t -rsf cia/mindustry-3ds.rsf -elf mindustry-3ds.elf \
  -icon mindustry-3ds.smdh -banner build/banner.bnr -exefslogo -major 0 -minor 1 -micro 0
python3 tools/verify_cia.py mindustry-3ds.cia
echo "CIA written: mindustry-3ds.cia"
