cmake_minimum_required(VERSION 3.22)
project(xbe_android_port LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
foreach(required XBE_REPO_DIR XBE_SDK_DIR XBE_DEPS_SOURCE XBE_DEPS_BUILD XBE_TITLE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}; use stage_port.py")
    endif()
endforeach()
set(common "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp")
set(gen "${XBE_REPO_DIR}/generated/game")
add_library(SDL3 SHARED IMPORTED)
set_target_properties(SDL3 PROPERTIES IMPORTED_LOCATION "${XBE_DEPS_BUILD}/sdl3-build/libSDL3.so"
    INTERFACE_INCLUDE_DIRECTORIES "${XBE_DEPS_SOURCE}/sdl3-src/include")
file(GLOB runtime CONFIGURE_DEPENDS "${XBE_SDK_DIR}/runtime/*/*.cpp")
list(FILTER runtime EXCLUDE REGEX "/app/(main|splash|handover|system_info)\\.cpp$|/gpu/(d3d12|vk_shaders)\\.cpp$|/audio/xaudio2\\.cpp$")
file(GLOB host CONFIGURE_DEPENDS "${XBE_REPO_DIR}/src/*/*.cpp")
list(FILTER host EXCLUDE REGEX "/src/game/")
file(GLOB game CONFIGURE_DEPENDS "${XBE_REPO_DIR}/src/game/*.cpp")
# The desktop movie replacement uses FFmpeg. Keep the original guest movie
# routines in these builds, as in the existing Burnout Android port.
list(FILTER game EXCLUDE REGEX "native_(movie|fmv)\\.cpp$")
if(XBE_TITLE STREQUAL "BLACK")
    list(FILTER runtime EXCLUDE REGEX "/(xdk/dsound|xdk/d3d8|gpu/render_thread|app/game_files)\\.cpp$|/gpu/vulkan[a-z_]*\\.cpp$|/audio/sdl_audio\\.cpp$")
    list(FILTER host EXCLUDE REGEX "/d3d/(d3d12|render_thread)\\.cpp$|/audio/xaudio2\\.cpp$|_win32\\.cpp$|/common/(cursor|monitor|game_files)\\.cpp$")
    list(FILTER host EXCLUDE REGEX "/vk/vk_(backend|present)\\.cpp$")
    list(APPEND host "${XBE_BLACK_FILES}" "${XBE_BLACK_RENDER}" "${XBE_BLACK_BACKEND}" "${XBE_BLACK_PRESENT}")
    file(READ "${XBE_REPO_DIR}/src/common/europe.patch" europe_hex HEX)
    string(LENGTH "${europe_hex}" europe_len)
    math(EXPR europe_size "${europe_len} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," europe_bytes "${europe_hex}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/europe_patch.cpp" CONTENT "#include <cstddef>\n#include <cstdint>\nnamespace app { extern const uint8_t kEuropePatch[] = {${europe_bytes}}; extern const size_t kEuropePatchSize = ${europe_size}; }\n")
    list(APPEND host "${CMAKE_CURRENT_BINARY_DIR}/europe_patch.cpp")
endif()
file(GLOB generated CONFIGURE_DEPENDS "${gen}/x86_func_mapping.cpp" "${gen}/x86_recomp.*.cpp")
file(SHA1 "${XBE_REPO_DIR}/assets/default.xbe" sha)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/image_hash.cpp" CONTENT "extern const char kImageSha1[] = \"${sha}\";\n")
set(imgui "${XBE_DEPS_SOURCE}/imgui-src")
add_library(main SHARED ${runtime} ${host} ${game} ${generated}
    "${imgui}/imgui.cpp" "${imgui}/imgui_draw.cpp" "${imgui}/imgui_tables.cpp" "${imgui}/imgui_widgets.cpp"
    "${imgui}/backends/imgui_impl_sdl3.cpp" "${imgui}/backends/imgui_impl_vulkan.cpp"
    "${common}/vk_shaders_glslang.cpp" "${common}/document_tree.cpp" "${CMAKE_CURRENT_BINARY_DIR}/image_hash.cpp")
target_include_directories(main BEFORE PRIVATE "${XBE_PORT_INCLUDE}")
target_include_directories(main PRIVATE "${XBE_SDK_DIR}/runtime" "${XBE_SDK_DIR}/runtime/platform/compat"
    "${XBE_REPO_DIR}/src" "${gen}")
target_include_directories(main SYSTEM PRIVATE "${imgui}" "${XBE_DEPS_SOURCE}/vma-src/include"
    "${XBE_DEPS_SOURCE}/tomlplusplus-src/include" "${XBE_DEPS_SOURCE}/glslang-src")
target_compile_definitions(main PRIVATE _stricmp=strcasecmp _strnicmp=strncasecmp ENABLE_HLSL=1
    XBE_LOG_VERBOSE_DEFAULT=1 XBE_ANDROID_LOG_TAG="${XBE_TITLE}")
target_compile_options(main PRIVATE -fno-strict-aliasing -Wno-parentheses-equality)
set_source_files_properties(${generated} PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(main PRIVATE SDL3
    "${XBE_DEPS_BUILD}/glslang-build/glslang/libglslang.a"
    "${XBE_DEPS_BUILD}/glslang-build/SPIRV/libSPIRV.a"
    "${XBE_DEPS_BUILD}/glslang-build/glslang/libglslang-default-resource-limits.a"
    "${XBE_DEPS_BUILD}/spirv_tools-build/source/opt/libSPIRV-Tools-opt.a"
    "${XBE_DEPS_BUILD}/spirv_tools-build/source/libSPIRV-Tools.a"
    vulkan log android)
target_link_options(main PRIVATE -Wl,-z,max-page-size=16384)
