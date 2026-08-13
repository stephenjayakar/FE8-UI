cmake_minimum_required(VERSION 3.16)
project(fe8_profile_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(test_fe8_profile
    test_fe8_profile.c
    ../src/fe8_profile.c)
target_include_directories(test_fe8_profile PRIVATE ../src)

add_executable(test_extended_map_renderer
    test_extended_map_renderer.c
    ../src/extended_map_renderer.c)
target_include_directories(test_extended_map_renderer PRIVATE ../src)

add_executable(test_extended_unit_renderer
    test_extended_unit_renderer.c
    ../src/extended_unit_renderer.c)
target_include_directories(test_extended_unit_renderer PRIVATE ../src)

add_executable(test_frame_alignment
    test_frame_alignment.c
    ../src/frame_alignment.c)
target_include_directories(test_frame_alignment PRIVATE ../src)

add_executable(test_mouse_controller
    test_mouse_controller.c
    ../src/mouse_controller.c)
target_include_directories(test_mouse_controller PRIVATE ../src)

add_executable(test_pointer_mapping
    test_pointer_mapping.c
    ../src/pointer_mapping.c)
target_include_directories(test_pointer_mapping PRIVATE ../src)

add_executable(test_viewport_controller
    test_viewport_controller.c
    ../src/viewport_controller.c
    ../src/extended_map_renderer.c)
target_include_directories(test_viewport_controller PRIVATE ../src)

find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(FE8_TEST_SDL_TARGET SDL2::SDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(FE8_TEST_SDL_TARGET PkgConfig::SDL2)
endif()

add_executable(test_host_settings
    test_host_settings.c
    ../src/host_settings.c)
target_include_directories(test_host_settings PRIVATE ../src)
target_link_libraries(test_host_settings PRIVATE ${FE8_TEST_SDL_TARGET})

add_executable(test_host_cursor
    test_host_cursor.c
    ../src/host_cursor.c)
target_include_directories(test_host_cursor PRIVATE ../src)

enable_testing()
add_test(NAME fe8_profile COMMAND test_fe8_profile)
add_test(NAME extended_map_renderer COMMAND test_extended_map_renderer)
add_test(NAME extended_unit_renderer COMMAND test_extended_unit_renderer)
add_test(NAME frame_alignment COMMAND test_frame_alignment)
add_test(NAME mouse_controller COMMAND test_mouse_controller)
add_test(NAME pointer_mapping COMMAND test_pointer_mapping)
add_test(NAME viewport_controller COMMAND test_viewport_controller)
add_test(NAME host_settings COMMAND test_host_settings)
add_test(NAME host_cursor COMMAND test_host_cursor)
