#!/usr/bin/env bash
# Puts the program tools/build_windows.sh built into build/PerfectDarkRecompiled-windows-x64.zip, a folder a player
# unzips and runs: PerfectDarkRecompiled.exe, the DLLs beside it (SDL2, dxcompiler, dxil), assets/ (the menus'),
# recompcontrollerdb.txt and LICENSE. Not the .pdb, which holds the builder's folder names: tools/release.sh keeps it. No ROM and nothing read from one: the program holds the recompiled code, as the
# page does, and the player picks their ROM in the launcher (Load ROM). From Git Bash, anywhere in the checkout:
#
#     tools/package_windows.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT/build"
[[ -f windows/PerfectDarkRecompiled.exe ]] || { echo "no build/windows/PerfectDarkRecompiled.exe: run tools/build_windows.sh first" >&2; exit 1; }
rm -rf package PerfectDarkRecompiled-windows-x64.zip
mkdir -p package/PerfectDarkRecompiled
cp windows/PerfectDarkRecompiled.exe windows/SDL2.dll windows/dxcompiler.dll windows/dxil.dll \
    ../recompcontrollerdb.txt ../LICENSE package/PerfectDarkRecompiled/
cp -r ../assets package/PerfectDarkRecompiled/
# Windows' own tar (bsdtar) writes zips; Git Bash's doesn't.
(cd package && "$SYSTEMROOT/System32/tar.exe" -a -cf ../PerfectDarkRecompiled-windows-x64.zip PerfectDarkRecompiled)
echo "build/PerfectDarkRecompiled-windows-x64.zip: $(du -h PerfectDarkRecompiled-windows-x64.zip | cut -f1)"
