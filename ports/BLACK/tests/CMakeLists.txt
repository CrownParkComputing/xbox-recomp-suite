# The SDK's test suite (add_xbe_tests) twice: on BLACK's own Vulkan renderer with its extra cases, and on the SDK's
# renderer, which the SDK's other games draw with; then the window test
set(RT ${SDK}/runtime)
add_xbe_tests(black_tests
    SOURCES test_audio.cpp test_passes.cpp ${CMAKE_SOURCE_DIR}/src/audio/fold.cpp
    RENDERER black_renderer.cpp ${CMAKE_SOURCE_DIR}/src/vk/vk_backend.cpp ${CMAKE_SOURCE_DIR}/src/vk/vk_occlusion.cpp
             ${CMAKE_SOURCE_DIR}/src/vk/vk_present.cpp ${CMAKE_SOURCE_DIR}/src/vk/vk_readback.cpp)
add_xbe_tests(sdk_tests)

# black_window_test: the SDL3 window and the Vulkan renderer presenting into it, on the session's display (skipped
# without one)
add_executable(black_window_test
    window_test.cpp
    ${RT}/app/window_sdl.cpp
    ${RT}/app/settings.cpp
    ${RT}/app/graphics_menu.cpp
    ${RT}/app/sdl_keys.cpp
    ${RT}/core/input.cpp
    ${RT}/core/keyboard_pad.cpp
    ${RT}/gpu/texture.cpp
    ${RT}/gpu/vertex.cpp
    ${RT}/gpu/shaders.cpp
    ${RT}/kernel/memory.cpp
    ${RT}/core/log.cpp
    ${CMAKE_SOURCE_DIR}/src/vk/vk_backend.cpp
    ${RT}/gpu/vk_context.cpp
    ${CMAKE_SOURCE_DIR}/src/vk/vk_factory.cpp
    ${RT}/gpu/vk_formats.cpp
    ${CMAKE_SOURCE_DIR}/src/vk/vk_occlusion.cpp
    ${CMAKE_SOURCE_DIR}/src/vk/vk_present.cpp
    ${CMAKE_SOURCE_DIR}/src/vk/vk_readback.cpp
    ${RT}/gpu/vk_shaders.cpp
    ${RT}/gpu/vk_swapchain.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(black_window_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${RT} ${RT}/platform/compat)
target_include_directories(black_window_test SYSTEM PRIVATE ${imgui_SOURCE_DIR} ${XBE_DXC_DIR}/include)
target_compile_options(black_window_test PRIVATE -fno-strict-aliasing)
target_compile_definitions(black_window_test PRIVATE _stricmp=strcasecmp _strnicmp=strncasecmp)
target_link_libraries(black_window_test PRIVATE tomlplusplus::tomlplusplus SDL3::SDL3 Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator ${CMAKE_DL_LIBS} pthread)
add_custom_command(TARGET black_window_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XBE_DXC_DIR}/lib/libdxcompiler.so $<TARGET_FILE_DIR:black_window_test>)
add_test(NAME black_window_test COMMAND black_window_test)
set_tests_properties(black_window_test PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "XBE_WINDOW_TEST_FRAMES=180")
