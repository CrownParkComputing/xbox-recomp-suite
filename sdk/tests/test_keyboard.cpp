// the keyboard as the first controller: the default layout, the settings file's [keyboard] table, and key events
// through to the report the game polls
#include <SDL3/SDL_scancode.h>

#include <chrono>
#include <sstream>
#include <thread>

#include "check.h"
#include "core/input.h"
#include "core/keyboard_pad.h"

using namespace input;
using namespace input::keyboard;

namespace {

struct Keys {
    bool down[kScancodes] = {};
    Keys(std::initializer_list<SDL_Scancode> held) {
        for (SDL_Scancode k : held) down[k] = true;
    }
};

Pad Press(std::initializer_list<SDL_Scancode> held) { return FromKeys(Keys(held).down, Defaults()); }

// key events as the window sends them, every key let go afterwards
struct Session {
    Session() { ReleaseAll(); }
    ~Session() {
        ReleaseAll();
        Set(Defaults());
    }
};

}  // namespace

TEST("keyboard: nothing held is a centred controller with nothing pressed") {
    Pad p = Press({});
    CHECK_EQ(p.buttons, 0);
    for (uint8_t a : p.analog) CHECK_EQ(int(a), 0);
    CHECK(!p.lx && !p.ly && !p.rx && !p.ry);
}

TEST("keyboard: the arrows push the left stick all the way, up as positive y as on the console") {
    CHECK_EQ(Press({SDL_SCANCODE_RIGHT}).lx, 32767);
    CHECK_EQ(Press({SDL_SCANCODE_LEFT}).lx, -32768);
    CHECK_EQ(Press({SDL_SCANCODE_UP}).ly, 32767);
    CHECK_EQ(Press({SDL_SCANCODE_DOWN}).ly, -32768);
    Pad diag = Press({SDL_SCANCODE_UP, SDL_SCANCODE_LEFT});
    CHECK_EQ(diag.lx, -32768);
    CHECK_EQ(diag.ly, 32767);
}

TEST("keyboard: opposite directions held together cancel") {
    Pad p = Press({SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_I, SDL_SCANCODE_K});
    CHECK_EQ(p.lx, 0);
    CHECK_EQ(p.ry, 0);
}

TEST("keyboard: face buttons and triggers press fully, each on its own analog slot") {
    // A B X Y black white left trigger right trigger
    const SDL_Scancode keys[8] = {SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S,
                                  SDL_SCANCODE_D, SDL_SCANCODE_C, SDL_SCANCODE_Q, SDL_SCANCODE_W};
    for (int i = 0; i < 8; i++) {
        Pad p = Press({keys[i]});
        for (int j = 0; j < 8; j++) CHECK_EQ(int(p.analog[j]), i == j ? 0xFF : 0);
        CHECK_EQ(p.buttons, 0);
    }
}

TEST("keyboard: either key of a two key binding presses it") {
    CHECK_EQ(int(Press({SDL_SCANCODE_SPACE}).analog[0]), 0xFF);   // A
    CHECK_EQ(int(Press({SDL_SCANCODE_LSHIFT}).analog[6]), 0xFF);  // left trigger
    CHECK_EQ(int(Press({SDL_SCANCODE_KP_ENTER}).buttons), 0x10);  // start
}

TEST("keyboard: start, back, the d-pad and the stick clicks set the digital buttons in the console's order") {
    CHECK_EQ(int(Press({SDL_SCANCODE_T}).buttons), 0x01);
    CHECK_EQ(int(Press({SDL_SCANCODE_G}).buttons), 0x02);
    CHECK_EQ(int(Press({SDL_SCANCODE_F}).buttons), 0x04);
    CHECK_EQ(int(Press({SDL_SCANCODE_H}).buttons), 0x08);
    CHECK_EQ(int(Press({SDL_SCANCODE_RETURN}).buttons), 0x10);
    CHECK_EQ(int(Press({SDL_SCANCODE_BACKSPACE}).buttons), 0x20);
    CHECK_EQ(int(Press({SDL_SCANCODE_V}).buttons), 0x40);
    CHECK_EQ(int(Press({SDL_SCANCODE_B}).buttons), 0x80);
}

TEST("keyboard: merged into a controller, the stronger press and a pushed key stick win, an idle one leaves the pad") {
    Pad pad;
    pad.buttons = 0x01;
    pad.analog[0] = 0x40;
    pad.analog[1] = 0x80;
    pad.lx = 1000;
    pad.rx = -2000;
    Pad keys = Press({SDL_SCANCODE_RETURN, SDL_SCANCODE_Z, SDL_SCANCODE_RIGHT});
    Merge(pad, keys);
    CHECK_EQ(int(pad.buttons), 0x11);
    CHECK_EQ(int(pad.analog[0]), 0xFF);
    CHECK_EQ(int(pad.analog[1]), 0x80);
    CHECK_EQ(pad.lx, 32767);
    CHECK_EQ(pad.ly, 0);
    CHECK_EQ(pad.rx, -2000);  // the keyboard's right stick is idle
}

TEST("keyboard: bindings read from SDL key names and written back as the same names") {
    std::array<uint16_t, kKeysPerControl> k{};
    CHECK(ParseKeys("Space", k));
    CHECK_EQ(k[0], SDL_SCANCODE_SPACE);
    CHECK_EQ(k[1], 0);
    CHECK(ParseKeys("Left Shift,  J", k));
    CHECK_EQ(k[0], SDL_SCANCODE_LSHIFT);
    CHECK_EQ(k[1], SDL_SCANCODE_J);
    CHECK_EQ(KeysText(k), std::string("Left Shift, J"));
    for (int c = 0; c < kControls; c++) {
        std::array<uint16_t, kKeysPerControl> back{};
        CHECK_MSG(ParseKeys(KeysText(Defaults().key[c]), back) && back == Defaults().key[c], Name(Control(c)));
    }
}

