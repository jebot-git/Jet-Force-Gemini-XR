#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <openxr/openxr.h>
namespace jfgxr {
inline std::array<float, 3> worldVector(float x, float y, float z, float yaw) {
  // Both spaces are right handed; identity game camera looks down +Z.
  const float c = std::cos(yaw), s = std::sin(yaw);
  return {-c * x - s * z, y, s * x - c * z};
}
inline void eyeMatrices(const XrView &eye, const std::array<float, 3> &anchor,
                        float yaw, float units, float *view,
                        float *projection) {
  const auto q = eye.pose.orientation;
  const float x = q.x, y = q.y, z = q.z, w = q.w;
  auto right = worldVector(1 - 2 * (y * y + z * z), 2 * (x * y + w * z),
                           2 * (x * z - w * y), yaw);
  auto up = worldVector(2 * (x * y - w * z), 1 - 2 * (x * x + z * z),
                        2 * (y * z + w * x), yaw);
  auto back = worldVector(2 * (x * z + w * y), 2 * (y * z - w * x),
                          1 - 2 * (x * x + y * y), yaw);
  auto position = worldVector(eye.pose.position.x, eye.pose.position.y,
                              eye.pose.position.z, yaw);
  std::fill(view, view + 16, 0);
  std::fill(projection, projection + 16, 0);
  for (int i = 0; i < 3; i++) {
    position[i] = anchor[i] + position[i] * units;
    view[i * 4] = right[i];
    view[i * 4 + 1] = up[i];
    view[i * 4 + 2] = back[i];
    view[12] -= position[i] * right[i];
    view[13] -= position[i] * up[i];
    view[14] -= position[i] * back[i];
  }
  view[15] = 1;
  const float l = std::tan(eye.fov.angleLeft), r = std::tan(eye.fov.angleRight);
  const float b = std::tan(eye.fov.angleDown), t = std::tan(eye.fov.angleUp);
  const float near = .03f * units, far = 30000;
  projection[0] = 2 / (r - l);
  projection[5] = 2 / (t - b);
  projection[8] = (r + l) / (r - l);
  projection[9] = (t + b) / (t - b);
  projection[10] = -(far + near) / (far - near);
  projection[11] = -1;
  projection[14] = -2 * far * near / (far - near);
}
inline float stickDeadzone(float v) {
  return std::abs(v) < .18f ? 0 : (v - std::copysign(.18f, v)) / .82f;
}
inline float smoothTurnDelta(float stick, float degreesPerSecond,
                             float seconds) {
  // Bound stalls so returning from a pause cannot cause a large rotation.
  return -stickDeadzone(std::clamp(stick, -1.f, 1.f)) * degreesPerSecond *
         (3.14159265358979323846f / 180.f) * std::clamp(seconds, 0.f, .1f);
}
} // namespace jfgxr
