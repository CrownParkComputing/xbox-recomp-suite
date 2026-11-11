#include "app/app.h"
#include "core/input.h"
#include "kernel/kernel.h"

#include <chrono>

namespace {
// Orta chooses its player's controller from an insertion after its first
// device query. A pad present immediately in XInitDevices is never selected.
// Keep the host pad state, but let XAPI's poller announce USB enumeration later.
void EnumeratePad(int, input::Pad& pad) {
    using Clock = std::chrono::steady_clock;
    static const auto started = Clock::now();
    if (Clock::now() - started < std::chrono::seconds(1)) pad.connected = false;
}
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Panzer Dragoon Orta",      // window and splash
    .short_title = L"PDOrta",               // message boxes
    .name = L"PDOrta",                      // window classes, crash dumps, handover event
    .log = "pdorta",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "PDOrta"}},  // each recompiled xbe and its exe, for title launches
    .options = {},                        // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = kernel::SerializeGuestCpu,     // the Xbox runs guest code on one processor
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = EnumeratePad,                  // announce controllers after USB enumeration
    .rumble = nullptr,                    // changes a rumble before it is set
    .keyboard_pad = true,                 // the keyboard plays as controller 1; false for a game with its own keys
};
