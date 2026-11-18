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
# One release carries every change merged since the last: each change's PR adds what the player gets to
# releases/next.md, and a release is a PR titled "Release v<version>" that bumps version_string in src/main.cpp and
# moves releases/next.md to releases/v<version>.md: what a player gets, not a list of merged PRs. The tag is
# v<version_string>. Both builds and the publish run on that PR's merge commit, checked out with a clean tree: each
# build refuses a tree with changes (the build scripts move the submodules onto their pins first, unpatch below) and
# records the commit it was built from (out/<archive>.commit), and publish refuses archives from another one or that
# haven't booted (boot), a tree with changes, a commit not on origin/main or still holding releases/next.md, or a tag
# that exists. A change merged meanwhile goes in the next release and doesn't stop this one. A clone kept from one
# release to the next keeps build/windows and build/linux, so there only what changed builds.
#
# Windows: tools/build_windows.sh's program, packed by tools/package_windows.sh (the DLLs, assets/,
# recompcontrollerdb.txt and LICENSE beside the exe). It needs the Visual C++ 2015-2022 redistributable (x64). Its .pdb
# goes to out/<name>-windows-x64.pdb and is never published (it holds the builder's folder names): a player's crash
# prints PerfectDarkRecompiled.exe+<offset> lines (src/crash_trace.cpp), which it names, e.g.
# llvm-symbolizer --obj=<the release's exe> --relative-address <offset>, with the .pdb beside that exe.
# Linux: tools/build_linux.sh's program, built in tools/docker/Dockerfile.linux, Ubuntu 22.04, so it needs glibc 2.35
# or newer, with its debug info stripped. It carries the libSDL2 it was built with and libatomic (not on every system)
# beside the binary (RUNPATH $ORIGIN), as the Windows zip carries SDL2.dll, and is a .tar.gz so the binary stays
# executable. GTK 3, FreeType and a Vulkan driver are the system's.
# Started from another folder, either program works from its own (src/main.cpp).
# Either build can come from elsewhere: with WINDOWS_BUILD naming a folder that holds tools/build_windows.sh's program
# from this commit, its .pdb and DLLs (a cross-build, say), windows packs that instead of building, and run inside a
# container of tools/docker/Dockerfile.linux's image (where there is no Docker), linux builds right there
# (tools/build_linux.sh does too).
# boot runs $BOOT_RELEASE on the archive, which unpacks it into a new folder with the ROM (tools/build_decomp.sh's copy)
# stored as Load ROM stores it, runs it with --boot (and any arguments after the OS) for 30 s with its own window
# captured, and writes the <archive>.booted publish asks for.
# v0.1.0's Linux archive was checked by running it in distribution containers under Xvfb with Mesa's software Vulkan
# (lavapipe), from another folder, the ROM stored beside it, --boot: on Ubuntu 22.04 and Debian 12 it played the intro
# for 3 minutes; on Ubuntu 24.04 (Mesa 25.2) it drew the title and then lavapipe crashed in one of its own llvmpipe
# threads, as rush-2049-recomp's release does there. No Linux GPU has run it.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

# Windows' paths stop at 260 characters, and the submodules' deepest files (rt64's spirv-cross tests) are 184 long
# inside the clone, so a clone in a deep folder can't check them out. core.longpaths set in the clone doesn't reach
# the submodules' own clones; set in the environment, it reaches every git command below, theirs included. Elsewhere
# than Windows it changes nothing.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.longpaths GIT_CONFIG_VALUE_0=true

version=$(sed -n 's/^const std::string version_string = "\(.*\)";$/\1/p' src/main.cpp)
[ -n "$version" ] || { echo "no version_string in src/main.cpp" >&2; exit 1; }
tag="v$version"
name="PerfectDarkRecompiled-$tag"
commit=$(git rev-parse HEAD)
mkdir -p out

clean() {
    [ -z "$(git status --porcelain --ignore-submodules=dirty)" ] || { echo "the tree has uncommitted changes" >&2; exit 1; }
}

