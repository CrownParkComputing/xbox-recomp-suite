cmake_minimum_required(VERSION 3.22)
project(xbe_android_dependencies LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Dependencies.cmake")
include(FetchContent)

# SDL3: shared, so SDLActivity can load libSDL3.so before libmain.so
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
fetchcontent_declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)

fetchcontent_declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
fetchcontent_declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
fetchcontent_declare(vma GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)

# glslang, HLSL front end on: the runtime's HLSL -> SPIR-V (vk_shaders_glslang.cpp). The optimizer is on with it:
# without SPIRV-Tools glslang is documented to generate illegal SPIR-V for some HLSL, and it does, for the shadow
# comparison hook (a texture and a comparison sampler passed to a function and combined there).
set(ENABLE_OPT ON CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
fetchcontent_declare(glslang GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git GIT_TAG 16.4.0 GIT_SHALLOW TRUE)

# SPIRV-Tools and SPIRV-Headers, at the commits glslang 16.4.0 pins in its known_good.json: SPIRV-Tools is built
# from the top so the SPIRV-Tools-opt target glslang looks for exists before glslang's configure runs
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV-Headers_SOURCE_DIR "" CACHE PATH "" FORCE)
fetchcontent_declare(spirv_headers GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
                     GIT_TAG 29981f65241605e08b0ede4cfeb999fe3b723c6a GIT_SHALLOW FALSE)
fetchcontent_declare(spirv_tools GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
                     GIT_TAG b707790a898e44038547df54580022fc1cf89c3d GIT_SHALLOW FALSE)

fetchcontent_makeavailable(SDL3 tomlplusplus imgui vma spirv_headers)
set(SPIRV-Headers_SOURCE_DIR "${spirv_headers_SOURCE_DIR}" CACHE PATH "" FORCE)
fetchcontent_makeavailable(spirv_tools glslang)

