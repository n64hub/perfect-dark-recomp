// Perfect Dark: Recompiled on RecompFrontend: its launcher (Load ROM, Start Game), settings menus, input and RT64
// render context, with no custom launcher. From GoldenEye64Recomp-web's main.cpp (GPL-3.0). The code sections and
// low RAM before the game starts are register_overlays.cpp's, the natives the patches call pd_natives.cpp's, the
// settings config.cpp's.

#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <array>
#include <string>
#include <stdexcept>
#include <cinttypes>
#include <filesystem>

#ifndef __EMSCRIPTEN__
#include "nfd.h" // a browser has no native file dialogs; RecompFrontend asks the page instead
#endif

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#define SDL_MAIN_HANDLED
#ifdef _WIN32
#include "SDL.h"
#else
#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
#undef None
#undef Status
#undef LockMask
#undef ControlMask
#undef Success
#undef Always
#endif

#include "recompui/recompui.h"
#include "recompui/program_config.h"
#include "recompui/renderer.h"
#include "recompui/config.h"
#include "util/file.h"
#ifdef __EMSCRIPTEN__
#include "audio_output.h"
#endif
#include "recompinput/input_events.h"
#include "recompinput/recompinput.h"
#include "recompinput/profiles.h"

#include "pd.h"
#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"
#include "librecomp/helpers.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <timeapi.h>
#include "SDL_syswm.h"
#endif


const std::string version_string = "0.1.0";

template<typename... Ts>
void exit_error(const char* str, Ts ...args) {
    ((void)fprintf(stderr, str, args), ...);
    assert(false);
    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    // Emscripten's SDL2 is built without haptics, and asking for them there fails the whole call, gamepads with it.
#ifdef __EMSCRIPTEN__
    const Uint32 sdl_subsystems = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK;
#else
    const Uint32 sdl_subsystems = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC;
#endif
    if (SDL_Init(sdl_subsystems) < 0) {
        exit_error("Failed to initialize SDL2: %s\n", SDL_GetError());
    }

    fprintf(stdout, "SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());
    return {};
}

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    if (recompinput::players::is_single_player_mode() || recompinput::players::get_player_is_assigned(controller_num)) {
        return ultramodern::input::connected_device_info_t{
            .connected_device = ultramodern::input::Device::Controller,
            .connected_pak = ultramodern::input::Pak::RumblePak,
        };
    }

    return ultramodern::input::connected_device_info_t{
        .connected_device = ultramodern::input::Device::None,
        .connected_pak = ultramodern::input::Pak::None,
    };
}

SDL_Window* window;
// --window <w>x<h>: the size the window opens at.
static int window_width = 1600;
static int window_height = 1200;

ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    uint32_t flags = SDL_WINDOW_RESIZABLE;

#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#elif defined(RT64_SDL_WINDOW_VULKAN)
    flags |= SDL_WINDOW_VULKAN;
#endif

    window = SDL_CreateWindow(std::string{ pd::program_name }.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_width, window_height, flags);
    if (window == nullptr) {
        exit_error("Failed to create window: %s\n", SDL_GetError());
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);

#if defined(_WIN32)
    return ultramodern::renderer::WindowHandle{ wmInfo.info.win.window, GetCurrentThreadId() };
#elif defined(__linux__) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    // In a browser the window is the page's canvas, sized by the page.
    return ultramodern::renderer::WindowHandle{ window };
#elif defined(__APPLE__)
    SDL_MetalView view = SDL_Metal_CreateView(window);
    return ultramodern::renderer::WindowHandle{ wmInfo.info.cocoa.window, SDL_Metal_GetLayer(view) };
#else
    static_assert(false && "Unimplemented");
#endif
}

void update_gfx(void*) {
    recompinput::handle_events();
}

static SDL_AudioCVT audio_convert;
static SDL_AudioDeviceID audio_device = 0;

