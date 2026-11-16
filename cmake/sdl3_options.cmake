# SDL3 build options: static, and only what the frontend uses (video, 2D
# render, audio, gamepads; SDL_INIT_* in runtime/platform/sdl3_main.c). The
# 2D renderer uses D3D11/12 on Windows and OpenGL elsewhere, so no Vulkan.
# Used by CMakeLists.txt and, as an initial cache (cmake -C), by the release
# workflow's separate SDL3 build (.github/workflows/release.yml).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
foreach(unused CAMERA HAPTIC POWER SENSOR DIALOG TRAY GPU RENDER_GPU VULKAN RENDER_VULKAN)
    set(SDL_${unused} OFF CACHE BOOL "" FORCE)
endforeach()
