set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# These tests use the C assert() macro as their test oracle. Release and
# RelWithDebInfo normally define NDEBUG, which would otherwise compile every
# assertion out and turn the suite into a collection of no-op executables.
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

add_executable(test_address_space
    test_address_space.c
    ../app/src/address_space.c)
target_include_directories(test_address_space PRIVATE ../app/src)
target_link_libraries(test_address_space PRIVATE mgba)

add_executable(test_fe8_profile
    test_fe8_profile.c
    ../app/src/address_space.c
    ../app/src/fe8_profile.c)
target_include_directories(test_fe8_profile PRIVATE ../app/src)
target_link_libraries(test_fe8_profile PRIVATE mgba)

add_executable(test_archanae_profile
    test_archanae_profile.c
    ../app/src/address_space.c
    ../app/src/fe8_profile.c)
target_include_directories(test_archanae_profile PRIVATE ../app/src)
target_link_libraries(test_archanae_profile PRIVATE mgba)

add_executable(test_frame_scheduler
    test_frame_scheduler.c
    ../app/src/frame_scheduler.c)
target_include_directories(test_frame_scheduler PRIVATE ../app/src)

add_executable(test_extended_map_renderer
    test_extended_map_renderer.c
    ../app/src/extended_map_renderer.c)
target_include_directories(test_extended_map_renderer PRIVATE ../app/src)

add_executable(test_extended_presentation
    test_extended_presentation.c
    ../app/src/extended_presentation.c)
target_include_directories(test_extended_presentation PRIVATE ../app/src)

add_executable(test_extended_unit_renderer
    test_extended_unit_renderer.c
    ../app/src/extended_unit_renderer.c)
target_include_directories(test_extended_unit_renderer PRIVATE ../app/src)

add_executable(test_frame_alignment
    test_frame_alignment.c
    ../app/src/frame_alignment.c)
target_include_directories(test_frame_alignment PRIVATE ../app/src)

add_executable(test_mouse_controller
    test_mouse_controller.c
    ../app/src/mouse_controller.c)
target_include_directories(test_mouse_controller PRIVATE ../app/src)

add_executable(test_pointer_mapping
    test_pointer_mapping.c
    ../app/src/pointer_mapping.c)
target_include_directories(test_pointer_mapping PRIVATE ../app/src)

add_executable(test_viewport_controller
    test_viewport_controller.c
    ../app/src/viewport_controller.c
    ../app/src/extended_map_renderer.c)
target_include_directories(test_viewport_controller PRIVATE ../app/src)

add_executable(test_display_scaling
    test_display_scaling.c
    ../app/src/display_scaling.c)
target_include_directories(test_display_scaling PRIVATE ../app/src)

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
    ../app/src/host_settings.c)
target_include_directories(test_host_settings PRIVATE ../app/src)
target_link_libraries(test_host_settings PRIVATE ${FE8_TEST_SDL_TARGET})

add_executable(test_host_cursor
    test_host_cursor.c
    ../app/src/host_cursor.c)
target_include_directories(test_host_cursor PRIVATE ../app/src)

add_executable(test_host_text
    test_host_text.c
    ../app/src/host_text_bitmap.c)
target_include_directories(test_host_text PRIVATE ../app/src)

add_test(NAME fe8_profile COMMAND test_fe8_profile)
add_test(NAME archanae_profile COMMAND test_archanae_profile)
add_test(NAME frame_scheduler COMMAND test_frame_scheduler)
add_test(NAME address_space COMMAND test_address_space)
add_test(NAME extended_map_renderer COMMAND test_extended_map_renderer)
add_test(NAME extended_presentation COMMAND test_extended_presentation)
add_test(NAME extended_unit_renderer COMMAND test_extended_unit_renderer)
add_test(NAME frame_alignment COMMAND test_frame_alignment)
add_test(NAME mouse_controller COMMAND test_mouse_controller)

add_executable(test_prebattle_inventory
    test_prebattle_inventory.c
    ../app/src/address_space.c
    ../app/src/fe8_catalog.c
    ../app/src/prebattle_inventory.c
    ../app/src/fe8_profile.c)
target_include_directories(test_prebattle_inventory PRIVATE ../app/src)
target_link_libraries(test_prebattle_inventory PRIVATE mgba)
add_test(NAME prebattle_inventory COMMAND test_prebattle_inventory)

add_executable(test_fe8_catalog_portrait
    test_fe8_catalog_portrait.c
    ../app/src/fe8_catalog.c)
target_include_directories(test_fe8_catalog_portrait PRIVATE ../app/src)
add_test(NAME fe8_catalog_portrait COMMAND test_fe8_catalog_portrait)

add_executable(test_fe8_catalog_effectiveness
    test_fe8_catalog_effectiveness.c
    ../app/src/fe8_catalog.c)
target_include_directories(test_fe8_catalog_effectiveness PRIVATE ../app/src)
add_test(NAME fe8_catalog_effectiveness COMMAND test_fe8_catalog_effectiveness)

add_executable(test_prebattle_inventory_ui
    test_prebattle_inventory_ui.c
    ../app/src/prebattle_inventory_ui.c
    ../app/src/host_text_bitmap.c)
target_include_directories(test_prebattle_inventory_ui PRIVATE ../app/src)
add_test(NAME prebattle_inventory_ui COMMAND test_prebattle_inventory_ui)
add_test(NAME pointer_mapping COMMAND test_pointer_mapping)
add_test(NAME viewport_controller COMMAND test_viewport_controller)
add_test(NAME display_scaling COMMAND test_display_scaling)
add_test(NAME host_settings COMMAND test_host_settings)
add_test(NAME host_cursor COMMAND test_host_cursor)
add_test(NAME host_text COMMAND test_host_text)
