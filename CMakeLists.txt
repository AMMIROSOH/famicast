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
    recomp/emit.c runtime/cart.c runtime/fds.c)
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
add_library(nescore STATIC runtime/nes.c runtime/ppu.c runtime/apu.c runtime/cart.c runtime/fds.c
    runtime/palette.c runtime/input_log.c)
target_link_libraries(nescore PUBLIC nescpu)
if(NOT MSVC)
    target_link_libraries(nescore PUBLIC m)
endif()

add_executable(nes_headless runtime/tests/nes_headless.c)
target_link_libraries(nes_headless PRIVATE nescore recomp_none)

# Options were named NES_* before: a build folder configured then keeps its
# values under the new names (unless given anew).
foreach(old NES_GAMES NES_EMBED_ROM NES_AUTO_HINTS NES_AUTO_HINTS_FRAMES NES_FRONTEND NES_SDL3_PREFIX)
    if(DEFINED CACHE{${old}})
        string(REPLACE "NES_GAMES" "ROMS" new "${old}")
        string(REPLACE "NES_" "" new "${new}")
        if(NOT DEFINED CACHE{${new}})
            set(${new} "${${old}}" CACHE STRING "")
        endif()
        unset(${old} CACHE)
        message(STATUS "${old} is now ${new}")
    endif()
endforeach()

