# Building

The recompilation is made from the [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark) (MIT,
pinned at `lib/perfect_dark`) built from your own ROM, relinked so that none of its code needs the N64's TLB. The
first step makes what the recompilation is made from: `syms/` (symbols and the relocation list, tracked) and
`build/relink/stage1.elf` (the relinked build, which holds the game and is never tracked). The second builds the
desktop program from it ([The desktop program](#the-desktop-program)), which plays the game.

## What you need

- Linux with Docker (WSL2 works), git, and about 2 GB of disk. A Windows build also needs the tools
  [On Windows](#on-windows) lists.
- The ROM: *Perfect Dark (U) (V1.1)*, a big-endian `.z64` with MD5 `e03b088b6ac9e0080440efed07c1e40f`.

## Build it

```bash
git clone --recurse-submodules https://github.com/n64hub/perfect-dark-recomp
cd perfect-dark-recomp
tools/build_decomp.sh path/to/pd.z64        # [cpus], all of them by default
```

The first run builds the Docker image (`tools/docker/Dockerfile`: Ubuntu 24.04, the MIPS binutils, armips,
N64Recomp) in a few minutes; the decomp then builds in about a minute on 12 cores. The script's header says each step:

1. The decomp's own build of `ntsc-final`, its `make test` (every segment against `checksums.ntsc-final.md5`) and the
   built ROM's MD5 against yours. It prints `The decompilation matches the ROM.`
2. The relink: the same objects linked again with `tools/relink.ld.patch` applied to a copy of the decomp's
   `ld/pd.ld`, into `build/relink/`, and once more at other bases into `build/relink-b/`, and the piracy checks' code
   checksums written into the relink's flat image (`tools/piracy.py`, The piracy checks below).
3. `syms/relocs.txt` from the two links, then the check that your ROM plus that list is the relink
   (`tools/relocs.py check`). It prints `OK: ...` or what differs.
4. `syms/dump.toml` and `syms/data_dump.toml`, from N64Recomp's `--dump-context` on the relink, and the check that
   they cover the code (`tools/syms.py check`).

`syms/` is tracked: after a run, `git diff syms/` is empty unless the decomp, the patch or the tools changed.

## The relink

Perfect Dark runs its lib segment and its game code behind the TLB: lib at 0x70001050, the game's .text and .rodata at
0x7f000000, paged in 4 KB zips by `src/lib/vm.s`. N64Recomp's memory macros index RDRAM by address - 0x80000000, so
the recompilation needs every address in KSEG0. `tools/relink.ld.patch` links each segment where it already sits in
physical memory on an 8 MB console:

```
segment   retail link   relink       in the ROM
preamble  (0x25e0)      0x80001000   0x1000, raw
lib       0x70001050    0x80001050   0x1050, zipped from 0x3050
.data     0x80059fe0    unchanged    zipped
.bss      0x8008ae20    unchanged    -
inflate   0x70200000    0x80200000   raw
game      0x7f000000    0x80220000   zipped page by page
```

lib was already at physical 0x80001050 (`.data` is linked at `0x80001050 + SIZEOF(.lib)`), the game's own 8 MB path
unzips game to 0x80220000 (`src/lib/vminit.c`), and `src/lib/boot.c` copies inflate to 0x70200000, physical
0x80200000. The preamble's call to `vm_boot` (`vm_boot_from_preamble`) becomes `vm_boot` itself, the same 0x80001050,
and the preamble, which the decomp's script gives no address, is linked where IPL3 runs it: its bytes are the ROM's.
Nothing is pushed to the decomp; the patch is applied to a copy in `build/`.

## The relocation list

At run time the player's ROM supplies every byte: the game's own inflate unpacks lib, `.data` and game into RDRAM,
and the words `syms/relocs.txt` names are shifted from the retail addresses to the relinked ones. A line is
`<segment> <offset in it>` (lib, data, inflate or game; hex), offsets only. A listed word holding an address in retail
lib (0x70001050 to its end) or inflate (0x70200000 on) moves up by 0x10000000, one in retail game (0x7f000000 on) by
0x01220000.

`tools/relocs.py make` finds them by linking twice, at the bases above and at each plus 0x01000000: the words outside
`.text` that differ are the pointers into lib, inflate and game. `tools/relocs.py check` inflates the ROM's segments
as the decomp's `tools/extract` does, applies the list and compares with the relink. Its result on the ROM above:

```
.lib      584 words relocated, 0 differ outside .text
.data    1589 words relocated, 0 differ
.inflate    0 words relocated, 0 differ outside .text
.game    4309 words relocated, 0 differ outside .text
preamble: the ROM's
```

6482 words, and the route holds: the ROM plus the list is the relink byte for byte outside `.text`. Inside `.text`,
672 words of lib, 72 of inflate and 18099 of game differ: instructions that carry an address (`lui`/`addiu` pairs,
`jal`). The recompiler reads code from the relink, and the game reads its own code as data only in the piracy checks
below, which sum the retail bytes they were made from.

## Symbols

`syms/dump.toml` (5686 functions) and `syms/data_dump.toml` (data symbols) are N64Recomp's `--dump-context` of
`build/relink/stage1.elf`, by N64Recomp at upstream commit 81213c1 (the one the image builds). Their
`rom` addresses are offsets in the relink's `stage1.bin` (`objcopy -O binary`), where lib, data and game sit
uncompressed past 32 MiB (`.lib` at 0x2000000, `.game` at 0x20ac170), so a recompilation reads its code from the
relink, built where the ROM is.

N64Recomp takes a symbol as a function only when it has a type and a size, which IDO's C functions have and the
decomp's assembly doesn't, so `tools/syms.py` writes N64Recomp's config with:

- a size for each of the 114 assembly functions, to the next function;
- the code no symbol names: IDO writes no ELF symbol for a static function, and some assembly calls its own local
  labels. 15 of them, found where code reaches them (12 subroutines in `modelasm.s`, one each in `lib_2f490.s`,
  `xprintf.c` and `camdraw.c`), named for their object file and address (`xprintf_8004E690`);
- a function that falls through into the next one sized to take it in, so the recompiled one runs to a return:
  `cosf` into `sinf` (as GoldenEye's), each `mtx4_*_in_place` into its `mtx4_*`, `mtx00015be0`, `func0002f560`, and
  the `modelasm` routines. `__osCleanupThread` ends in a call to `osDestroyThread`, which never returns, and isn't
  stretched.

`tools/syms.py check` then reads the dump back: every word of code that isn't a nop is in a function, and every `jal`
lands on the start of one.

## What still needs the TLB

Outside the VM code (`src/lib/vm.s`, `src/lib/vminit.c`, `src/lib/boot.c`, `src/lib/ultra/os/exceptasm.s`), which the
recompilation replaces, as of the pinned decomp commit. Two scans of the relink back the list: its code loads a
retail address (`lui` 0x7000-0x702f or 0x7f00-0x7f1f) only in `vm_boot`, `vm_handle_miss`, `vm_init` and `boot`, plus
three values that aren't addresses (a length limit in `rzip.s`, two menu colours); and of the 19 words outside code
that hold a value in those ranges (`tools/relocs.py scan`), none is an address (strings, the audio and sine tables, AI
command bytes, menu colours, microcode data).

Changed by the relink:

- `ld/pd.ld`: the lib, inflate and game addresses, and `vm_boot_from_preamble`, now `vm_boot` itself.
- `src/lib/segments.s:32-36`: `seg_inflate` jumps to `inflate1173` at its link address, so it follows inflate to
  0x80200000.

For the recompilation to handle (card 2 on):

- `src/lib/main.c:626,637-638`: `vm_init()`, then the heap runs from the end of `.bss` to `g_VmMarker`, which only
  `vm_init` sets (the start of game, 0x80220000, on 8 MB). Without `vm_init`, `g_VmMarker` has to be set.
- `src/lib/main.c:405-420`: the 4 MB path places the copyright framebuffer below the VM's pages and zip buffer. The
  8 MB path doesn't.
- `src/game/vmstats.c:13-31`: resets `g_VmNumTlbMisses` and the page counters on 4 MB, so they have to exist.
- The game segment is zipped as 4 KB pages, each with a 2-byte checksum (`lib/perfect_dark/tools/mkrom/game.c`), which
  `vm.s` and `vminit.c` unzip: whatever loads the game in their place unzips the same pages.
- lib's first 0x2000 bytes are raw in the ROM and the rest zipped (`tools/mkrom/pack.c`), and `boot.c` unzips it with
  inflate.
- The piracy checks. Twelve sum the code between two functions (`sched.c:404`, `cheats.c:407`, `bot.c:366`,
  `chr.c:2063` and `4871`, `lv.c:470`, `body.c:547`, `game_006900.c:61`, `chraction.c:7632`, `bondgun.c:3766`,
  `explosions.c:207`, `bg.c:2094`) against a constant the decomp compiles as a placeholder (0x99aabbcc) and its mkrom
  fills in only in the ROM it makes (`tools/mkrom/piracy.c`). They pass when RDRAM holds the ROM's code at those
  functions' relinked addresses and the relink holds mkrom's constants, which `tools/piracy.py` computes as mkrom does
  and writes into `build/relink/stage1.bin`; they equal the ROM's own. The others read the ROM at 0xa5c, 0x454 and 0xdc0 (`lv.c:1947`, `objectives.c:108`, `mtx_c.c:309`, `bg.c:1587`,
  `propobj.c:14210`), XOR loads with the ROM's word at 0x340 (`dma.c:94`), or compare `osCicId` with 6105
  (`propobj.c:18836`). None depends on an address; on failure each corrupts code or data.

Harmless or dead in `ntsc-final`: `VM_VIRTUAL_ADDR` (`src/include/lib/vm.h:27`, used only by the VM code); the
addresses in `src/game/debug2.c:436-441` (DEBUG only); `crash.c`'s range checks from `vm_boot` to the end of lib
(DEBUG only, and still right); `osVirtualToPhysical` (KSEG0 addresses never reach its TLB probe); `osMapTLB` (called
only by `vminit.c`) and `osMapTLBRdb` (`initialize.c:55`, the debugger's mapping).

## The desktop program

```bash
tools/build_decomp.sh path/to/pd.z64        # once, and again when the decomp or syms/ change
tools/build_linux.sh                        # [cpus]
tools/run_linux.sh path/to/pd.z64 120 10    # [seconds, 60] [seconds between screenshots, 5]
```

`tools/build_linux.sh` builds the image `tools/docker/Dockerfile.linux`, runs `tools/recompile.sh` (N64Recomp with
`us.toml` into `RecompiledFuncs/`, RSPRecomp with `aspMain.us.toml` into `rsp/`) and the game's CMake build into
`build/linux/PerfectDarkRecompiled`. `tools/run_linux.sh` runs it where there is no display, on Xvfb, with Mesa's
lavapipe on the CPU (`tools/linux_env.sh`), which draws a level at a few frames a minute. It never takes WSL2's GPU
(`/dev/dxg`, through Mesa's Dozen): a device lost there froze the whole Windows host (Not done yet). On a GPU the game
plays on Windows, below, or as its page. It runs with `--rom` (store
the ROM as the launcher's Load ROM does) and `--boot` (start the game without the launcher), and writes screenshots
to `build/shots/`, the log to `build/run.log` and the sound to `build/sound.wav` (SDL's disk audio driver).

To play it by hand, `tools/play_linux.sh` leaves that container running between commands: `start` (in a window of
a size given, 1280x720 for widescreen), `hold` (keys, by xdotool's names, held together for a time), `mouse` and
`click` (the mouse's motion and buttons), `shot`, `peek` (the game's variables, `tools/peek.py`) and `stop`, which
writes `build/play/sound.wav`. Saves and settings go to `build/home`, which the next `start` finds. The keys are
RecompFrontend's defaults as `src/mouse.cpp` changes them: A space, B e and r, Z q, Start return, L none,
R left shift, the stick w s and the left and right arrows, the C buttons a d and the up and down arrows, the d-pad i j k l, and
Escape opens the recomp's own menu. With Mouse Sensitivity above 0% the mouse looks and its buttons fire and aim.

### What it plays

With an NVIDIA desktop GPU through Dozen, *dataDyne Central - Defection* plays from its briefing to its end screen
("Completed", Agent, 3:55). The play had sound throughout, its guards fought back, a lift rode the whole height of the
building, and the outro played. The menus work: a new agent file, the Perfect Menu, mission select, difficulty, the
briefing, the retry menu, Next Mission and the recomp's menu. The save survives a restart. The next start
loads the agent file and goes straight to the Perfect Menu, its mission select shows Defection's Agent star, and
*dataDyne Research - Investigation* is unlocked (and plays). The game finds the Expansion Pak (`g_Is4Mb` 0), so the
MP3 player is on. Defection's and Investigation's skies are a fill and stars, which draw. The clouds and water of
other levels draw too (`patches/sky.c`), seen in Villa's and Chicago's intros and in Chicago in play, each reached
by setting `g_MainChangeToStageNum` while the game ran. rt64#198's flicker hasn't been seen.

The frame rate, from `g_Vars` read while it runs (`thisframestartt` per frame, `diffframe60` its VIs, `lvframe240`
the game's clock), with the host idle, is 59-60 a second in the levels by the game's own clock, every frame one VI:
Defection 59.9, Investigation 60.0, Chicago 59.2, the menus 58-60. The game counts each frame's length in VIs and its
clock keeps to real time, so nothing in the game runs too fast and no fix for a rate above the console's is needed.
Each frame tells RT64 the game's rate, the fastest of its last three frames' (`patches/frame_rate.c`), and RT64
interpolates to the display's. As the game is, frames
take two VIs, even in a menu that draws little, because of its own controller reads: a tick that finds no new sample
restarts the controller polling (`joy_debug_joy`, `joy.c`), which takes two VIs, and the retraces queued meanwhile
start the next tick before the next sample, so the ticks after it miss too. `patches/frame_rate.c` puts such a tick off
until the poll that is out has its sample, at the next VI; without it the same play draws Defection at 38.4,
Investigation at 30.0 and Chicago at 48.8 ([web.md](web.md), Frame rate).

### On Windows

The program builds for Windows x64 with clang-cl and draws through D3D12. The decomp and `patches/` still build in
WSL2: the decomp's build is Linux's, and LLVM's Windows releases have no MIPS target. Git for Windows checks text out
with CRLF line endings, which the decomp's build can't read, so clone with them off, in the repository and in each
submodule:

```bash
git -c core.autocrlf=false clone --recurse-submodules https://github.com/n64hub/perfect-dark-recomp
cd perfect-dark-recomp
git config core.autocrlf false
git submodule foreach --recursive git config core.autocrlf false
```

Then, in that checkout:

```bash
tools/build_decomp.sh /mnt/c/path/to/pd.z64   # in a WSL2 shell (Docker, as above)
tools/build_windows.sh                        # in Git Bash: build/windows/PerfectDarkRecompiled.exe; [cpus]
tools/package_windows.sh                      # in Git Bash: build/PerfectDarkRecompiled-windows-x64.zip
```

`tools/build_windows.sh` needs clang-cl, lld-link, CMake (3.20-3.31) and Ninja on `PATH`, the MSVC libraries and
the Windows SDK (from Visual Studio or its Build Tools, or else through `INCLUDE` and `LIB`), and `wsl`, whose default
distro has clang, ld.lld and make (`sudo apt install clang lld make`) for `patches/` (`CMakeLists.txt`,
`PATCHES_MAKE`). It has been built with LLVM 23.1.4, CMake 3.31.10, Ninja 1.13.2 and an
[xwin](https://github.com/Jake-Shadle/xwin) 0.10 copy of the MSVC libraries and the Windows SDK 10.0.26100, with
Visual Studio itself not tried. Without WSL, `MIPS_CC` (a Windows clang with a MIPS backend, such as MSYS2's clang64)
and `MIPS_LD` (an ld.lld) with GNU make on `PATH` build `patches/` instead; the decomp's build, its relink and the
headers it generates (`build/relink/`, `lib/perfect_dark/build/ntsc-final/rsp/`, `lib/perfect_dark/src/generated/`)
then come from a Linux checkout of the same commit.
`tools/package_windows.sh` zips the program, the DLLs beside it (SDL2, dxcompiler,
dxil), `assets/`, `recompcontrollerdb.txt` and `LICENSE`, and nothing from the ROM: the player picks theirs with Load
ROM. The program needs a recent Visual C++ 2015-2022 redistributable (x64), as the MSVC libraries it links with
(14.44) are newer than older installs. It keeps its data (the stored ROM, saves, settings) in
`%LOCALAPPDATA%\PerfectDarkRecompiled`, or beside itself when a `portable.txt` is there.

With an NVIDIA desktop GPU, the zip, unpacked and run with `portable.txt`, plays: the launcher, Load ROM (Windows'
file dialog), Start Game, the intro, a new agent file saved to the Game Pak, the Perfect Menu, *dataDyne Central -
Defection* on Agent from its briefing, with a minute and a half of walking, turning, strafing and firing, and sound
throughout (written by SDL's disk audio driver: the real device, WASAPI, hasn't been listened to). The window's close
button asks to quit and Quit closes it. The next start finds the stored ROM, and Start Game goes straight to the
Perfect Menu with the agent file loaded.

### Releases

`tools/release.sh windows` (Git Bash) and `tools/release.sh linux` (Linux with Docker) build and pack the release's
two archives into `out/`, from the same commit of `main`, and `tools/release.sh publish` publishes them as the GitHub
release `v<version_string>` (`src/main.cpp`) with `releases/v<version>.md` as its notes. Its header says what each
archive holds and what publishing checks first.

### What the build needs that isn't obvious

- **The patches link first** (`CMakeLists.txt`). A patch has the name of the game function it replaces, and every
  recompiled function is weak, so the linker keeps the first definition it sees: after `RecompiledFuncs` the game's
  own `vm_boot` (a stub here), `boot` and `vm_init` run, and nothing boots.
- **FR on in every thread** (`src/register_overlays.cpp`). IPL3 and the game's `osCreateThread` set the status
  register's FR bit, and the game's code relies on it: doubles in odd floating-point registers, singles in both of a
  pair (`src/game/mtxf2lbulkasm.s`). The runtime starts threads with FR off, where those overlap, and every matrix
  came out wrong (the logos drew as a line, the demo's rooms as streaks). N64Recomp's `CHECK_FR` asserts would have
  said so, but a release build compiles them out.
- **`ULTRAMODERN_HOLD_BLOCKED_MESSAGES` on** (`CMakeLists.txt`), as the page always has it. Off, an external message
  to a full queue goes back into the runtime's queue: the scheduler's audio timer (`sched.c`, every other retrace,
  to the audio thread's queue, which nothing reads until the game has loaded) kept the idle thread taking it again
  and again, and `main_init`'s own timer message never came.
- **Relocating through a sign-extended base** (`src/pd_natives.cpp`). `MEM_W` takes a 64-bit register value; a
  32-bit sum is zero-extended and lands 4 GB past RDRAM.
- **No hardware reads.** `amgr_handle_frame_msg` reads `AI_LEN_REG` itself: `us.toml` drops the load and a hook puts
  `osAiGetLength`'s value in its place. Two piracy checks read the PI and the ROM through KSEG1 (`patches/piracy.c`
  reads the player's ROM instead, `recomp_pd_read_rom`), and `osPiReadIo` (`src/pd_natives.cpp`) reads it too.
- **The piracy checks** (the list above): `osCicId` is 6105 (`pd::on_init`), the ROM words they compare come from the
  player's ROM, boot's own check is left out of `patches/boot.c`, and the code checksums sum RDRAM, which holds the
  ROM's code, against mkrom's constants (`tools/piracy.py`). Each failure rewrites code in RDRAM (which the
  recompiled code never runs, but a later check sums), corrupts a DMA's data, or erases the EEPROM. With the
  placeholder in place every code checksum failed: `lv_reset`'s wrote an empty file system over the start of the
  EEPROM each time a stage loaded, so the Game Pak read Full with no files on it, and five words of `bg_reset` in RDRAM
  read 0x12.
- **`modelasm.s`**, whose pieces branch into each other: `us.toml` stubs them, and `patches/modelasm.c` puts the
  decompilation's C for it behind its one entry.
- **The libultra the runtime leaves out** (`src/pd_libultra.cpp`): the controller pak's internals answer that none is
  plugged in, two of IDO's 64-bit conversions, the TLB and RDP counters. C library names the game defines are renamed
  in `us.toml`.
- **The paks** (`patches/paks.c`): a Rumble Pak in each connected controller, no Controller Pak and no Transfer Pak.
  The game asks for a Controller Pak first and tries the Rumble Pak only when that answers with another device, so a
  controller answers as a console's with a Rumble Pak does. Saves go to the cartridge's EEPROM (16 Kbit), which the
  runtime keeps in `saves/` under the program's data.
- **The audio microcode** (`aspMain.us.toml`): Rare's `aspMain` with an MP3 decoder as a second overlay, which
  RSPRecomp swaps in where the code DMAs it over the main one. `src/main.cpp` queues the samples to SDL and gives the
  game what is still queued (`get_frames_remaining`). When more than 100 ms is queued it drops whole frames, since
  dropping part of one put every later sample out of step.
- **The sky's clouds and water** (`patches/sky.c`): the game draws them as raw RDP triangles (`sky_render_tri`),
  which RT64 drops, so a level with clouds showed none. The patch draws the same polygons as 3D triangles, as the
  decompilation's PC port and GoldenEye's recompilation do. Its header says how they're kept inside the far plane, in
  a Vtx's range, at the clouds' height and in the view.
- **The world's origin** (`patches/origin_interpolation.c`): the game draws the world relative to the room the camera
  is in, so every matrix jumps when the camera crosses into another room, and RT64 interpolated the frames between
  with the world shifted by part of the jump (through a door, the room beyond gone to the clear colour for a frame).
  Each frame's display list starts with the matrix groups RT64 gives untagged matrices, with an id that changes when
  the origin moves, so that frame is shown as it is.
- **The IR scanner's lens** (`patches/ir_lens.c`): the game copies the view into itself a row at a time, and a row
  that crosses the semicircle in the middle in three pieces, each loaded from mid-row after a draw into that row.
  RT64 found no copy of the framebuffer on the GPU for those pieces and waited for it in RAM for each one, so the game
  drew 2 frames a second on the page with the IR scanner on. The patch loads such a row once and draws the pieces from
  it. Every other full-screen effect (the goggles, the X-ray and combat boost blurs, the Slayer's and the cutscenes'
  interlace, CamSpy's fisheye, the horizon scanner) loads its rows whole, which RT64 draws from that copy.

### Not done yet

- The GPU device was lost twice under Dozen. After 22 minutes of play the log said `D3D12: Removing Device.` and then
  every Vulkan call failed, and the program crashed; the NVIDIA driver logged an error in the same second, and a later
  17-minute play never lost it. A sweep through missions' starts lost it again (`vkWaitForFences`
  `VK_ERROR_DEVICE_LOST`): the program stayed in the host's GPU driver, where nothing could stop it,
  and the whole Windows host froze until it restarted. So `tools/linux_env.sh` takes lavapipe only. Whether Dozen
  (which Mesa calls non-conformant) or the game's drawing causes it isn't known. The Windows build draws through D3D12
  without Dozen, but hasn't been played that long.
- Speech: the guards' lines start and play to their end in the game's MP3 player (`g_SndCurMp3`, `g_Mp3Vars`), and
  the sound has no gaps, but nobody has listened to a line.
- Rumble on a pad, the Windows build's sound through a real device, and a Windows build with Visual Studio's own
  compiler.
- The sky patch's clouds and water haven't been compared with a console's. The water's texture scale is the PC
  port's (a tenth of what the game computes), and nobody has played a level with clouds from start to end.
- `frametime_calculate` (`src/game/timing.c`) spins on `osGetCount`, but `main_loop` calls a tick only half a frame
  after the last, so it spins little; it has no yield. On the page it spun 9.7 ms in 10 s of the logos and none in
  Defection ([web.md](web.md), Frame rate).

### Debugging it

The program ignores SIGTERM (`run_linux.sh` follows with SIGKILL). gdb with breakpoints set from the start never let
the game boot in 100 s; attaching once the log shows the relocations works, and a game thread's frames (`rdram`)
give RDRAM's host address, from which `/proc/<pid>/mem` reads any of the game's variables (addresses in
`build/relink/pd.map`) while it runs: the title's mode (`g_TitleMode`) and `g_Vars.lvframenum` say where it is. The
program logs that address itself (`[pd] rdram at`), and `tools/peek.py` reads variables by name from it.
