cmake_minimum_required(VERSION 3.20)

project(fe8_mgba_sdl LANGUAGES C)

include(FetchContent)

# Keep the core build small and reproducible. This is the full mGBA commit for
# the 0.10.3 release, not a moving branch or a shallow version label.
set(MGBA_REVISION 1c61b54208ca6266129d0f2394c04bd8c44f98c5)
set(LIBMGBA_ONLY ON CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_SDL OFF CACHE BOOL "" FORCE)
set(BUILD_QT OFF CACHE BOOL "" FORCE)
set(BUILD_LIBRETRO OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_SUITE OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(M_CORE_GBA ON CACHE BOOL "" FORCE)
set(M_CORE_GB OFF CACHE BOOL "" FORCE)
set(DISABLE_DEPS ON CACHE BOOL "" FORCE)
set(USE_DEBUGGERS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    mgba
    GIT_REPOSITORY https://github.com/mgba-emu/mgba.git
    GIT_TAG ${MGBA_REVISION}
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(mgba)

find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(FE8_SDL_TARGET SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
        set(FE8_SDL_MAIN_TARGET SDL2::SDL2main)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(FE8_SDL_TARGET PkgConfig::SDL2)
endif()

add_executable(fe8-mgba-sdl src/main.c)
target_compile_features(fe8-mgba-sdl PRIVATE c_std_11)
target_link_libraries(fe8-mgba-sdl PRIVATE mgba ${FE8_SDL_MAIN_TARGET} ${FE8_SDL_TARGET})

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    # Homebrew's native arm64 SDL2 and the host compiler are the default on
    # Apple Silicon. Override this cache variable for a universal build.
    set_target_properties(fe8-mgba-sdl PROPERTIES OSX_ARCHITECTURES arm64)
endif()

if(MSVC)
    target_compile_options(fe8-mgba-sdl PRIVATE /W4)
else()
    target_compile_options(fe8-mgba-sdl PRIVATE -Wall -Wextra -Wpedantic)
endif()
