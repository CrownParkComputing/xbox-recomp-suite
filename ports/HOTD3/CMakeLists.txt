cmake_minimum_required(VERSION 3.25)
project(HOTD3Native LANGUAGES C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ChihiroPort.cmake")
chihiro_port(HOTD3 hod3xb.xbe HOTD3_SDL_INPUT
    89a0e7778002205c114a91bc1797291344b2afeea8b5c7d5085cce7e47dc71de)
