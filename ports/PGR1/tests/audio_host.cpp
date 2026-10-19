// Link the Linux runtime for audio checks without creating a window or GPU.
#include <cstdlib>
#include "app/app.h"

namespace app {
std::vector<const char*> VulkanInstanceExtensions() { return {}; }
bool CreateVulkanSurface(void*, uint64_t*) { return false; }
void WindowPixels(uint32_t& w, uint32_t& h) { w = 640; h = 480; }
void UiNewFrame() {}
bool GraphicsMenuOpen() { return false; }
void DrawGraphicsMenu(double) {}
bool Unfocused() { return false; }
void Quit() { std::exit(0); }
void ShowError(const wchar_t*) {}
bool AskYesNo(const wchar_t*) { return false; }
}
