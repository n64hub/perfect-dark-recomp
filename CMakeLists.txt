# Perfect Dark: Recompiled.
# Based on rush-2049-recomp's CMakeLists.txt (GPL-3.0), itself on BanjoRecomp's. Desktop and web in one: the desktop
# program builds on Linux (clang; tools/build_linux.sh) and Windows (clang-cl; tools/build_windows.sh). With Emscripten (emcmake cmake;
# tools/build_web.sh) it builds the page, PerfectDarkRecompiled.html, through lib/N64Recomp-browser/frontend
# (docs/web.md). RecompiledFuncs/ and RecompiledPatches/ come from tools/recompile.sh and the patches step below, both
# from the player's ROM (docs/building.md).

cmake_minimum_required(VERSION 3.20)

project(PerfectDarkRecompiled LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()

# Opt out of the constexpr mutex constructor on Windows to avoid vcredist issues.
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
endif()

# Speed for everything built here, RT64 and the runtime included, not just the game's own code. RelWithDebInfo (the
# build the README uses) inlines only functions marked inline by default: inline as Release does, and keep the symbols for
# crash traces. And build every x86-64 library for Nehalem (SSE4.2, 2008), which the executable has always been built
# for, so that rounding functions such as ceilf become single instructions instead of library calls.
foreach (lang C CXX)
    string(REPLACE "/Ob1" "/Ob2" CMAKE_${lang}_FLAGS_RELWITHDEBINFO "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}")
endforeach()
if (CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    add_compile_options(-march=nehalem)
endif()

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

if (EMSCRIPTEN)
    # Threads are pthreads (SharedArrayBuffer), and the runtime ends a game thread by throwing. -msimd128 is the web's
    # -march=nehalem: wasm SIMD, which every browser with WebGPU has, for hlslpp (its wasm backend) and the
    # auto-vectorizer; a race display list took ~5% less time with it (5.83 -> 5.55 ms, the median of 8 runs of each,
    # interleaved, on the headless page at 960x720 on a busy host, Mann-Whitney p ~ 0.04), the browser's CPU the same
    # (95 s per 90 s either way). -mtail-call is wasm's tail calls
    # (every such browser has them too): with it the recompiled code's jumps to other functions are return_calls
    # (N64Recomp-web's RECOMP_TAIL_CALL), not calls that grow the stack, as N64Recomp-browser's frontend says to.
    add_compile_options(-pthread -fwasm-exceptions -msimd128 -mtail-call)
    # With it xxHash would take its NEON path through Emscripten's arm_neon.h (SIMDe), which it includes inside its
    # extern "C", where the C++ headers that reaches don't compile. XXH_HAS_INCLUDE is used for nothing else, so XXH3
    # stays scalar, as it was before -msimd128.
    add_compile_options("-DXXH_HAS_INCLUDE(x)=0")
    add_link_options(-pthread -fwasm-exceptions -mtail-call)
    # ccache when it is on PATH (README, "Building the page").
    # The page is ~640 compiles. On a fully busy Windows host (wall times 1.5-2 times a quiet one's), a fresh build dir
    # with nothing cached took 72-200 s to configure and 590-810 s to build, the link 120-190 s of it. With ccache warm
    # from another checkout (636 of 636 hits) the build took 382 s at -O3 (the link 170 s; the objects waited ~200 s
    # for the shader chain, dxc and naga) and 116 s with -DN64RECOMP_BROWSER_FAST_LINK=ON; one changed file, 185 s and
    # 25 s. The fast link is -O1: 11 s against 190, the wasm 21.1 MB against 19.7. What it costs the frame rate wasn't
    # measurable on that host (42-54 presents a second either way), so the page people play links at -O3.
    include(${CMAKE_SOURCE_DIR}/lib/N64Recomp-browser/frontend/build_speed.cmake)
    # The shaders the page specializes at build time and builds before the game needs them, in sections: the boot's
    # before the first frame, each level's when the level first draws (src/rt64_shaders.txt says how it was recorded).
    if (EXISTS ${CMAKE_SOURCE_DIR}/src/rt64_shaders.txt)
        set(RT64_PREBUILT_SHADER_LIST ${CMAKE_SOURCE_DIR}/src/rt64_shaders.txt)
    endif()
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(
    HLSL_CPU
    __PRFCHWINTRIN_H
)

# The web support, N64Recomp-browser's patches/, on the submodules at the upstream commits it names: the desktop build
# takes them too, as tools/recompile.sh's N64Recomp is the patched one and the code it writes uses the patched recomp.h.
# Does nothing when they are applied already.
include(${CMAKE_SOURCE_DIR}/lib/N64Recomp-browser/patches/apply.cmake)
n64recomp_browser_apply_patches(${CMAKE_SOURCE_DIR}/lib)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)

set(BUILD_SHARED_LIBS OFF)

# On the desktop too, as the page always has it: an external message to a full queue waits aside for room instead of
# going back into the runtime's queue of them. Requeued, the game's audio timer (sched.c, every other retrace, to the
# audio thread's queue, which nothing reads until the game has loaded) kept the idle thread taking it again and again,
# and main_init's own timer message, behind it, never came: the boot stopped before the game was unzipped.
set(ULTRAMODERN_HOLD_BLOCKED_MESSAGES ON CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)

if (EMSCRIPTEN)
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64Recomp-browser/frontend ${CMAKE_BINARY_DIR}/web_frontend)
    # fmt 11.1 (N64Recomp's) leans on <cstdlib> arriving through other headers, which Emscripten's libc++ doesn't do.
    target_compile_options(fmt PRIVATE -include cstdlib)
