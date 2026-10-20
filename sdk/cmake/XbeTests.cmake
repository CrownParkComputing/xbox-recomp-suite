# add_xbe_tests(target [SOURCES extra...] [RENDERER sources...]): the SDK's tests (tests/) as one executable, run by
# ctest: state and shader translation, keys, disc images, and the renderer against the CPU reference renderer on the
# machine's GPU. RENDERER replaces the SDK's Vulkan renderer with a game's own, with a test hook (render_backend.h).
# Linux only.
include_guard(GLOBAL)

function(add_xbe_tests target)
    cmake_parse_arguments(T "" "" "SOURCES;RENDERER" ${ARGN})
    set(RT ${XBE_SDK_DIR}/runtime)
    set(TD ${XBE_SDK_DIR}/tests)
    if(NOT T_RENDERER)
        set(T_RENDERER ${RT}/gpu/vulkan.cpp ${RT}/gpu/vulkan_readback.cpp ${RT}/gpu/vulkan_present.cpp ${TD}/sdk_renderer.cpp)
    endif()
    add_executable(${target}
        ${TD}/main.cpp ${TD}/stubs.cpp ${TD}/test_formats.cpp ${TD}/test_shaders.cpp ${TD}/test_keys.cpp
        ${TD}/test_render.cpp ${TD}/test_iso.cpp ${TD}/test_keyboard.cpp ${TD}/test_nv2a_texture.cpp
        ${TD}/irql.cpp ${TD}/test_audio_limiter.cpp ${TD}/test_host_pause.cpp
        ${RT}/kernel/disc.cpp ${RT}/gpu/texture.cpp ${RT}/gpu/vertex.cpp ${RT}/gpu/shaders.cpp ${RT}/kernel/memory.cpp
        ${RT}/core/log.cpp ${RT}/app/sdl_keys.cpp ${RT}/core/keyboard_pad.cpp ${RT}/gpu/nv2a_texture.cpp ${RT}/core/input.cpp ${XBE_SDK_DIR}/harness/reference_renderer.cpp
        ${RT}/gpu/vk_context.cpp ${RT}/gpu/vk_formats.cpp ${RT}/gpu/vk_shaders.cpp ${RT}/gpu/vk_swapchain.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        ${T_RENDERER} ${T_SOURCES})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src ${RT} ${RT}/platform/compat
        ${XBE_SDK_DIR}/harness ${TD})
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR} ${XBE_DXC_DIR}/include)
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${target} PRIVATE XBE_ISO_EXTRACT="$<TARGET_FILE:iso_extract>"
        _stricmp=strcasecmp _strnicmp=strncasecmp)
    add_dependencies(${target} iso_extract)
    target_link_libraries(${target} PRIVATE SDL3::SDL3 tomlplusplus::tomlplusplus Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator ${CMAKE_DL_LIBS} pthread)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XBE_DXC_DIR}/lib/libdxcompiler.so $<TARGET_FILE_DIR:${target}>)
    add_test(NAME ${target} COMMAND ${target})
endfunction()
