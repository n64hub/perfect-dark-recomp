// Mouse look and aim: the program's side (the game's is patches/mouse.c), after GoldenEye's recompilation's
// mouse_keyboard.cpp. RecompFrontend locks the pointer to the game while Mouse Sensitivity is above 0% and gives a game
// only the motion since its last controller poll, which comes every retrace while Perfect Dark ticks at 30 or less; so
// the motion and the wheel are added up here from every SDL event, until the game's tick takes them:
//  - the motion, scaled as the PC port scales it (0.022/3.5 a count at its default, which is 50% here), its up and
//    down flipped by Invert Mouse Y, worded and stored as GoldenEye64Recomp-web's option;
//  - the wheel's notches, up positive;
//  - the left button as Z (fire) and the right as R (aim, and the menus' shoulder button) on controller 1 (get_input);
//  - whether the mouse's aim is held (recomp_pd_mouse_aim_held): the right button or a key on L or R, which the
//    patches turn into an aim that lets her walk, where a pad's L and R stay the game's aim mode;
//  - the C key's presses (recomp_pd_crouch_take), which crouch, a key of its own as GoldenEye64Recomp-web's Crouch
//    and the only one here that plays with the pointer free too, as the keyboard's bindings do.
// Nothing reaches the game while the frontend's menus have the input or (but for C) the pointer isn't locked, and what
// the game didn't take for 100 ms (a cutscene, a pause, the radial menu) is dropped, so it doesn't turn her all at once
// after.

#include <algorithm>
#include <chrono>
#include <mutex>

#include "SDL.h"

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "recompinput/input_mapping.h"
#include "recompinput/profiles.h"
#include "recompinput/recompinput.h"
#include "recompui/config.h"
#include "pd.h"

namespace {
    constexpr auto stale = std::chrono::milliseconds(100);
    // At most this many notches wait for the game, so a spun wheel doesn't keep changing weapon for seconds.
    constexpr int max_wheel = 2;

    std::mutex mutex;
    float pending_dx = 0.0f;
    float pending_dy = 0.0f;
    int pending_wheel = 0;
    int pending_crouch = 0;
    std::chrono::steady_clock::time_point last_take{};
    std::chrono::steady_clock::time_point last_wheel_take{};
    std::chrono::steady_clock::time_point last_crouch_take{};

    bool game_has_mouse() {
        return !recompinput::game_input_disabled() && SDL_GetRelativeMouseMode() == SDL_TRUE;
    }

    int watch_events(void*, SDL_Event* event) {
        switch (event->type) {
        case SDL_MOUSEMOTION:
            if (game_has_mouse()) {
                std::lock_guard lock{ mutex };
                pending_dx += event->motion.xrel;
                pending_dy += event->motion.yrel;
            }
            break;
        case SDL_MOUSEWHEEL:
            if (game_has_mouse() && event->wheel.y != 0) {
                int notches = (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -event->wheel.y : event->wheel.y;
                std::lock_guard lock{ mutex };
                pending_wheel = std::clamp(pending_wheel + notches, -max_wheel, max_wheel);
            }
            break;
        case SDL_KEYDOWN:
            if (event->key.keysym.scancode == SDL_SCANCODE_C && !event->key.repeat
                    && !recompinput::game_input_disabled()) {
                std::lock_guard lock{ mutex };
                pending_crouch++;
            }
            break;
        }
        return 0;
    }

    void set_keyboard(recompinput::GameInput input, std::initializer_list<SDL_Scancode> keys) {
        std::vector<recompinput::InputField> fields;
        for (SDL_Scancode key : keys) {
            fields.push_back(recompinput::InputField::keyboard(key));
        }
        recompinput::set_default_mapping_for_keyboard(input, fields);
    }

    constexpr uint16_t n64_button_z = 0x2000;
    constexpr uint16_t n64_button_r = 0x0010;

    const std::string mouse_invert_y = "mouse_invert_y";
    enum class InvertY {
        Off,
        On,
        OptionCount
    };
    const std::vector<recomp::config::ConfigOptionEnumOption> invert_y_options = {
        { InvertY::Off, "Off" },
        { InvertY::On, "On" },
    };

    bool get_mouse_invert_y() {
        auto value = recompui::config::get_general_config().get_option_value(mouse_invert_y);
        return static_cast<InvertY>(std::get<uint32_t>(value)) == InvertY::On;
    }
}

void pd::init_mouse() {
    using recompinput::GameInput;
    // RecompFrontend's defaults, but as a PC player expects with the mouse turning too: A and D strafing (the C
    // buttons' left and right), the arrows turning, and E and R on B, which uses what is in front of Joanna (a door, a
    // console, an object) and reloads otherwise. The game reads L and R alike (both aim, both are the menus'
    // shoulder), so L gets no key and R, aiming as the right button does, moves to B's left shift. Only defaults:
    // bindings the player changed and the Controls tab still apply.
    set_keyboard(GameInput::X_AXIS_NEG, { SDL_SCANCODE_LEFT });
    set_keyboard(GameInput::X_AXIS_POS, { SDL_SCANCODE_RIGHT });
    set_keyboard(GameInput::C_LEFT, { SDL_SCANCODE_A });
    set_keyboard(GameInput::C_RIGHT, { SDL_SCANCODE_D });
    set_keyboard(GameInput::B, { SDL_SCANCODE_E, SDL_SCANCODE_R });
    set_keyboard(GameInput::L, {});
    set_keyboard(GameInput::R, { SDL_SCANCODE_LSHIFT });

    SDL_AddEventWatch(watch_events, nullptr);
}

void pd::add_mouse_option(recomp::config::Config& general) {
    // The frontend's own option, by its id: the frontend locks the pointer while it is above zero. Then Invert Mouse Y.
    general.add_percent_number_option(
        recompui::config::general::options::mouse_sensitivity,
        "Mouse Sensitivity",
        "How far the mouse turns Joanna. At 50% a count turns her 0.055 degrees, as the Perfect Dark PC port's default "
        "does, less when zoomed. <b>0% is the original game</b>: no mouse, and the pointer stays free."
        "<br />"
        "<br />"
        "Left click fires and right click aims (Z and R): aiming shows the crosshair and zooms the weapons that zoom, "
        "and she walks, strafes and turns as she does without it, the mouse turning her. The wheel changes weapon, or "
        "zooms the sniper rifle while aiming. The game's Look Ahead is off while the mouse plays."
        "<br />"
        "<br />"
        "E or R (B) uses what is in front of her, a door or a console, and reloads otherwise; held, it changes the "
        "gun's function. Left Shift aims too, and C crouches, or stands her back up.",
        50.0
    );
    general.add_enum_option(
        mouse_invert_y,
        "Invert Mouse Y",
        "Moving the mouse up looks down.",
        invert_y_options,
        InvertY::Off
    );
}

bool pd::get_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    bool ret = recompinput::profiles::get_n64_input(controller_num, buttons_out, x_out, y_out);
    if (controller_num == 0 && game_has_mouse()) {
        uint32_t mouse = SDL_GetMouseState(nullptr, nullptr);
        if (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) {
            *buttons_out |= n64_button_z;
        }
        if (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
            *buttons_out |= n64_button_r;
        }
    }
    return ret;
}

