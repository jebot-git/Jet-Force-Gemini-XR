#pragma once
#include <algorithm>
#include <cmath>
namespace jfgxr {
struct MenuInput {
  bool open = false, menuHeld = false, stickHeld = false, triggerHeld = false;
  int selected = 0;
  // Returns -1/1 for a row edit, 2 for activation, and 0 otherwise.
  int update(bool focused, bool menu, bool trigger, float x, float y, int rows) {
    if (!focused) { menuHeld = menu; triggerHeld = trigger; stickHeld = true; return 0; }
    const bool toggle = menu && !menuHeld;
    menuHeld = menu;
    const bool activate = trigger && !triggerHeld;
    triggerHeld = trigger;
    if(toggle) { open=!open;stickHeld=true;return 0; }
    if (!open) { stickHeld = true; return 0; }
    if (std::abs(x) < .35f && std::abs(y) < .35f) stickHeld = false;
    if (!stickHeld && (std::abs(x) > .7f || std::abs(y) > .7f)) {
      stickHeld = true;
      if (std::abs(y) > std::abs(x)) selected = (selected + (y > 0 ? -1 : 1) + rows) % rows;
      else return x > 0 ? 1 : -1;
    }
    return activate ? 2 : 0;
  }
};
} // namespace jfgxr