// Samples come in at the game's rate and go out at the device's, both stereo floats.
static uint32_t sample_rate = 48000;
static uint32_t output_sample_rate = 48000;
constexpr uint32_t input_channels = 2;
static uint32_t output_channels = 2;

// A few frames of each buffer are repeated at the start of the next, so the resampler has context across buffers,
// and the frames they turn into are dropped again.
constexpr uint32_t duplicated_input_frames = 4;
static uint32_t discarded_output_frames;

constexpr uint32_t bytes_per_frame = input_channels * sizeof(float);

// The size of the buffer queued last, in output bytes (get_frames_remaining).
static uint64_t last_queued_bytes = 0;

// On a page, sound plays through N64Recomp-browser's AudioWorklet output (audio_output.h) instead of SDL's queue:
// Emscripten's SDL2 plays from the page's thread, where WebGPU's work runs, and its sound broke up whenever that
// thread was busy.
#ifdef __EMSCRIPTEN__
static uint64_t get_queued_audio_bytes() {
    return uint64_t(n64recomp_browser::audio::queued_frames()) * output_channels * sizeof(float);
}

static void queue_audio(const float* samples, uint32_t byte_count) {
    n64recomp_browser::audio::queue(samples, byte_count / (output_channels * sizeof(float)));
}
#else
static uint64_t get_queued_audio_bytes() {
    return SDL_GetQueuedAudioSize(audio_device);
}

static void queue_audio(const float* samples, uint32_t byte_count) {
    SDL_QueueAudio(audio_device, samples, byte_count);
}
#endif

void queue_samples(int16_t* audio_data, size_t sample_count) {
    static std::vector<float> swap_buffer;
    static std::array<float, duplicated_input_frames * input_channels> duplicated_sample_buffer;

    size_t resampled_sample_count = sample_count + duplicated_input_frames * input_channels;
    size_t max_sample_count = std::max(resampled_sample_count, resampled_sample_count * audio_convert.len_mult);
    if (max_sample_count > swap_buffer.size()) {
        swap_buffer.resize(max_sample_count);
    }

    for (size_t i = 0; i < duplicated_input_frames * input_channels; i++) {
        swap_buffer[i] = duplicated_sample_buffer[i];
    }

    // The game's samples are 16-bit with the channels swapped; the Sound tab's main volume scales them.
    float cur_main_volume = recompui::config::sound::get_main_volume() / 100.0f;
    for (size_t i = 0; i < sample_count; i += input_channels) {
        swap_buffer[i + 0 + duplicated_input_frames * input_channels] = audio_data[i + 1] * (0.5f / 32768.0f) * cur_main_volume;
        swap_buffer[i + 1 + duplicated_input_frames * input_channels] = audio_data[i + 0] * (0.5f / 32768.0f) * cur_main_volume;
    }

    assert(sample_count > duplicated_input_frames * input_channels);
    for (size_t i = 0; i < duplicated_input_frames * input_channels; i++) {
        duplicated_sample_buffer[i] = swap_buffer[i + sample_count];
    }

    audio_convert.buf = reinterpret_cast<Uint8*>(swap_buffer.data());
    audio_convert.len = (sample_count + duplicated_input_frames * input_channels) * sizeof(swap_buffer[0]);

    if (SDL_ConvertAudio(&audio_convert) < 0) {
        printf("Error using SDL audio converter: %s\n", SDL_GetError());
        throw std::runtime_error("Error using SDL audio converter");
    }

    uint64_t cur_queued_microseconds = get_queued_audio_bytes() / (output_channels * sizeof(float)) * 1000000 / output_sample_rate;
    uint32_t num_bytes_to_queue = audio_convert.len_cvt - output_channels * discarded_output_frames * sizeof(swap_buffer[0]);
    float* samples_to_queue = swap_buffer.data() + output_channels * discarded_output_frames / 2;

    // With more than 100 ms queued (the window was dragged, the tab was hidden), drop samples to catch up, whole frames
    // only: a part of one would put every sample queued after it out of step, read as noise.
    uint32_t skip_factor = cur_queued_microseconds / 100000;
    if (skip_factor != 0) {
        uint32_t skip_ratio = 1 << skip_factor;
        uint32_t output_frame_bytes = output_channels * sizeof(swap_buffer[0]);
        num_bytes_to_queue = num_bytes_to_queue / skip_ratio / output_frame_bytes * output_frame_bytes;
        for (size_t i = 0; i < num_bytes_to_queue / (output_channels * sizeof(swap_buffer[0])); i++) {
            samples_to_queue[2 * i + 0] = samples_to_queue[2 * skip_ratio * i + 0];
            samples_to_queue[2 * i + 1] = samples_to_queue[2 * skip_ratio * i + 1];
        }
    }

    queue_audio(samples_to_queue, num_bytes_to_queue);
    last_queued_bytes = num_bytes_to_queue;
}

