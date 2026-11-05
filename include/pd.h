#ifndef PD_H
#define PD_H

#include <string_view>

#include "recomp.h"
#include "librecomp/config.hpp"

namespace pd {
    inline constexpr std::u8string_view program_id = u8"PerfectDarkRecompiled";
    inline constexpr std::string_view program_name = "Perfect Dark: Recompiled";
    // XXH3 (64-bit) of the one ROM the game takes, as the runtime hashes a ROM the player loads (src/main.cpp).
    inline constexpr uint64_t rom_xxh3 = 0x7F97EB141007861EULL;

    void register_overlays();
    void register_patches();
    void init_config();

    // Mouse look and aim (src/mouse.cpp): the keyboard's default bindings and the event watch, called before the
    // config's tabs are made; the Mouse Sensitivity option, added to the General tab; and the controller callback,
    // which adds the mouse's buttons to controller 1.
    void init_mouse();
    void add_mouse_option(recomp::config::Config& general);
    bool get_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out);
    // The port the keyboard and mouse play, and the keyboard's profile on a port (src/mouse.cpp).
    int keyboard_port();
    int keyboard_profile(int port);
    // Ext's pad layout (src/ext_controls.cpp): plays a port's controller as the PC port's while the game says Ext
    // plays its player, after get_input reads it.
    void ext_remap_input(int port, uint16_t* buttons, float* x, float* y);

    // The game entry's on_init_callback (src/register_overlays.cpp): before the preamble runs, every code section at
    // the address the relink links it at, and the values IPL3 leaves in low RAM that the runtime doesn't.
    void on_init(uint8_t* rdram, recomp_context* ctx);
    // The game entry's thread_create_callback (src/register_overlays.cpp): each game thread starts with the status
    // register's FR bit set, as the game's osCreateThread gives it.
    void on_thread_create(uint8_t* rdram, recomp_context* ctx);
}

#endif
