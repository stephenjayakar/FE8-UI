cmake_minimum_required(VERSION 3.20)

project(fe8_extended_frontend VERSION 0.2.1 LANGUAGES C)

option(FE8_BUILD_TESTS "Build frontend unit tests" ON)

find_package(ZLIB REQUIRED)

# Keep the core build small. The submodule gitlink pins the exact mGBA revision.
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
set(DISABLE_DEPS OFF CACHE BOOL "" FORCE)
set(USE_DEBUGGERS OFF CACHE BOOL "" FORCE)

set(FE8_MGBA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mgba")
if(NOT EXISTS "${FE8_MGBA_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "The required mGBA submodule is missing. Run: "
        "git submodule update --init third_party/mgba")
endif()

# mGBA is an implementation dependency, not part of this project's install
# payload. EXCLUDE_FROM_ALL keeps its install rules out of this project.
add_subdirectory("${FE8_MGBA_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/mgba" EXCLUDE_FROM_ALL)
include(cmake/MgbaStateCompatibility.cmake)
# All static-core consumers need BUILD_STATIC on MSVC to avoid DLL imports.
# The feature definitions also determine the public mCore struct layout.
target_compile_definitions(mgba INTERFACE
    $<TARGET_PROPERTY:mgba,COMPILE_DEFINITIONS>)

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

add_executable(fe8-mgba-sdl
    app/src/voxel_renderer.c
    app/src/voxel_buildings.c
    app/src/main.c
    app/src/native_hud.c
    app/src/native_hud_host.c
    app/src/address_space.c
    app/src/fe8_profile.c
    app/src/frame_alignment.c
    app/src/frame_scheduler.c
    app/src/extended_map_renderer.c
    app/src/extended_presentation.c
    app/src/extended_unit_renderer.c
    app/src/mouse_controller.c
    app/src/fe8_catalog.c
    app/src/prebattle_inventory.c
    app/src/prebattle_inventory_ui.c
    app/src/inventory_desktop.c
    app/src/inventory_pins.c
    app/src/inventory_effective_stats.c
    app/src/inventory_history.c
    app/src/pointer_mapping.c
    app/src/viewport_controller.c
    app/src/display_scaling.c
    app/src/host_cursor.c
    app/src/host_audio.c
    app/src/host_settings.c)

if(APPLE)
    enable_language(OBJC)
    set_source_files_properties(
        LICENSE
        THIRD_PARTY_NOTICES.md
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties(
        third_party/mgba/LICENSE
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/ThirdParty/mGBA")
    set_target_properties(fe8-mgba-sdl PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "FE8 Extended Frontend"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.feuniverse.fe8-extended-frontend"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    target_sources(fe8-mgba-sdl PRIVATE
        app/src/host_video_gl.c
        app/src/host_text_macos.m
        app/src/macos_library.m
        app/src/macos_settings.m
        app/src/macos_shader_menu.m
        LICENSE
        THIRD_PARTY_NOTICES.md
        third_party/mgba/LICENSE
        ${FE8_MGBA_SOURCE_DIR}/src/platform/opengl/gles2.c)
    target_include_directories(fe8-mgba-sdl PRIVATE
        ${FE8_MGBA_SOURCE_DIR}/src/platform/opengl)
    target_compile_definitions(fe8-mgba-sdl PRIVATE BUILD_GLES3 GL_SILENCE_DEPRECATION)
    target_link_libraries(fe8-mgba-sdl PRIVATE
        "-framework Cocoa"
        "-framework Carbon"
        "-framework CoreText"
        "-framework OpenGL"
        "-framework UniformTypeIdentifiers")
    add_custom_command(TARGET fe8-mgba-sdl POST_BUILD
        COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:fe8-mgba-sdl>"
        COMMENT "Ad-hoc signing macOS application bundle"
        VERBATIM)
else()
    find_package(Freetype REQUIRED)
    add_library(fe8_desktop_text STATIC
        app/src/host_text_freetype.c app/src/host_text_bitmap.c)
    target_compile_definitions(fe8_desktop_text PRIVATE FE8_BITMAP_FALLBACK)
    target_link_libraries(fe8_desktop_text PUBLIC Freetype::Freetype)
    target_link_libraries(fe8-mgba-sdl PRIVATE fe8_desktop_text)
    target_sources(fe8-mgba-sdl PRIVATE
        app/src/host_video_sdl.c
        app/src/macos_library_stub.c)
    if(WIN32)
        target_sources(fe8-mgba-sdl PRIVATE app/src/windows_settings.c app/src/windows_library.c)
        target_link_libraries(fe8-mgba-sdl PRIVATE comctl32)
    else()
        target_sources(fe8-mgba-sdl PRIVATE app/src/macos_settings_stub.c)
    endif()
endif()
target_compile_features(fe8-mgba-sdl PRIVATE c_std_11)
target_include_directories(fe8-mgba-sdl PRIVATE app/src)
# libmGBA's public struct layout is feature-macro dependent. Its static target
# does not export those definitions, so mirror the exact build definitions.
target_compile_definitions(fe8-mgba-sdl PRIVATE
    $<TARGET_PROPERTY:mgba,COMPILE_DEFINITIONS>)
target_link_libraries(fe8-mgba-sdl PRIVATE mgba ZLIB::ZLIB ${FE8_SDL_MAIN_TARGET} ${FE8_SDL_TARGET})

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

if(APPLE)
    install(TARGETS fe8-mgba-sdl
        BUNDLE DESTINATION ".")
else()
    install(TARGETS fe8-mgba-sdl
        RUNTIME DESTINATION bin)
endif()

if(WIN32)
    target_sources(fe8-mgba-sdl PRIVATE app/src/windows_launcher.c)
    target_link_libraries(fe8-mgba-sdl PRIVATE comdlg32)
    install(FILES LICENSE THIRD_PARTY_NOTICES.md docs/windows.md DESTINATION .)
    install(FILES third_party/mgba/LICENSE DESTINATION licenses/mgba)
    install(FILES docs/mgba-source.md DESTINATION licenses/mgba)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mgba-compat/unlicensed.c"
        DESTINATION licenses/mgba/modified-source/src/gba/cart)
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        foreach(_dependency IN ITEMS sdl2 zlib freetype)
            install(FILES
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${_dependency}/copyright"
                DESTINATION "licenses/${_dependency}")
        endforeach()
    endif()
endif()

if(FE8_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
