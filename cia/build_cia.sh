#!/bin/bash
# Builds VN3DS.cia from the .elf produced by `make` (needs bannertool + makerom in PATH).
set -e
cd "$(dirname "$0")"
[ -f ../VN3DS.elf ] || { echo "run make first"; exit 1; }
# version from the Makefile (VERSION_MAJOR/MINOR/MICRO)
ver() { sed -n "s/^VERSION_$1[[:space:]]*:=[[:space:]]*\([0-9]*\).*/\1/p" ../Makefile; }
MAJOR=$(ver MAJOR); MINOR=$(ver MINOR); MICRO=$(ver MICRO)
bannertool makebanner -i banner.png -a banner.wav -o banner.bnr
bannertool makesmdh -s "VN3DS" -l "VNDS visual novel player (DS, Vita, zip)" -p "Pedro8b" -i ../icon.png -o icon.icn
makerom -f cia -o ../VN3DS.cia -elf ../VN3DS.elf -rsf app.rsf -icon icon.icn -banner banner.bnr -exefslogo -target t \
    -major "${MAJOR:-1}" -minor "${MINOR:-0}" -micro "${MICRO:-0}"
ls -la ../VN3DS.cia
