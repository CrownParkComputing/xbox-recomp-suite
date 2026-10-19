// The Linux host's window with the Vulkan renderer presenting into it, as the game runs them: the SDL3 window and its
// event loop on the main thread, the renderer on its own, frames presented through the swap chain with the settings
// panel on top. Phases: SDR, SDR with SMAA, HDR10 where the display offers it (the swap chain made again), then 4x MSAA
// at a 150% render scale (the targets made again).
// Exit 0 when every frame presented; 77 (skipped) without a display. XBE_WINDOW_TEST_FRAMES sets the frame count.
#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>

#include "app/app.h"
#include "app/window_sdl.h"
#include "core/keyboard_pad.h"
#include "common/options.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace kernel {
uint8_t* g_base = nullptr;
}  // namespace kernel

namespace {

std::atomic<int> g_frame{0};
std::atomic<int> g_phase{0};

void Panel(double fps) {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::Begin("Vulkan window test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    static const char* kPhase[] = {"SDR", "SDR, SMAA", "HDR10 if offered, SMAA", "HDR10 if offered, 4x MSAA, 150% scale"};
    ImGui::Text("frame %d, %.0f fps", g_frame.load(), fps);
    ImGui::Text("phase: %s", kPhase[g_phase.load()]);
    ImGui::End();
}

uint32_t Contiguous(uint32_t bytes) { return mem::AllocContiguous(bytes, 0, mem::kPhysicalSize - 1, 0x1000); }

float Bits(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

}  // namespace

namespace app {
const GameInfo kGame = {.title = L"Black window test", .short_title = L"window test", .name = L"window_test",
                        .log = "window_test", .settings = "window_test.toml", .ui = Panel};
}  // namespace app

namespace game {
Options& Opt() {
    static Options o;
    return o;
}
}  // namespace game

int main(int argc, char** argv) {
    if (!std::getenv("WAYLAND_DISPLAY") && !std::getenv("DISPLAY")) {
        std::printf("no display: skipped\n");
        return 77;
    }
    int frames = std::getenv("XBE_WINDOW_TEST_FRAMES") ? std::atoi(std::getenv("XBE_WINDOW_TEST_FRAMES")) : 240;
    app::SetArguments(argc, argv);
    app::Settings& s = app::Get();
    s.display_mode = app::DisplayMode::Windowed;
    s.width = 1280;
    s.height = 720;
    game::Opt().hdr_output = false;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) || !app::OpenWindow("Black window test")) {
        std::printf("FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(app::Window()));
    input::Init();
    if (!mem::Init()) return 1;
    kernel::g_base = mem::Base();

    // the console's front buffer: 640x480 linear ARGB in contiguous memory, its header in system memory
    constexpr uint32_t kW = 640, kH = 480, kPitch = kW * 4;
    uint32_t data = Contiguous(kPitch * kH), header = mem::AllocSystem(20);
    kernel::W32(header, 0x50001);
    kernel::W32(header + 4, mem::PhysicalFromVirtual(data));
    kernel::W32(header + 12, 0x12 << 8);
    kernel::W32(header + 16, (kW - 1) | (kH - 1) << 12 | (kPitch / 64 - 1) << 24);
    gpu::Surface front{header, mem::PhysicalFromVirtual(data), 0x12, 0};
    uint32_t vb = mem::AllocSystem(16), verts = Contiguous(3 * 20);
    kernel::W32(vb, 1);
    kernel::W32(vb + 4, mem::PhysicalFromVirtual(verts));

    // key events through the window's event loop reach the keyboard controller, and losing the focus lets them go
    {
        SDL_WindowID id = SDL_GetWindowID(static_cast<SDL_Window*>(app::Window()));
        auto push = [&](SDL_EventType type, SDL_Scancode sc = SDL_SCANCODE_UNKNOWN) {
            SDL_Event e{};
            e.type = type;
            if (type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP) {
                e.key.windowID = id;
                e.key.scancode = sc;
                e.key.down = type == SDL_EVENT_KEY_DOWN;
            } else {
                e.window.windowID = id;
            }
            SDL_PushEvent(&e);
            app::PumpEvents(0);
        };
        push(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Z);
        push(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LEFT);
        input::Pad held = input::keyboard::Held();
        std::this_thread::sleep_for(std::chrono::milliseconds(70));  // held longer than the shortest press
        push(SDL_EVENT_KEY_UP, SDL_SCANCODE_Z);
        input::Pad after_up = input::keyboard::Held();
        push(SDL_EVENT_WINDOW_FOCUS_LOST);
        input::Pad after_focus = input::keyboard::Held();
        push(SDL_EVENT_WINDOW_FOCUS_GAINED);
        if (held.analog[0] != 0xFF || held.lx != -32768 || after_up.analog[0] || after_up.lx != -32768 ||
            after_focus.lx) {
            std::printf("FAIL: keyboard: held A %d x %d, after Z up A %d x %d, after focus loss x %d\n", held.analog[0],
                        held.lx, after_up.analog[0], after_up.lx, after_focus.lx);
            return 1;
        }
        std::printf("ok: keyboard events reach the controller\n");
    }

    std::atomic<int> result{-1};
    std::thread render([&] {
        auto backend = gpu::CreateBackend();
        if (!backend->Init(kW, kH)) {
            result = 2;
            app::Quit();
            return;
        }
        gpu::State st{};
        st.rt = front;
        st.viewport = {0, 0, kW, kH, 0, 1};
        st.simple[gpu::kMColorWriteEnable >> 2] = 0x01010101;
        st.zscale = 16777215.0f;
        st.tss[0][gpu::kTssColorOp] = 2;
        st.tss[0][gpu::kTssAlphaOp] = 2;
        st.tss[1][gpu::kTssColorOp] = 1;
        st.vs_handle = 0x44;  // XYZRHW | DIFFUSE
        st.streams[0] = {vb, 20};
        for (int f = 0; f < frames; f++) {
            g_frame = f;
            int phase = f * 4 / frames;
            if (phase != g_phase || f == 0) {
                g_phase = phase;
                game::Opt().smaa = phase >= 1;
                game::Opt().hdr_output = phase >= 2;
                if (phase == 3) {
                    game::Opt().msaa = 4;
                    app::Get().render_percent = 150;
                }
            }
            // a triangle turning about the centre, its corners red, green and blue
            float* v = static_cast<float*>(kernel::HostPtr(verts));
            const uint32_t colour[3] = {0xFFFF2020, 0xFF20FF20, 0xFF2020FF};
            for (int i = 0; i < 3; i++) {
                float a = f * 0.03f + i * 2.0944f;
                float p[5] = {kW / 2 + std::cos(a) * 200, kH / 2 + std::sin(a) * 200, 0.5f, 1, Bits(colour[i])};
                std::memcpy(v + i * 5, p, sizeof(p));
            }
            backend->Clear(st, 0, nullptr, gpu::kClearTarget, 0xFF102030, 1, 0);
            backend->Draw(st, 5, 0, 3, nullptr);
            backend->Present(front);
        }
        result = 0;
        app::Quit();
    });
    while (app::PumpEvents(16)) {
    }
    render.join();
    logx::Flush();
    std::printf("%s: %d frames presented\n", result == 0 ? "ok" : "FAIL", result == 0 ? frames : g_frame.load());
    return result == 0 ? 0 : 1;
}
