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

function(fe8_add_test name)
    add_executable(test_${name} ${ARGN})
    target_include_directories(test_${name} PRIVATE ../app/src)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

fe8_add_test(address_space
    test_address_space.c ../app/src/address_space.c)
target_link_libraries(test_address_space PRIVATE mgba)

foreach(name IN ITEMS fe8_profile archanae_profile)
    fe8_add_test(${name}
        test_${name}.c
        ../app/src/address_space.c
        ../app/src/fe8_profile.c)
    target_link_libraries(test_${name} PRIVATE mgba)
endforeach()

foreach(name IN ITEMS
        frame_scheduler extended_map_renderer extended_presentation
        extended_unit_renderer frame_alignment mouse_controller
        pointer_mapping display_scaling host_cursor)
    fe8_add_test(${name} test_${name}.c ../app/src/${name}.c)
endforeach()

fe8_add_test(viewport_controller
    test_viewport_controller.c
    ../app/src/viewport_controller.c
    ../app/src/extended_map_renderer.c)

fe8_add_test(host_settings
    test_host_settings.c ../app/src/host_settings.c)
# Reuse the SDL target already resolved by the top-level build.
target_link_libraries(test_host_settings PRIVATE ${FE8_SDL_TARGET})

fe8_add_test(host_text
    test_host_text.c ../app/src/host_text_bitmap.c)

fe8_add_test(prebattle_inventory
    test_prebattle_inventory.c
    ../app/src/address_space.c
    ../app/src/fe8_catalog.c
    ../app/src/prebattle_inventory.c
    ../app/src/fe8_profile.c)
target_link_libraries(test_prebattle_inventory PRIVATE mgba)

foreach(name IN ITEMS fe8_catalog_portrait fe8_catalog_effectiveness)
    fe8_add_test(${name} test_${name}.c ../app/src/fe8_catalog.c)
endforeach()

set(inventory_ui_sources
    ../app/src/prebattle_inventory_ui.c
    ../app/src/inventory_desktop.c
    ../app/src/inventory_pins.c
    ../app/src/inventory_history.c
    ../app/src/prebattle_inventory.c
    ../app/src/fe8_catalog.c)

foreach(name IN ITEMS prebattle_inventory_ui inventory_hover)
    fe8_add_test(${name}
        test_${name}.c ${inventory_ui_sources} ../app/src/host_text_bitmap.c)
endforeach()

