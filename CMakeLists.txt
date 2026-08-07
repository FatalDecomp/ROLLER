cmake_minimum_required(VERSION 3.24...4.0)
project(roller VERSION 0.1.0 LANGUAGES C)

option(ROLLER_BUILD_GAME "Build the ROLLER executable" ON)
option(ROLLER_BUILD_EDITOR_CORE "Build roller-core" OFF)
option(ROLLER_WARNINGS_AS_ERRORS "Treat ROLLER target warnings as errors" OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RollerCoreManifest.cmake")
roller_load_core_manifest(
    "${CMAKE_CURRENT_SOURCE_DIR}/roller-core.srclist"
    "${CMAKE_CURRENT_SOURCE_DIR}")

# These versions match the source revisions pinned by build.zig.zon. Package
# managers may supply a newer compatible release, but never an older one.
set(ROLLER_SDL3_MIN_VERSION "3.2.22")
set(ROLLER_SDL3_IMAGE_MIN_VERSION "3.2.4")

# Keep these component lists aligned with the exe_mod.addCSourceFiles calls in
# build.zig. tools/check_source_set_drift.py compares their effective source
# sets for Linux, macOS, Windows, Android, and Emscripten.
set(ROLLER_GAME_BASE_SOURCES
    PROJECTS/ROLLER/3d.c
    PROJECTS/ROLLER/building.c
    PROJECTS/ROLLER/car.c
    PROJECTS/ROLLER/carplans.c
    PROJECTS/ROLLER/cdx.c
    PROJECTS/ROLLER/colision.c
    PROJECTS/ROLLER/comms.c
    PROJECTS/ROLLER/control.c
    PROJECTS/ROLLER/date.c
    PROJECTS/ROLLER/drawtrk3.c
    PROJECTS/ROLLER/editor_camera.c
    PROJECTS/ROLLER/editor_api.c
    PROJECTS/ROLLER/editor_legacy_scene.c
    PROJECTS/ROLLER/editor_overlay.c
    PROJECTS/ROLLER/editor_helpers.c
    PROJECTS/ROLLER/editor_test_car.c
    PROJECTS/ROLLER/editor_reference_mesh.c
    PROJECTS/ROLLER/editor_surface.c
    PROJECTS/ROLLER/editor_track_loader.c
    PROJECTS/ROLLER/render_queue_3d.c
    PROJECTS/ROLLER/engines.c
    PROJECTS/ROLLER/frontend.c
    PROJECTS/ROLLER/frontend_config.c
    PROJECTS/ROLLER/frontend_data.c
    PROJECTS/ROLLER/frontend_lobby.c
    PROJECTS/ROLLER/frontend_pause.c
    PROJECTS/ROLLER/frontend_screens.c
    PROJECTS/ROLLER/frontend_select_car.c
    PROJECTS/ROLLER/frontend_select_disk.c
    PROJECTS/ROLLER/frontend_select_players.c
    PROJECTS/ROLLER/frontend_select_track.c
    PROJECTS/ROLLER/frontend_select_type.c
    PROJECTS/ROLLER/frontend_util.c
    PROJECTS/ROLLER/func2.c
    PROJECTS/ROLLER/func3.c
    PROJECTS/ROLLER/function.c
    PROJECTS/ROLLER/graphics.c
    PROJECTS/ROLLER/gpu_parity.c
    PROJECTS/ROLLER/horizon.c
    PROJECTS/ROLLER/loadtrak.c
    PROJECTS/ROLLER/menu_render.c
    PROJECTS/ROLLER/menu_render_software.c
    PROJECTS/ROLLER/game_render.c
    PROJECTS/ROLLER/game_render_software.c
    PROJECTS/ROLLER/scene_render.c
    PROJECTS/ROLLER/scene_render_software.c
    PROJECTS/ROLLER/moving.c
    PROJECTS/ROLLER/network.c
    PROJECTS/ROLLER/plans.c
    PROJECTS/ROLLER/platform_log.c
    PROJECTS/ROLLER/phone_ui.c
    PROJECTS/ROLLER/png_writer.c
    PROJECTS/ROLLER/polyf.c
    PROJECTS/ROLLER/polytex.c
    PROJECTS/ROLLER/replay.c
    PROJECTS/ROLLER/roller_core_error.c
    PROJECTS/ROLLER/roller.c
    PROJECTS/ROLLER/rollercd.c
    PROJECTS/ROLLER/rollerinput.c
    PROJECTS/ROLLER/rollersound.c
    PROJECTS/ROLLER/snapshot.c
    PROJECTS/ROLLER/snapshot_scenes.c
    PROJECTS/ROLLER/sound.c
    PROJECTS/ROLLER/tower.c
    PROJECTS/ROLLER/touch_ui.c
    PROJECTS/ROLLER/transfrm.c
    PROJECTS/ROLLER/userfns.c
    PROJECTS/ROLLER/view.c)

set(ROLLER_GAME_NATIVE_SOURCES
    PROJECTS/ROLLER/debug_overlay.c
    PROJECTS/ROLLER/crashdump.c
    PROJECTS/ROLLER/menu_render_gpu.c
    PROJECTS/ROLLER/crt_filter.c
    PROJECTS/ROLLER/game_render_hardware.c
    PROJECTS/ROLLER/scene_render_gpu.c
    PROJECTS/ROLLER/rollercomms.c)

set(ROLLER_GAME_WEB_SOURCES
    PROJECTS/ROLLER/crashdump_stub.c
    PROJECTS/ROLLER/debug_overlay.c
    PROJECTS/ROLLER/nuklear_sdl_renderer.c
    PROJECTS/ROLLER/present_sdlrenderer.c
    PROJECTS/ROLLER/rollercomms_stub.c
    PROJECTS/ROLLER/web_default_config.c
    PROJECTS/ROLLER/web_gamepad_axis.c
    PROJECTS/ROLLER/roller_web.c)

set(ROLLER_GAME_WINDOWS_SOURCES
    PROJECTS/ROLLER/midi_player.c
    external/rtmidi/RtMidi.cpp
    external/rtmidi/rtmidi_c.cpp)

function(roller_configure_c_target target_name)
    if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
            -Wno-unused-variable
            -Wno-unused-but-set-variable)
    endif()

    set_property(TARGET ${target_name} PROPERTY
        COMPILE_WARNING_AS_ERROR "${ROLLER_WARNINGS_AS_ERRORS}")

    if(NOT WIN32)
        target_compile_definitions(${target_name} PRIVATE
            __int16=int16
            _O_RDONLY=O_RDONLY
            "_O_BINARY=0x200")
    endif()
