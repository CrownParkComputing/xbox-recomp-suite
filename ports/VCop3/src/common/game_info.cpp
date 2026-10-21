#include "app/app.h"
#include "image_symbols.h"
#ifdef SYM_VC3_InputMode
#include "game/mouse.h"
#endif

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Virtua Cop 3",               // window and splash
    .short_title = L"VCop3",               // message boxes
    .name = L"VCop3",                      // window classes, crash dumps, handover event
    .log = "vcop3",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or vc3.xbe with its folders beside it.",
    .images = {{IMAGE_XBE, "VCop3"}},   // selected executable, verified by the build's hash guard
    .options = {},                        // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
#ifdef SYM_VC3_InputMode
    .init = vcop3::HostInit,
#else
    .init = nullptr,
#endif
    .loaded = nullptr,                    // image in memory, before its entry point runs
#ifdef SYM_VC3_InputMode
    .window = vcop3::MouseWindow,
#else
    .window = nullptr,
#endif
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
#ifdef SYM_VC3_InputMode
    .pad = vcop3::MousePad,
#else
    .pad = nullptr,
#endif
    .rumble = nullptr,                    // changes a rumble before it is set
    .keyboard_pad = true,                 // the keyboard plays as controller 1; false for a game with its own keys
};
