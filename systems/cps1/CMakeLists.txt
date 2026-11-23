# The Capcom Play System I (included from ../../CMakeLists.txt). Its CPUs are
# in cpu/m68k and cpu/z80, listed in the root CMakeLists.txt (CPU_cps1).

# Its own copy of the 68000 interpreter: the bus functions are global
# symbols, so the Genesis and the CPS-1 cannot share one in retrocast.
m68k_library(cps1_m68k cps1_)
add_library(cps1core STATIC runtime/games.c runtime/load.c runtime/detect.c)
target_include_directories(cps1core PUBLIC runtime)
target_link_libraries(cps1core PUBLIC cps1_m68k core)

if(TESTS)
    add_executable(cps1_tests tests/cps1_tests.c)
    target_link_libraries(cps1_tests PRIVATE cps1core)
endif()