endfunction()

if(ROLLER_BUILD_GAME OR ROLLER_BUILD_EDITOR_CORE)
    find_package(SDL3 ${ROLLER_SDL3_MIN_VERSION} CONFIG REQUIRED)
    find_package(SDL3_image ${ROLLER_SDL3_IMAGE_MIN_VERSION} CONFIG REQUIRED)
endif()

find_library(ROLLER_MATH_LIBRARY NAMES m)

if(ROLLER_BUILD_EDITOR_CORE)
    add_library(roller-core STATIC ${ROLLER_CORE_SOURCES})
    add_library(ROLLER::core ALIAS roller-core)

    set_target_properties(roller-core PROPERTIES
        OUTPUT_NAME "roller-core"
        PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/PROJECTS/ROLLER/editor_api.h"
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(roller-core
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/PROJECTS/ROLLER>"
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/external/Nuklear-4.13.2")
    target_compile_definitions(roller-core PRIVATE ROLLER_EDITOR_CORE=1)
    target_compile_features(roller-core PUBLIC c_std_11)
    target_link_libraries(roller-core PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image)
    if(ROLLER_MATH_LIBRARY)
        target_link_libraries(roller-core PRIVATE "${ROLLER_MATH_LIBRARY}")
    endif()
    if(WIN32)
        target_link_libraries(roller-core PRIVATE winmm)
    endif()
    roller_configure_c_target(roller-core)

    add_executable(editor-core-link-test tests/editor_core_link_test.c)
    target_link_libraries(editor-core-link-test PRIVATE ROLLER::core)
    roller_configure_c_target(editor-core-link-test)
endif()

if(ROLLER_BUILD_GAME)
    find_package(WildMidi REQUIRED)

    if(UNIX AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBCDIO REQUIRED libcdio libiso9660)
        if(NOT TARGET libcdio::cdio)
            add_library(libcdio::cdio INTERFACE IMPORTED)
            set_target_properties(libcdio::cdio PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBCDIO_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${LIBCDIO_LIBRARIES}")
        endif()
    else()
        find_package(libcdio REQUIRED)
    endif()

    set(ROLLER_GAME_SOURCES ${ROLLER_GAME_BASE_SOURCES})
    if(EMSCRIPTEN)
        list(APPEND ROLLER_GAME_SOURCES ${ROLLER_GAME_WEB_SOURCES})
    else()
        list(APPEND ROLLER_GAME_SOURCES ${ROLLER_GAME_NATIVE_SOURCES})
        if(WIN32)
            enable_language(CXX)
            list(APPEND ROLLER_GAME_SOURCES ${ROLLER_GAME_WINDOWS_SOURCES})
        endif()
    endif()

    if(WIN32)
        set(ROLLER_PLATFORM_SOURCES ROLLER.rc)
    else()
        set(ROLLER_PLATFORM_SOURCES "")
    endif()

    add_executable(roller
        ${ROLLER_PLATFORM_SOURCES}
        ${ROLLER_GAME_SOURCES})

    target_include_directories(roller PRIVATE external/Nuklear-4.13.2)
    target_link_libraries(roller PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        WildMidi::libwildmidi
        libcdio::cdio)
    if(ROLLER_MATH_LIBRARY)
        target_link_libraries(roller PRIVATE "${ROLLER_MATH_LIBRARY}")
    endif()

    if(WIN32)
        target_include_directories(roller PRIVATE external/rtmidi)
        set_source_files_properties(
            external/rtmidi/RtMidi.cpp
            external/rtmidi/rtmidi_c.cpp
            PROPERTIES COMPILE_DEFINITIONS __WINDOWS_MM__)
        target_compile_definitions(roller PRIVATE WILDMIDI_STATIC=1)
        target_link_libraries(roller PRIVATE ws2_32 iphlpapi winmm)
        set_target_properties(roller PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    roller_configure_c_target(roller)

    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY STREQUAL "${CMAKE_BINARY_DIR}")
        add_custom_command(TARGET roller POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/midi"
                "$<TARGET_FILE_DIR:roller>/midi")
    endif()
endif()
