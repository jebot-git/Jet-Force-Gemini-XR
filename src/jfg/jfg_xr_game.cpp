#include "jfg_xr_game.h"
#include "jfg_fields.h"
#include "jfg_game.h"
#include "jfg_port.h"
#include "jfg_xr.h"
#include "xr_math.h"
#include "xr_posture.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
namespace jfg {
namespace {
float yaw = 0, units = 35;
bool initialized = false, turnHeld = false;
uint16_t previousButtons = 0;
uint32_t hero = 0;
XrVector3f origin{};
float anchor[3]{};
jfgxr::Input tracked;
jfgxr::Posture posture;
float dead(float v) { return jfgxr::stickDeadzone(v); }
std::array<float, 3> rotate(float x, float y, float z) {
  return jfgxr::worldVector(x, y, z, yaw);
}

std::array<float, 3> forward(const XrQuaternionf &q) {
  return rotate(-2 * (q.x * q.z + q.w * q.y), -2 * (q.y * q.z - q.w * q.x),
                -(1 - 2 * (q.x * q.x + q.y * q.y)));
}
std::array<float, 3> point(const XrVector3f &p) {
  auto v = rotate(p.x, p.y, p.z);
  return {anchor[0] + v[0] * units, anchor[1] + v[1] * units,
          anchor[2] + v[2] * units};
}
} // namespace
bool jfg_xr_controls(recomp_context *ctx) {
  if (!jfgxr::enabled())
    return false;
  using Clock = std::chrono::steady_clock;
  const auto now = Clock::now();
  static auto lastTurnTime = now;
  const float turnSeconds =
      std::chrono::duration<float>(now - lastTurnTime).count();
  lastTurnTime = now;
  static const bool smoothTurn = [] {
    const char *mode = std::getenv("JFG_XR_TURN_MODE");
    return mode && std::strcmp(mode, "smooth") == 0;
  }();
  static const float turnSpeed = [] {
    const char *value = std::getenv("JFG_XR_TURN_SPEED");
    if (!value)
      return 90.f;
    char *end = nullptr;
    const float speed = std::strtof(value, &end);
    return end != value && *end == '\0' && std::isfinite(speed) && speed > 0
               ? std::clamp(speed, 15.f, 360.f)
               : 90.f;
  }();
  tracked = jfgxr::input();
  if (!tracked.active) {
    posture.update(false, false, 0, tracked.crouchClick, false);
    return false;
  }
  const bool game = g_rdram && rd32(ADDR_mainGameMode) == 0 && !in_cutscene();
  hero = game ? player_object(ctx) : 0;
  g_cam.player = hero;
  g_cam.active = false;
  g_cam.shoulder = false;
  const uint32_t cam =
      ADDR_cameraActorArray + Camera::Size * (rd32(ADDR_cameraNo) & 3);
  if (!initialized && hero && tracked.headValid) {
    origin = tracked.head.position;
    yaw = -angle_from_s16(rd16(cam));
    if (const char *e = std::getenv("JFG_XR_UNITS_PER_METER"))
      units = std::clamp(std::strtof(e, nullptr), 10.f, 150.f);
    initialized = true;
  }
  if (smoothTurn) {
    if (tracked.focused && hero && tracked.headValid && initialized)
      yaw += jfgxr::smoothTurnDelta(tracked.turnX, turnSpeed, turnSeconds);
    turnHeld = false;
  } else if (tracked.focused && std::abs(tracked.turnX) > .7f && !turnHeld) {
    yaw -= std::copysign(PI_F / 6, tracked.turnX);
    turnHeld = true;
  }
  if (std::abs(tracked.turnX) < .35f)
    turnHeld = false;
  uint16_t b = tracked.focused ? tracked.buttons : 0;
  const bool postureActive = tracked.focused && hero && initialized;
  const float heightDrop =
      tracked.headValid ? origin.y - tracked.head.position.y : 0.f;
  const bool jump = postureActive && (tracked.jumpHeld || (b & 0x0008));
  if (posture.update(postureActive, tracked.headValid, heightDrop,
                     tracked.crouchClick, jump))
    b |= 0x0004; // Original C-down: crouch.
  if (postureActive && tracked.jumpHeld)
    b |= 0x0008; // Original C-up: jump / sustained jetpack input.
  float x = dead(tracked.moveX), y = dead(tracked.moveY);
  // Menus: trigger confirms, action cancels; face buttons keep A/B navigation.
  if (!hero) {
    if (b & 0x2000)
      b = (b & ~0x2000) | 0x8000;
    if (b & 8)
      b = (b & ~8) | 0x4000;
  }
  if (hero && tracked.headValid) {
    auto f = forward(tracked.head.orientation);
    float h = std::atan2(f[0], f[2]);
    float delta = h + angle_from_s16(rd16(cam));
    float c = std::cos(delta), s = std::sin(delta), ox = x;
    x = c * x - s * y;
    y = s * ox + c * y;
  }
  const uint32_t i = rd8(ADDR_sPlayerID) & 3u,
                 pad = ADDR_sControllerCurrData + 6 * i;
  wr16(pad, b);
  wr8(pad + 2, (uint8_t)(int8_t)std::lround(std::clamp(x, -1.f, 1.f) * 80));
  wr8(pad + 3, (uint8_t)(int8_t)std::lround(std::clamp(y, -1.f, 1.f) * 80));
  wr16(ADDR_buttonsPressed + 2 * i, b & ~previousButtons);
  previousButtons = b;
  if (hero) {
    auto off = rotate(origin.x, origin.y, origin.z);
    anchor[0] = rdf(hero + 0x0C) - off[0] * units;
    anchor[1] = rdf(hero + 0x10) + 60 - off[1] * units -
                posture.extraCameraDrop(heightDrop) * units;
    anchor[2] = rdf(hero + 0x14) - off[2] * units;
    const uint32_t data = Obj::Data::get(hero);
    if (valid_ptr(data))
      jfg_xr_aim(data);
  }
  jfgxr::setWorld(anchor[0], anchor[1], anchor[2], yaw, units,
                  hero && tracked.headValid);
  return true;
}
void jfg_xr_aim(uint32_t data) {
  if (!hero || !tracked.active || !tracked.handValid[1] || !valid_ptr(data) ||
      data != Obj::Data::get(hero))
    return;
  auto f = forward(tracked.aim[1].orientation);
  const float h = std::atan2(f[0], f[2]);
  const float pitch = std::asin(std::clamp(f[1], -1.f, 1.f));
  uint16_t ay = angle_to_s16(h - PI_F), ap = angle_to_s16(pitch);
  wr16(data + 0x1C6, ay);
  wr16(data + 0x1CA, ay);
  wr16(data + 0x1CC, ap);
  auto p = point(tracked.aim[1].position);
  for (int i = 0; i < 3; i++)
    wrf(data + 0x544 + i * 4, p[i]);
  PlayerData::TargetLock::set(data, 0);
}
bool jfg_xr_camera(recomp_context *ctx) {
  if (!jfgxr::enabled() || !initialized || !hero || !tracked.headValid ||
      in_cutscene())
    return false;
  const uint32_t cam =
      ADDR_cameraActorArray + Camera::Size * (rd32(ADDR_cameraNo) & 3);
  for (uint32_t i = 0; i < Camera::Size; i += 4) {
    uint32_t v = rd32(cam + i);
    std::memcpy(&g_cam.saved[i], &v, 4);
  }
  g_cam.camera_addr = cam;
  g_cam.overridden = true;
  auto p = point(tracked.head.position), f = forward(tracked.head.orientation);
  wrf(cam + 0x0C, p[0]);
  wrf(cam + 0x10, p[1]);
  wrf(cam + 0x14, p[2]);
  wr16(cam, angle_to_s16(-std::atan2(f[0], f[2])));
  wr16(cam + 2, angle_to_s16(-std::asin(std::clamp(f[1], -1.f, 1.f))));
  wr16(cam + 4, 0);
  for (int i = 0; i < 3; i++)
    wrf(cam + 0x30 + i * 4, 0);
  return true;
}
bool jfg_xr_hide(uint32_t object) {
  return jfgxr::enabled() && tracked.active && hero && object == hero;
}
} // namespace jfg

#include "jfg_hooks.h"
// The barrel API is shared by the original weapons: supply the tracked aim
// origin.
JFG_WRAP(controlGetGunBarrelPos) {
  using namespace jfg;
  if (jfgxr::enabled() && hero && (uint32_t)ctx->r4 == hero &&
      tracked.handValid[1]) {
    const auto p = point(tracked.aim[1].position);
    const uint32_t out[3] = {(uint32_t)ctx->r6, (uint32_t)ctx->r7,
                             rd32((uint32_t)ctx->r29 + 0x10)};
    for (int i = 0; i < 3; i++)
      if (valid_ptr(out[i]))
        wrf(out[i], p[i]);
    return;
  }
  controlGetGunBarrelPos_orig(rdram, ctx);
}
JFG_WRAP(controlUpdateWeapon) {
  using namespace jfg;
  if (jfgxr::enabled() && (uint32_t)ctx->r4 == hero)
    jfg_xr_aim((uint32_t)ctx->r5);
  controlUpdateWeapon_orig(rdram, ctx);
}