# Each configure patches the submodules' files (N64Recomp-browser's patches/apply.cmake, from CMakeLists.txt), so a
# clone kept from one release to the next holds the last build's patches. Where the pins or the patches have moved
# since, the files go back to their commits before the build scripts move the submodules, so the new patches go onto
# pinned files as in a fresh clone and never merge over the old ones. The commit that last moved them is kept in .git,
# outside the tree.
unpatch() {
    local stamp moved
    stamp=$(git rev-parse --git-path release-patched-for)
    moved=$(git log -1 --format=%H -- .gitmodules lib runtime-patches)
    if [ "$(cat "$stamp" 2> /dev/null)" != "$moved" ]; then
        git submodule foreach -q --recursive 'git checkout -q -- . && git clean -qfd'
        echo "$moved" > "$stamp"
    fi
}

case "${1:-}" in
windows)
    if [ -n "${WINDOWS_BUILD:-}" ]; then
        build=$WINDOWS_BUILD
        [ -f "$build/PerfectDarkRecompiled.exe" ] && [ -f "$build/PerfectDarkRecompiled.pdb" ] ||
            { echo "no $build/PerfectDarkRecompiled.exe and .pdb" >&2; exit 1; }
        clean
        # The folder tools/package_windows.sh packs, zipped by CMake's own tar where Windows' isn't.
        stage=build/package-cross/PerfectDarkRecompiled
        rm -rf build/package-cross "out/$name-windows-x64.zip"
        mkdir -p "$stage"
        cp "$build"/{PerfectDarkRecompiled.exe,SDL2.dll,dxcompiler.dll,dxil.dll} recompcontrollerdb.txt LICENSE "$stage"/
        cp -r assets "$stage"/
        (cd build/package-cross && cmake -E tar cf "../../out/$name-windows-x64.zip" --format=zip PerfectDarkRecompiled)
    else
        unpatch
        bash tools/build_windows.sh
        clean
        bash tools/package_windows.sh
        mv build/PerfectDarkRecompiled-windows-x64.zip "out/$name-windows-x64.zip"
        build=build/windows
    fi
    cp "$build/PerfectDarkRecompiled.pdb" "out/$name-windows-x64.pdb"
    echo "$commit" > "out/$name-windows-x64.zip.commit"
    echo "out/$name-windows-x64.zip ($(du -h "out/$name-windows-x64.zip" | cut -f1))"
    ;;
linux)
    unpatch
    bash tools/build_linux.sh
    clean
    # Packed in tools/docker/Dockerfile.linux's image: in this host's Docker, or right here when already in it.
    pack="set -euo pipefail
        stage=out/$name-linux-x64
        rm -rf \$stage \$stage.tar.gz
        mkdir -p \$stage
        strip --strip-debug -o \$stage/PerfectDarkRecompiled build/linux/PerfectDarkRecompiled
        cp recompcontrollerdb.txt LICENSE \$stage/
        cp -L /usr/local/lib/libSDL2-2.0.so.0 /usr/lib/x86_64-linux-gnu/libatomic.so.1 \$stage/
        patchelf --set-rpath '\$ORIGIN' \$stage/PerfectDarkRecompiled
        cp -r assets \$stage/
        tar -C out -czf \$stage.tar.gz $name-linux-x64"
    if [ -f /.dockerenv ] && ! command -v docker > /dev/null; then
        bash -c "$pack"
    else
        image=pd-linux:$(sha256sum tools/docker/Dockerfile.linux | cut -c1-12)
        docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -w /src "$image" bash -c "$pack"
    fi
    echo "$commit" > "out/$name-linux-x64.tar.gz.commit"
    echo "out/$name-linux-x64.tar.gz ($(du -h "out/$name-linux-x64.tar.gz" | cut -f1))"
    ;;
boot)
    case "${2:-}" in
    windows) archive="out/$name-windows-x64.zip" ;;
    linux) archive="out/$name-linux-x64.tar.gz" ;;
    *) echo "usage: tools/release.sh boot windows|linux [program args...]" >&2; exit 1 ;;
    esac
    boot=${BOOT_RELEASE:-}
    [ -n "$boot" ] && [ -f "$boot" ] || {
        echo "set BOOT_RELEASE to the script that boots a release archive: <archive> <rom> <game id> <dir> [args...]" >&2
        exit 1
    }
    bash "$boot" "$archive" lib/perfect_dark/pd.ntsc-final.z64 pd.n64.us.1 "out/boot-$2" "${@:3}"
    ;;
publish)
    notes="releases/$tag.md"
    [ -s "$notes" ] || { echo "no $notes: write the release notes for players first" >&2; exit 1; }
    clean
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
