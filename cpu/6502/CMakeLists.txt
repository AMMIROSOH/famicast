# The 6502 (2A03) (included from ../../CMakeLists.txt, which sets GEN_DIR,
# for the systems that use it). Knows no machine: the system implements
# cpu_read/cpu_write (cpu6502.h).

# The opcode table and C templates (ops.c, c6502.c), for recompilers and the
# interpreter's switch, so instruction semantics exist once.
add_library(cpu6502_templates STATIC ops.c c6502.c)
target_include_directories(cpu6502_templates PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cpu6502_templates PUBLIC recomp_template)

add_executable(cpu6502_gen cpu6502_gen.c)
target_link_libraries(cpu6502_gen PRIVATE cpu6502_templates)

# WORKING_DIRECTORY on custom commands that run our programs: without it the
# Visual Studio generator writes a bare "cpu6502_gen.exe" (relative to build/),
# which MSBuild's cmd does not look for in the current folder.
add_custom_command(
    OUTPUT ${GEN_DIR}/cpu6502_switch.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
    COMMAND cpu6502_gen ${GEN_DIR}/cpu6502_switch.inc
    DEPENDS cpu6502_gen
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating the 6502 interpreter's opcode switch")

# The interpreter (cpu6502.c) and the instruction helpers recompiled code
# uses (cpu6502_ops.h). cpu6502_library(NAME PREFIX): another copy, its
# functions and the bus it calls renamed PREFIXcpu_step, PREFIXcpu_read, ...
# (cpu6502.h), for a second system with this CPU in the same program.
set(CPU6502_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")  # seen from the systems' folders
function(cpu6502_library name prefix)
    add_library(${name} STATIC ${CPU6502_DIR}/cpu6502.c ${GEN_DIR}/cpu6502_switch.inc)
    target_include_directories(${name} PUBLIC ${CPU6502_DIR} PRIVATE ${GEN_DIR})
    if(prefix)
        target_compile_definitions(${name} PUBLIC CPU6502_PREFIX=${prefix})
        add_dependencies(${name} cpu6502)  # the switch's rule belongs to this folder's target
    endif()
endfunction()
cpu6502_library(cpu6502 "")