TEST("keyboard: a name SDL does not know, or a third key, is refused and the binding stays") {
    std::array<uint16_t, kKeysPerControl> k = {uint16_t(SDL_SCANCODE_Z), 0};
    CHECK(!ParseKeys("Banana", k));
    CHECK(!ParseKeys("A, B, C", k));
    CHECK_EQ(k[0], SDL_SCANCODE_Z);
}

TEST("keyboard: the settings file's [keyboard] table rebinds a control and keeps the others, and writes every control") {
    Session s;
    toml::table file = toml::parse("[keyboard]\na = \"Return\"\nleft_stick_left = \"Banana\"\n");
    Load(*file["keyboard"].as_table());
    CHECK_EQ(Current().key[kA][0], SDL_SCANCODE_RETURN);
    CHECK_EQ(Current().key[kA][1], 0);
    CHECK(Current().key[kLeftLeft] == Defaults().key[kLeftLeft]);
    toml::table out = Table();
    CHECK_EQ(out.size(), size_t(kControls));
    CHECK_EQ(out["a"].value_or(std::string()), std::string("Return"));
    CHECK_EQ(out["left_stick_left"].value_or(std::string()), std::string("Left"));
    // and the written table reads back to the same bindings
    std::ostringstream text;
    text << toml::table{{"keyboard", out}};
    Set(Defaults());
    toml::table again = toml::parse(text.str());
    Load(*again["keyboard"].as_table());
    CHECK_EQ(Current().key[kA][0], SDL_SCANCODE_RETURN);
}

TEST("keyboard: key events reach the first controller the game polls, which is connected without a pad") {
    Session s;
    CHECK(Poll(0).connected);
    CHECK(!Poll(1).connected);
    OnKey(SDL_SCANCODE_RETURN, true);
    OnKey(SDL_SCANCODE_LEFT, true);
    Pad p = Poll(0);
    CHECK_EQ(int(p.buttons), 0x10);
    CHECK_EQ(p.lx, -32768);
    CHECK_EQ(Poll(1).buttons, 0);  // the other ports stay the pads'
    std::this_thread::sleep_for(std::chrono::milliseconds(70));  // held longer than the shortest press
    OnKey(SDL_SCANCODE_RETURN, false);
    CHECK_EQ(Poll(0).buttons, 0);
    CHECK_EQ(Poll(0).lx, -32768);
}

TEST("keyboard: a key pressed and let go between two polls reads as held for 50 ms, however often it is polled") {
    Session s;
    Poll(0);
    OnKey(SDL_SCANCODE_RETURN, true);
    OnKey(SDL_SCANCODE_RETURN, false);
    CHECK_EQ(int(Poll(0).buttons), 0x10);
    CHECK_EQ(int(Poll(0).buttons), 0x10);  // a second read in the same frame sees it too
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    CHECK_EQ(int(Poll(0).buttons), 0);
}

TEST("keyboard: a tap is seen by one poll even when the game polls later than 50 ms after it") {
    Session s;
    OnKey(SDL_SCANCODE_Z, true);
    OnKey(SDL_SCANCODE_Z, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    CHECK_EQ(int(Poll(0).analog[0]), 0xFF);
    CHECK_EQ(int(Poll(0).analog[0]), 0);
}

TEST("keyboard: losing the focus lets every key go, so none stays held when its key up goes elsewhere") {
    Session s;
    OnKey(SDL_SCANCODE_Z, true);
    OnKey(SDL_SCANCODE_UP, true);
    CHECK_EQ(int(Poll(0).analog[0]), 0xFF);
    ReleaseAll();
    Pad p = Poll(0);
    CHECK_EQ(int(p.analog[0]), 0);
    CHECK_EQ(p.ly, 0);
}

TEST("keyboard: a game's hotkey counts each press, however short, without taking the controller's taps") {
    Session s;
    uint32_t tab = Presses(SDL_SCANCODE_TAB);
    OnKey(SDL_SCANCODE_TAB, true);
    OnKey(SDL_SCANCODE_TAB, true);  // a repeat is not another press
    OnKey(SDL_SCANCODE_TAB, false);
    CHECK_EQ(Presses(SDL_SCANCODE_TAB), tab + 1);
    uint32_t ret = Presses(SDL_SCANCODE_RETURN);
    OnKey(SDL_SCANCODE_RETURN, true);
    OnKey(SDL_SCANCODE_RETURN, false);
    CHECK_EQ(Presses(SDL_SCANCODE_RETURN), ret + 1);
    CHECK_EQ(int(Poll(0).buttons), 0x10);  // the tap is still the controller's
    CHECK_EQ(Presses(-1), 0u);
    CHECK_EQ(Presses(kScancodes), 0u);
}

TEST("keyboard: scancodes outside SDL's range are ignored") {
    Session s;
    OnKey(-1, true);
    OnKey(0, true);
    OnKey(kScancodes, true);
    OnKey(100000, true);
    Pad p = Poll(0);
    CHECK_EQ(p.buttons, 0);
    for (uint8_t a : p.analog) CHECK_EQ(int(a), 0);
}
