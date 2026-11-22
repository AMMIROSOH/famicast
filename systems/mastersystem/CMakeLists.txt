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

add_library(sms_recomp_none STATIC runtime/recomp_none.c)
target_link_libraries(sms_recomp_none PUBLIC smscore)

add_executable(sms_headless tests/sms_headless.c)
target_link_libraries(sms_headless PRIVATE smscore sms_loader sms_recomp_none)

if(TESTS)
    add_executable(sms_tests tests/sms_tests.c)
    target_link_libraries(sms_tests PRIVATE smscore sms_recomp_none)
endif()

if(FRONTEND)
    frontend_system(mastersystem smscore sms_loader sms_recomp_none)
endif()