# ROMs are user-supplied local test inputs, never repository or CI artifacts.
add_executable(test_inventory_rom
    test_inventory_rom.c
    ../app/src/address_space.c
    ../app/src/fe8_profile.c
    ${inventory_ui_sources}
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
    fe8_add_test(inventory_hover_native
        test_inventory_hover.c ${inventory_ui_sources} ../app/src/host_text_macos.m)
    target_link_libraries(test_inventory_hover_native PRIVATE
        "-framework CoreGraphics" "-framework CoreText" "-framework CoreFoundation")

    fe8_add_test(host_text_native
        test_host_text.c ../app/src/host_text_macos.m)
    target_compile_definitions(test_host_text_native PRIVATE FE8_TEST_NATIVE_TEXT)
    target_link_libraries(test_host_text_native PRIVATE
        "-framework CoreGraphics" "-framework CoreText" "-framework CoreFoundation")
endif()

# The desktop path is tested separately from the retained bitmap layout.
if(APPLE)
    set(DESKTOP_TEXT_SOURCE ../app/src/host_text_macos.m)
    set(DESKTOP_TEXT_LIBS
        "-framework CoreGraphics" "-framework CoreText" "-framework CoreFoundation")
else()
    set(DESKTOP_TEXT_SOURCE "")
    set(DESKTOP_TEXT_LIBS fe8_desktop_text)
endif()

fe8_add_test(inventory_desktop
    test_inventory_desktop.c ${inventory_ui_sources} ${DESKTOP_TEXT_SOURCE})
target_link_libraries(test_inventory_desktop PRIVATE ${DESKTOP_TEXT_LIBS})

fe8_add_test(desktop_text test_host_text.c ${DESKTOP_TEXT_SOURCE})
target_compile_definitions(test_desktop_text PRIVATE
    FE8_TEST_NATIVE_TEXT FE8_TEST_SCALABLE_TEXT)
target_link_libraries(test_desktop_text PRIVATE ${DESKTOP_TEXT_LIBS})

add_executable(test_inventory_rom_dense
    test_inventory_rom.c
    ../app/src/address_space.c
    ../app/src/fe8_profile.c
    ${inventory_ui_sources}
    ${DESKTOP_TEXT_SOURCE})
target_include_directories(test_inventory_rom_dense PRIVATE ../app/src)
target_compile_definitions(test_inventory_rom_dense PRIVATE
    FE8_TEST_DESKTOP $<TARGET_PROPERTY:mgba,COMPILE_DEFINITIONS>)
target_link_libraries(test_inventory_rom_dense PRIVATE mgba ${DESKTOP_TEXT_LIBS})
if(FE8_ARCHANAE_ROM)
    add_test(NAME inventory_dense_archanae
        COMMAND test_inventory_rom_dense "${FE8_ARCHANAE_ROM}" archanae)
    set_tests_properties(inventory_dense_archanae PROPERTIES TIMEOUT 90)
endif()
if(FE8_SACRED_ECHOES_ROM)
    add_test(NAME inventory_dense_sacred_echoes
        COMMAND test_inventory_rom_dense "${FE8_SACRED_ECHOES_ROM}" sacred-echoes)
    set_tests_properties(inventory_dense_sacred_echoes PROPERTIES TIMEOUT 90)
endif()

# Workspace interactions are tested against the same model and native text
# backend as the app, without needing copyrighted ROMs or save files in CI.
fe8_add_test(inventory_workspace
    test_inventory_workspace.c ${inventory_ui_sources} ${DESKTOP_TEXT_SOURCE})
target_link_libraries(test_inventory_workspace PRIVATE ${DESKTOP_TEXT_LIBS})

fe8_add_test(fe8_catalog_locks test_fe8_catalog_locks.c
    ../app/src/fe8_catalog.c ../app/src/prebattle_inventory.c)

fe8_add_test(inventory_loadouts
    test_inventory_loadouts.c ${inventory_ui_sources} ${DESKTOP_TEXT_SOURCE})
target_link_libraries(test_inventory_loadouts PRIVATE ${DESKTOP_TEXT_LIBS})
fe8_add_test(inventory_history
    test_inventory_history.c ../app/src/inventory_history.c
    ../app/src/prebattle_inventory.c ../app/src/fe8_catalog.c)

# Text recorder asserts actual label/value submissions in both inventory views.
fe8_add_test(inventory_stats test_inventory_stats.c ${inventory_ui_sources})


fe8_add_test(inventory_effective_stats test_inventory_effective_stats.c
    ../app/src/inventory_effective_stats.c ../app/src/address_space.c
    ../app/src/fe8_profile.c ${inventory_ui_sources} ${DESKTOP_TEXT_SOURCE})
target_compile_definitions(test_inventory_effective_stats PRIVATE
    $<TARGET_PROPERTY:mgba,COMPILE_DEFINITIONS>)
target_link_libraries(test_inventory_effective_stats PRIVATE mgba ${DESKTOP_TEXT_LIBS})
if(FE8_ARCHANAE_ROM)
    add_test(NAME effective_stats_archanae COMMAND test_inventory_effective_stats "${FE8_ARCHANAE_ROM}" archanae)
    set_tests_properties(effective_stats_archanae PROPERTIES TIMEOUT 90)
endif()
if(FE8_SACRED_ECHOES_ROM)
    add_test(NAME effective_stats_sacred_echoes COMMAND test_inventory_effective_stats "${FE8_SACRED_ECHOES_ROM}" sacred-echoes)
    set_tests_properties(effective_stats_sacred_echoes PROPERTIES TIMEOUT 90)
endif()

fe8_add_test(inventory_pins test_inventory_pins.c ${inventory_ui_sources} ${DESKTOP_TEXT_SOURCE})
target_link_libraries(test_inventory_pins PRIVATE ${DESKTOP_TEXT_LIBS})

fe8_add_test(inventory_unit_search test_inventory_unit_search.c ${inventory_ui_sources} ${DESKTOP_TEXT_SOURCE})
target_link_libraries(test_inventory_unit_search PRIVATE ${DESKTOP_TEXT_LIBS})

fe8_add_test(native_hud test_native_hud.c ../app/src/native_hud.c)

add_executable(test_native_hud_rom test_native_hud_rom.c
    ../app/src/native_hud.c ../app/src/fe8_profile.c ../app/src/address_space.c
    ../app/src/native_hud_host.c ../app/src/host_cursor.c ../app/src/host_settings.c)
target_include_directories(test_native_hud_rom PRIVATE ../app/src)
target_compile_definitions(test_native_hud_rom PRIVATE
    $<TARGET_PROPERTY:mgba,COMPILE_DEFINITIONS>)
target_link_libraries(test_native_hud_rom PRIVATE mgba ${FE8_SDL_TARGET})
if(FE8_ARCHANAE_ROM)
    add_test(NAME native_hud_archanae COMMAND test_native_hud_rom "${FE8_ARCHANAE_ROM}")
    set_tests_properties(native_hud_archanae PROPERTIES TIMEOUT 90)
endif()
if(FE8_SACRED_ECHOES_ROM)
    add_test(NAME native_hud_sacred_echoes COMMAND test_native_hud_rom "${FE8_SACRED_ECHOES_ROM}")
    set_tests_properties(native_hud_sacred_echoes PROPERTIES TIMEOUT 90)
endif()

fe8_add_test(native_hud_host test_native_hud_host.c
    ../app/src/native_hud.c ../app/src/native_hud_host.c
    ../app/src/host_cursor.c ../app/src/host_settings.c)
target_link_libraries(test_native_hud_host PRIVATE ${FE8_SDL_TARGET})

# Cold-renderer terrain regression with a real native-frame pixel oracle.
# ROM and optional PPM/state outputs stay local, just like inventory ROM tests.
add_executable(test_archanae_map_loading test_archanae_map_loading.c
    ../app/src/address_space.c ../app/src/fe8_profile.c ../app/src/extended_map_renderer.c)
target_include_directories(test_archanae_map_loading PRIVATE ../app/src)
target_compile_definitions(test_archanae_map_loading PRIVATE
    $<TARGET_PROPERTY:mgba,COMPILE_DEFINITIONS>)
target_link_libraries(test_archanae_map_loading PRIVATE mgba)
if(FE8_ARCHANAE_ROM)
    add_test(NAME archanae_map_loading
        COMMAND test_archanae_map_loading "${FE8_ARCHANAE_ROM}")
    set_tests_properties(archanae_map_loading PROPERTIES TIMEOUT 90)
endif()
