cmake_minimum_required(VERSION 3.20)
project(nes_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra)
endif()

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

# The interpreter's opcode switch is generated from recomp/ops.py, the same
# table and C templates the recompiler uses (recomp/c6502.py).
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
add_custom_command(
    OUTPUT ${GEN_DIR}/cpu_switch.inc
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_cpu_switch.py ${GEN_DIR}/cpu_switch.inc
    DEPENDS tools/gen_cpu_switch.py recomp/c6502.py recomp/ops.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating CPU switch from recomp/ops.py")

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
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.20
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
    add_executable(nes runtime/platform/sdl3_main.c)
    target_link_libraries(nes PRIVATE nescore recomp_none SDL3::SDL3-static)
endif()

# ---- Recompiled programs (M5) ----
# nes_recompile(NAME ROM [HINTS]) generates ${GEN_DIR}/NAME.c with
# `python -m recomp` and builds NAME_headless (and NAME with the frontend).
file(GLOB RECOMP_PY ${CMAKE_SOURCE_DIR}/recomp/*.py)
function(nes_recompile name rom)
    set(out ${GEN_DIR}/${name}.c)
    set(hints "")
    set(hint_args "")
    if(ARGC GREATER 2 AND EXISTS "${ARGV2}")
        set(hints "${ARGV2}")
        set(hint_args --hints "${ARGV2}")
    endif()
    add_custom_command(
        OUTPUT ${out}
        COMMAND Python3::Interpreter -m recomp "${rom}" -o ${out} ${hint_args}
        DEPENDS "${rom}" ${hints} ${RECOMP_PY}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Recompiling ${name}")
    add_library(${name}_code STATIC ${out})
    target_link_libraries(${name}_code PUBLIC nescpu)
    add_executable(${name}_headless runtime/tests/nes_headless.c)
    target_link_libraries(${name}_headless PRIVATE nescore ${name}_code)
    if(NES_FRONTEND)
        add_executable(${name} runtime/platform/sdl3_main.c)
        target_link_libraries(${name} PRIVATE nescore ${name}_code SDL3::SDL3-static)
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
# Target name = file name in lower case, non-alphanumerics -> '_';
# hints are read from games/<name>.toml when present.
set(NES_GAMES "" CACHE STRING "ROM files to recompile (semicolon-separated)")
foreach(rom IN LISTS NES_GAMES)
    get_filename_component(stem "${rom}" NAME_WE)
    string(TOLOWER "${stem}" name)
    string(REGEX REPLACE "[^a-z0-9]+" "_" name "${name}")
    nes_recompile(${name} "${rom}" ${CMAKE_SOURCE_DIR}/games/${name}.toml)
endforeach()
