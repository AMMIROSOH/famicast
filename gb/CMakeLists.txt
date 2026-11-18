# The Game Boy (included from ../CMakeLists.txt, which sets GEN_DIR). So
# far the SM83 CPU: gbrecomp writes the interpreter's opcode switch from the
# instruction templates (recomp/sm83.c), as nesrecomp does for the 6502.
add_executable(gbrecomp recomp/main.c recomp/sm83.c)
target_link_libraries(gbrecomp PRIVATE recomp_template)

add_custom_command(
    OUTPUT ${GEN_DIR}/gb_cpu_switch.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
    COMMAND gbrecomp --cpu-switch ${GEN_DIR}/gb_cpu_switch.inc
    DEPENDS gbrecomp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating the Game Boy interpreter's opcode switch")

add_library(gbcpu STATIC runtime/cpu.c ${GEN_DIR}/gb_cpu_switch.inc)
target_include_directories(gbcpu PUBLIC runtime PRIVATE ${GEN_DIR})

# SingleStepTests sm83 on a flat bus (tests/test_gb_cpu.py)
add_executable(gb_cpu_tests tests/cpu_tests.c)
target_link_libraries(gb_cpu_tests PRIVATE gbcpu)

# The Game Boy system (bus, timer, interrupts, cartridges).
add_library(gbcore STATIC runtime/gb.c runtime/ppu.c runtime/cart.c)
target_link_libraries(gbcore PUBLIC gbcpu core)

# Test ROMs report on the serial port (tests/test_gb_roms.py)
add_executable(gb_headless tests/gb_headless.c)
target_link_libraries(gb_headless PRIVATE gbcore)

if(FRONTEND)
    # gb: plays a Game Boy ROM in the interpreter (core/frontend + frontend.c)
    frontend(gb ${CMAKE_CURRENT_SOURCE_DIR}/frontend.c)
    target_link_libraries(gb PRIVATE gbcore)
endif()
