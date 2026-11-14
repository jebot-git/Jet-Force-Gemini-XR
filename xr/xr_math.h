#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <openxr/openxr.h>
#include "xr_ui.h"
namespace jfgxr {
inline XrQuaternionf multiplyQuaternion(const XrQuaternionf &a, const XrQuaternionf &b) {
  return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
          a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
          a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
          a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline XrQuaternionf calibrationRotation(float pitch, float yaw, float roll) {
  constexpr float halfDegree=3.14159265358979323846f/360;
  pitch*=halfDegree; yaw*=halfDegree; roll*=halfDegree;
  return multiplyQuaternion(multiplyQuaternion({0,std::sin(yaw),0,std::cos(yaw)},
      {std::sin(pitch),0,0,std::cos(pitch)}),{0,0,std::sin(roll),std::cos(roll)});
}
inline std::array<float, 3> rotateVector(const XrQuaternionf &q, float x,
                                        float y, float z) {
  return {(1 - 2 * (q.y*q.y + q.z*q.z))*x + 2*(q.x*q.y-q.w*q.z)*y + 2*(q.x*q.z+q.w*q.y)*z,
          2*(q.x*q.y+q.w*q.z)*x + (1-2*(q.x*q.x+q.z*q.z))*y + 2*(q.y*q.z-q.w*q.x)*z,
          2*(q.x*q.z-q.w*q.y)*x + 2*(q.y*q.z+q.w*q.x)*y + (1-2*(q.x*q.x+q.y*q.y))*z};
}
inline std::array<float, 3> worldVector(float x, float y, float z, float yaw) {
  // Both spaces are right handed; identity game camera looks down +Z.
  const float c = std::cos(yaw), s = std::sin(yaw);
  return {-c * x - s * z, y, s * x - c * z};
}
inline void heldWeaponMatrix(const XrPosef &grip, const XrQuaternionf &aim,
                             const std::array<float,3> &anchor, float yaw,
                             float units, float scale,
                             const std::array<float,3> &nativeForward, float *matrix,
                             const std::array<float,3> &nativeUp = {0,1,0}) {
  auto dot = [](const auto &a, const auto &b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
  auto cross = [](const auto &a, const auto &b) {
    return std::array<float,3>{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
  };
  auto normalized = [&](std::array<float,3> a) {
    const float length=std::sqrt(dot(a,a));
    if(length > .0001f)for(auto &v:a)v/=length;
    return a;
  };
  const auto localForward = normalized(nativeForward);
  // Avoid a degenerate basis for models whose muzzle is nearly vertical.
  const std::array<float,3> up = std::abs(dot(localForward,nativeUp)) < .95f ?
      nativeUp : std::array<float,3>{1,0,0};
  const auto localRight = normalized(cross(localForward,up));
  const auto localUp = cross(localRight,localForward);
  auto right=rotateVector(aim,1,0,0), worldUp=rotateVector(aim,0,1,0), forward=rotateVector(aim,0,0,-1);
  right=worldVector(right[0],right[1],right[2],yaw);
  worldUp=worldVector(worldUp[0],worldUp[1],worldUp[2],yaw);
  forward=worldVector(forward[0],forward[1],forward[2],yaw);
  std::fill(matrix,matrix+16,0);
  for(int r=0;r<3;r++)for(int c=0;c<3;c++)
    matrix[r*4+c]=scale*(localRight[r]*right[c]+localUp[r]*worldUp[c]+localForward[r]*forward[c]);
  const auto position=worldVector(grip.position.x,grip.position.y,grip.position.z,yaw);
  for(int i=0;i<3;i++)matrix[12+i]=anchor[i]+position[i]*units;
  matrix[15]=1;
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
  const float nearDistance = .03f * units, farDistance = 30000;
  projection[0] = 2 / (r - l);
  projection[5] = 2 / (t - b);
  projection[8] = (r + l) / (r - l);
  projection[9] = (t + b) / (t - b);
  projection[10] = -(farDistance + nearDistance) / (farDistance - nearDistance);
  projection[11] = -1;
  projection[14] = -2 * farDistance * nearDistance / (farDistance - nearDistance);
}
inline bool sceneCamera(const float *view, const std::array<float,3> &head,
                         float units) {
  float p[3]{};
  for(int c=0;c<3;c++)p[c]=head[0]*view[c]+head[1]*view[4+c]+head[2]*view[8+c]+view[12+c];
  return std::hypot(p[0],p[1],p[2]) <= units*.75f;
}
inline float stickDeadzone(float v) {
  return std::abs(v) < .18f ? 0 : (v - std::copysign(.18f, v)) / .82f;
}
// Map the original screen to a head-relative plane 3.5 metres away. Each
// eye gets its own homography, including asymmetric FOV and canted displays.
inline void hudMatrix(const XrView &eye, const XrPosef &head, float *matrix) {
  float view[16], projection[16], panel[16]{};
  eyeMatrices(eye, {0, 0, 0}, 0, 1, view, projection);
  auto right = rotateVector(head.orientation, uiWidth*.5f, 0, 0);
  auto up = rotateVector(head.orientation, 0, uiHeight*.5f, 0);
  auto centre = rotateVector(head.orientation, 0, 0, -hudDistance);
  centre[0] += head.position.x;
  centre[1] += head.position.y;
  centre[2] += head.position.z;
  right = worldVector(right[0], right[1], right[2], 0);
  up = worldVector(up[0], up[1], up[2], 0);
  centre = worldVector(centre[0], centre[1], centre[2], 0);
  for (int i=0;i<3;i++) {
    panel[i]=right[i]; panel[4+i]=up[i]; panel[12+i]=centre[i];
  }
  panel[15]=1;
  float pv[16]{};
  std::fill(matrix, matrix+16, 0);
  for(int r=0;r<4;r++)for(int c=0;c<4;c++)for(int k=0;k<4;k++)
    pv[r*4+c] += panel[r*4+k]*view[k*4+c];
  for(int r=0;r<4;r++)for(int c=0;c<4;c++)for(int k=0;k<4;k++)
    matrix[r*4+c] += pv[r*4+k]*projection[k*4+c];
  // HUD is composited over the world; its source z must not change its depth.
  for(int r=0;r<4;r++)matrix[r*4+2]=0;
}
inline float smoothTurnDelta(float stick, float degreesPerSecond,
                             float seconds) {
  // Bound stalls so returning from a pause cannot cause a large rotation.
  return -stickDeadzone(std::clamp(stick, -1.f, 1.f)) * degreesPerSecond *
         (3.14159265358979323846f / 180.f) * std::clamp(seconds, 0.f, .1f);
}
} // namespace jfgxr
