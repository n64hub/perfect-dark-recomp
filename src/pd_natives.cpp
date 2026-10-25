// What the patches call (patches/syms.ld) and the libultra functions the runtime leaves to the game.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "librecomp/game.hpp"
#include "librecomp/addresses.hpp"
#include "ultramodern/error_handling.hpp"

// pd_relocs.h (CMakeLists.txt, from syms/relocs.txt and syms/data_dump.toml): pd_relocs[], each segment's
// relinked start and end, PD_SEG_*.
#include "pd_relocs.h"

namespace {

// A word that points into retail lib, inflate or game, at their TLB-mapped addresses (lib/perfect_dark/ld/pd.ld),
// moves by as much as its segment moved in the relink (docs/building.md, The relocation list).
struct Move {
    uint32_t retail_start;
    uint32_t start;
    uint32_t end;
};

constexpr Move moves[] = {
    { 0x70001050u, PD_LIB_START, PD_LIB_END },
    { 0x70200000u, PD_INFLATE_START, PD_INFLATE_END },
    { 0x7F000000u, PD_GAME_START, PD_GAME_END },
};

constexpr uint32_t segment_starts[] = { PD_LIB_START, PD_DATA_START, PD_INFLATE_START, PD_GAME_START };

} // namespace

// recomp_pd_relocate(segment): once the game has unzipped a segment, its listed words go from retail to relinked.
extern "C" void recomp_pd_relocate(uint8_t* rdram, recomp_context* ctx) {
    uint32_t segment = (uint32_t)ctx->r4;
    if (segment >= sizeof(segment_starts) / sizeof(segment_starts[0])) {
        ultramodern::error_handling::message_box("recomp_pd_relocate: no such segment");
        std::exit(EXIT_FAILURE);
    }
    // A register's value, sign-extended as MEM_W expects: a 32-bit sum would be zero-extended, 4 GB past RDRAM.
    gpr base = (int32_t)segment_starts[segment];
    size_t moved = 0;
    size_t unmoved = 0;
    for (const PdReloc& reloc : pd_relocs) {
        if (reloc.segment != segment) {
            continue;
        }
        int32_t offset = (int32_t)reloc.offset;
        uint32_t value = (uint32_t)MEM_W(offset, base);
        bool found = false;
        for (const Move& move : moves) {
            if (value >= move.retail_start && value <= move.retail_start + (move.end - move.start)) {
                MEM_W(offset, base) = (int32_t)(value - move.retail_start + move.start);
                found = true;
                break;
            }
        }
        if (found) {
            moved++;
        }
        else {
            unmoved++;
            fprintf(stderr, "[pd] relocation %u+0x%06X holds 0x%08X, no retail address\n", segment, reloc.offset, value);
        }
    }
    fprintf(stderr, "[pd] segment %u: %zu pointers moved, %zu not\n", segment, moved, unmoved);
    // Where RDRAM is in the program's memory, for tools/peek.py, which reads the game's variables while it runs.
    if (segment == PD_SEG_LIB) {
        fprintf(stderr, "[pd] rdram at %p\n", (void*)rdram);
    }
}

extern "C" void recomp_pd_crash(uint8_t* rdram, recomp_context* ctx) {
    char message[256];
    size_t i = 0;
    for (; i < sizeof(message) - 1; i++) {
        message[i] = (char)MEM_B(i, ctx->r4);
        if (message[i] == '\0') {
            break;
        }
    }
    message[i] = '\0';
    fprintf(stderr, "[pd] %s\n", message);
    ultramodern::error_handling::message_box(message);
    std::exit(EXIT_FAILURE);
}

// recomp_pd_read_rom(offset, dst): the word of the player's ROM at offset (patches/piracy.c).
extern "C" void recomp_pd_read_rom(uint8_t* rdram, recomp_context* ctx) {
    recomp::do_rom_pio(rdram, ctx->r5, recomp::rom_base + (uint32_t)ctx->r4);
}

