// The settings: RecompFrontend's stock General, Controls, Graphics and Sound tabs, with its default bindings but A and
// D strafing, and the enhancements' options (README, Modern enhancements), each with a value that plays as the
// console does:
//  - Mouse Sensitivity and Invert Mouse Y, on the General tab (src/mouse.cpp, patches/mouse.c), 0% the original;
//  - Field of View, on the Graphics tab (patches/fov.c), Original the game's 60 degrees;
//  - the Graphics tab's own Aspect Ratio and HUD Placement, which patches/widescreen.c follows, Original 4:3.
// And the natives the patches read them through.

#include <cmath>
#include <filesystem>
#include <algorithm>
#include <atomic>

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "recompui/config.h"
#include "recompui/recompui.h"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"
#include "util/file.h"
#include "pd.h"

namespace {
    enum class FieldOfView {
        Original,
        Fov70,
        Fov80,
        Fov90,
        Fov100,
        OptionCount
    };
    const std::vector<recomp::config::ConfigOptionEnumOption> field_of_view_options = {
        {FieldOfView::Original, "Original", "Original (60°)"},
        {FieldOfView::Fov70, "70", "70°"},
        {FieldOfView::Fov80, "80", "80°"},
        {FieldOfView::Fov90, "90", "90°"},
        {FieldOfView::Fov100, "100", "100°"},
    };
    const std::string field_of_view_option = "field_of_view";

    // The tangent of half the angle chosen over the tangent of half the game's 60 degrees; read on the game thread.
    std::atomic<float> fov_tan_scale{ 1.0f };
}

void pd::init_config() {
    std::filesystem::path app_dir = recompui::file::get_app_folder_path();
    if (!app_dir.empty()) {
        std::filesystem::create_directories(app_dir);
    }

    pd::init_mouse();

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = true;
    recomp::config::Config& general = recompui::config::create_general_tab(general_options);
    pd::add_mouse_option(general);
    recompui::config::create_controls_tab();

    recomp::config::Config& graphics = recompui::config::create_graphics_tab();
    graphics.add_enum_option(
        field_of_view_option,
        "Field of View",
        "How much of the level Joanna sees, from the top of the screen to the bottom. The game shows 60 degrees; a "
        "wider angle shows more around her, and the zoom of a gun or the Farsight magnifies as much as ever. "
        "Cutscenes and the menus keep the game's angle. Aspect Ratio's Expand already widens the view to the sides.",
        field_of_view_options,
        FieldOfView::Original
    );
    // Called on load too.
    graphics.add_option_change_callback(field_of_view_option,
        [](recomp::config::ConfigValueVariant cur_value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            static constexpr float degrees[] = { 60.0f, 70.0f, 80.0f, 90.0f, 100.0f };
            float fov = degrees[std::get<uint32_t>(cur_value)];
            fov_tan_scale = std::tan(fov * (float)M_PI / 360.0f) / std::tan(30.0f * (float)M_PI / 180.0f);
        });

    recompui::config::create_sound_tab();
    recompui::config::finalize();
}

// recomp_pd_widen_fov(fovy): the angle drawn for one the game sets, its half-angle's tangent scaled (patches/fov.c).
extern "C" void recomp_pd_widen_fov(uint8_t* rdram, recomp_context* ctx) {
    float fovy = _arg<0, float>(rdram, ctx);
    float scale = fov_tan_scale.load(std::memory_order_relaxed);
    if (scale == 1.0f || fovy <= 0.0f || fovy >= 180.0f) {
        _return(ctx, fovy);
        return;
    }
    float half = std::atan(std::tan(fovy * (float)M_PI / 360.0f) * scale);
    _return(ctx, std::min(half * 360.0f / (float)M_PI, 170.0f));
}

// recomp_pd_get_aspect_ratio(original): the aspect ratio RT64 draws the game's 3D at (patches/widescreen.c), as
// GoldenEye's recompilation's recomp_get_aspect_ratio: the window's with Expand, never narrower than the game's.
extern "C" void recomp_pd_get_aspect_ratio(uint8_t* rdram, recomp_context* ctx) {
    float original = _arg<0, float>(rdram, ctx);
    ultramodern::renderer::GraphicsConfig graphics_config = ultramodern::renderer::get_graphics_config();
    int width, height;
    recompui::get_window_size(width, height);

    if (graphics_config.ar_option != ultramodern::renderer::AspectRatio::Expand || height <= 0) {
        _return(ctx, original);
        return;
    }
    _return(ctx, std::max(static_cast<float>(width) / height, original));
}
