# One CMake project, two targets:
#   - Linux:     native g++, links libsimulant.so + SDL2/OpenAL/GL
#   - Dreamcast: sh-elf-g++ via Simulant's toolchain file (-DCMAKE_TOOLCHAIN_FILE=.../Dreamcast.cmake,
#                which sets PLATFORM_DREAMCAST), links the static libsimulant.a + KallistiOS
# The Makefile passes the right options; you normally don't run cmake by hand.
cmake_minimum_required(VERSION 3.16)
project(DreamGame CXX C)

set(SIMULANT_SOURCE_DIR "" CACHE PATH "Simulant source checkout (<kit>/sdk/simulant)")
set(SIMULANT_BUILD_DIR "" CACHE PATH "Simulant build tree (<kit>/sdk/build/simulant-<target>-<type>)")
if(NOT SIMULANT_SOURCE_DIR OR NOT SIMULANT_BUILD_DIR)
    message(FATAL_ERROR "Set SIMULANT_SOURCE_DIR and SIMULANT_BUILD_DIR (the Makefile does this)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Every .cpp/.c under sources/ is part of the game
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/sources/*.cpp
    ${CMAKE_SOURCE_DIR}/sources/*.c)

add_executable(game ${GAME_SOURCES})
target_include_directories(game PRIVATE ${SIMULANT_SOURCE_DIR})   # for <simulant/simulant.h>
target_compile_options(game PRIVATE -Wall)

if(PLATFORM_DREAMCAST)
    # Static engine lib (GLdc + ALdc are compiled into it) + KallistiOS. The group lets the
    # linker resolve the circular references between them. libpthread is a KOS add-on.
    target_link_libraries(game PRIVATE
        -Wl,--start-group
        ${SIMULANT_BUILD_DIR}/simulant/libsimulant.a
        stdc++ pthread kallisti gcc c m
        -Wl,--end-group)
    # SH-4 fast paths: hardware sin/cos (fsca) and 1/sqrt (fsrra)
    target_compile_options(game PRIVATE $<$<CONFIG:Release>:-mfsrra -mfsca -ffast-math -O3>)

    # The whole .elf is loaded into the 16MB of RAM: keep debug info in a side file
    if(CMAKE_BUILD_TYPE MATCHES Debug)
        add_custom_command(TARGET game POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:game> $<TARGET_FILE:game>.debug
            COMMAND ${CMAKE_OBJCOPY} -S $<TARGET_FILE:game>
            COMMENT "Splitting debug info out of game.elf")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    find_package(OpenAL REQUIRED)
    set(OpenGL_GL_PREFERENCE "LEGACY")
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)

    target_include_directories(game PRIVATE ${SDL2_INCLUDE_DIRS} ${OPENAL_INCLUDE_DIR})
    target_link_libraries(game PRIVATE
        ${SIMULANT_BUILD_DIR}/simulant/libsimulant.so
        ${SDL2_LINK_LIBRARIES} ${OPENAL_LIBRARY} OpenGL::GL Threads::Threads m)
    # Find libsimulant.so at runtime without installing it
    set_target_properties(game PROPERTIES BUILD_RPATH "${SIMULANT_BUILD_DIR}/simulant")
endif()

# Runtime assets next to the binary (and, for Dreamcast, onto the disc):
#   build/<target>/assets/            <- this project's assets/
#   build/<target>/assets/simulant/   <- Simulant's core assets (materials, fonts, ...)
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets/simulant
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SIMULANT_SOURCE_DIR}/assets/fonts     ${CMAKE_BINARY_DIR}/assets/simulant/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SIMULANT_SOURCE_DIR}/assets/materials ${CMAKE_BINARY_DIR}/assets/simulant/materials
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SIMULANT_SOURCE_DIR}/assets/textures  ${CMAKE_BINARY_DIR}/assets/simulant/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SIMULANT_SOURCE_DIR}/assets/particles ${CMAKE_BINARY_DIR}/assets/simulant/particles
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SIMULANT_SOURCE_DIR}/assets/other     ${CMAKE_BINARY_DIR}/assets/simulant/other
    COMMENT "Copying assets")
