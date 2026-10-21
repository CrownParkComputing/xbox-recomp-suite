#include "app/app.h"
#include "image_symbols.h"
#include "game/mouse.h"

namespace {
void HostInit() {
    auto& settings = app::Get();
    if (!settings.width && !settings.height) {
        settings.width = 1280;
        settings.height = 960;
        settings.display_mode = app::DisplayMode::Windowed;
    }
}

}

const app::GameInfo app::kGame = {
    .title = L"The House of the Dead III",
    .short_title = L"HOTD3",
    .name = L"HOTD3",
    .log = "hotd3",
    .settings = "settings.toml",
    .splash = L"splash",
    .missing = L"Put hod3xb.xbe and its game folders in assets.",
    .images = {{IMAGE_XBE, "HOTD3"}},
    .options = {},
    .load = nullptr,
    .save = nullptr,
    .init = HostInit,
    .loaded = nullptr,
    .window = hotd3::MouseWindow,
    .message = nullptr,
    .ui = nullptr,
    .pad = hotd3::MousePad,
    .rumble = nullptr,
    .keyboard_pad = true,
};
