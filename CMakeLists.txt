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

add_executable(roller
    ROLLER.rc
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
    PROJECTS/ROLLER/frontend.c
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
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY STREQUAL "${CMAKE_BINARY_DIR}")
    add_custom_command(TARGET roller POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/midi" "$<TARGET_FILE_DIR:roller>/midi"
    )
endif()
find_library(MATH_LIBRARY NAMES "m")
if(MATH_LIBRARY)
    target_link_libraries(roller PRIVATE "m")
endif()
