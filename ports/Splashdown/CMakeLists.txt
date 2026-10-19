# Game Title (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prepared by the suite, with Splashdown's tested overlay isolated from other titles.
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
if(NOT EXISTS "${SDK}/cmake/XbeRecomp.cmake")
    message(FATAL_ERROR "Run python3 tools/prepare.py Splashdown from the suite root first")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Dependencies.cmake")
include(${SDK}/cmake/XbeRecomp.cmake)

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")

# partitions as xbe_symbols wrote them into config/game.toml
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
# no config yet: build.bat writes it with xbe_symbols, the change reconfigures
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# target name = GAME_EXE in build.bat and build.sh, xbe = GAME_XBE; without the xbe only the tools and tests build
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe)
    add_xbe_image(Splashdown default.xbe game.toml ${GAME_PARTITIONS} game)
else()
    message(STATUS "assets/default.xbe not found: building the tools and tests only")
endif()

# Linux: the SDK's tests on this machine's GPU (ctest)
if(NOT WIN32)
    include(CTest)
    if(BUILD_TESTING)
        add_xbe_tests(game_tests)
        option(SPLASHDOWN_ANDROID_SHADER_TESTS "Test the Android shader adapter on desktop Vulkan" OFF)
        if(SPLASHDOWN_ANDROID_SHADER_TESTS)
            find_package(glslang CONFIG REQUIRED)
            add_xbe_tests(android_shader_tests)
            get_target_property(android_test_sources android_shader_tests SOURCES)
            list(FILTER android_test_sources EXCLUDE REGEX "/gpu/vk_shaders\\.cpp$")
            set_property(TARGET android_shader_tests PROPERTY SOURCES "${android_test_sources}")
            target_sources(android_shader_tests PRIVATE ../../android/app/src/main/cpp/vk_shaders_glslang.cpp)
            target_compile_definitions(android_shader_tests PRIVATE ENABLE_HLSL=1)
            target_link_libraries(android_shader_tests PRIVATE glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
        endif()
    endif()
endif()
