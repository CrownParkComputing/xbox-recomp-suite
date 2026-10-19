// the host and game pieces the renderer reads, as a test without a window has them
#include <filesystem>
#include <mutex>

#include "app/app.h"

namespace kernel {
uint8_t* g_base = nullptr;  // set to mem::Base() once guest memory is up (test_render.cpp)
}  // namespace kernel

namespace app {

Settings& Get() {
    static Settings s;
    return s;
}
Mode Desktop() { return {640, 480}; }
Mode Output() { return {640, 480}; }
bool Widescreen() { return false; }
float RenderScale(uint32_t) { return 1.0f; }
std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}
std::filesystem::path ExeDir() { return std::filesystem::current_path(); }
std::vector<const char*> VulkanInstanceExtensions() { return {}; }
bool CreateVulkanSurface(void*, uint64_t*) { return false; }
void WindowPixels(uint32_t& w, uint32_t& h) {
    w = 640;
    h = 480;
}
void UiNewFrame() {}
bool GraphicsMenuOpen() { return false; }
void DrawGraphicsMenu(double) {}
const GameInfo kGame = {.title = L"xbe_tests", .short_title = L"tests", .name = L"tests", .log = "tests", .settings = "tests.toml"};

}  // namespace app
