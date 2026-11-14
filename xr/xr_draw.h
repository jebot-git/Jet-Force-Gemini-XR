#pragma once
#include <cstdint>
namespace jfgxr {
// RDP no-op markers travel with the queued display list. They must not be
// inferred from current game-thread state while an older frame is rendered.
constexpr uint32_t drawModeMarker=0xc0585200u;
enum class DrawMode : uint32_t { Normal=0, ScreenEffect=1 };
// Point-sampled 1:1 RDP rectangles normally retain a native-pixel UV grid.
// That grid is invalid after an XR homography and makes identity-scaled glyphs
// stay blocky even in a large target. Reconstruct original 2D texels smoothly;
// depth-tested sprites and 3D world materials retain their own sampling rules.
inline bool nativeUiSampling(bool xrEnabled,bool rectangle,bool depthCompare,bool depthWrite) {
  return xrEnabled && rectangle && !depthCompare && !depthWrite;
}
inline bool hudDraw(bool world, bool viewport, bool depthCompare, bool depthWrite,
                    DrawMode mode) {
  return mode==DrawMode::Normal && !world && (viewport || (!depthCompare && !depthWrite));
}
}
