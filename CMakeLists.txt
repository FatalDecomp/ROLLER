cmake_minimum_required(VERSION 3.0...4.0)
project(roller C)

set(CMAKE_COMPILE_WARNING_AS_ERROR "OFF" CACHE BOOL "Treat warnings as errors")

if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    add_compile_options(-Wall)
    add_compile_options(-Wextra)
    add_compile_options(-Wno-unused-parameter)
    add_compile_options(-Wno-unused-variable)
    add_compile_options(-Wno-unused-but-set-variable)
endif()

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(WildMidi REQUIRED)
find_package(libcdio REQUIRED)

if(WIN32)
    set(PLATFORM_SOURCES ROLLER.rc)
else()
    set(PLATFORM_SOURCES "")
endif()

add_executable(roller
    ${PLATFORM_SOURCES}
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
    PROJECTS/ROLLER/engines.c
    PROJECTS/ROLLER/frontend_config.c
    PROJECTS/ROLLER/frontend_data.c
    PROJECTS/ROLLER/frontend_network.c
    PROJECTS/ROLLER/frontend_screens.c
    PROJECTS/ROLLER/frontend_select_car.c
    PROJECTS/ROLLER/frontend_select_disk.c
    PROJECTS/ROLLER/frontend_select_players.c
    PROJECTS/ROLLER/frontend_select_track.c
    PROJECTS/ROLLER/frontend_select_type.c
    PROJECTS/ROLLER/frontend_util.c
    PROJECTS/ROLLER/menu_render.c
    PROJECTS/ROLLER/menu_render_gpu.c
    PROJECTS/ROLLER/menu_render_software.c
    PROJECTS/ROLLER/func2.c
    PROJECTS/ROLLER/func3.c
    PROJECTS/ROLLER/function.c
    PROJECTS/ROLLER/graphics.c
    PROJECTS/ROLLER/horizon.c
    PROJECTS/ROLLER/loadtrak.c
    PROJECTS/ROLLER/mouse.c
    PROJECTS/ROLLER/moving.c
    PROJECTS/ROLLER/network.c
    PROJECTS/ROLLER/plans.c
    PROJECTS/ROLLER/polyf.c
    PROJECTS/ROLLER/polytex.c
    PROJECTS/ROLLER/replay.c
    PROJECTS/ROLLER/roller.c
    PROJECTS/ROLLER/rollercomms.c
    PROJECTS/ROLLER/sound.c
    PROJECTS/ROLLER/svgacpy.c
    PROJECTS/ROLLER/tower.c
    PROJECTS/ROLLER/transfrm.c
    PROJECTS/ROLLER/userfns.c
    PROJECTS/ROLLER/view.c
)
target_link_libraries(roller PRIVATE SDL3::SDL3)
target_link_libraries(roller PRIVATE SDL3_image::SDL3_image)
target_link_libraries(roller PRIVATE WildMidi::libwildmidi)
target_link_libraries(roller PRIVATE libcdio::cdio)

if(WIN32)
    target_compile_definitions(roller PRIVATE WILDMIDI_STATIC=1)
    target_link_libraries(roller PRIVATE ws2_32)
    set_target_properties(roller PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    target_compile_definitions(roller PRIVATE
        __int16=int16
        _O_RDONLY=O_RDONLY
        "_O_BINARY=0x200"
    )
endif()
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY STREQUAL "${CMAKE_BINARY_DIR}")
    add_custom_command(TARGET roller POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/midi" "$<TARGET_FILE_DIR:roller>/midi"
    )
endif()
find_library(MATH_LIBRARY NAMES "m")
if(MATH_LIBRARY)
    target_link_libraries(roller PRIVATE "m")
endif()
