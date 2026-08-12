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

add_executable(test_mouse_controller
    test_mouse_controller.c
    ../src/mouse_controller.c)
target_include_directories(test_mouse_controller PRIVATE ../src)

add_executable(test_viewport_controller
    test_viewport_controller.c
    ../src/viewport_controller.c
    ../src/extended_map_renderer.c)
target_include_directories(test_viewport_controller PRIVATE ../src)

enable_testing()
add_test(NAME fe8_profile COMMAND test_fe8_profile)
add_test(NAME extended_map_renderer COMMAND test_extended_map_renderer)
add_test(NAME extended_unit_renderer COMMAND test_extended_unit_renderer)
add_test(NAME mouse_controller COMMAND test_mouse_controller)
add_test(NAME viewport_controller COMMAND test_viewport_controller)
