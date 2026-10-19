// Test-only replacement for runtime/xdk/d3d8.cpp: exercise the native render queue
// while retaining the headless harness's CPU renderer and scripted controller.
#undef XBE_RENDER_THREAD
#define XBE_RENDER_THREAD 1
#include "../sdk/runtime/xdk/d3d8.cpp"
