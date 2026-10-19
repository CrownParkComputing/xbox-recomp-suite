# Black (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(black LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prepared by the suite, with Splashdown's tested overlay isolated from other titles.
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
if(NOT EXISTS "${SDK}/cmake/XbeRecomp.cmake")
    message(FATAL_ERROR "Run python3 tools/prepare.py BLACK from the suite root first")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Dependencies.cmake")
include(${SDK}/cmake/XbeRecomp.cmake)
# src/sound, src/d3d, src/audio and src/common/game_files.cpp replace the SDK ones
list(FILTER XBE_RUNTIME_SOURCES EXCLUDE REGEX "/(xdk/dsound|xdk/d3d8|gpu/d3d12|gpu/render_thread|audio/xaudio2|app/game_files)\\.cpp$")
# and on Linux the SDK's Vulkan renderer and SDL audio: src/vk and src/audio are this game's (the Vulkan core stays)
list(FILTER XBE_RUNTIME_SOURCES EXCLUDE REGEX "/gpu/vulkan[a-z_]*\\.cpp$|/audio/sdl_audio\\.cpp$")

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")
# Windows: D3D12, XAudio2 and Win32; Linux: Vulkan (src/vk), SDL3 audio and the SDL3 window
if(WIN32)
    list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/vk/|_sdl\\.cpp$|/audio/sdl_audio\\.cpp$")
else()
    list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/d3d/d3d12\\.cpp$|/audio/xaudio2\\.cpp$|_win32\\.cpp$|/common/(cursor|monitor)\\.cpp$")
    include(cmake/linux.cmake)
endif()

# partitions as config/game.toml sets them
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# the game's own code needs its xbe: without assets/default.xbe only the tools and the tests build
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe)
    add_xbe_image(Black default.xbe game.toml ${GAME_PARTITIONS} game)
    set_target_properties(Black PROPERTIES OUTPUT_NAME BLACK)
    if(WIN32)
        # build_info.h for the version resource, rewritten only when the commit changes
        find_package(Git QUIET)
        set(BUILD_INFO ${CMAKE_CURRENT_BINARY_DIR}/build_info.h)
        add_custom_target(build_info
            COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${BUILD_INFO} -DGIT=${GIT_EXECUTABLE}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_info.cmake
            BYPRODUCTS ${BUILD_INFO}
            VERBATIM)
        # version.rc also embeds src/common/europe.patch
        set_source_files_properties(src/common/version.rc PROPERTIES
            INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR};${CMAKE_CURRENT_SOURCE_DIR}/src/common"
            OBJECT_DEPENDS "${BUILD_INFO};${CMAKE_CURRENT_SOURCE_DIR}/src/common/europe.patch")
        target_sources(Black PRIVATE src/common/version.rc)
        add_dependencies(Black build_info)
    else()
        black_linux_image(Black)
    endif()
else()
    message(STATUS "assets/default.xbe not found: building the tools and tests only")
endif()

if(NOT WIN32)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
