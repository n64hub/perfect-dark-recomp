#!/usr/bin/env bash
# Builds the decompilation (lib/perfect_dark) as ntsc-final from the player's ROM, in the image tools/docker/Dockerfile,
# checks it matches, relinks it without the TLB, and makes what the recompilation is made from (docs/building.md):
#
#  1. The decomp's own build and `make test` (its checksums.ntsc-final.md5, every segment), and the built ROM's MD5
#     against the ROM's.
#  2. The relink (tools/relink.ld.patch on the decomp's ld/pd.ld): the same objects linked with lib, inflate and game
#     where they sit in physical memory, into build/relink/stage1.elf, and again at other bases into
#     build/relink-b/stage1.elf; and build/relink/stage1.bin, the relink as a flat image, where each section sits at
#     its load address, which is the rom address syms/dump.toml gives it (us.toml recompiles from it), with the piracy
#     checks' code checksums mkrom would write (tools/piracy.py).
#  3. syms/relocs.txt from the two (tools/relocs.py make), then the local check that the ROM plus that list is the
#     relink outside .text (tools/relocs.py check), and build/relink/scan.txt (tools/relocs.py scan).
#  4. syms/dump.toml and syms/data_dump.toml: N64Recomp --dump-context on the relink, with the config tools/syms.py
#     writes (a size for each function the ELF gives none, and the code no symbol names), and the check that it
#     covers the code (tools/syms.py check).
#
# syms/ is tracked: a change to it shows in `git diff`. Linux with Docker, or inside a container of that image (no
# Docker there), where it runs right there; from anywhere in the checkout:
#
#     tools/build_decomp.sh path/to/pd.z64 [cpus]
#
# The decomp writes its build/, extracted/ and src/assets/ inside lib/perfect_dark, which its own .gitignore ignores,
# so the submodule stays clean; the ROM is copied there as pd.ntsc-final.z64 (ignored too). `make extract` runs only
# the first time (extracted/ntsc-final/ missing): it writes nothing over an asset that is already there.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=$(realpath "$1")
CPUS=${2:-$(nproc)}
MD5=e03b088b6ac9e0080440efed07c1e40f

[ "$(md5sum < "$ROM" | cut -d' ' -f1)" = "$MD5" ] || { echo "$ROM is not Perfect Dark (U) (V1.1), MD5 $MD5" >&2; exit 1; }
git -C "$ROOT" submodule update --init --recursive lib/perfect_dark

# The checkout as the build sees it: /src in the container, or where it is when this already runs in the image.
src=/src
[ -f /.dockerenv ] && ! command -v docker > /dev/null && src=$ROOT

cp "$ROM" "$ROOT/lib/perfect_dark/pd.ntsc-final.z64"
build="
    [ -f extracted/ntsc-final/lib.bin ] || make extract
    make -j$CPUS rom
    make -j$CPUS test
    echo \"\$(md5sum < build/ntsc-final/pd.z64 | cut -d' ' -f1) build/ntsc-final/pd.z64\"
    [ \"\$(md5sum < build/ntsc-final/pd.z64 | cut -d' ' -f1)\" = $MD5 ]
    echo 'The decompilation matches the ROM.'

    # The Makefile's own link of stage1.elf (its cpp and ld lines), with the relink patch and the bases as cpp macros.
    relink() {
        out=$src/build/\$1
        mkdir -p \$out
        patch -s -o \$out/pd.ld.in ld/pd.ld $src/tools/relink.ld.patch
        cpp -P -I ld -DROMID=ntsc-final -DVERSION=2 -DROMALLOCATION_DATA=0x015000 -DROMALLOCATION_LIB=0x038800 \
            -DROMALLOCATION_GAME=0x144ee0 -DROM_SIZE=32 -DRELINK_LIB=\$2 -DRELINK_INFLATE=\$3 -DRELINK_GAME=\$4 \
            \$out/pd.ld.in -o \$out/pd.ld
        mips-linux-gnu-ld --no-check-sections -z muldefs -T \$out/pd.ld --print-map -o \$out/stage1.elf > \$out/pd.map
    }
    relink relink 0x80001050 0x80200000 0x80220000
    relink relink-b 0x81001050 0x81200000 0x81220000
    # The relink as a flat image (an ELF section at file offset = its load address), which us.toml recompiles from.
    mips-linux-gnu-objcopy -O binary $src/build/relink/stage1.elf $src/build/relink/stage1.bin
    # The piracy checks' code checksums, which the decomp's mkrom writes only into its ROM.
    python3 -I $src/tools/piracy.py build/ntsc-final/stage1.bin build/ntsc-final/pd.map $src/build/relink/stage1.bin
    mkdir -p $src/syms
    python3 -I $src/tools/relocs.py make $src/build/relink/stage1.elf $src/build/relink/pd.map \
        $src/build/relink-b/stage1.elf $src/syms/relocs.txt
    python3 -I $src/tools/relocs.py check pd.ntsc-final.z64 $src/build/relink/stage1.elf $src/build/relink/pd.map \
        $src/syms/relocs.txt
    python3 -I $src/tools/relocs.py scan $src/build/relink/stage1.elf $src/build/relink/pd.map > $src/build/relink/scan.txt

    cd $src/build
    python3 -I $src/tools/syms.py relink/stage1.elf relink/pd.map $src/syms/relocs.txt dump_syms.toml
    N64Recomp dump_syms.toml --dump-context
    mv dump.toml data_dump.toml $src/syms/
    python3 -I $src/tools/syms.py check relink/stage1.elf relink/pd.map $src/syms/dump.toml
"
if [ "$src" = /src ]; then
    IMAGE=pd-decomp:$(sha256sum "$ROOT/tools/docker/Dockerfile" | cut -c1-12)
    docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$ROOT/tools/docker"
    docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -w /src/lib/perfect_dark -e ROMID=ntsc-final "$IMAGE" \
        bash -euo pipefail -c "$build"
else
    (cd "$ROOT/lib/perfect_dark" && ROMID=ntsc-final bash -euo pipefail -c "$build")
fi