// What osAiGetLength reads: the frames left of the buffer playing. On the console that is the one buffer, with the
// one queued after it waiting in the AI's FIFO. The game (amgr_handle_frame_msg, src/lib/audiomgr.c) reads it every
// other retrace, before it queues the buffer it made the time before, and makes its next buffer 736 frames at 22020 Hz
// (two retraces' 734, rounded up), or 552 when more than 248 are left: so the queue grows by two frames a buffer until
// it is ahead, and one short buffer takes it back. The buffer queued last isn't counted while an earlier one still
// plays, as on the console, and a retrace's worth less is, so the queue holds that much more against a late buffer.
size_t get_frames_remaining() {
    constexpr float buffer_offset_frames = 1.0f;
    uint64_t buffered_byte_count = get_queued_audio_bytes();
    if (buffered_byte_count > last_queued_bytes) {
        buffered_byte_count -= last_queued_bytes;
    }
    buffered_byte_count = buffered_byte_count * 2 * sample_rate / output_sample_rate / output_channels;
    uint32_t frames_per_vi = (sample_rate / 60);
    if (buffered_byte_count > (buffer_offset_frames * bytes_per_frame * frames_per_vi)) {
        buffered_byte_count -= (buffer_offset_frames * bytes_per_frame * frames_per_vi);
    }
    else {
        buffered_byte_count = 0;
    }
    return static_cast<uint32_t>(buffered_byte_count / bytes_per_frame);
}

void update_audio_converter() {
    if (SDL_BuildAudioCVT(&audio_convert, AUDIO_F32, input_channels, sample_rate, AUDIO_F32, output_channels, output_sample_rate) < 0) {
        printf("Error creating SDL audio converter: %s\n", SDL_GetError());
        throw std::runtime_error("Error creating SDL audio converter");
    }
    discarded_output_frames = duplicated_input_frames * output_sample_rate / sample_rate;
}

void set_frequency(uint32_t freq) {
    sample_rate = freq;
    update_audio_converter();
}

void reset_audio(uint32_t output_freq) {
#ifdef __EMSCRIPTEN__
    output_freq = n64recomp_browser::audio::open(output_channels);
#else
    SDL_AudioSpec spec_desired{
        .freq = (int)output_freq,
        .format = AUDIO_F32,
        .channels = (Uint8)output_channels,
        .silence = 0,
        .samples = 0x100, // small, for the latency of SDL's own buffering
        .padding = 0,
        .size = 0,
        .callback = nullptr,
        .userdata = nullptr
    };

    audio_device = SDL_OpenAudioDevice(nullptr, false, &spec_desired, nullptr, 0);
    if (audio_device == 0) {
        exit_error("SDL error opening audio device: %s\n", SDL_GetError());
    }
    SDL_PauseAudioDevice(audio_device, 0);
#endif

    output_sample_rate = output_freq;
    update_audio_converter();
}

// The audio microcode, with its MP3 overlay (aspMain.us.toml, recompiled by tools/recompile.sh into rsp/aspMain.cpp).
extern RspUcodeFunc aspMain;

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    switch (task->t.type) {
    case M_AUDTASK:
        return aspMain;
    default:
        fprintf(stderr, "Unknown task: %" PRIu32 "\n", task->t.type);
        return nullptr;
    }
}

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();

