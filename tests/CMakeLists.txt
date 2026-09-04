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
    ../app/src/inventory_desktop.c
    ../app/src/prebattle_inventory.c
    ../app/src/fe8_catalog.c
    ../app/src/host_text_bitmap.c)
target_include_directories(test_prebattle_inventory_ui PRIVATE ../app/src)
add_test(NAME prebattle_inventory_ui COMMAND test_prebattle_inventory_ui)
add_test(NAME pointer_mapping COMMAND test_pointer_mapping)
add_test(NAME viewport_controller COMMAND test_viewport_controller)
add_test(NAME display_scaling COMMAND test_display_scaling)
add_test(NAME host_settings COMMAND test_host_settings)
add_test(NAME host_cursor COMMAND test_host_cursor)
add_test(NAME host_text COMMAND test_host_text)

add_executable(test_inventory_hover
    test_inventory_hover.c
    ../app/src/prebattle_inventory_ui.c
    ../app/src/inventory_desktop.c
    ../app/src/prebattle_inventory.c
    ../app/src/fe8_catalog.c
    ../app/src/host_text_bitmap.c)
target_include_directories(test_inventory_hover PRIVATE ../app/src)
add_test(NAME inventory_hover COMMAND test_inventory_hover)

# ROMs are user-supplied local test inputs, never repository or CI artifacts.
add_executable(test_inventory_rom
    test_inventory_rom.c
    ../app/src/address_space.c
    ../app/src/fe8_profile.c
    ../app/src/prebattle_inventory.c
    ../app/src/prebattle_inventory_ui.c
    ../app/src/inventory_desktop.c
    ../app/src/fe8_catalog.c
    ../app/src/host_text_bitmap.c)
target_include_directories(test_inventory_rom PRIVATE ../app/src)
# mCore's public struct layout depends on the exact feature macros used by
# the core; linking mgba alone does not export them (same as the app target).
target_compile_definitions(test_inventory_rom PRIVATE
    $<TARGET_PROPERTY:mgba,COMPILE_DEFINITIONS>)
target_link_libraries(test_inventory_rom PRIVATE mgba)
set(FE8_ARCHANAE_ROM "" CACHE FILEPATH "Local Archanae ROM for inventory integration tests")
set(FE8_SACRED_ECHOES_ROM "" CACHE FILEPATH "Local Sacred Echoes ROM for inventory integration tests")
if(FE8_ARCHANAE_ROM)
    add_test(NAME inventory_rom_archanae
        COMMAND test_inventory_rom "${FE8_ARCHANAE_ROM}" archanae)
    set_tests_properties(inventory_rom_archanae PROPERTIES TIMEOUT 90)
endif()
if(FE8_SACRED_ECHOES_ROM)
    add_test(NAME inventory_rom_sacred_echoes
        COMMAND test_inventory_rom "${FE8_SACRED_ECHOES_ROM}" sacred-echoes)
    set_tests_properties(inventory_rom_sacred_echoes PROPERTIES TIMEOUT 90)
endif()

if(APPLE)
    add_executable(test_inventory_hover_native
        test_inventory_hover.c
        ../app/src/prebattle_inventory_ui.c
    ../app/src/inventory_desktop.c
        ../app/src/prebattle_inventory.c
        ../app/src/fe8_catalog.c
        ../app/src/host_text_macos.m)
    target_include_directories(test_inventory_hover_native PRIVATE ../app/src)
    target_link_libraries(test_inventory_hover_native PRIVATE
        "-framework CoreGraphics" "-framework CoreText" "-framework CoreFoundation")
    add_test(NAME inventory_hover_native COMMAND test_inventory_hover_native)

    add_executable(test_host_text_native
        test_host_text.c ../app/src/host_text_macos.m)
    target_include_directories(test_host_text_native PRIVATE ../app/src)
    target_compile_definitions(test_host_text_native PRIVATE FE8_TEST_NATIVE_TEXT)
    target_link_libraries(test_host_text_native PRIVATE
        "-framework CoreGraphics" "-framework CoreText" "-framework CoreFoundation")
    add_test(NAME host_text_native COMMAND test_host_text_native)
endif()

# The desktop path is tested separately from the retained bitmap layout.
if(APPLE)
    set(DESKTOP_TEXT_SOURCE ../app/src/host_text_macos.m)
    set(DESKTOP_TEXT_LIBS "-framework CoreGraphics" "-framework CoreText" "-framework CoreFoundation")
else()
    set(DESKTOP_TEXT_SOURCE "")
    set(DESKTOP_TEXT_LIBS fe8_desktop_text)
endif()
add_executable(test_inventory_desktop test_inventory_desktop.c
    ../app/src/prebattle_inventory_ui.c ../app/src/inventory_desktop.c
    ../app/src/prebattle_inventory.c ../app/src/fe8_catalog.c ${DESKTOP_TEXT_SOURCE})
target_include_directories(test_inventory_desktop PRIVATE ../app/src)
target_link_libraries(test_inventory_desktop PRIVATE ${DESKTOP_TEXT_LIBS})
add_test(NAME inventory_desktop COMMAND test_inventory_desktop)
add_executable(test_desktop_text test_host_text.c ${DESKTOP_TEXT_SOURCE})
target_include_directories(test_desktop_text PRIVATE ../app/src)
target_compile_definitions(test_desktop_text PRIVATE FE8_TEST_NATIVE_TEXT FE8_TEST_SCALABLE_TEXT)
target_link_libraries(test_desktop_text PRIVATE ${DESKTOP_TEXT_LIBS})
add_test(NAME desktop_text COMMAND test_desktop_text)
add_executable(test_inventory_rom_dense test_inventory_rom.c
    ../app/src/address_space.c ../app/src/fe8_profile.c
    ../app/src/prebattle_inventory.c ../app/src/prebattle_inventory_ui.c
    ../app/src/inventory_desktop.c ../app/src/fe8_catalog.c ${DESKTOP_TEXT_SOURCE})
target_include_directories(test_inventory_rom_dense PRIVATE ../app/src)
target_compile_definitions(test_inventory_rom_dense PRIVATE FE8_TEST_DESKTOP
    $<TARGET_PROPERTY:mgba,COMPILE_DEFINITIONS>)
target_link_libraries(test_inventory_rom_dense PRIVATE mgba ${DESKTOP_TEXT_LIBS})
if(FE8_ARCHANAE_ROM)
    add_test(NAME inventory_dense_archanae COMMAND test_inventory_rom_dense "${FE8_ARCHANAE_ROM}" archanae)
    set_tests_properties(inventory_dense_archanae PROPERTIES TIMEOUT 90)
endif()
if(FE8_SACRED_ECHOES_ROM)
    add_test(NAME inventory_dense_sacred_echoes COMMAND test_inventory_rom_dense "${FE8_SACRED_ECHOES_ROM}" sacred-echoes)
    set_tests_properties(inventory_dense_sacred_echoes PROPERTIES TIMEOUT 90)
endif()
