#pragma once
#include <cstdint>
namespace jfgxr {
// The N64 swaps framebuffer addresses. The reusable XR target has a stable
// identity; its debug address is not a reliable test of which pass draws HUD.
struct EyeTarget {
  const void *output = nullptr;
  uint32_t framebuffer = 0;
  void set(const void *target, uint32_t address) {
    output = target;
    framebuffer = address;
  }
  void clear() { output = nullptr; framebuffer = 0; }
  bool contains(const void *target, uint32_t address) const {
    return output && target && (output == target || framebuffer == address);
  }
};
} // namespace jfgxr