// recomp_pd_mouse_playing(): whether the mouse plays: Mouse Sensitivity above 0% and the pointer locked to the game.
extern "C" void recomp_pd_mouse_playing(uint8_t* rdram, recomp_context* ctx) {
    _return<int32_t>(ctx, recompui::config::general::get_mouse_sensitivity() > 0.0 && game_has_mouse() ? 1 : 0);
}

// recomp_pd_mouse_aim_held(): whether the mouse's aim is held: the right button, or a key bound to L or R (Left Shift),
// while the mouse plays. Not a pad's L or R, which stay the game's aim mode, the stick moving its crosshair.
extern "C" void recomp_pd_mouse_aim_held(uint8_t* rdram, recomp_context* ctx) {
    using recompinput::GameInput;
    bool held = false;
    if (game_has_mouse()) {
        held = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
        int profile = recompinput::profiles::get_sp_keyboard_profile_index();
        for (size_t i = 0; profile >= 0 && i < recompinput::num_bindings_per_input; i++) {
            for (GameInput input : { GameInput::L, GameInput::R }) {
                held |= recompinput::get_input_digital(0, recompinput::profiles::get_input_binding(profile, input, i));
            }
        }
    }
    _return<int32_t>(ctx, held ? 1 : 0);
}

// recomp_pd_mouse_take(f32 *dx, f32 *dy): the motion since the last take, in the PC port's units, right and down
// positive (up with Invert Mouse Y).
extern "C" void recomp_pd_mouse_take(uint8_t* rdram, recomp_context* ctx) {
    float* dx_out = _arg<0, float*>(rdram, ctx);
    float* dy_out = _arg<1, float*>(rdram, ctx);
    float dx = 0.0f;
    float dy = 0.0f;
    {
        std::lock_guard lock{ mutex };
        auto now = std::chrono::steady_clock::now();
        if (now - last_take < stale) {
            dx = pending_dx;
            dy = pending_dy;
        }
        pending_dx = 0.0f;
        pending_dy = 0.0f;
        last_take = now;
    }
    // 50% is the PC port's default, 2.5.
    float scale = (float)(recompui::config::general::get_mouse_sensitivity() / 20.0) * (0.022f / 3.5f);
    *dx_out = dx * scale;
    *dy_out = (get_mouse_invert_y() ? -dy : dy) * scale;
}

// recomp_pd_mouse_take_wheel(): the wheel's notches since the last take, up positive.
extern "C" void recomp_pd_mouse_take_wheel(uint8_t* rdram, recomp_context* ctx) {
    int wheel = 0;
    {
        std::lock_guard lock{ mutex };
        auto now = std::chrono::steady_clock::now();
        if (now - last_wheel_take < stale) {
            wheel = pending_wheel;
        }
        pending_wheel = 0;
        last_wheel_take = now;
    }
    _return<int32_t>(ctx, wheel);
}

// recomp_pd_crouch_take(): the C key's presses since the last take.
extern "C" void recomp_pd_crouch_take(uint8_t* rdram, recomp_context* ctx) {
    int presses = 0;
    {
        std::lock_guard lock{ mutex };
        auto now = std::chrono::steady_clock::now();
        if (now - last_crouch_take < stale) {
            presses = pending_crouch;
        }
        pending_crouch = 0;
        last_crouch_take = now;
    }
    _return<int32_t>(ctx, presses);
}
