#pragma once
#include <algorithm>

namespace jfgxr {
struct Posture {
  bool toggled = false, physical = false, previousClick = false;

  bool update(bool active, bool headValid, float heightDrop, bool click,
              bool jump) {
    if (!active) {
      toggled = physical = false;
      previousClick = click;
      return false;
    }
    if (click && !previousClick)
      toggled = !toggled;
    previousClick = click;
    // Hysteresis avoids flickering between poses near the threshold.
    if (!headValid)
      physical = false;
    else if (heightDrop >= .25f)
      physical = true;
    else if (heightDrop <= .15f)
      physical = false;
    // Jump/jet input takes priority over crouch; no stuck toggle after landing.
    if (jump)
      toggled = false;
    return !jump && (toggled || physical);
  }

  float extraCameraDrop(float heightDrop) const {
    // Physical tracking already lowers the view; do not apply that drop twice.
    return toggled ? std::max(0.f, .30f - std::max(0.f, heightDrop)) : 0.f;
  }
};
} // namespace jfgxr
