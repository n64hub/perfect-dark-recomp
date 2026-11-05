#!/usr/bin/env bash
# Builds the desktop program for Windows x64 with clang-cl: tools/recompile.sh, then the game (CMakeLists.txt) into
# build/windows/PerfectDarkRecompiled.exe, beside the DLLs it needs (SDL2, dxcompiler, dxil). Run tools/build_decomp.sh
# first, in WSL2 on the same checkout, which makes the relink the code is recompiled from (docs/building.md, Windows).
# From Git Bash, anywhere in the checkout:
#
#     tools/build_windows.sh [cpus]
#
# On PATH: clang-cl and lld-link, CMake 3.20-3.31 and Ninja, and wsl, whose default distro has clang, ld.lld and make
# for patches/ (CMakeLists.txt, PATCHES_MAKE). Without WSL, set MIPS_CC to a Windows clang with a MIPS backend (MSYS2's
# clang64 has one) and MIPS_LD to an ld.lld, with GNU make on PATH: patches/ builds with those instead. clang-cl finds
# the MSVC libraries and the Windows SDK by itself where Visual Studio or its Build Tools are installed, or else from
# INCLUDE and LIB.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPUS=${1:-$(nproc)}
cd "$ROOT"
patches=()
if [[ -n "${MIPS_CC:-}" ]]; then
    patches=(-DPATCHES_MAKE=make -DPATCHES_C_COMPILER="$(cygpath -m "$MIPS_CC")" -DPATCHES_LD="$(cygpath -m "${MIPS_LD:?set MIPS_LD with MIPS_CC}")")
    tools=(clang-cl lld-link cmake ninja make)
else
    tools=(clang-cl lld-link cmake ninja wsl)
fi
for tool in "${tools[@]}"; do
    command -v "$tool" > /dev/null || { echo "$tool isn't on PATH (tools/build_windows.sh says what the build needs)" >&2; exit 1; }
done
[[ -n "${MIPS_CC:-}" ]] || wsl which clang ld.lld make > /dev/null ||
    { echo "WSL has no clang, ld.lld or make, which patches/ needs" >&2; exit 1; }
git submodule update --init --recursive lib/N64ModernRuntime lib/rt64 lib/RecompFrontend lib/N64Recomp-browser
tools/recompile.sh -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
mkdir -p build
# Through ccache when it is on PATH: its cache is the user's, outside the tree, so a build in a fresh clone (a
# release's) compiles only what changed since the last one built here (the shaders dxc builds always build). ccache
# can't cache the .pdb that -Zi shares between compiles, so each object holds its debug info (-Z7; the link still
# writes the .pdb). CCACHE_BASEDIR makes the tree's paths relative and CCACHE_NOHASHDIR leaves the working directory
# out of the hash, so another clone's compiles match; their debug info then names the files where that clone had them.
# Precompiled headers are off with it (RmlUi's): ccache can't cache a file that includes one.
cc= nopch=OFF
command -v ccache > /dev/null && cc=ccache nopch=ON
export CCACHE_BASEDIR="$(cygpath -w "$ROOT")" CCACHE_NOHASHDIR=1
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang-cl \
    -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link "${patches[@]}" \
    -DCMAKE_C_COMPILER_LAUNCHER="$cc" -DCMAKE_CXX_COMPILER_LAUNCHER="$cc" -DCMAKE_DISABLE_PRECOMPILE_HEADERS=$nopch \
    -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O2 -Ob1 -DNDEBUG -Z7" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -Ob1 -DNDEBUG -Z7" \
    > build/configure-windows.log 2>&1 ||
    { tail -30 build/configure-windows.log; exit 1; }
ninja -C build/windows -j"$CPUS" PerfectDarkRecompiled
