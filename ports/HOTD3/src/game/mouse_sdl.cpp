#include "game/mouse.h"

#ifdef HOTD3_SDL_INPUT
#include <SDL3/SDL.h>
#include <cstdlib>
#include <mutex>
#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace hotd3 {
namespace {
std::mutex lock;
MouseSample mouse;
MouseButtons buttons;
SDL_WindowID window_id = 0;
SDL_FingerID aim_finger = 0;
bool touching = false;
thread_local bool guest_poll = false;

bool SDLCALL Watch(void*, SDL_Event* event) {
    std::lock_guard guard(lock);
    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.windowID == window_id && event->motion.which != SDL_TOUCH_MOUSEID) {
            mouse.x = event->motion.x; mouse.y = event->motion.y;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.windowID == window_id && event->button.which != SDL_TOUCH_MOUSEID) {
            mouse.x = event->button.x; mouse.y = event->button.y;
            const auto button = event->button.button;
            const uint8_t bit = button == SDL_BUTTON_LEFT ? 1 : button == SDL_BUTTON_RIGHT ? 2 : button == SDL_BUTTON_MIDDLE ? 4 : 0;
            if (mouse.active) buttons.Set(bit, event->button.down);
        }
        break;
    // Finger coordinates are normalized over the SDL window. Ignore SDL's
    // synthetic touch-mouse events above so one tap produces one shot.
    case SDL_EVENT_FINGER_DOWN:
        if (event->tfinger.windowID == window_id && !touching) {
            touching = true; aim_finger = event->tfinger.fingerID;
            mouse.active = true;
            mouse.x = event->tfinger.x * std::max(1.0f, mouse.width - 1);
            mouse.y = event->tfinger.y * std::max(1.0f, mouse.height - 1);
            buttons.Set(1, true);
        }
        break;
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (event->tfinger.windowID == window_id && touching && event->tfinger.fingerID == aim_finger) {
            mouse.x = event->tfinger.x * std::max(1.0f, mouse.width - 1);
            mouse.y = event->tfinger.y * std::max(1.0f, mouse.height - 1);
            if (event->type != SDL_EVENT_FINGER_MOTION) {
                touching = false;
                if (event->type == SDL_EVENT_FINGER_CANCELED) buttons.Release();
                else buttons.Set(1, false);
            }
        }
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        if (event->window.windowID == window_id) {
            mouse.width = float(event->window.data1); mouse.height = float(event->window.data2);
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        if (event->window.windowID == window_id) {
            mouse.active = event->type == SDL_EVENT_WINDOW_FOCUS_GAINED || event->type == SDL_EVENT_WINDOW_MOUSE_ENTER;
            if (!mouse.active) { buttons.Release(); touching = false; }
        }
        break;
    default: break;
    }
    return true;
}
}

void HostInit() {
    // A 4:3 default matches the game's 640x480 canvas. Borderless display can
    // still fill the desktop; the renderer centers the game between side bars.
    auto& settings = app::Get();
    if (!settings.width && !settings.height) { settings.width = 1280; settings.height = 960; }
}

void MouseWindow(void* window) {
    auto* handle = static_cast<SDL_Window*>(window);
    int w = 1, h = 1;
    SDL_GetWindowSize(handle, &w, &h);
    {
        std::lock_guard guard(lock);
        window_id = SDL_GetWindowID(handle);
        mouse.width = float(w); mouse.height = float(h);
        SDL_GetMouseState(&mouse.x, &mouse.y);
        mouse.active = (SDL_GetWindowFlags(handle) & SDL_WINDOW_INPUT_FOCUS) != 0;
    }
    SDL_AddEventWatch(Watch, nullptr);
    if (auto* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR)) SDL_SetCursor(cursor);
    SDL_ShowCursor();
    LOGI("hotd3: mouse gun enabled: left fire, right reload; Enter starts, Backspace inserts a coin");
}

MouseSample ReadMouse() {
    std::lock_guard guard(lock);
    auto result = mouse;
    result.buttons = buttons.held;
    return result;
}

void BeginGuestMousePoll() { guest_poll = true; }
void EndGuestMousePoll() { guest_poll = false; }

void MousePad(int port, input::Pad& pad) {
    if (port != 0) return;
    std::lock_guard guard(lock);
    if (!mouse.active || app::Unfocused() || app::GraphicsMenuOpen()) { buttons.Release(); return; }
    const auto aim = MapMouse(mouse.x, mouse.y, mouse.width, mouse.height, app::Widescreen());
    // XPP also polls on a background thread to detect connected controllers.
    // Only the game's cabinet poll may consume a short click.
    const uint8_t down = guest_poll ? buttons.Poll() : buttons.Peek();
    pad.connected = true;
    const bool reload = (down & 2) || ((down & 1) && !aim.inside);
    // A/B become trigger/reload in the host cabinet bridge.
    if ((down & 1) && !reload) pad.analog[0] = 255;
    if (reload) pad.analog[1] = 255;
    if (down & 4) pad.analog[2] = 255;
    static const bool trace = std::getenv("HOTD3_TRACE_INPUT") != nullptr;
    static uint32_t previous = 0;
    const uint32_t state = down | (uint32_t(pad.buttons) << 8);
    if (trace && guest_poll && state != previous) {
        previous = state;
        LOG_FIRST(64, "hotd3 input: mouse %u, pad buttons %04X, aim %d,%d", down, pad.buttons, aim.x, aim.y);
    }
}
}
#else
// The CPU-only harness has no SDL window or mouse.
namespace hotd3 {
void HostInit() {}
void MouseWindow(void*) {}
void MousePad(int, input::Pad&) {}
void BeginGuestMousePoll() {}
void EndGuestMousePoll() {}
MouseSample ReadMouse() { return {}; }
}
#endif
