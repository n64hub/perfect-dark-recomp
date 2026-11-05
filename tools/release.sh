#!/usr/bin/env bash
# Builds the desktop program's release, for Windows and for Linux, and publishes both as one GitHub release. CI has no
# ROM (none is ever uploaded), so it can't build the game; a release is built where the ROM is, after
# tools/build_decomp.sh, and holds nothing from it: the player picks theirs with Load ROM, as on the page.
#
#     tools/release.sh windows   # Git Bash on Windows, as tools/build_windows.sh says: out/<name>-windows-x64.zip
#     tools/release.sh linux     # Linux with Docker: out/<name>-linux-x64.tar.gz
#     tools/release.sh boot windows   # Git Bash on Windows: boots that archive from a new folder, out/boot-windows
#     tools/release.sh boot linux     # Linux with Docker: the same for the Linux archive, out/boot-linux
#     tools/release.sh publish   # both archives, built from this commit: the GitHub release (gh signed in)
#
# One release carries every change merged since the last (N64Recomp-browser's CLAUDE.md, Releasing): each change's
# PR adds what the player gets to releases/next.md, and a release is a PR titled "Release v<version>" that bumps
# version_string in src/main.cpp and moves releases/next.md to releases/v<version>.md: what a player gets, not a
# list of merged PRs. The tag is v<version_string>. Both builds and the publish run on that PR's merge commit,
# checked out with a clean tree: each archive records the commit it was built from (out/<archive>.commit), and
# publish refuses archives from another one or that haven't booted (boot), a tree with changes, a commit not on
# origin/main or still holding releases/next.md, or a tag that exists. A change merged meanwhile goes in the next
# release and doesn't stop this one.
#
# Windows: tools/build_windows.sh's program, packed by tools/package_windows.sh (the DLLs, assets/,
# recompcontrollerdb.txt and LICENSE beside the exe). It needs the Visual C++ 2015-2022 redistributable (x64).
# Linux: tools/build_linux.sh's program, built in tools/docker/Dockerfile.linux, Ubuntu 22.04, so it needs glibc 2.35
# or newer, with its debug info stripped. It carries the libSDL2 it was built with and libatomic (not on every system)
# beside the binary (RUNPATH $ORIGIN), as the Windows zip carries SDL2.dll, and is a .tar.gz so the binary stays
# executable. GTK 3, FreeType and a Vulkan driver are the system's.
# Started from another folder, either program works from its own (src/main.cpp).
# boot runs lib/N64Recomp-browser's tools/boot_release.sh, which says how, on the archive: unpacked into a new folder
# with the ROM (tools/build_decomp.sh's copy) stored as Load ROM stores it, run with --boot (and any arguments after the
# OS) for 30 s, its own window captured. It writes the <archive>.booted publish asks for.
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
boot)
    case "${2:-}" in
    windows) archive="out/$name-windows-x64.zip" ;;
    linux) archive="out/$name-linux-x64.tar.gz" ;;
    *) echo "usage: tools/release.sh boot windows|linux [program args...]" >&2; exit 1 ;;
    esac
    boot=${BOOT_RELEASE:-lib/N64Recomp-browser/tools/boot_release.sh}
    [ -f "$boot" ] ||
        { echo "no $boot: set BOOT_RELEASE to tools/boot_release.sh in a checkout of N64Recomp-browser's main" >&2; exit 1; }
    bash "$boot" "$archive" lib/perfect_dark/pd.ntsc-final.z64 pd.n64.us.1 "out/boot-$2" "${@:3}"
    ;;
publish)
    notes="releases/$tag.md"
    [ -s "$notes" ] || { echo "no $notes: write the release notes for players first" >&2; exit 1; }
    [ -z "$(git status --porcelain --ignore-submodules=dirty)" ] || { echo "the tree has uncommitted changes" >&2; exit 1; }
    git fetch -q origin main
    git merge-base --is-ancestor "$commit" origin/main || { echo "HEAD is not on origin/main" >&2; exit 1; }
    [ ! -e releases/next.md ] || { echo "releases/next.md is still there: move it to $notes in the release's PR" >&2; exit 1; }
    ! gh release view "$tag" > /dev/null 2>&1 || { echo "release $tag exists: bump version_string first" >&2; exit 1; }
    files=("out/$name-windows-x64.zip" "out/$name-linux-x64.tar.gz")
    for f in "${files[@]}"; do
        [ -f "$f" ] || { echo "no $f: run tools/release.sh windows and linux first" >&2; exit 1; }
        [ "$(cat "$f.commit" 2> /dev/null)" = "$commit" ] || { echo "$f wasn't built from $commit" >&2; exit 1; }
        [ "$(cat "$f.booted" 2> /dev/null)" = "$(sha256sum "$f" | cut -d' ' -f1)" ] ||
            { echo "$f hasn't booted: run tools/release.sh boot windows and linux first" >&2; exit 1; }
    done
    gh release create "$tag" "${files[@]}" --target "$commit" --title "Perfect Dark: Recompiled $tag" --notes-file "$notes"
    ;;
*)
    echo "usage: tools/release.sh windows|linux|boot <os>|publish" >&2
    exit 1
    ;;
esac