std::vector<recomp::GameEntry> supported_games = {
    {
        // XXH3 of Perfect Dark (USA) (Rev 1), the decompilation's ntsc-final: MD5 e03b088b6ac9e0080440efed07c1e40f,
        // SHA-1 af8788ac4d1a57260eae9c53ffe851fcf2a3319b. The ROM is stored as it is; the game unzips its own code.
        .rom_hash = pd::rom_xxh3,
        .internal_name = "Perfect Dark",
        .display_name = "Perfect Dark",
        .game_id = u8"pd.n64.us.1",
        .mod_game_id = "pd",
        .save_type = recomp::SaveType::Eep16k,
        .is_enabled = true,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = recomp_entrypoint,
        .thread_create_callback = pd::on_thread_create,
        .on_init_callback = pd::on_init,
    },
};

static std::string game_thread_name(const OSThread* t) {
    return "[Game] " + std::to_string(t->id);
}

// --boot (development): start the game on the launcher's first frame, as its Start Game option does, so a run
// needs nobody at the window. Needs the ROM already stored (or --rom).
static bool autoboot = false;
static const char* rom_path = nullptr;

void on_launcher_update(recompui::LauncherMenu*) {
    static bool done = false;
    if (!autoboot || done) {
        return;
    }
    done = true;
    std::u8string game_id = supported_games[0].game_id;
    if (!recomp::is_rom_valid(game_id)) {
        fprintf(stderr, "--boot: no valid stored ROM, staying in the launcher\n");
        return;
    }
    recomp::start_game(game_id, "");
    recompui::hide_all_contexts();
}

void on_launcher_init(recompui::LauncherMenu* menu) {
    const recomp::GameEntry& game = supported_games[0];
    auto game_options_menu = menu->init_game_options_menu(game.game_id, game.mod_game_id, game.display_name, game.thumbnail_bytes);
    game_options_menu->add_default_options();
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--boot") == 0) {
            autoboot = true;
        }
        else if (strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &window_width, &window_height);
        }
        else if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            rom_path = argv[++i];
        }
    }

    recomp::Version project_version{};
    if (!recomp::Version::from_string(version_string, project_version)) {
        ultramodern::error_handling::message_box(("Invalid version string: " + version_string).c_str());
        return EXIT_FAILURE;
    }

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    // RecompFrontend finds assets/ (and portable.txt) in the working directory, which a desktop launcher or a shell
    // elsewhere doesn't set to the release's folder: when assets/ isn't there, work from the program's own folder.
    // A dev build run from the repo's root keeps the root, which is where assets/ is.
    {
        std::error_code ec;
        if (!std::filesystem::is_directory("assets", ec)) {
            std::filesystem::path exe_dir = std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
            if (!ec && std::filesystem::is_directory(exe_dir / "assets", ec)) {
                std::filesystem::current_path(exe_dir, ec);
            }
        }
    }
#endif

#ifdef _WIN32
    timeBeginPeriod(1);
    SetConsoleOutputCP(CP_UTF8);
    // wasapi; directsound has had sample queueing issues. An SDL_AUDIODRIVER already set wins, as on Linux: a test
    // run writes the sound to a file with SDL's disk driver.
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", false);
#endif

#ifndef __EMSCRIPTEN__
    NFD_Init();