# SDL3 frontend (zlib license), linked statically; turn off with
# -DFRONTEND=OFF for headless builds (CI, tests). A prebuilt SDL3 in
# SDL3_PREFIX (the release kit ships one in sdl3/) is used when present;
# otherwise SDL3 is fetched and built (cmake/sdl3_options.cmake).
option(FRONTEND "Build the SDL3 frontend" ON)
set(SDL3_PREFIX "${CMAKE_SOURCE_DIR}/sdl3" CACHE PATH "Prebuilt static SDL3 (install prefix)")
if(FRONTEND)
    # only this prefix: a system SDL3 (often shared-only) is never picked up
    find_package(SDL3 CONFIG QUIET COMPONENTS SDL3-static PATHS "${SDL3_PREFIX}" NO_DEFAULT_PATH)
    if(NOT SDL3_FOUND)
        include(FetchContent)
        include(cmake/sdl3_options.cmake)
        FetchContent_Declare(SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.4.20
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(SDL3)
    endif()
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
# An .fds disk image also needs FDS_BIOS: the BIOS is recompiled and embedded
# with it, and the code of the disk's files wherever it ran (nesrecomp --bios).
#
# AUTO_HINTS (on by default): first run the game in the interpreter with
# scripted input (nes_headless --play) and give every address it ran to
# nesrecomp, so code reached in that session is recompiled without hand-made
# hints (~6 s per game; coverage from as low as 0.3% static to 99.9%, docs/06).
# Traced once per ROM: a rebuilt nes_headless does not trace again. The merged
# hints are written to ${GEN_DIR}/NAME.toml; copy them to games/ to keep them.
#
# NAME.misses next to the program: ROM code your own sessions had to
# interpret (the frontend writes it, setting record_misses); always passed to
# nesrecomp, so playing and rebuilding recompiles what you reached.
#
# EMBED_ROM=OFF: the program carries no ROM and needs the file at start
# (the generated code still holds the game's code bytes: not for sharing
# either). FDS programs always carry the BIOS.
option(AUTO_HINTS "Trace each game in the interpreter before recompiling it" ON)
option(EMBED_ROM "Embed the ROM in recompiled programs" ON)
set(AUTO_HINTS_FRAMES 6000 CACHE STRING "Frames of scripted play traced by AUTO_HINTS")
function(nes_recompile name rom)
    set(out ${GEN_DIR}/${name}.c)
    set(hints "")
    set(hint_args "")
    if(ARGC GREATER 2 AND EXISTS "${ARGV2}")
        set(hints "${ARGV2}")
        set(hint_args --hints "${ARGV2}")
    endif()
    set(bios_args "")
    get_filename_component(ext "${rom}" LAST_EXT)
    string(TOLOWER "${ext}" ext)
    if(ext STREQUAL ".fds")
        if(NOT FDS_BIOS)
            message(FATAL_ERROR "${rom}: Famicom Disk System games need the BIOS: -DFDS_BIOS=path/to/disksys.rom")
        endif()
        set(bios_args --bios "${FDS_BIOS}")
        list(APPEND hints "${FDS_BIOS}" ${CMAKE_SOURCE_DIR}/games/fds.toml)  # BIOS entries
        list(APPEND hint_args --hints ${CMAKE_SOURCE_DIR}/games/fds.toml)
    endif()
    set(trace "")
    if(AUTO_HINTS)
        set(trace ${GEN_DIR}/${name}.trace)
        add_custom_command(
            OUTPUT ${trace}
            COMMAND nes_headless "${rom}" ${bios_args} --play 1 --frames ${AUTO_HINTS_FRAMES} --misses --out ${trace}
            # nes_headless as COMMAND is built first (target-level dependency) but is
            # no file dependency: runtime changes do not re-trace every game
            DEPENDS "${rom}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Tracing ${name} in the interpreter (${AUTO_HINTS_FRAMES} frames)")
        list(APPEND hint_args --misses ${trace} --save-hints ${GEN_DIR}/${name}.toml)
    endif()
    set(played ${CMAKE_BINARY_DIR}/${name}.misses)
    if(NOT EXISTS ${played})
        file(TOUCH ${played})
    endif()
    list(APPEND hint_args --misses ${played})
    if(NOT EMBED_ROM)
        list(APPEND hint_args --no-rom)
    endif()
    add_custom_command(
        OUTPUT ${out}
        COMMAND nesrecomp "${rom}" -o ${out} ${hint_args} ${bios_args}
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
    if(FRONTEND)
        nes_frontend(${name} ${name}_code)
    endif()
endfunction()

# nestest (public, fetched by tests/fetch_test_roms.py): checks the recompiler
# against nestest.log through the CPU test runner.
set(NESTEST_ROM ${CMAKE_SOURCE_DIR}/tests/roms/nestest.nes)
if(EXISTS ${NESTEST_ROM})
    # hand-made hints only: tests/test_recomp.py compares its blocks with nestest.log
    set(AUTO_HINTS OFF)
    nes_recompile(nestest ${NESTEST_ROM} ${CMAKE_SOURCE_DIR}/tests/data/nestest.toml)
    unset(AUTO_HINTS)  # the cache value again, for the games below
    add_executable(nestest_recomp_tests runtime/tests/cpu_tests.c)
    target_link_libraries(nestest_recomp_tests PRIVATE nestest_code)
endif()

# Your own games (never committed): -DROMS="a.nes;b.fds" (.fds needs FDS_BIOS).
# Target name = file name without "(...)"/"[...]" tags, lower case,
# non-alphanumerics -> '_' (e.g. "Arkista's Ring (U) [!].nes" -> arkista_s_ring);
# hints are read from games/<name>.toml when present.
set(ROMS "" CACHE STRING "ROM files (.nes, .fds) to recompile (semicolon-separated)")
# Relative paths are relative to the folder cmake was run from (for the
# release kit that is the archive folder, not kit/), else to this source
# folder. CMake has no variable for the former, but a cmake -P child sees it
# as CMAKE_SOURCE_DIR. The absolute paths go back into the cache, so later
# re-configures (from cmake --build, an IDE) resolve nothing again.
set(FDS_BIOS "" CACHE FILEPATH "FDS BIOS (8 KB) to recompile into fds and fds_headless")
if(ROMS OR FDS_BIOS)
    file(WRITE ${CMAKE_BINARY_DIR}/print_cwd.cmake
        "execute_process(COMMAND \"\${CMAKE_COMMAND}\" -E echo \"\${CMAKE_SOURCE_DIR}\")\n")
    execute_process(COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/print_cwd.cmake
        OUTPUT_VARIABLE cwd OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(FDS_BIOS)  # the same resolution as ROMS below
    get_filename_component(from_cwd "${FDS_BIOS}" ABSOLUTE BASE_DIR "${cwd}")
    get_filename_component(from_src "${FDS_BIOS}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(EXISTS "${from_cwd}")
        set(FDS_BIOS "${from_cwd}" CACHE FILEPATH "FDS BIOS (8 KB) to recompile into fds and fds_headless" FORCE)
    elseif(EXISTS "${from_src}")
        set(FDS_BIOS "${from_src}" CACHE FILEPATH "FDS BIOS (8 KB) to recompile into fds and fds_headless" FORCE)
    else()
        message(FATAL_ERROR "FDS_BIOS: not found: ${FDS_BIOS} (looked in ${cwd} and ${CMAKE_SOURCE_DIR})")
    endif()
endif()
if(ROMS)
    set(absolute_roms "")
    foreach(rom IN LISTS ROMS)
        get_filename_component(from_cwd "${rom}" ABSOLUTE BASE_DIR "${cwd}")
        get_filename_component(from_src "${rom}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(EXISTS "${from_cwd}")
            list(APPEND absolute_roms "${from_cwd}")
        elseif(EXISTS "${from_src}")
            list(APPEND absolute_roms "${from_src}")
        else()
            message(FATAL_ERROR "ROMS: not found: ${rom} (looked in ${cwd} and ${CMAKE_SOURCE_DIR})")
        endif()
    endforeach()
    set(ROMS "${absolute_roms}" CACHE STRING "ROM files (.nes, .fds) to recompile (semicolon-separated)" FORCE)
endif()
foreach(rom IN LISTS ROMS)
    get_filename_component(stem "${rom}" NAME_WLE)
    string(REGEX REPLACE "[ ]*[[(][^])]*[])]" "" stem "${stem}")  # drop (U), [!], ...
    string(TOLOWER "${stem}" name)
    string(REGEX REPLACE "[^a-z0-9]+" "_" name "${name}")
    string(REGEX REPLACE "^_+|_+$" "" name "${name}")
    nes_recompile(${name} "${rom}" ${CMAKE_SOURCE_DIR}/games/${name}.toml)
    list(APPEND game_names ${name})
endforeach()
# Famicom Disk System: -DFDS_BIOS=disksys.rom (your 8 KB BIOS, never
# committed) also recompiles the BIOS alone into fds and fds_headless, which
# play any .fds image with it embedded (no fds_bios setting needed); the
# games' code stays interpreted there (.fds files in ROMS recompile it). No
# tracing (it would need a disk): hints come from games/fds.toml and
# fds.misses, which the program writes as you play.
if(FDS_BIOS)
    set(AUTO_HINTS OFF)
    nes_recompile(fds "${FDS_BIOS}" ${CMAKE_SOURCE_DIR}/games/fds.toml)
    unset(AUTO_HINTS)  # the cache value again
endif()
# for tests/test_regression.py: the games this build recompiles
list(JOIN game_names "\n" game_lines)
file(WRITE ${CMAKE_BINARY_DIR}/nes_games.txt "${game_lines}\n")
