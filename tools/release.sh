#!/usr/bin/env bash
# Builds the desktop program's release, for Windows and for Linux, and publishes both as one GitHub release. CI has no
# ROM (none is ever uploaded), so it can't build the game; a release is built where the ROM is, after
# tools/build_decomp.sh, and holds nothing from it: the player picks theirs with Load ROM, as on the page.
#
#     tools/release.sh windows   # Git Bash on Windows, as tools/build_windows.sh says: out/<name>-windows-x64.zip
#     tools/release.sh linux     # Linux with Docker: out/<name>-linux-x64.tar.gz
#     tools/release.sh publish   # both archives, built from this commit: the GitHub release (gh signed in)
#
# The tag is v<version_string> from src/main.cpp, so bump that first, and write the release's notes beside it in
# releases/v<version>.md: what a player gets, not a list of merged PRs. Both builds and the publish run on the same
# commit, origin/main's, with a clean tree: each archive records the commit it was built from (out/<archive>.commit),
# and publish refuses archives from another one, a tree with changes, or a tag that exists.
#
# Windows: tools/build_windows.sh's program, packed by tools/package_windows.sh (the DLLs, assets/,
# recompcontrollerdb.txt and LICENSE beside the exe). It needs the Visual C++ 2015-2022 redistributable (x64).
# Linux: tools/build_linux.sh's program, built in tools/docker/Dockerfile.linux, Ubuntu 22.04, so it needs glibc 2.35
# or newer, with its debug info stripped. It carries the libSDL2 it was built with and libatomic (not on every system)
# beside the binary (RUNPATH $ORIGIN), as the Windows zip carries SDL2.dll, and is a .tar.gz so the binary stays
# executable. GTK 3, FreeType and a Vulkan driver are the system's.
# Started from another folder, either program works from its own (src/main.cpp).
# v0.1.0's Linux archive was checked by running it in distribution containers under Xvfb with Mesa's software Vulkan
# (lavapipe), from another folder, the ROM stored beside it, --boot: on Ubuntu 22.04 and Debian 12 it played the intro
# for 3 minutes; on Ubuntu 24.04 (Mesa 25.2) it drew the title and then lavapipe crashed in one of its own llvmpipe
# threads, as rush-2049-recomp's release does there. No Linux GPU has run it.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

version=$(sed -n 's/^const std::string version_string = "\(.*\)";$/\1/p' src/main.cpp)
[ -n "$version" ] || { echo "no version_string in src/main.cpp" >&2; exit 1; }
tag="v$version"
name="PerfectDarkRecompiled-$tag"
commit=$(git rev-parse HEAD)
mkdir -p out

case "${1:-}" in
windows)
    bash tools/build_windows.sh
    bash tools/package_windows.sh
    mv build/PerfectDarkRecompiled-windows-x64.zip "out/$name-windows-x64.zip"
    echo "$commit" > "out/$name-windows-x64.zip.commit"
    echo "out/$name-windows-x64.zip ($(du -h "out/$name-windows-x64.zip" | cut -f1))"
    ;;
linux)
    bash tools/build_linux.sh
    image=pd-linux:$(sha256sum tools/docker/Dockerfile.linux | cut -c1-12)
    docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -w /src "$image" bash -euo pipefail -c "
        stage=out/$name-linux-x64
        rm -rf \$stage \$stage.tar.gz
        mkdir -p \$stage
        strip --strip-debug -o \$stage/PerfectDarkRecompiled build/linux/PerfectDarkRecompiled
        cp recompcontrollerdb.txt LICENSE \$stage/
        cp -L /usr/local/lib/libSDL2-2.0.so.0 /usr/lib/x86_64-linux-gnu/libatomic.so.1 \$stage/
        patchelf --set-rpath '\$ORIGIN' \$stage/PerfectDarkRecompiled
        cp -r assets \$stage/
        tar -C out -czf \$stage.tar.gz $name-linux-x64"
    echo "$commit" > "out/$name-linux-x64.tar.gz.commit"
    echo "out/$name-linux-x64.tar.gz ($(du -h "out/$name-linux-x64.tar.gz" | cut -f1))"
    ;;
publish)
    notes="releases/$tag.md"
    [ -s "$notes" ] || { echo "no $notes: write the release notes for players first" >&2; exit 1; }
    [ -z "$(git status --porcelain --ignore-submodules=dirty)" ] || { echo "the tree has uncommitted changes" >&2; exit 1; }
    git fetch -q origin main
    [ "$commit" = "$(git rev-parse origin/main)" ] || { echo "HEAD is not origin/main" >&2; exit 1; }
    ! gh release view "$tag" > /dev/null 2>&1 || { echo "release $tag exists: bump version_string first" >&2; exit 1; }
    files=("out/$name-windows-x64.zip" "out/$name-linux-x64.tar.gz")
    for f in "${files[@]}"; do
        [ -f "$f" ] || { echo "no $f: run tools/release.sh windows and linux first" >&2; exit 1; }
        [ "$(cat "$f.commit" 2> /dev/null)" = "$commit" ] || { echo "$f wasn't built from $commit" >&2; exit 1; }
    done
    gh release create "$tag" "${files[@]}" --target "$commit" --title "Perfect Dark: Recompiled $tag" --notes-file "$notes"
    ;;
*)
    echo "usage: tools/release.sh windows|linux|publish" >&2
    exit 1
    ;;
esac
