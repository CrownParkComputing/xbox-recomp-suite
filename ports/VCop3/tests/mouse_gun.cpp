#include <cassert>
#include <limits>
#include "game/mouse.h"

int main() {
    using vcop3::MapMouse;
    auto center = MapMouse(960, 600, 1920, 1200, false);
    assert(center.inside && center.x == 0 && center.y == 0);
    auto left_top = MapMouse(160, 0, 1920, 1200, false);
    assert(left_top.inside && left_top.x == -320 && left_top.y == 240);
    auto right_bottom = MapMouse(1759, 1199, 1920, 1200, false);
    assert(right_bottom.inside && right_bottom.x == 319 && right_bottom.y == -239);
    assert(!MapMouse(159, 600, 1920, 1200, false).inside);
    assert(!MapMouse(1760, 600, 1920, 1200, false).inside);
    assert(!MapMouse(960, 1200, 1920, 1200, false).inside);
    auto wide = MapMouse(960, 600, 1920, 1200, true);
    assert(wide.inside && wide.x == 0 && wide.y == 0);
    assert(!MapMouse(960, 59, 1920, 1200, true).inside);
    // A window scaled by the compositor must aim at the same game pixel.
    auto normal = MapMouse(400, 300, 800, 600, false);
    auto dense = MapMouse(800, 600, 1600, 1200, false);
    assert(normal.x == dense.x && normal.y == dense.y);
    assert(!MapMouse(0, 0, 0, 0, false).inside);
    assert(!MapMouse(0, 0, std::numeric_limits<float>::infinity(), 600, false).inside);
    assert(!MapMouse(std::numeric_limits<float>::quiet_NaN(), 0, 800, 600, false).inside);

    vcop3::MouseButtons buttons;
    buttons.Set(1, true);
    buttons.Set(1, false); // complete click between game polls
    assert(buttons.Peek() == 1 && buttons.Peek() == 1); // connection checks
    assert(buttons.Poll() == 1);
    assert(buttons.Poll() == 0);
    buttons.Set(2, true);
    assert(buttons.Poll() == 2 && buttons.Poll() == 2);
    buttons.Set(4, true);
    assert(buttons.Poll() == 6);
    buttons.Release(); // focus loss or graphics menu
    assert(buttons.Poll() == 0);
}
