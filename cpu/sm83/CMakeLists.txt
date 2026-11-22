# The SM83 (Game Boy CPU) (included from ../../CMakeLists.txt, which sets
# GEN_DIR, for the systems that use it). Knows no machine: the system
# implements gb_read/gb_write/gb_idle/gb_irq_ack (sm83_cpu.h).

# The instruction templates (sm83.c), for recompilers and the interpreter's
# switch, so instruction semantics exist once.
add_library(sm83_templates STATIC sm83.c)
target_include_directories(sm83_templates PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm83_templates PUBLIC recomp_template)

add_executable(sm83_gen sm83_gen.c)
target_link_libraries(sm83_gen PRIVATE sm83_templates)

# WORKING_DIRECTORY: see cpu/6502/CMakeLists.txt
add_custom_command(
    OUTPUT ${GEN_DIR}/sm83_switch.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
    COMMAND sm83_gen ${GEN_DIR}/sm83_switch.inc
    DEPENDS sm83_gen
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating the SM83 interpreter's opcode switch")

# The interpreter (sm83_cpu.c) and the instruction helpers (sm83_ops.h).
add_library(sm83 STATIC sm83_cpu.c ${GEN_DIR}/sm83_switch.inc)
target_include_directories(sm83 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${GEN_DIR})

if(TESTS)
    # SingleStepTests sm83 on a flat bus (tests/gb/test_cpu.py)
    add_executable(gb_cpu_tests sm83_tests.c)
    target_link_libraries(gb_cpu_tests PRIVATE sm83)
endif()
