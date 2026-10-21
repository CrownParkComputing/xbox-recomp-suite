cmake_minimum_required(VERSION 3.25)
project(OutRun2Native LANGUAGES C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ChihiroPort.cmake")
chihiro_port(OutRun2 outrun2.xbe OUTRUN2_SDL_INPUT
    d98a7efc0ee4ba48b6e938743fa309a9fc774c05713ef735e67d1b17b9bbbd1b)
