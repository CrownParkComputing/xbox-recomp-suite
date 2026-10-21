cmake_minimum_required(VERSION 3.25)
project(VCop3Native LANGUAGES C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ChihiroPort.cmake")
chihiro_port(VCop3 default.xbe VCOP3_SDL_INPUT
    832b6b786ae44acca64ad10aa82621ce86a8e209866259e78821e3d3c396e83c)
