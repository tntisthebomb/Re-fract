#!/usr/bin/env bash
set -euo pipefail
makerom_bin=${1:-makerom}
for input in Re-fract.elf Re-fract.smdh build/banner/cube.bnr tools/build-cia.rsf; do
    test -s "$input" || { echo "Missing package input: $input" >&2; exit 1; }
done
"$makerom_bin" -f cia -target t -exefslogo -o Re-fract.cia \
    -elf Re-fract.elf -rsf tools/build-cia.rsf -icon Re-fract.smdh \
    -banner build/banner/cube.bnr -major 0 -minor 1 -micro 0
test -s Re-fract.cia
