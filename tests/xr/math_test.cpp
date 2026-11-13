#include "xr_math.h"
#include "xr_posture.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
std::array<float, 4> multiply(std::array<float, 4> v, const float *m) {
  std::array<float, 4> o{};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      o[j] += v[i] * m[i * 4 + j];
  return o;
}
bool near(float a, float b) { return std::abs(a - b) < .0001f; }
int main() {
  XrView e{XR_TYPE_VIEW};
  e.pose.orientation.w = 1;
  e.fov = {-.8f, .8f, .8f, -.8f};
  float v[16], p[16];
  jfgxr::eyeMatrices(e, {0, 0, 0}, 0, 35, v, p);
  auto forward = multiply({0, 0, 35, 1}, v);
  assert(near(forward[2], -35));
  auto clip = multiply(forward, p);
  assert(clip[3] > 0 && near(clip[0], 0) && near(clip[1], 0));
  e.pose.position.x = -.032f;
  jfgxr::eyeMatrices(e, {0, 0, 0}, 0, 35, v, p);
  auto left = multiply({0, 0, 35, 1}, v);
  e.pose.position.x = .032f;
  jfgxr::eyeMatrices(e, {0, 0, 0}, 0, 35, v, p);
  auto right = multiply({0, 0, 35, 1}, v);
  assert(near(left[0] - right[0], .064f * 35));
  e.pose.position = {.2f, .4f, -.3f};
  jfgxr::eyeMatrices(e, {10, 20, 30}, .7f, 35, v, p);
  auto pos = jfgxr::worldVector(.2f, .4f, -.3f, .7f);
  auto eye =
      multiply({10 + pos[0] * 35, 20 + pos[1] * 35, 30 + pos[2] * 35, 1}, v);
  assert(near(eye[0], 0) && near(eye[1], 0) && near(eye[2], 0));
  e.pose.orientation = {0, 0, std::sin(.3f), std::cos(.3f)};
  jfgxr::eyeMatrices(e, {0, 0, 0}, 0, 35, v, p);
  assert(std::abs(v[1]) > .5f); // roll must survive
  assert(near(jfgxr::stickDeadzone(.1f), 0) &&
         near(jfgxr::stickDeadzone(1), 1) &&
         near(jfgxr::stickDeadzone(-1), -1));
  // One second of turning must have the same angle at 30 and 90 updates/sec.
  float turn30 = 0, turn90 = 0;
  for (int i = 0; i < 30; ++i)
    turn30 += jfgxr::smoothTurnDelta(1, 90, 1.f / 30);
  for (int i = 0; i < 90; ++i)
    turn90 += jfgxr::smoothTurnDelta(1, 90, 1.f / 90);
  assert(near(turn30, -3.14159265358979323846f / 2));
  assert(near(turn30, turn90));
  assert(near(jfgxr::smoothTurnDelta(.1f, 90, .05f), 0));
  assert(near(jfgxr::smoothTurnDelta(-1, 90, .05f),
              -jfgxr::smoothTurnDelta(1, 90, .05f)));
  assert(near(jfgxr::smoothTurnDelta(1, 90, 10),
              jfgxr::smoothTurnDelta(1, 90, .1f)));
  jfgxr::Posture posture;
  assert(!posture.update(true, true, 0, false, false));
  assert(posture.update(true, true, 0, true, false));
  assert(
      posture.update(true, true, 0, true, false)); // Held click toggles once.
  assert(near(posture.extraCameraDrop(0), .30f));
  assert(near(posture.extraCameraDrop(.2f), .10f));
  assert(near(posture.extraCameraDrop(.4f), 0));
  assert(posture.update(true, true, 0, false, false));
  assert(!posture.update(true, true, 0, true, false)); // Second click stands.
  assert(posture.update(true, true, .26f, false, false));
  assert(posture.update(true, true, .20f, false, false)); // Hysteresis.
  assert(!posture.update(true, true, .14f, false, false));
  assert(posture.update(true, true, .26f, true, false));  // Both sources.
  assert(!posture.update(true, true, .26f, false, true)); // Jump wins.
  assert(!posture.toggled);
  assert(
      posture.update(true, true, .26f, false, false)); // Still physically low.
  assert(!posture.update(true, false, .26f, false, false)); // Tracking lost.
  assert(!posture.update(false, true, 0, true, false));
  assert(!posture.update(true, true, 0, true, false)); // No focus-return click.
  std::puts("PASS: stereo transforms, stick deadzone, smooth turning, crouch "
            "toggle/height hysteresis/camera offset/jump priority/focus");
}