endif()

target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)

# RecompiledFuncs: N64Recomp's output (run it on us.toml first).
add_library(RecompiledFuncs STATIC)

target_compile_options(RecompiledFuncs PRIVATE
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-implicit-function-declaration
    -falign-functions=16
)
# A call to an undeclared function links to a trap in WebAssembly, so it is an error there (N64Recomp's funcs.h declares
# every function the recompiled code calls).
if (EMSCRIPTEN)
    target_compile_options(RecompiledFuncs PRIVATE -Werror=implicit-function-declaration)
endif()

target_include_directories(RecompiledFuncs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

file(GLOB FUNC_C_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.cpp)
target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

# PatchesLib: the game functions patches/ replaces (its boot without the TLB, patches/boot.c), compiled for MIPS by
# patches/Makefile and recompiled by N64Recomp (patches.toml; the runtime's own N64Recomp, target N64RecompCLI).
# Needs a clang with a MIPS backend and ld.lld (PATCHES_C_COMPILER, PATCHES_LD), and the headers the decompilation's build
# generates from the extracted ROM (patches/Makefile). PD_PATCHES=OFF is for CI only, which has no ROM and stops
# before linking: without the library the game doesn't boot. On Windows, make, clang and ld.lld are WSL's
# (PATCHES_MAKE): LLVM's Windows releases (21 and 23, the installer and the archive) have no MIPS target.
option(PD_PATCHES "Build patches/ (the game needs it)" ON)
set(PATCHES_C_COMPILER clang CACHE STRING "clang with a MIPS backend, for patches/")
set(PATCHES_LD ld.lld CACHE STRING "ld.lld, for patches/")
if (CMAKE_HOST_WIN32)
    set(PATCHES_MAKE wsl make CACHE STRING "make for patches/, which runs PATCHES_C_COMPILER and PATCHES_LD")
else()
    set(PATCHES_MAKE make CACHE STRING "make for patches/, which runs PATCHES_C_COMPILER and PATCHES_LD")
endif()

if (PD_PATCHES)
    add_library(PatchesLib STATIC)

    target_compile_options(PatchesLib PRIVATE
        -fno-strict-aliasing
        -Wno-unused-variable
        -Wno-implicit-function-declaration
        -falign-functions=16
    )

    target_include_directories(PatchesLib PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    )

    target_sources(PatchesLib PRIVATE
        ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c
        ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c
    )

    # make is incremental, so this runs every build and relinks only when a patch source changed.
    add_custom_target(PatchesBin
        COMMAND ${PATCHES_MAKE} CC=${PATCHES_C_COMPILER} LD=${PATCHES_LD}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/patches
        BYPRODUCTS ${CMAKE_SOURCE_DIR}/patches/patches.elf
    )

    # The recompiler runs where the build does: for the page that is tools/recompile.sh's native N64Recomp, not the
    # runtime's target, which Emscripten would build for the browser.
    if (EMSCRIPTEN)
        set(PD_N64RECOMP ${CMAKE_SOURCE_DIR}/build/n64recomp/N64Recomp)
        # The patches call the game's functions: undeclared, a call links to a trap in WebAssembly.
        target_compile_options(PatchesLib PRIVATE -Werror=implicit-function-declaration
            "SHELL:-include ${CMAKE_SOURCE_DIR}/RecompiledFuncs/funcs.h")
    else()
        set(PD_N64RECOMP N64RecompCLI)
    endif()

    add_custom_command(OUTPUT
            ${CMAKE_SOURCE_DIR}/patches/patches.bin
            ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c
            ${CMAKE_SOURCE_DIR}/RecompiledPatches/recomp_overlays.inl
            ${CMAKE_SOURCE_DIR}/RecompiledPatches/funcs.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/RecompiledPatches
        COMMAND ${PD_N64RECOMP} patches.toml
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${CMAKE_SOURCE_DIR}/patches/patches.elf ${CMAKE_SOURCE_DIR}/patches.toml
    )

    add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.h
        COMMAND file_to_c ${CMAKE_SOURCE_DIR}/patches/patches.bin pd_patches_bin
            ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.h
        DEPENDS ${CMAKE_SOURCE_DIR}/patches/patches.bin
    )
