// the keyboard and mouse code's system calls on Windows: raw input, GetAsyncKeyState, the layout, ClipCursor
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "common/platform_input.h"

namespace input::os {

namespace {

HWND g_hwnd = nullptr;

// the keyboard layout the player types with, switched per window thread
HKL Layout() { return GetKeyboardLayout(GetWindowThreadProcessId(g_hwnd, nullptr)); }

// US layout scan codes of the keys that move with the layout: a bind names the US key at that position
struct Position {
    int vk;
    UINT scan;
};
constexpr Position kPositions[] = {
    {'1', 0x02}, {'2', 0x03}, {'3', 0x04}, {'4', 0x05}, {'5', 0x06}, {'6', 0x07}, {'7', 0x08}, {'8', 0x09},
    {'9', 0x0A}, {'0', 0x0B}, {VK_OEM_MINUS, 0x0C}, {VK_OEM_PLUS, 0x0D},
    {'Q', 0x10}, {'W', 0x11}, {'E', 0x12}, {'R', 0x13}, {'T', 0x14}, {'Y', 0x15}, {'U', 0x16}, {'I', 0x17},
    {'O', 0x18}, {'P', 0x19}, {VK_OEM_4, 0x1A}, {VK_OEM_6, 0x1B},
    {'A', 0x1E}, {'S', 0x1F}, {'D', 0x20}, {'F', 0x21}, {'G', 0x22}, {'H', 0x23}, {'J', 0x24}, {'K', 0x25},
    {'L', 0x26}, {VK_OEM_1, 0x27}, {VK_OEM_7, 0x28}, {VK_OEM_3, 0x29}, {VK_OEM_5, 0x2B},
    {'Z', 0x2C}, {'X', 0x2D}, {'C', 0x2E}, {'V', 0x2F}, {'B', 0x30}, {'N', 0x31}, {'M', 0x32},
    {VK_OEM_COMMA, 0x33}, {VK_OEM_PERIOD, 0x34}, {VK_OEM_2, 0x35},
};

UINT ScanOf(int vk) {
    for (const Position& p : kPositions)
        if (p.vk == vk) return p.scan;
    return 0;
}

}  // namespace

void Start(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    RAWINPUTDEVICE mouse{1, 2, 0, g_hwnd};  // generic desktop page, mouse usage
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
}

bool Foreground() { return g_hwnd && GetForegroundWindow() == g_hwnd; }


// the key the current layout has at the bind's position
int LiveVk(int vk) {
    UINT scan = ScanOf(vk);
    if (!scan) return vk;
    UINT live = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, Layout());
    return live ? int(live) : vk;
}

// a key the window was sent, as the US key at its position; keys that keep their place come back as they are
int PositionVk(int live) {
    UINT scan = MapVirtualKeyExW(UINT(live), MAPVK_VK_TO_VSC, Layout());
    for (const Position& p : kPositions)
        if (p.scan == scan) return p.vk;
    return live;
}

bool KeyDown(int vk) { return GetAsyncKeyState(LiveVk(vk)) < 0; }

uint32_t KeyChar(int live) {
    // the top bit flags a dead key, which still shows its character
    return MapVirtualKeyExW(UINT(live), MAPVK_VK_TO_CHAR, Layout()) & 0x7FFF;
}

void Clip(bool on) {
    if (!on) {
        ClipCursor(nullptr);
        return;
    }
    RECT r;
    RECT cur;
    GetClientRect(g_hwnd, &r);
    MapWindowPoints(g_hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    if (!GetClipCursor(&cur) || !EqualRect(&r, &cur)) ClipCursor(&r);
}

// Windows asks for the cursor only when the mouse moves
void RefreshCursor() {
    if (g_hwnd) PostMessageW(g_hwnd, WM_SETCURSOR, WPARAM(g_hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

bool CursorInWindow(float& x, float& y, float& w, float& h) {
    POINT p;
    RECT r;
    if (!GetCursorPos(&p) || !ScreenToClient(g_hwnd, &p) || !GetClientRect(g_hwnd, &r)) return false;
    x = float(p.x);
    y = float(p.y);
    w = float(r.right);
    h = float(r.bottom);
    return true;
}

bool ReadRawMouse(void* handle, int& dx, int& dy, int& wheel, bool& absolute) {
    RAWINPUT ri;
    UINT size = sizeof(ri);
    if (GetRawInputData(static_cast<HRAWINPUT>(handle), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return false;
    if (ri.header.dwType != RIM_TYPEMOUSE) return false;
    const RAWMOUSE& m = ri.data.mouse;
    wheel = 0;
    if (m.usButtonFlags & RI_MOUSE_WHEEL) wheel = SHORT(m.usButtonData) > 0 ? 1 : -1;
    absolute = (m.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
    dx = m.lLastX;
    dy = m.lLastY;
    return true;
}

}  // namespace input::os