#endif

    recompui::programconfig::set_program_name(std::string{ pd::program_name });
    recompui::programconfig::set_program_id(std::u8string{ pd::program_id });

    SDL_InitSubSystem(SDL_INIT_AUDIO);
    reset_audio(48000);

    std::u8string controller_db_path = (recompui::file::get_program_path() / "recompcontrollerdb.txt").u8string();
    if (SDL_GameControllerAddMappingsFromFile(reinterpret_cast<const char*>(controller_db_path.c_str())) < 0) {
        fprintf(stderr, "Failed to load controller mappings: %s\n", SDL_GetError());
    }

    recompui::register_primary_font("InterVariable.ttf", "Inter Variable");

    recomp::register_config_path(recompui::file::get_app_folder_path());

    for (const auto& game : supported_games) {
        recomp::register_game(game);
    }

    // --rom <path> (development): store that ROM as the launcher's Load ROM does, so --boot can start it.
    if (rom_path != nullptr) {
        std::u8string game_id = supported_games[0].game_id;
        recomp::RomValidationError error = recomp::select_rom(rom_path, game_id);
        if (error != recomp::RomValidationError::Good) {
            fprintf(stderr, "--rom: %s isn't the ROM (%d)\n", rom_path, static_cast<int>(error));
            return EXIT_FAILURE;
        }
    }

    pd::register_overlays();
    pd::register_patches();

    // Up to four players, each assigned a pad (or the keyboard) on the controls page's Assign players, which is then
    // their own port (get_connected_device_info): four people play four players. Until players are assigned every pad and
    // the keyboard play as one on every port, as in single-player mode, with RECOMPINPUT_SINGLE_PLAYER_UNTIL_ASSIGNED
    // (CMakeLists.txt).
    recompinput::players::set_single_player_mode(false);
    recompinput::players::set_player_count_range(1, 4);

    pd::init_config();

    recompui::register_launcher_init_callback(on_launcher_init);
    recompui::register_launcher_update_callback(on_launcher_update);

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = get_rsp_microcode,
    };

    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context = [](uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
            auto presentation_mode = ultramodern::renderer::PresentationMode::PresentEarly;
#ifdef __EMSCRIPTEN__
            // Render to RAM stays on (RT64's default): the light and sun glares read the depth buffer on the CPU when the
            // RDP is done (sched_update_pending_artifacts). On the page a display list doesn't wait for its frame's copy
            // into RDRAM, which lands while the next list is drawn, so the glares read their depths a list later, in a list
            // of their own (patches/artifacts.c, recomp_pd_render_to_ram_late). The CPU wrote to no framebuffer with a
            // copy pending in the shader recording's 21 levels (docs/web.md, Frame rate).
            recompui::renderer::set_render_to_ram_late(true);
#endif
            return recompui::renderer::create_render_context(rdram, window_handle, presentation_mode, developer_mode);
        },
    };

    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
    };

    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = queue_samples,
        .get_frames_remaining = get_frames_remaining,
        .set_frequency = set_frequency,
    };

    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = recompinput::poll_inputs,
        .get_input = pd::get_input,
        .set_rumble = recompinput::set_rumble,
        .get_connected_device_info = get_connected_device_info,
    };

    ultramodern::events::callbacks_t thread_callbacks{
        .vi_callback = recompinput::update_rumble,
        .gfx_init_callback = nullptr,
    };

    ultramodern::error_handling::callbacks_t error_handling_callbacks{
        .message_box = recompui::message_box,
    };

    // Value-initialized: the runtime calls create_window only while cfg.window_handle is empty.
    recomp::Configuration cfg{};
    cfg.project_version = project_version;
    cfg.rsp_callbacks = rsp_callbacks;
    cfg.renderer_callbacks = renderer_callbacks;
    cfg.audio_callbacks = audio_callbacks;
    cfg.input_callbacks = input_callbacks;
    cfg.gfx_callbacks = gfx_callbacks;
    cfg.events_callbacks = thread_callbacks;
    cfg.error_handling_callbacks = error_handling_callbacks;
    cfg.threads_callbacks = { .get_game_thread_name = game_thread_name };
    // message_queue_control keeps its defaults: an SP or DP task's completion that arrives while the game's scheduler
    // queue is full is delivered once there is room instead of dropped.
    recomp::start(cfg);

#ifndef __EMSCRIPTEN__
    NFD_Quit();
#endif

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    return EXIT_SUCCESS;
}
