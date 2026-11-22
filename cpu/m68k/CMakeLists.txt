# The 68000 (included from ../../CMakeLists.txt, which sets GEN_DIR, for the
# systems that use it). Knows no machine: the system implements m68k_read,
# m68k_write, m68k_idle, m68k_aborted, m68k_iack, m68k_reset_out (m68k_cpu.h).

# The instruction templates (m68k.c), for recompilers and the interpreter's
# handlers, so instruction semantics exist once.
add_library(m68k_templates STATIC m68k.c)
target_include_directories(m68k_templates PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(m68k_templates PUBLIC recomp_template)

add_executable(m68k_gen m68k_gen.c)
target_link_libraries(m68k_gen PRIVATE m68k_templates)

# WORKING_DIRECTORY: see cpu/6502/CMakeLists.txt
add_custom_command(
    OUTPUT ${GEN_DIR}/m68k_handlers.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
    COMMAND m68k_gen ${GEN_DIR}/m68k_handlers.inc
    DEPENDS m68k_gen
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating the 68000 interpreter's handlers")

# The interpreter (m68k_cpu.c) and the instruction helpers (m68k_ops.h).
add_library(m68k STATIC m68k_cpu.c ${GEN_DIR}/m68k_handlers.inc)
target_include_directories(m68k PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${GEN_DIR})

if(TESTS)
    # SingleStepTests m68000 on a flat bus (tests/genesis/test_m68k.py)
    add_executable(m68k_tests m68k_tests.c)
    target_link_libraries(m68k_tests PRIVATE m68k)
endif()
