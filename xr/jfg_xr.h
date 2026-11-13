#pragma once
#include <cstdint>
#include <openxr/openxr.h>
namespace RT64 {
struct RenderWorker;
struct RenderTarget;
} // namespace RT64
namespace jfgxr {
struct Input {
  bool active = false, focused = false, headValid = false, handValid[2] = {};
  XrPosef head{{0, 0, 0, 1}, {}};
  XrPosef grip[2]{}, aim[2]{};
  float moveX = 0, moveY = 0, turnX = 0;
  uint16_t buttons = 0;
  bool crouchClick = false, jumpHeld = false;
  uint64_t sequence = 0;
};
bool enabled();
Input input();
void setWorld(float x, float y, float z, float yaw, float units, bool gameplay);
bool begin(RT64::RenderWorker *worker);
void eye(int index);
void copyEye(RT64::RenderWorker *worker, RT64::RenderTarget *target, int index);
void end();
void abortFrame();
unsigned refreshRate();
void shutdown();
bool projection(float *view, float *proj);
bool renderingEye();
} // namespace jfgxr
extern "C" const char *jfg_xr_vk_extensions(int device);