// osPiReadIo(devAddr, data): N64Recomp takes it as one of the runtime's, which has only osEPiReadIo. It reads a word
// of the ROM, and the game's piracy checks compare the words they read (at 0x340, 0x454, 0xa5c, 0xdc0) with the ones
// they were built with, so it reads the player's ROM.
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t physical_addr = (uint32_t)ctx->r4 & 0x1FFFFFFF;
    if (physical_addr >= recomp::rom_base) {
        recomp::do_rom_pio(rdram, ctx->r5, physical_addr);
    }
    else {
        MEM_W(0, ctx->r5) = 0;
    }
    ctx->r2 = 0;
}

// recomp_pd_render_to_ram_late(): whether render to RAM's copy into RDRAM lands a display list late, as src/main.cpp
// sets it on the page, for the light glares' depth test (patches/artifacts.c).
extern "C" void recomp_pd_render_to_ram_late(uint8_t* rdram, recomp_context* ctx) {
#ifdef __EMSCRIPTEN__
    ctx->r2 = 1;
#else
    ctx->r2 = 0;
#endif
}

// recomp_pd_hold_at_30(): called once a tick (patches/frame_rate.c), whether the ticks after it are held to every other
// VI, 30 a second. On the page, while the game can't keep 60 up: RT64 draws few frames between those of a game that
// mixes frames of one and two VIs (it takes a frame's length from the shortest of the last three), so on a phone whose
// CPU held the intro to 32 frames a second, the page presented 41-42 a second (N64Recomp-browser's docs/phones.md).
// Held, every frame is two VIs and gets two. Two seconds in a row with 20-50 ticks each hold it (fewer is a load or a
// stall, and a single one a load's end), and it tries 60 again for a second 10 s later, twice as long after each try
// that didn't keep up, at most 80 s. As Rush 2049's recompilation does at its 60 fps (its src/frame_rate.cpp).
extern "C" void recomp_pd_hold_at_30(uint8_t* rdram, recomp_context* ctx) {
#ifdef __EMSCRIPTEN__
    static struct {
        bool held = false;      // at 30
        bool trying = false;    // at 60 again for a second
        uint32_t ticks = 0;     // in this second
        uint32_t seconds = 0;   // held so far
        uint32_t slow = 0;      // seconds in a row with 20-50 ticks
        uint32_t wait = 10;     // seconds held before the next try
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    } f;
    f.ticks++;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - f.start).count();
    if (elapsed >= 1.0) {
        const uint32_t ticks = uint32_t(f.ticks / elapsed + 0.5);
        f.ticks = 0;
        f.start = now;
        if (f.held) {
            if (++f.seconds >= f.wait) {
                f.held = false;
                f.trying = true;
                fprintf(stderr, "[frame rate] trying 60 again after %u s at 30\n", f.seconds);
            }
        }
        else if (ticks < 20) {
            // A load or a stall, not a game that can't keep up.
            f.slow = 0;
        }
        else if (ticks < 51) {
            if (++f.slow >= 2 || f.trying) {
                f.slow = 0;
                f.wait = f.trying ? std::min<uint32_t>(f.wait * 2, 80) : 10;
                f.held = true;
                f.trying = false;
                f.seconds = 0;
                fprintf(stderr, "[frame rate] %u frames in a second: 30 for %u s\n", ticks, f.wait);
            }
        }
        else {
            f.slow = 0;
            if (f.trying) {
                f.trying = false;
                f.wait = 10;
                fprintf(stderr, "[frame rate] %u frames in a second: kept\n", ticks);
            }
        }
    }
    ctx->r2 = f.held ? 1 : 0;
#else
    ctx->r2 = 0;
#endif
}

// recomp_pd_motor_init(mq, pfs, channel): the runtime's osMotorInit, for patches/paks.c: 0 when the controller has a
// Rumble Pak (src/main.cpp, get_connected_device_info), which then rumbles through the runtime's __osMotorAccess.
extern "C" void osMotorInit_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void recomp_pd_motor_init(uint8_t* rdram, recomp_context* ctx) {
    osMotorInit_recomp(rdram, ctx);
}
