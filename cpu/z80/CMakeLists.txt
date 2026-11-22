# The Z80 (included from ../../CMakeLists.txt, which sets GEN_DIR, for the
# systems that use it). Knows no machine: the system implements z80_fetch,
# z80_read, z80_write, z80_in, z80_out, z80_idle and z80_irq_ack (z80_cpu.h).

# The instruction templates (z80.c), for recompilers and the interpreter's
# switch, so instruction semantics exist once.
add_library(z80_templates STATIC z80.c)
target_include_directories(z80_templates PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(z80_templates PUBLIC recomp_template)

add_executable(z80_gen z80_gen.c)
target_link_libraries(z80_gen PRIVATE z80_templates)

# WORKING_DIRECTORY: see cpu/6502/CMakeLists.txt
add_custom_command(
    OUTPUT ${GEN_DIR}/z80_switch.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
    COMMAND z80_gen ${GEN_DIR}/z80_switch.inc
    DEPENDS z80_gen
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating the Z80 interpreter's opcode switch")

# The interpreter (z80_cpu.c) and the instruction helpers (z80_ops.h).
add_library(z80 STATIC z80_cpu.c ${GEN_DIR}/z80_switch.inc)
target_include_directories(z80 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${GEN_DIR})

# SingleStepTests z80 on a flat bus (tests/genesis/test_z80.py)
add_executable(z80_tests z80_tests.c)
target_link_libraries(z80_tests PRIVATE z80)
