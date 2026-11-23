# The Sega Master System / Mark III. It shares cpu/z80 with the Genesis;
# the Z80 core's bus functions are in runtime/sms.c.

# Its own copy of the Z80 interpreter: the bus functions are global symbols,
# so Genesis and the Master System cannot share one in the combined program.
z80_library(sms_z80 sms_)
add_library(smscore STATIC runtime/sms.c runtime/cart.c runtime/vdp.c runtime/psg.c runtime/detect.c)
target_include_directories(smscore PUBLIC runtime)
target_link_libraries(smscore PUBLIC sms_z80 core)

add_library(sms_loader STATIC runtime/loader.c)
target_include_directories(sms_loader PUBLIC runtime)

add_executable(smsrecomp recomp/main.c)
target_include_directories(smsrecomp PRIVATE runtime)
target_link_libraries(smsrecomp PRIVATE sms_loader smscore sms_recomp_none z80_templates)

add_library(sms_recomp_none STATIC runtime/recomp_none.c)
target_link_libraries(sms_recomp_none PUBLIC smscore)

add_executable(sms_headless tests/sms_headless.c)
target_link_libraries(sms_headless PRIVATE smscore sms_loader sms_recomp_none)

# sms_recompile(NAME ROM [TRACE]...): generates ${GEN_DIR}/NAME.c with
# smsrecomp and builds NAME_headless. AUTO_HINTS traces the ROM in the
# interpreter first; a NAME.misses file next to the program is always read,
# so playing and rebuilding recompiles the code the player reached.
function(sms_recompile name rom)
    set(args "")
    set(deps "${rom}" smsrecomp)
    if(AUTO_HINTS)
        set(trace ${GEN_DIR}/${name}.trace)
        add_custom_command(OUTPUT ${trace}
            COMMAND sms_headless "${rom}" --frames ${AUTO_HINTS_FRAMES} --play --misses ${trace}
            DEPENDS "${rom}" sms_headless
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR} VERBATIM
            COMMENT "Tracing ${name} in the interpreter")
        list(APPEND args --misses ${trace})
        list(APPEND deps ${trace})
    endif()
    set(played ${CMAKE_BINARY_DIR}/${name}.misses)
    if(NOT EXISTS ${played})
        file(TOUCH ${played})
    endif()
    list(APPEND args --misses ${played})
    list(APPEND deps ${played})  # play, then build again: the code reached is recompiled
    if(NOT EMBED_ROM)
        list(APPEND args --no-rom)
    endif()
    set(out ${GEN_DIR}/${name}.c)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND smsrecomp "${rom}" -o ${out} ${args}
        DEPENDS ${deps}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR} VERBATIM
        COMMENT "Recompiling ${name}")
    add_library(${name}_code STATIC ${out})
    target_link_libraries(${name}_code PUBLIC smscore)
    add_executable(${name}_headless ${CMAKE_SOURCE_DIR}/systems/mastersystem/tests/sms_headless.c)
    target_link_libraries(${name}_headless PRIVATE smscore sms_loader ${name}_code)
    if(FRONTEND)
        frontend(${name} mastersystem)
        target_link_libraries(${name} PRIVATE smscore sms_loader ${name}_code)
    endif()
endfunction()

if(TESTS)
    add_executable(sms_random_rom tests/random_rom.c)
    target_link_libraries(sms_random_rom PRIVATE z80_templates)

    add_executable(sms_tests tests/sms_tests.c)
    target_link_libraries(sms_tests PRIVATE smscore sms_recomp_none)

    # A ROM of random legal Z80 instructions, recompiled for the lockstep test.
    set(random ${GEN_DIR}/smstest_random.sms)
    add_custom_command(OUTPUT ${random}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND sms_random_rom ${random}
        DEPENDS sms_random_rom VERBATIM)
    set(AUTO_HINTS OFF)  # a synthetic ROM has no play trace
    sms_recompile(smstest_random ${random})
endif()

if(FRONTEND)
    frontend_system(mastersystem smscore sms_loader sms_recomp_none)
endif()
