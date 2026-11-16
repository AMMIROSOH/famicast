cmake_minimum_required(VERSION 3.20)
project(famicast C)

# Release unless asked otherwise: tests, timings and playable speed assume it.
# (Multi-config generators such as Visual Studio: cmake --build build --config Release;
# without it, cmake --build always asks MSBuild for Debug.)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Programs in build/ with every generator (the $<> stops Visual Studio adding
# Release/ or Debug/): NAME.misses is written next to the program and read
# from here, and the tests look for build/NAME.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}>)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra)
endif()

# The recompiler is a build-time tool. It also writes the interpreter's opcode
# switch, from the same table and C templates (recomp/ops.c, recomp/c6502.c),
# so instruction semantics exist once.
add_executable(nesrecomp recomp/main.c recomp/ops.c recomp/c6502.c recomp/analyze.c
    recomp/emit.c runtime/cart.c)
target_include_directories(nesrecomp PRIVATE runtime)

set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
# WORKING_DIRECTORY on custom commands that run our programs: without it the
# Visual Studio generator writes a bare "nesrecomp.exe" (relative to build/),
# which MSBuild's cmd does not look for in the current folder.
add_custom_command(
    OUTPUT ${GEN_DIR}/cpu_switch.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
    COMMAND nesrecomp --cpu-switch ${GEN_DIR}/cpu_switch.inc
    DEPENDS nesrecomp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating the interpreter's opcode switch")

add_library(nescpu STATIC runtime/cpu.c ${GEN_DIR}/cpu_switch.inc)
target_include_directories(nescpu PUBLIC runtime PRIVATE ${GEN_DIR})

# Programs without recompiled code link this instead (recomp_program = NULL).
add_library(recomp_none STATIC runtime/recomp_none.c)
target_link_libraries(recomp_none PUBLIC nescpu)

add_executable(cpu_tests runtime/tests/cpu_tests.c)
target_link_libraries(cpu_tests PRIVATE nescpu recomp_none)

# The NES system: bus, PPU, cartridge (implements cpu_read/cpu_write).
add_library(nescore STATIC runtime/nes.c runtime/ppu.c runtime/apu.c runtime/cart.c
    runtime/palette.c runtime/input_log.c)
target_link_libraries(nescore PUBLIC nescpu)
if(NOT MSVC)
    target_link_libraries(nescore PUBLIC m)
endif()

add_executable(nes_headless runtime/tests/nes_headless.c)
target_link_libraries(nes_headless PRIVATE nescore recomp_none)

# SDL3 frontend (zlib license). Fetched and built statically; turn off with
# -DNES_FRONTEND=OFF for headless builds (CI, tests).
option(NES_FRONTEND "Build the SDL3 frontend" ON)
if(NES_FRONTEND)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    # Only video, 2D render, audio and gamepads are used (SDL_INIT_* in sdl3_main.c).
    # Vulkan: the 2D renderer uses D3D11/12 on Windows, OpenGL elsewhere.
    foreach(unused CAMERA HAPTIC POWER SENSOR DIALOG TRAY GPU RENDER_GPU VULKAN RENDER_VULKAN)
        set(SDL_${unused} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.20
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
    # A game window (no console window on Windows); display settings are read
    # from TARGET.ini next to the program, created on first run.
    function(nes_frontend target code)
        add_executable(${target} WIN32 runtime/platform/sdl3_main.c runtime/platform/settings.c
            runtime/platform/tv_frame.c)
        target_compile_definitions(${target} PRIVATE NES_APP_NAME="${target}")
        target_link_libraries(${target} PRIVATE nescore ${code} SDL3::SDL3-static)
    endfunction()
    nes_frontend(nes recomp_none)
endif()

# ---- Recompiled programs (M5) ----
# nes_recompile(NAME ROM [HINTS]) generates ${GEN_DIR}/NAME.c with nesrecomp
# (the ROM is embedded) and builds NAME_headless (and NAME with the frontend).
#
# NES_AUTO_HINTS: first run the game in the interpreter with scripted input
# (nes_headless --play) and give every address it ran to nesrecomp, so code
# reached in that session is recompiled without hand-made hints. The merged
# hints are written to ${GEN_DIR}/NAME.toml; copy them to games/ to keep them.
#
# NAME.misses next to the program: ROM code your own sessions had to
# interpret (the frontend writes it, setting record_misses); always passed to
# nesrecomp, so playing and rebuilding recompiles what you reached.
#
# NES_EMBED_ROM=OFF: the program carries no ROM and needs the file at start
# (the generated code still holds the game's code bytes: not for sharing either).
option(NES_AUTO_HINTS "Trace each game in the interpreter before recompiling it" OFF)
option(NES_EMBED_ROM "Embed the ROM in recompiled programs" ON)
set(NES_AUTO_HINTS_FRAMES 6000 CACHE STRING "Frames of scripted play traced by NES_AUTO_HINTS")
function(nes_recompile name rom)
    set(out ${GEN_DIR}/${name}.c)
    set(hints "")
    set(hint_args "")
    if(ARGC GREATER 2 AND EXISTS "${ARGV2}")
        set(hints "${ARGV2}")
        set(hint_args --hints "${ARGV2}")
    endif()
    set(trace "")
    if(NES_AUTO_HINTS)
        set(trace ${GEN_DIR}/${name}.trace)
        add_custom_command(
            OUTPUT ${trace}
            COMMAND nes_headless "${rom}" --play 1 --frames ${NES_AUTO_HINTS_FRAMES} --misses --out ${trace}
            DEPENDS "${rom}" nes_headless
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Tracing ${name} in the interpreter (${NES_AUTO_HINTS_FRAMES} frames)")
        list(APPEND hint_args --misses ${trace} --save-hints ${GEN_DIR}/${name}.toml)
    endif()
    set(played ${CMAKE_BINARY_DIR}/${name}.misses)
    if(NOT EXISTS ${played})
        file(TOUCH ${played})
    endif()
    list(APPEND hint_args --misses ${played})
    if(NOT NES_EMBED_ROM)
        list(APPEND hint_args --no-rom)
    endif()
    add_custom_command(
        OUTPUT ${out}
        COMMAND nesrecomp "${rom}" -o ${out} ${hint_args}
        DEPENDS "${rom}" ${hints} ${trace} ${played} nesrecomp
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Recompiling ${name}")
    add_library(${name}_code STATIC ${out})
    # bus helpers out of line in generated code: thousands of blocks with them
    # inlined outgrow the CPU caches (Kirby: 8.6 -> 4.6 MB, 19.9x -> 23.4x, docs/06)
    target_compile_definitions(${name}_code PRIVATE CPU_OPS_OUTLINE)
    target_link_libraries(${name}_code PUBLIC nescpu)
    add_executable(${name}_headless runtime/tests/nes_headless.c)
    target_link_libraries(${name}_headless PRIVATE nescore ${name}_code)
    if(NES_FRONTEND)
        nes_frontend(${name} ${name}_code)
    endif()
endfunction()

# nestest (public, fetched by tests/fetch_test_roms.py): checks the recompiler
# against nestest.log through the CPU test runner.
set(NESTEST_ROM ${CMAKE_SOURCE_DIR}/tests/roms/nestest.nes)
if(EXISTS ${NESTEST_ROM})
    nes_recompile(nestest ${NESTEST_ROM} ${CMAKE_SOURCE_DIR}/tests/data/nestest.toml)
    add_executable(nestest_recomp_tests runtime/tests/cpu_tests.c)
    target_link_libraries(nestest_recomp_tests PRIVATE nestest_code)
endif()

# Your own game ROMs (never committed): -DNES_GAMES="a.nes;b.nes".
# Target name = file name without "(...)"/"[...]" tags, lower case,
# non-alphanumerics -> '_' (e.g. "Arkista's Ring (U) [!].nes" -> arkista_s_ring);
# hints are read from games/<name>.toml when present.
set(NES_GAMES "" CACHE STRING "ROM files to recompile (semicolon-separated)")
foreach(rom IN LISTS NES_GAMES)
    # Build commands run from the build directory; resolve user paths here.
    get_filename_component(rom "${rom}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    get_filename_component(stem "${rom}" NAME_WLE)
    string(REGEX REPLACE "[ ]*[[(][^])]*[])]" "" stem "${stem}")  # drop (U), [!], ...
    string(TOLOWER "${stem}" name)
    string(REGEX REPLACE "[^a-z0-9]+" "_" name "${name}")
    string(REGEX REPLACE "^_+|_+$" "" name "${name}")
    nes_recompile(${name} "${rom}" ${CMAKE_SOURCE_DIR}/games/${name}.toml)
    list(APPEND game_names ${name})
endforeach()
# for tests/test_regression.py: the games this build recompiles
list(JOIN game_names "\n" game_lines)
file(WRITE ${CMAKE_BINARY_DIR}/nes_games.txt "${game_lines}\n")
