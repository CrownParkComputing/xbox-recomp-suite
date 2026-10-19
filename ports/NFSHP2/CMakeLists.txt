# Need for Speed: Hot Pursuit 2 (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(nfshp2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prepared by the suite, with Splashdown's tested overlay isolated from other titles.
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
if(NOT EXISTS "${SDK}/cmake/XbeRecomp.cmake")
    message(FATAL_ERROR "Run python3 tools/prepare.py NFSHP2 from the suite root first")
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
    add_xbe_image(NFSHP2 default.xbe game.toml ${GAME_PARTITIONS} game)
else()
    message(STATUS "assets/default.xbe not found: building the tools and tests only")
endif()

# Linux: the SDK's tests on this machine's GPU (ctest)
if(NOT WIN32)
    include(CTest)
    if(BUILD_TESTING)
        add_xbe_tests(nfshp2_tests)
    endif()
endif()
