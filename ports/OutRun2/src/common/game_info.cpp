#include "app/app.h"
#include "image_symbols.h"

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
    .title = L"OutRun 2",
    .short_title = L"OutRun2",
    .name = L"OutRun2",
    .log = "outrun2",
    .settings = "settings.toml",
    .splash = L"splash",
    .missing = L"Put outrun2.xbe and its Media folder in assets.",
    .images = {{IMAGE_XBE, "OutRun2"}},
    .init = HostInit,
    .keyboard_pad = true,
};
