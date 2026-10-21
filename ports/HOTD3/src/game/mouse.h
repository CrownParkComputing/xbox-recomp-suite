#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace input { struct Pad; }

namespace hotd3 {

struct Aim {
    int32_t x = 0, y = 0;
    bool inside = false;
};

// Invert the renderer's centered presentation rectangle. SDL positions and
// window dimensions are both logical pixels, so display density cancels out.
inline Aim MapMouse(float x, float y, float width, float height, bool wide) {
    if (!(width > 0 && height > 0) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(x) || !std::isfinite(y)) return {};
    const float aspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f;
    const float w = std::min(width, height * aspect), h = w / aspect;
    const float u = (x - (width - w) * 0.5f) / w;
    const float v = (y - (height - h) * 0.5f) / h;
    return {int32_t(std::lround(std::clamp(u, 0.0f, 1.0f) * 639.0f)) - 320,
            240 - int32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 479.0f)),
            u >= 0 && u < 1 && v >= 0 && v < 1};
}

struct MouseSample {
    float x = 0, y = 0, width = 1, height = 1;
    bool active = false;
    uint8_t buttons = 0; // left 1, right 2, middle 4
};

// A tap between two guest polls still gets one down report. Focus loss clears
// held and pending buttons, preventing shots when the player returns.
struct MouseButtons {
    uint8_t held = 0, pending = 0;
    void Set(uint8_t bit, bool down) {
        if (down) { if (!(held & bit)) pending |= bit; held |= bit; }
        else held &= uint8_t(~bit);
    }
    uint8_t Poll() { const uint8_t result = held | pending; pending = 0; return result; }
    uint8_t Peek() const { return held | pending; }
    void Release() { held = pending = 0; }
};

void HostInit();
void MouseWindow(void* window);
void MousePad(int port, input::Pad& pad);
void BeginGuestMousePoll();
void EndGuestMousePoll();
MouseSample ReadMouse();

} // namespace hotd3