endif()

add_executable(PerfectDarkRecompiled)

target_sources(PerfectDarkRecompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/mouse.cpp
    ${CMAKE_SOURCE_DIR}/src/register_overlays.cpp
    ${CMAKE_SOURCE_DIR}/src/pd_natives.cpp
    ${CMAKE_SOURCE_DIR}/src/pd_libultra.cpp
    ${CMAKE_BINARY_DIR}/pd_relocs.h
)

# pd_relocs.h, for src/pd_natives.cpp: syms/relocs.txt as an array, and where each segment the list moves pointers
# into starts and ends in the relink (syms/data_dump.toml's _<segment>SegmentStart and End).
file(STRINGS ${CMAKE_SOURCE_DIR}/syms/relocs.txt pd_reloc_lines REGEX "^[a-z]+ [0-9a-f]+$")
list(JOIN pd_reloc_lines "\n" pd_relocs)
string(REGEX REPLACE "([a-z]+) ([0-9a-f]+)" "    { PD_SEG_\\1, 0x\\2 }," pd_relocs "${pd_relocs}")
foreach (seg lib data inflate game)
    string(TOUPPER ${seg} SEG)
    string(REPLACE "PD_SEG_${seg}," "PD_SEG_${SEG}," pd_relocs "${pd_relocs}")
endforeach()
file(READ ${CMAKE_SOURCE_DIR}/syms/data_dump.toml pd_data_syms)
set(pd_relocs_h "// Made by CMakeLists.txt from syms/relocs.txt and syms/data_dump.toml.\n#pragma once\n#include <cstdint>\n\n")
foreach (seg lib data inflate game)
    string(TOUPPER ${seg} SEG)
    foreach (end Start End)
        if (NOT pd_data_syms MATCHES "name = \"_${seg}Segment${end}\", vram = (0x[0-9A-F]+)")
            message(FATAL_ERROR "syms/data_dump.toml has no _${seg}Segment${end}")
        endif()
        string(TOUPPER ${end} END)
        string(APPEND pd_relocs_h "#define PD_${SEG}_${END} ${CMAKE_MATCH_1}u\n")
    endforeach()
endforeach()
string(APPEND pd_relocs_h "\nenum : uint32_t { PD_SEG_LIB, PD_SEG_DATA, PD_SEG_INFLATE, PD_SEG_GAME };\n\n"
    "struct PdReloc {\n    uint32_t segment;\n    uint32_t offset;\n};\n\n"
    "constexpr PdReloc pd_relocs[] = {\n${pd_relocs}\n};\n")
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/pd_relocs.h CONTENT "${pd_relocs_h}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/syms/relocs.txt ${CMAKE_SOURCE_DIR}/syms/data_dump.toml)

