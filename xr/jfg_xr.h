#pragma once
#include <cstdint>
#include <string>
#include <vector>
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
#ifdef __ANDROID__
void setAndroidContext(void *vm,void *activity);
#endif
bool enabled();
bool requireHmd();
bool initializeSession(RT64::RenderWorker *worker);
bool exitRequested();
void requestExit();
bool hasError();
Input input();
void setWorld(float x, float y, float z, float yaw, float units, bool gameplay,
               const XrPosef &gameHead);
bool begin(RT64::RenderWorker *worker);
void eye(int index, RT64::RenderTarget *target, uint32_t framebufferAddress);
void copyEye(RT64::RenderWorker *worker, RT64::RenderTarget *target, int index, bool nearest);
void end();
void abortFrame();
unsigned refreshRate();
void shutdown();
// view is the original camera on input; outputs are replaced for scene views.
bool projection(float *view, float *proj);
bool renderingEye();
bool hudTransform(float *matrix);
bool hudTarget(const RT64::RenderTarget *target);
void noteUiDraw(uint32_t mode, bool hud);
void renderSize(unsigned &width, unsigned &height);
void sourceSize(unsigned width, unsigned height);
void setMenu(bool open, const std::vector<std::string> &lines, int selected);
void resetScene();
enum class AvatarPart { Ordinary, Hidden, Tracked };
constexpr uint32_t trackedWeaponId = 0x58524755u;
void setAvatarParts(uint32_t objectId, const std::vector<uint8_t> &visibleBones);
AvatarPart avatarPart(uint32_t matrixId);
struct HandBinding;
void setHandBindings(const std::vector<HandBinding> &bindings);
bool trackedTransform(uint32_t matrixId, float *matrix);
void setCalibration(int x, int y, int z, int pitch, int yaw, int roll);
void deriveHandBinding(uint32_t id, uint32_t parent, const float *local);
} // namespace jfgxr
extern "C" const char *jfg_xr_vk_extensions(int device);
