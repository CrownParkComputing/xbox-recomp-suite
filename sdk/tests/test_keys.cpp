// the SDL host's key codes: every bindable key reaches the game as the virtual key the Windows build gets
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <windows.h>

#include "app/sdl_keys.h"
#include "check.h"

TEST("keys: letters, digits and function keys keep their position both ways") {
    for (int vk = 'A'; vk <= 'Z'; vk++) {
        int sc[2];
        app::ScancodesFromVk(vk, sc);
        CHECK_EQ(sc[0], SDL_SCANCODE_A + (vk - 'A'));
        CHECK_EQ(app::VkFromScancode(sc[0]), vk);
    }
    for (int vk = '0'; vk <= '9'; vk++) {
        int sc[2];
        app::ScancodesFromVk(vk, sc);
        CHECK(sc[0] != 0);
        CHECK_EQ(app::VkFromScancode(sc[0]), vk);
    }
    for (int vk = VK_F1; vk <= VK_F12; vk++) {
        int sc[2];
        app::ScancodesFromVk(vk, sc);
        CHECK_EQ(app::VkFromScancode(sc[0]), vk);
    }
}

TEST("keys: the US symbol keys map to the OEM codes Windows reports for them") {
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_SEMICOLON), int(VK_OEM_1));
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_GRAVE), int(VK_OEM_3));
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_COMMA), int(VK_OEM_COMMA));
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_LEFTBRACKET), int(VK_OEM_4));
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_NONUSBACKSLASH), int(VK_OEM_102));
}

TEST("keys: either shift, ctrl or alt is the one modifier the game binds, and both sides count as held") {
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_LSHIFT), int(VK_SHIFT));
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_RSHIFT), int(VK_SHIFT));
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_RALT), int(VK_MENU));
    int sc[2];
    app::ScancodesFromVk(VK_CONTROL, sc);
    CHECK(sc[0] == SDL_SCANCODE_LCTRL && sc[1] == SDL_SCANCODE_RCTRL);
    app::ScancodesFromVk(VK_RETURN, sc);
    CHECK_MSG(sc[0] == SDL_SCANCODE_RETURN && sc[1] == SDL_SCANCODE_KP_ENTER, "keypad Enter is Enter, as on Windows");
}

TEST("keys: mouse buttons are read as mouse buttons, keyboard keys never are") {
    CHECK_EQ(app::MouseMaskFromVk(VK_LBUTTON), uint32_t(SDL_BUTTON_LMASK));
    CHECK_EQ(app::MouseMaskFromVk(VK_XBUTTON2), uint32_t(SDL_BUTTON_X2MASK));
    CHECK_EQ(app::MouseMaskFromVk('A'), 0u);
    int sc[2];
    app::ScancodesFromVk(VK_LBUTTON, sc);
    CHECK_EQ(sc[0], 0);
}

TEST("keys: a scancode no virtual key names reaches the game as nothing") {
    CHECK_EQ(app::VkFromScancode(SDL_SCANCODE_APPLICATION), 0);
    CHECK_EQ(app::VkFromScancode(0), 0);
}
