# The 6507: the shared 6502 interpreter, its own copy (bus functions a26_cpu_read,
# ...) so a program with the NES too (retrocast) links both (cpu/6502).
cpu6502_library(atari2600_cpu a26_)

add_library(atari2600_cart STATIC runtime/cart.c runtime/detect.c)
target_include_directories(atari2600_cart PUBLIC runtime)
target_link_libraries(atari2600_cart PUBLIC core)

add_library(atari2600_loader STATIC runtime/loader.c)
target_include_directories(atari2600_loader PUBLIC runtime)

add_library(atari2600_bus STATIC runtime/bus.c runtime/riot.c)
target_link_libraries(atari2600_bus PUBLIC atari2600_cart atari2600_cpu)

add_library(atari2600_core STATIC runtime/atari.c runtime/tia.c runtime/palette.c)
target_link_libraries(atari2600_core PUBLIC atari2600_bus core)

add_library(atari2600_none STATIC runtime/recomp_none.c)
target_link_libraries(atari2600_none PUBLIC atari2600_cpu)

add_executable(atari2600recomp recomp/main.c)
target_link_libraries(atari2600recomp PRIVATE atari2600_loader atari2600_cart cpu6502_templates)

add_executable(atari2600_headless tests/headless.c)
target_link_libraries(atari2600_headless PRIVATE atari2600_core atari2600_loader atari2600_none)

if(FRONTEND)
    frontend_system(atari2600 atari2600_core atari2600_loader atari2600_none)  # in retrocast
endif()

if(TESTS)
    add_executable(atari2600_cart_tests tests/cart_tests.c)
    target_link_libraries(atari2600_cart_tests PRIVATE atari2600_cart)

    add_executable(atari2600_bus_tests tests/bus_tests.c)
    target_link_libraries(atari2600_bus_tests PRIVATE atari2600_bus)

    add_executable(atari2600_runtime_tests tests/runtime_tests.c)
    target_link_libraries(atari2600_runtime_tests PRIVATE atari2600_core atari2600_none)

    add_executable(atari2600_bad_recomp_tests tests/headless.c tests/bad_recomp.c)
    target_link_libraries(atari2600_bad_recomp_tests PRIVATE atari2600_core atari2600_loader)
endif()

function(atari2600_recompile name rom hints)
    set(out ${GEN_DIR}/${name}.c)
    set(options "")
    set(hint_dep "")
    if(EXISTS "${hints}")
        set(hint_dep "${hints}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${hints}")
        file(STRINGS "${hints}" hint_lines REGEX "^[ \t]*(region|mapper)[ \t]*=")
        foreach(line IN LISTS hint_lines)
            if(line MATCHES "^[ \t]*(region|mapper)[ \t]*=[ \t]*\"([^\"]+)\"")
                list(APPEND options "--${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
            else()
                message(FATAL_ERROR "Invalid Atari hint: ${line}")
            endif()
        endforeach()
    endif()
    set(trace_dep "")
    if(AUTO_HINTS)
        set(trace_dep ${GEN_DIR}/${name}.a26trace)
        add_custom_command(OUTPUT ${trace_dep}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
            COMMAND atari2600_headless "${rom}" --frames ${AUTO_HINTS_FRAMES} --play --trace ${trace_dep} ${options}
            DEPENDS "${rom}" ${hint_dep} atari2600_headless
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR} VERBATIM)
        list(APPEND options --trace ${trace_dep})
    endif()
    if(NOT EMBED_ROM)
        list(APPEND options --no-rom)
    endif()
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND atari2600recomp "${rom}" -o ${out} ${options}
        DEPENDS "${rom}" ${trace_dep} ${hint_dep} atari2600recomp
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR} VERBATIM)
    add_executable(${name}_headless ${CMAKE_SOURCE_DIR}/systems/atari2600/tests/headless.c ${out})
    target_link_libraries(${name}_headless PRIVATE atari2600_core atari2600_loader)
    if(FRONTEND AND NOT ARGV3 STREQUAL "HEADLESS_ONLY")
        frontend(${name} atari2600)
        target_sources(${name} PRIVATE ${out})
        target_link_libraries(${name} PRIVATE atari2600_core atari2600_loader)
    endif()
endfunction()

if(TESTS)
    # Generated instruction-only test cartridges exercise RAM and bank-switch fallback.
    add_executable(atari2600_make_fixtures tests/make_fixtures.c)
    set(fixture_fixed ${GEN_DIR}/a26_fixed.a26)
    set(fixture_banked ${GEN_DIR}/a26_banked.a26)
    set(fixture_small ${GEN_DIR}/a26_small.a26)
    set(fixture_f6 ${GEN_DIR}/a26_f6.a26)
    set(fixture_f4 ${GEN_DIR}/a26_f4.a26)
    add_custom_command(OUTPUT ${fixture_fixed} ${fixture_banked} ${fixture_small} ${fixture_f6} ${fixture_f4}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND atari2600_make_fixtures ${fixture_fixed} ${fixture_banked} ${fixture_small} ${fixture_f6} ${fixture_f4}
        DEPENDS atari2600_make_fixtures VERBATIM)
    set(previous_trace_frames ${AUTO_HINTS_FRAMES})
    set(AUTO_HINTS_FRAMES 2)
    set(previous_auto_hints ${AUTO_HINTS})
    set(previous_embed_rom ${EMBED_ROM})
    set(AUTO_HINTS ON)
    set(EMBED_ROM ON)
    atari2600_recompile(a26_fixed ${fixture_fixed} "" HEADLESS_ONLY)
    atari2600_recompile(a26_banked ${fixture_banked} "" HEADLESS_ONLY)
    atari2600_recompile(a26_small ${fixture_small} "" HEADLESS_ONLY)
    atari2600_recompile(a26_f6 ${fixture_f6} "" HEADLESS_ONLY)
    atari2600_recompile(a26_f4 ${fixture_f4} "" HEADLESS_ONLY)
    set(EMBED_ROM OFF)
    atari2600_recompile(a26_external ${fixture_fixed} "" HEADLESS_ONLY)
    set(EMBED_ROM ON)
    atari2600_recompile(a26_pal ${fixture_banked} ${CMAKE_CURRENT_SOURCE_DIR}/tests/banked.toml HEADLESS_ONLY)
    set(AUTO_HINTS OFF)
    atari2600_recompile(a26_untraced ${fixture_fixed} "" HEADLESS_ONLY)
    set(AUTO_HINTS ${previous_auto_hints})
    set(EMBED_ROM ${previous_embed_rom})
    set(AUTO_HINTS_FRAMES ${previous_trace_frames})
endif()
