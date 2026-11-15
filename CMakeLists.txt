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

add_executable(cpu_tests runtime/tests/cpu_tests.c)
target_link_libraries(cpu_tests PRIVATE nescpu)

# The NES system: bus, PPU, cartridge (implements cpu_read/cpu_write).
add_library(nescore STATIC runtime/nes.c runtime/ppu.c runtime/cart.c runtime/palette.c
    runtime/input_log.c)
target_link_libraries(nescore PUBLIC nescpu)
if(NOT MSVC)
    target_link_libraries(nescore PUBLIC m)
endif()

add_executable(nes_headless runtime/tests/nes_headless.c)
target_link_libraries(nes_headless PRIVATE nescore)

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
    target_link_libraries(nes PRIVATE nescore SDL3::SDL3-static)
endif()
