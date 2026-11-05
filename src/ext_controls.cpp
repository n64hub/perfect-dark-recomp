// Ext, the control style a modern pad plays as the decompilation's PC port plays it: the program's side (the game's is
// patches/ext_controls.c, which says what each button does there). While the game says Ext plays a port's player as
// they walk in a level (recomp_pd_ext_take, once a tick; a port it stops saying so for 100 ms, as in the menus, plays
// as before), that port's controller reads (ext_remap_input):
//  - its stick from the pad's right stick, as the PC port swaps them, so it looks and turns as the game's 1.2 stick;
//    the stick it read before (the left stick, and the keyboard's W, S and arrows) is kept for the take, to walk and
//    strafe;
//  - its buttons as the PC port's layout: RT is Z, LT is R, A and RB are B, LB is A, Start is Start and the d-pad the C
//    buttons, whatever the Controls tab binds the pad's buttons to (the menus keep those). The keyboard's keys are its
//    own bindings still, and the mouse's buttons are added after (src/mouse.cpp);
//  - X, Y, B and a click of the left stick, the pad's buttons N64 pads don't have, as presses for the take (EXT_*).
// The right stick then isn't the C buttons, as the PC port's isn't.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>

#include "SDL.h"

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "recompinput/input_mapping.h"
#include "recompinput/profiles.h"
#include "recompinput/recompinput.h"
#include "pd.h"

namespace {
    using recompinput::GameInput;
    using recompinput::InputField;

    constexpr auto stale = std::chrono::milliseconds(100);
    constexpr int max_ports = 4;

    // patches/ext_controls.c's EXT_*.
    constexpr uint32_t ext_reload = 0x01;
    constexpr uint32_t ext_next_weapon = 0x02;
    constexpr uint32_t ext_prev_weapon = 0x04;
    constexpr uint32_t ext_radial = 0x08;
    constexpr uint32_t ext_crouch = 0x10;

    // The N64's buttons, in RecompFrontend's order from GameInput::N64_BUTTON_START.
    constexpr std::array<uint16_t, (size_t)GameInput::N64_BUTTON_COUNT> n64_buttons = {
        0x8000, 0x4000, 0x2000, 0x0020, 0x0010, 0x1000, 0x0008, 0x0004, 0x0002, 0x0001, 0x0800, 0x0400, 0x0200, 0x0100,
    };
    constexpr uint16_t n64_a = 0x8000;
    constexpr uint16_t n64_b = 0x4000;
    constexpr uint16_t n64_z = 0x2000;
    constexpr uint16_t n64_r = 0x0010;
    constexpr uint16_t n64_start = 0x1000;
    constexpr uint16_t n64_c_up = 0x0008;
    constexpr uint16_t n64_c_down = 0x0004;
    constexpr uint16_t n64_c_left = 0x0002;
    constexpr uint16_t n64_c_right = 0x0001;

    struct PortState {
        std::chrono::steady_clock::time_point playing_until{};
        float move_x = 0.0f;
        float move_y = 0.0f;
        uint32_t held = 0;
        uint32_t pressed = 0;
    };

    std::mutex mutex;
    std::array<PortState, max_ports> ports;

    bool pad(int port, SDL_GameControllerButton button) {
        return recompinput::get_input_digital(port, InputField::controller_digital(button));
    }

    bool trigger(int port, SDL_GameControllerAxis axis) {
        return recompinput::get_input_digital(port, InputField::controller_analog(axis, true));
    }

    // The keyboard's own N64 buttons, by its bindings, on the port it plays.
    uint16_t keyboard_buttons(int port) {
        int profile = port == pd::keyboard_port() ? pd::keyboard_profile(port) : -1;
        uint16_t buttons = 0;
        for (size_t i = 0; profile >= 0 && i < n64_buttons.size(); i++) {
            GameInput input = (GameInput)((size_t)GameInput::N64_BUTTON_START + i);
            for (size_t j = 0; j < recompinput::num_bindings_per_input; j++) {
                const InputField& field = recompinput::profiles::get_input_binding(profile, input, j);
                if (field.input_type == recompinput::InputType::Keyboard && recompinput::get_input_digital(port, field)) {
                    buttons |= n64_buttons[i];
                }
            }
        }
        return buttons;
    }
}

void pd::ext_remap_input(int port, uint16_t* buttons, float* x, float* y) {
    if (port < 0 || port >= max_ports || recompinput::game_input_disabled()) {
        return;
    }

    using namespace recompinput;
    uint32_t held =
        (pad(port, SDL_CONTROLLER_BUTTON_WEST) ? ext_reload : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_NORTH) ? ext_next_weapon : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_EAST) ? ext_prev_weapon : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? ext_radial : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_LEFTSTICK) ? ext_crouch : 0);

    std::lock_guard lock{ mutex };
    PortState& state = ports[port];
    bool playing = std::chrono::steady_clock::now() < state.playing_until;
    if (playing) {
        state.pressed |= held & ~state.held;
    }
    state.held = held;
    if (!playing) {
        return;
    }

    state.move_x = *x;
    state.move_y = *y;

    float rx = 0.0f;
    float ry = 0.0f;
    get_right_analog(port, &rx, &ry);
    *x = rx;
    // SDL's down is positive, the N64's up.
    *y = -ry;

    *buttons = keyboard_buttons(port) |
        (trigger(port, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) ? n64_z : 0) |
        (trigger(port, SDL_CONTROLLER_AXIS_TRIGGERLEFT) ? n64_r : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_SOUTH) || pad(port, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? n64_b : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? n64_a : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_START) ? n64_start : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_DPAD_UP) ? n64_c_up : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? n64_c_down : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? n64_c_left : 0) |
        (pad(port, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? n64_c_right : 0);
}

// recomp_pd_ext_take(s32 port, s32 playing, s32 *movex, s32 *movey): whether Ext plays the port's pad from now (the
// player walks in a level), and the stick it read before, in the N64's units, up positive; returns the EXT_* pressed
// since the last take while it played.
extern "C" void recomp_pd_ext_take(uint8_t* rdram, recomp_context* ctx) {
    int port = _arg<0, int32_t>(rdram, ctx);
    bool playing = _arg<1, int32_t>(rdram, ctx) != 0;
    int32_t* move_x_out = _arg<2, int32_t*>(rdram, ctx);
    int32_t* move_y_out = _arg<3, int32_t*>(rdram, ctx);
    uint32_t pressed = 0;
    float move_x = 0.0f;
    float move_y = 0.0f;

    if (port >= 0 && port < max_ports) {
        std::lock_guard lock{ mutex };
        PortState& state = ports[port];
        auto now = std::chrono::steady_clock::now();
        if (now < state.playing_until) {
            pressed = state.pressed;
            move_x = state.move_x;
            move_y = state.move_y;
        }
        state.pressed = 0;
        state.playing_until = playing ? now + stale : std::chrono::steady_clock::time_point{};
    }

    // A full tilt is about 80 on an N64 pad, the walk's full speed 70.
    *move_x_out = (int32_t)std::lround(std::clamp(move_x, -1.0f, 1.0f) * 80.0f);
    *move_y_out = (int32_t)std::lround(std::clamp(move_y, -1.0f, 1.0f) * 80.0f);
    _return<int32_t>(ctx, (int32_t)pressed);
}