target_include_directories(PerfectDarkRecompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/sse2neon
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/inc
    ${CMAKE_SOURCE_DIR}/lib/rt64/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/rhi
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/render
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompinput/include
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/include
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/nativefiledialog-extended/src/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_options(PerfectDarkRecompiled PRIVATE -fno-strict-aliasing -fms-extensions)

if (MSVC)
    # Identical code folding would merge recompiled functions, which breaks function lookup by address.
    target_link_options(PerfectDarkRecompiled PRIVATE /OPT:NOICF)
endif()

if (WIN32)
    include(FetchContent)
    set(SDL2_VERSION "2.30.3")
    FetchContent_Declare(
        sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
    )
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(PerfectDarkRecompiled PRIVATE ${sdl2_SOURCE_DIR}/include)
    target_link_directories(PerfectDarkRecompiled PRIVATE ${sdl2_SOURCE_DIR}/lib/x64)

    add_custom_command(TARGET PerfectDarkRecompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:PerfectDarkRecompiled>)

    target_link_libraries(PerfectDarkRecompiled PRIVATE SDL2 Winmm.lib)
    # A window program, so no console window opens behind the game; main() stays the entry point. The log still
    # reaches a redirect of stdout and stderr.
    target_link_options(PerfectDarkRecompiled PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED)
    add_compile_definitions("PLUME_SDL_VULKAN_ENABLED")
    add_compile_definitions("RT64_SDL_WINDOW_VULKAN")
    target_include_directories(PerfectDarkRecompiled PRIVATE ${SDL2_INCLUDE_DIRS})
    find_package(Freetype REQUIRED)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)
    target_link_libraries(PerfectDarkRecompiled PRIVATE ${FREETYPE_LIBRARIES} SDL2::SDL2 "-latomic -static-libstdc++" ${CMAKE_DL_LIBS} Threads::Threads)
    # Export the recompiled functions' names, so the crash handler's backtrace can print them.
    target_link_options(PerfectDarkRecompiled PRIVATE -rdynamic)
endif()

# DXC settings RT64 expects from the parent project.
set(DXC_COMMON_OPTS "-I${PROJECT_SOURCE_DIR}/src")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_0" "-D DYNAMIC_RENDER_PARAMS")
set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_0" "-D DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
set(DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T cs_6_0")
set(DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T gs_6_0")
set(DXC_RT_OPTS "${DXC_COMMON_OPTS}" "-D" "RT_SHADER" "-T" "lib_6_3" "-fspv-target-env=vulkan1.1spirv1.4" "-fspv-extension=SPV_KHR_ray_tracing" "-fspv-extension=SPV_EXT_descriptor_indexing")

if (WIN32)
    add_compile_definitions(NOMINMAX)
endif()
if (CMAKE_HOST_WIN32)
    set(DXC "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe")
else()
    set(DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/x64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc-linux")
endif()

set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)
set(RECOMP_FRONTEND_RT64_PATH ${CMAKE_SOURCE_DIR}/lib/rt64)

# The game turns RecompFrontend's single-player mode off, so that four people play four players (src/main.cpp), and
# plays as single-player mode until players are assigned, as with Emscripten: without it a player alone needed to
# assign their pad or keyboard first, and the keyboard played every pad's player.
set(RECOMPINPUT_SINGLE_PLAYER_UNTIL_ASSIGNED ON CACHE BOOL "" FORCE)
# And until then each pad plays a port of its own, in the order they were connected, the keyboard port 1
# (RecompFrontend-web's option): four people with four pads play four players without assigning them.
set(RECOMPINPUT_PADS_IN_ORDER ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/RecompFrontend)

# PatchesLib before RecompiledFuncs: a patch (RECOMP_PATCH) has the name of the game function it replaces, and every
# recompiled function is weak (N64Recomp's recomp.h), so the linker keeps the first definition it is given. After
# RecompiledFuncs, the game's own vm_boot (an empty stub), boot and vm_init would run instead of patches/boot.c's.
# rsp/aspMain.cpp, the audio microcode, comes from tools/recompile.sh with RecompiledFuncs/, so CI has neither.
if (PD_PATCHES)
    target_sources(PerfectDarkRecompiled PRIVATE ${CMAKE_SOURCE_DIR}/src/register_patches.cpp ${CMAKE_SOURCE_DIR}/rsp/aspMain.cpp)
    target_link_libraries(PerfectDarkRecompiled PRIVATE PatchesLib)
    target_compile_definitions(PerfectDarkRecompiled PRIVATE PD_PATCHES)
endif()

target_link_libraries(PerfectDarkRecompiled PRIVATE
    RecompiledFuncs
    recompui
    recompinput
    librecomp
    ultramodern
    rt64
    libzstd_static
)

if (EMSCRIPTEN)
    # The page (lib/N64Recomp-browser/frontend), with the menus' assets and the controller mappings in its .data file. No ROM:
    # the player picks theirs in the launcher, and it stays in the browser.
    n64recomp_browser_frontend_page(PerfectDarkRecompiled)
    target_link_options(PerfectDarkRecompiled PRIVATE
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/recompcontrollerdb.txt@/recompcontrollerdb.txt")
else()
    target_link_libraries(PerfectDarkRecompiled PRIVATE nfd)
endif()

set_property(TARGET PerfectDarkRecompiled PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
