# Linux + Vulkan port

Branch `linux-vulkan` here and in the SDK checkout beside it (`../XboxXBE-Recomp`, branch `linux-vulkan`).
The Windows build keeps its files; Linux builds its own set, chosen in CMake.

## What already ran on Linux

The SDK's headless harness proves the recompiled x86, the kernel, the XDK HLE (D3D8 state, DirectSound mixer,
XAPI, XNet) and SDL3 pads all build and run on Linux. The port is everything the harness stubs out:

| Windows piece                         | Linux replacement                                   | Where |
|---------------------------------------|-----------------------------------------------------|-------|
| Win32 window, message loop, crash dump | SDL3 window and event pump, signal handler          | SDK `runtime/app/main_sdl.cpp` |
| Win32 messages to `GameInfo::message`  | SDL events translated to the same WM_ codes         | SDK `runtime/app/main_sdl.cpp` |
| Win32 names portable code uses         | constants only                                      | SDK `runtime/platform/compat/windows.h` |
| Desktop mode, message boxes, system log | SDL3 / procfs                                      | SDK `app/*_linux.cpp` |
| D3D12 renderer (`src/d3d/d3d12.cpp`)   | Vulkan renderer, same features                       | `src/vk/` |
| D3DCompile                              | glslang HLSL front end to SPIR-V                    | `src/vk/vk_shaders.cpp` |
| DXGI swap chain, HDR10                  | VkSwapchain on an SDL surface, HDR10 ST2084         | `src/vk/vk_context.cpp` |
| ImGui Win32 + DX12 backends             | ImGui SDL3 + Vulkan backends                        | `src/vk/vk_ui.cpp` |
| XAudio2                                 | SDL3 audio stream                                   | `src/audio/sdl_audio.cpp` |
| Raw input, GetAsyncKeyState, ClipCursor | SDL relative mouse, keyboard state, mouse grab     | `src/common/platform_input_sdl.cpp` |
| WIC cursor                              | SDL PNG/BMP loader + colour cursor                  | `src/common/cursor_sdl.cpp` |
| Monitor enumeration                     | SDL displays                                        | `src/common/monitor_sdl.cpp` |
| `EUROPE` RCDATA resource                | generated source holding the bytes                  | CMake |

## Renderer mapping (D3D12 to Vulkan)

- Root signature (b0, b1, t0-t3, s0-s3) is one descriptor set layout pushed per draw (push descriptors).
  HLSL registers map to bindings 0-1 (constant buffers), 2-5 (textures), 6-9 (samplers).
- Clip space: negative-height viewports keep D3D's y-up and winding.
- Resource states become image layouts tracked per image, with a barrier on each change, as `Transition` did.
- Render target binds open a dynamic rendering scope; any copy, barrier or blit closes it first.
- D24S8 is used where the device has it, else D32S8 (AMD); depth bias is scaled for the float format.
- Null texture views are small dummy images of each kind.

## Tests

- `tests/` unit tests: shader translation through glslang for the generated and the renderer's own HLSL,
  state translation tables, key mapping, speaker fold matrices.
- Integration: the Vulkan renderer offscreen against the SDK's CPU reference renderer, same draws,
  pixels compared after a read-back through guest memory.
- End to end needs `assets/default.xbe` and the game files.
