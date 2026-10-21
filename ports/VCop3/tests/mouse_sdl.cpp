#include <SDL3/SDL.h>
#include <cassert>
#include "app/app.h"
#include "core/input.h"
#include "game/mouse.h"

namespace {
bool menu_open = false, unfocused = false;
app::Settings settings;
}
namespace app {
Settings& Get() { return settings; }
bool GraphicsMenuOpen() { return menu_open; }
bool Unfocused() { return unfocused; }
bool Widescreen() { return false; }
}

int main() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    auto* window = SDL_CreateWindow("VC3 mouse test", 800, 600, SDL_WINDOW_HIDDEN);
    assert(window);
    vcop3::HostInit();
    assert(settings.width == 1280 && settings.height == 960);
    settings.width = 1600; settings.height = 900;
    vcop3::HostInit();
    assert(settings.width == 1600 && settings.height == 900);
    vcop3::MouseWindow(window);
    const auto id = SDL_GetWindowID(window);
    auto focus = [&](Uint32 kind) {
        SDL_Event e{}; e.type = kind; e.window.windowID = id;
        assert(SDL_PushEvent(&e));
    };
    auto motion = [&](float x, float y) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.windowID = id;
        e.motion.x = x; e.motion.y = y;
        assert(SDL_PushEvent(&e));
    };
    auto button = [&](Uint8 which, bool down, float x = 400, float y = 300) {
        SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = id; e.button.button = which; e.button.down = down;
        e.button.x = x; e.button.y = y;
        assert(SDL_PushEvent(&e));
    };
    auto connection_poll = [&] { input::Pad pad{}; vcop3::MousePad(0, pad); return pad; };
    auto poll = [&] {
        vcop3::BeginGuestMousePoll();
        const auto pad = connection_poll();
        vcop3::EndGuestMousePoll();
        return pad;
    };
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    motion(400, 300);
    const auto mouse = vcop3::ReadMouse();
    const auto aim = vcop3::MapMouse(mouse.x, mouse.y, mouse.width, mouse.height, false);
    assert(mouse.active && aim.x == 0 && aim.y == 0);
    button(SDL_BUTTON_LEFT, true);
    button(SDL_BUTTON_LEFT, false);
    assert(connection_poll().analog[0] == 255);
    assert(connection_poll().analog[0] == 255); // background checks cannot steal taps
    assert(poll().analog[0] == 255); // tap survives until the guest poll
    assert(poll().analog[0] == 0);
    button(SDL_BUTTON_RIGHT, true);
    button(SDL_BUTTON_LEFT, true);
    auto pad = poll();
    assert(pad.analog[1] == 255 && pad.analog[0] == 0); // reload takes precedence
    button(SDL_BUTTON_RIGHT, false);
    button(SDL_BUTTON_LEFT, false);
    poll(); // acknowledge the held buttons' last report
    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_RESIZED; resize.window.windowID = id;
    resize.window.data1 = 1920; resize.window.data2 = 1200;
    assert(SDL_PushEvent(&resize));
    button(SDL_BUTTON_LEFT, true, 50, 600); // left black bar
    pad = poll();
    assert(pad.analog[1] == 255 && pad.analog[0] == 0);
    button(SDL_BUTTON_LEFT, false, 50, 600);
    button(SDL_BUTTON_MIDDLE, true);
    assert(poll().analog[2] == 255);
    menu_open = true;
    assert(poll().analog[2] == 0);
    menu_open = false;
    assert(poll().analog[2] == 0); // no stale shot when a menu closes
    button(SDL_BUTTON_MIDDLE, false);
    button(SDL_BUTTON_LEFT, true);
    focus(SDL_EVENT_WINDOW_FOCUS_LOST);
    assert(poll().analog[0] == 0);
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    assert(poll().analog[0] == 0);
    button(SDL_BUTTON_LEFT, false);
    button(SDL_BUTTON_LEFT, true);
    unfocused = true;
    assert(poll().analog[0] == 0);
    unfocused = false;
    assert(poll().analog[0] == 0);
    // A complete touch tap between cabinet polls keeps its aim and one shot.
    auto finger = [&](Uint32 type, SDL_FingerID finger_id, float x, float y) {
        SDL_Event e{}; e.type = type; e.tfinger.windowID = id;
        e.tfinger.fingerID = finger_id; e.tfinger.x = x; e.tfinger.y = y;
        assert(SDL_PushEvent(&e));
    };
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    finger(SDL_EVENT_FINGER_DOWN, 11, .5f, .5f);
    finger(SDL_EVENT_FINGER_MOTION, 11, .6f, .5f);
    finger(SDL_EVENT_FINGER_UP, 11, .6f, .5f);
    assert(poll().analog[0] == 255);
    assert(poll().analog[0] == 0);
    auto touch_sample = vcop3::ReadMouse();
    assert(touch_sample.x > touch_sample.width * .59f);
    SDL_Event synthetic{};
    synthetic.type = SDL_EVENT_MOUSE_BUTTON_DOWN; synthetic.button.windowID = id;
    synthetic.button.which = SDL_TOUCH_MOUSEID; synthetic.button.button = SDL_BUTTON_LEFT;
    synthetic.button.down = true; assert(SDL_PushEvent(&synthetic));
    assert(poll().analog[0] == 0); // SDL touch-mouse synthesis must not double-fire.
    // A second finger does not steal aim or create another shot.
    finger(SDL_EVENT_FINGER_DOWN, 12, .5f, .5f);
    finger(SDL_EVENT_FINGER_DOWN, 13, .1f, .1f);
    finger(SDL_EVENT_FINGER_UP, 13, .1f, .1f);
    touch_sample = vcop3::ReadMouse();
    assert(touch_sample.x == (touch_sample.width - 1) * .5f);
    poll();
    finger(SDL_EVENT_FINGER_CANCELED, 12, .5f, .5f);
    assert(poll().analog[0] == 0);
    input::Pad second{};
    vcop3::MousePad(1, second);
    assert(!second.connected && second.analog[0] == 0);
    SDL_Quit();
}
