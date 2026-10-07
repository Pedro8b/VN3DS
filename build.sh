#!/bin/bash
# Build helper for Windows (devkitPro msys2):  bash build.sh [clean] [cia]
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
cd "$(dirname "$0")"
for a in "$@"; do [ "$a" = "clean" ] && make clean; done
make -j8 2>&1 | grep -v "^[a-z_0-9]*\.\(c\|cpp\)$"
for a in "$@"; do [ "$a" = "cia" ] && bash cia/build_cia.sh; done
ls -la *.3dsx *.cia 2>/dev/null
