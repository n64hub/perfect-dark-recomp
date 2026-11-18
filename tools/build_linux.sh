#!/usr/bin/env bash
# Builds the desktop program for Linux in the image tools/docker/Dockerfile.linux: tools/recompile.sh, then the game
# (CMakeLists.txt) into build/linux/PerfectDarkRecompiled, with ccache in build/ccache. Run tools/build_decomp.sh
# first, which makes the relink the code is recompiled from. Linux with Docker, or inside a container of that image
# (no Docker there), where it builds right there; run from anywhere in the checkout:
#
#     tools/build_linux.sh [cpus]
#
# tools/run_linux.sh runs the result where there is no display.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPUS=${1:-$(nproc)}
git -C "$ROOT" submodule update --init --recursive lib/N64ModernRuntime lib/rt64 lib/RecompFrontend lib/N64Recomp-browser
build="
    git config --global --add safe.directory '*'
    tools/recompile.sh -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache > build/configure.log
    ninja -C build/linux -j$CPUS PerfectDarkRecompiled
"
if [ -f /.dockerenv ] && ! command -v docker > /dev/null; then
    (cd "$ROOT" && bash -euo pipefail -c "$build")
else
    IMAGE=pd-linux:$(sha256sum "$ROOT/tools/docker/Dockerfile.linux" | cut -c1-12)
    mkdir -p "$ROOT/build/home" "$ROOT/build/ccache"
    docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" -f "$ROOT/tools/docker/Dockerfile.linux" "$ROOT/tools/docker"
    docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -w /src -e CCACHE_DIR=/src/build/ccache -e HOME=/src/build/home "$IMAGE" \
        bash -euo pipefail -c "$build"
fi
