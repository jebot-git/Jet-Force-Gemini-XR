#include "jfg_xr.h"
#include "plume_vulkan.h"
#include "xr_math.h"
#define XR_USE_GRAPHICS_API_VULKAN
#include "render/rt64_render_target.h"
#include "render/rt64_render_worker.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <openxr/openxr_platform.h>
#include <stdexcept>
#include <string>
#include <vector>
namespace jfgxr {
namespace {
void check(XrResult r, const char *s) {
  if (XR_FAILED(r))
    throw std::runtime_error(std::string(s) + ": " + std::to_string(r));
}
#define XR(cmd) check(cmd, #cmd)
void vkcheck(VkResult r) {
  if (r != VK_SUCCESS)
    throw std::runtime_error("XR Vulkan failure " + std::to_string(r));
}
struct World {
  float x = 0, y = 0, z = 0, yaw = 0, units = 35;
  bool gameplay = false;
};
std::mutex mutex;
Input published;
World world, frameWorld;
XrInstance instance = XR_NULL_HANDLE;
XrSystemId systemId = 0;
XrSession session = XR_NULL_HANDLE;
XrSpace local = XR_NULL_HANDLE, headSpace = XR_NULL_HANDLE, gripSpace[2]{},
        aimSpace[2]{};
XrActionSet actionSet = XR_NULL_HANDLE;
XrAction moveAction{}, turnAction{}, gripAction{}, aimAction{}, fireAction{},
    useAction{}, nextAction{}, prevAction{}, menuAction{}, crouchAction{},
    jumpAction{};
XrPath hands[2]{};
XrSessionState state = XR_SESSION_STATE_UNKNOWN;
bool running = false, failed = false, begun = false, draw = false;
int currentEye = -1;
uint64_t frames = 0;
XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
XrFrameState frame{XR_TYPE_FRAME_STATE};
struct Swap {
  XrSwapchain handle{};
  uint32_t width = 0, height = 0, index = 0;
  std::vector<XrSwapchainImageVulkanKHR> images;
  bool acquired = false;
} swaps[2], handSwaps[2];
plume::VulkanDevice *device = nullptr;
plume::VulkanCommandQueue *queue = nullptr;
VkCommandPool pool = VK_NULL_HANDLE;
VkCommandBuffer command = VK_NULL_HANDLE;
VkFence fence = VK_NULL_HANDLE;
std::string instanceExtensions, deviceExtensions;
int64_t swapchainFormat = 0;
XrPath path(const char *s) {
  XrPath p;
  XR(xrStringToPath(instance, s, &p));
  return p;
}
template <class T> T function(const char *n) {
  PFN_xrVoidFunction f;
  XR(xrGetInstanceProcAddr(instance, n, &f));
  return reinterpret_cast<T>(f);
}
void initInstance() {
  if (instance || failed || !enabled())
    return;
  try {
    const char *extensions[] = {XR_KHR_VULKAN_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ci.applicationInfo.applicationName,
                "Jet Force Gemini XR prototype");
    ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = extensions;
    XR(xrCreateInstance(&ci, &instance));
    XrSystemGetInfo gi{XR_TYPE_SYSTEM_GET_INFO};
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR(xrGetSystem(instance, &gi, &systemId));
    auto get = [&](bool dev) {
      uint32_t count = 0;
      auto fn = dev ? function<PFN_xrGetVulkanDeviceExtensionsKHR>(
                          "xrGetVulkanDeviceExtensionsKHR")
                    : function<PFN_xrGetVulkanInstanceExtensionsKHR>(
                          "xrGetVulkanInstanceExtensionsKHR");
      XR(fn(instance, systemId, 0, &count, nullptr));
      std::string s(count, '\0');
      XR(fn(instance, systemId, count, &count, s.data()));
      if (!s.empty() && s.back() == 0)
        s.pop_back();
      return s;
    };
    instanceExtensions = get(false);
    deviceExtensions = get(true);
    std::fprintf(stderr, "[XR] instance ready\n");
  } catch (const std::exception &e) {
    failed = true;
    std::fprintf(stderr, "[XR] unavailable: %s\n", e.what());
  }
}
XrAction action(const char *n, XrActionType type, bool twoHands = false) {
  XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
  std::strncpy(ci.actionName, n, XR_MAX_ACTION_NAME_SIZE - 1);
  std::strncpy(ci.localizedActionName, n,
               XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
  ci.actionType = type;
  if (twoHands) {
    ci.countSubactionPaths = 2;
    ci.subactionPaths = hands;
  }
  XrAction a;
  XR(xrCreateAction(actionSet, &ci, &a));
  return a;
}
void actions() {
  hands[0] = path("/user/hand/left");
  hands[1] = path("/user/hand/right");
  XrActionSetCreateInfo ci{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::strcpy(ci.actionSetName, "game");
  std::strcpy(ci.localizedActionSetName, "Game");
  XR(xrCreateActionSet(instance, &ci, &actionSet));
  moveAction = action("move", XR_ACTION_TYPE_VECTOR2F_INPUT);
  turnAction = action("turn", XR_ACTION_TYPE_VECTOR2F_INPUT);
  gripAction = action("grip", XR_ACTION_TYPE_POSE_INPUT, true);
  aimAction = action("aim", XR_ACTION_TYPE_POSE_INPUT, true);
  fireAction = action("fire", XR_ACTION_TYPE_FLOAT_INPUT);
  useAction = action("action", XR_ACTION_TYPE_FLOAT_INPUT);
  nextAction = action("next_weapon", XR_ACTION_TYPE_BOOLEAN_INPUT);
  prevAction = action("previous_weapon", XR_ACTION_TYPE_BOOLEAN_INPUT);
  menuAction = action("pause", XR_ACTION_TYPE_BOOLEAN_INPUT);
  crouchAction = action("crouch_toggle", XR_ACTION_TYPE_BOOLEAN_INPUT);
  jumpAction = action("jump_jet", XR_ACTION_TYPE_BOOLEAN_INPUT);
  std::vector<XrActionSuggestedBinding> b;
  auto bind = [&](XrAction a, const char *p) { b.push_back({a, path(p)}); };
  bind(moveAction, "/user/hand/left/input/thumbstick");
  bind(turnAction, "/user/hand/right/input/thumbstick");
  bind(crouchAction, "/user/hand/left/input/thumbstick/click");
  bind(jumpAction, "/user/hand/right/input/thumbstick/click");
  bind(fireAction, "/user/hand/right/input/trigger/value");
  bind(useAction, "/user/hand/left/input/trigger/value");
  bind(nextAction, "/user/hand/right/input/a/click");
  bind(prevAction, "/user/hand/right/input/b/click");
  bind(nextAction, "/user/hand/left/input/x/click");
  bind(prevAction, "/user/hand/left/input/y/click");
  bind(menuAction, "/user/hand/left/input/menu/click");
  for (int i = 0; i < 2; i++) {
    std::string p = i ? "/user/hand/right" : "/user/hand/left";
    bind(gripAction, (p + "/input/grip/pose").c_str());
    bind(aimAction, (p + "/input/aim/pose").c_str());
  }
  for (const char *profile :
       {"/interaction_profiles/oculus/touch_controller",
        "/interaction_profiles/bytedance/pico4_controller"}) {
    XrInteractionProfileSuggestedBinding sb{
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = path(profile);
    sb.countSuggestedBindings = b.size();
    sb.suggestedBindings = b.data();
    XrResult r = xrSuggestInteractionProfileBindings(instance, &sb);
    if (XR_FAILED(r))
      std::fprintf(stderr, "[XR] optional profile %s: %d\n", profile, r);
  }
  // Index has A/B on both controllers and no application menu button. Left B is
  // pause.
  b.clear();
  bind(moveAction, "/user/hand/left/input/thumbstick");
  bind(turnAction, "/user/hand/right/input/thumbstick");
  bind(crouchAction, "/user/hand/left/input/thumbstick/click");
  bind(jumpAction, "/user/hand/right/input/thumbstick/click");
  bind(fireAction, "/user/hand/right/input/trigger/value");
  bind(useAction, "/user/hand/left/input/trigger/value");
  bind(nextAction, "/user/hand/right/input/a/click");
  bind(prevAction, "/user/hand/right/input/b/click");
  bind(menuAction, "/user/hand/left/input/b/click");
  for (int i = 0; i < 2; i++) {
    std::string p = i ? "/user/hand/right" : "/user/hand/left";
    bind(gripAction, (p + "/input/grip/pose").c_str());
    bind(aimAction, (p + "/input/aim/pose").c_str());
  }
  XrInteractionProfileSuggestedBinding sb{
      XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  sb.interactionProfile = path("/interaction_profiles/valve/index_controller");
  sb.countSuggestedBindings = b.size();
  sb.suggestedBindings = b.data();
  XR(xrSuggestInteractionProfileBindings(instance, &sb));
  XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  ai.countActionSets = 1;
  ai.actionSets = &actionSet;
  XR(xrAttachSessionActionSets(session, &ai));
  for (int i = 0; i < 2; i++) {
    XrActionSpaceCreateInfo si{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    si.poseInActionSpace.orientation.w = 1;
    si.subactionPath = hands[i];
    si.action = gripAction;
    XR(xrCreateActionSpace(session, &si, &gripSpace[i]));
    si.action = aimAction;
    XR(xrCreateActionSpace(session, &si, &aimSpace[i]));
  }
}
void initSession(RT64::RenderWorker *w) {
  initInstance();
  if (failed || !instance || session)
    return;
  device = dynamic_cast<plume::VulkanDevice *>(w->device);
  queue = dynamic_cast<plume::VulkanCommandQueue *>(w->commandQueue.get());
  if (!device || !queue)
    throw std::runtime_error("Vulkan renderer required");
  XrGraphicsRequirementsVulkanKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
  XR(function<PFN_xrGetVulkanGraphicsRequirementsKHR>(
      "xrGetVulkanGraphicsRequirementsKHR")(instance, systemId, &req));
  VkPhysicalDevice required;
  XR(function<PFN_xrGetVulkanGraphicsDeviceKHR>("xrGetVulkanGraphicsDeviceKHR")(
      instance, systemId, device->renderInterface->instance, &required));
  if (required != device->physicalDevice)
    throw std::runtime_error("runtime and game selected different GPUs");
  XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
  binding.instance = device->renderInterface->instance;
  binding.physicalDevice = device->physicalDevice;
  binding.device = device->vk;
  binding.queueFamilyIndex = queue->familyIndex;
  binding.queueIndex = queue->queueIndex;
  XrSessionCreateInfo si{XR_TYPE_SESSION_CREATE_INFO};
  si.next = &binding;
  si.systemId = systemId;
  XR(xrCreateSession(instance, &si, &session));
  XrReferenceSpaceCreateInfo ri{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  ri.poseInReferenceSpace.orientation.w = 1;
  ri.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  XR(xrCreateReferenceSpace(session, &ri, &local));
  ri.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  XR(xrCreateReferenceSpace(session, &ri, &headSpace));
  actions();
  uint32_t count = 0;
  XR(xrEnumerateViewConfigurationViews(
      instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count,
      nullptr));
  if (count != 2)
    throw std::runtime_error("stereo view count required");
  XrViewConfigurationView vc[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW},
                                   {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
  XR(xrEnumerateViewConfigurationViews(
      instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count,
      vc));
  XR(xrEnumerateSwapchainFormats(session, 0, &count, nullptr));
  std::vector<int64_t> formats(count);
  XR(xrEnumerateSwapchainFormats(session, count, &count, formats.data()));
  int64_t format = 0;
  for (auto f : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                 VK_FORMAT_R8G8B8A8_UNORM})
    if (std::find(formats.begin(), formats.end(), f) != formats.end()) {
      format = f;
      break;
    }
  if (!format)
    throw std::runtime_error("no RGBA swapchain format");
  swapchainFormat = format;
  for (int i = 0; i < 2; i++) {
    auto &s = swaps[i];
    s.width = vc[i].recommendedImageRectWidth;
    s.height = vc[i].recommendedImageRectHeight;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                    XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = format;
    ci.sampleCount = 1;
    ci.width = s.width;
    ci.height = s.height;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    XR(xrCreateSwapchain(session, &ci, &s.handle));
    XR(xrEnumerateSwapchainImages(s.handle, 0, &count, nullptr));
    s.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    XR(xrEnumerateSwapchainImages(
        s.handle, count, &count,
        reinterpret_cast<XrSwapchainImageBaseHeader *>(s.images.data())));
  }
  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pi.queueFamilyIndex = queue->familyIndex;
  vkcheck(vkCreateCommandPool(device->vk, &pi, nullptr, &pool));
  VkCommandBufferAllocateInfo ca{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = pool;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ca.commandBufferCount = 1;
  vkcheck(vkAllocateCommandBuffers(device->vk, &ca, &command));
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  vkcheck(vkCreateFence(device->vk, &fi, nullptr, &fence));
  std::fprintf(stderr, "[XR] Vulkan session ready, %ux%u per eye\n",
               swaps[0].width, swaps[0].height);
}
void handTextures() {
  for (int i = 0; i < 2; i++) {
    auto &sw = handSwaps[i];
    if (sw.handle)
      continue;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                    XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = swapchainFormat;
    ci.sampleCount = 1;
    ci.width = ci.height = 16;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    XR(xrCreateSwapchain(session, &ci, &sw.handle));
    sw.width = sw.height = 16;
    uint32_t n = 0;
    XR(xrEnumerateSwapchainImages(sw.handle, 0, &n, nullptr));
    sw.images.resize(n, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    XR(xrEnumerateSwapchainImages(
        sw.handle, n, &n,
        reinterpret_cast<XrSwapchainImageBaseHeader *>(sw.images.data())));
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XR(xrAcquireSwapchainImage(sw.handle, &ai, &sw.index));
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    XR(xrWaitSwapchainImage(sw.handle, &wi));
    vkcheck(vkResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkcheck(vkBeginCommandBuffer(command, &bi));
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = sw.images[sw.index].image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkClearColorValue color{
        {i ? 0.9f : 0.05f, i ? 0.45f : 0.65f, i ? 0.1f : 0.95f, 1}};
    vkCmdClearColorImage(command, b.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &b.subresourceRange);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &b);
    vkcheck(vkEndCommandBuffer(command));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &command;
    {
      std::lock_guard<std::mutex> lock(*queue->queue->mutex);
      vkcheck(vkQueueSubmit(queue->queue->vk, 1, &si, fence));
    }
    vkcheck(vkWaitForFences(device->vk, 1, &fence, VK_TRUE, UINT64_MAX));
    vkcheck(vkResetFences(device->vk, 1, &fence));
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XR(xrReleaseSwapchainImage(sw.handle, &ri));
  }
}
bool pose(XrSpace s, XrPosef &p) {
  XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
  XR(xrLocateSpace(s, local, frame.predictedDisplayTime, &l));
  constexpr auto flags = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                         XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  if ((l.locationFlags & flags) != flags)
    return false;
  p = l.pose;
  return true;
}
void pollInput() {
  Input in;
  in.active = running;
  in.focused = state == XR_SESSION_STATE_FOCUSED;
  in.sequence = ++frames;
  in.headValid = pose(headSpace, in.head);
  XrActiveActionSet aset{actionSet, XR_NULL_PATH};
  XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
  sync.countActiveActionSets = 1;
  sync.activeActionSets = &aset;
  XR(xrSyncActions(session, &sync));
  auto getBool = [&](XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
    XR(xrGetActionStateBoolean(session, &gi, &s));
    return s.isActive && s.currentState;
  };
  auto getFloat = [&](XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
    XR(xrGetActionStateFloat(session, &gi, &s));
    return s.isActive ? s.currentState : 0.f;
  };
  auto getVec = [&](XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};
    XR(xrGetActionStateVector2f(session, &gi, &s));
    return s.isActive ? s.currentState : XrVector2f{};
  };
  if (in.focused) {
    auto m = getVec(moveAction), t = getVec(turnAction);
    in.moveX = m.x;
    in.moveY = m.y;
    in.turnX = t.x;
    in.crouchClick = getBool(crouchAction);
    in.jumpHeld = getBool(jumpAction);
    in.buttons = (getFloat(fireAction) > .55f ? 0x2000 : 0) |
                 (getFloat(useAction) > .55f ? 0x0008 : 0) |
                 (getBool(nextAction) ? 0x8000 : 0) |
                 (getBool(prevAction) ? 0x4000 : 0) |
                 (getBool(menuAction) ? 0x1000 : 0);
    for (int i = 0; i < 2; i++) {
      XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
      gi.action = aimAction;
      gi.subactionPath = hands[i];
      XrActionStatePose a{XR_TYPE_ACTION_STATE_POSE};
      XR(xrGetActionStatePose(session, &gi, &a));
      in.handValid[i] = a.isActive && pose(aimSpace[i], in.aim[i]) &&
                        pose(gripSpace[i], in.grip[i]);
    }
  }
  std::lock_guard<std::mutex> lock(mutex);
  published = in;
  frameWorld = world;
}
} // namespace
bool enabled() {
  static const bool yes = []() {
    const char *e = std::getenv("JFG_OPENXR");
    return e && std::strcmp(e, "0");
  }();
  return yes;
}
Input input() {
  std::lock_guard<std::mutex> lock(mutex);
  return published;
}
void setWorld(float x, float y, float z, float yaw, float units, bool game) {
  std::lock_guard<std::mutex> lock(mutex);
  world = {x, y, z, yaw, units, game};
}
bool begin(RT64::RenderWorker *w) {
  if (!enabled() || failed)
    return false;
  try {
    initSession(w);
    if (!session)
      return false;
    XrEventDataBuffer e{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance, &e) == XR_SUCCESS) {
      if (e.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        state = reinterpret_cast<XrEventDataSessionStateChanged *>(&e)->state;
        if (state == XR_SESSION_STATE_READY) {
          XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
          bi.primaryViewConfigurationType =
              XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
          XR(xrBeginSession(session, &bi));
          running = true;
        }
        if (state == XR_SESSION_STATE_STOPPING) {
          XR(xrEndSession(session));
          running = false;
        }
        if (state == XR_SESSION_STATE_EXITING ||
            state == XR_SESSION_STATE_LOSS_PENDING) {
          running = false;
          failed = true;
        }
      }
      e = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (!running)
      return false;
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    XR(xrWaitFrame(session, &wi, &frame));
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    XR(xrBeginFrame(session, &bi));
    begun = true;
    handTextures();
    pollInput();
    draw = frame.shouldRender;
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = frame.predictedDisplayTime;
    li.space = local;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t n;
    XR(xrLocateViews(session, &li, &vs, 2, &n, views));
    draw = draw && n == 2 &&
           (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
           (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    if (!draw) {
      end();
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[XR] frame disabled: %s\n", e.what());
    abortFrame();
    return false;
  }
}
void eye(int i) { currentEye = i; }
bool renderingEye() { return currentEye >= 0; }
bool projection(float *v, float *p) {
  if (currentEye < 0 || !frameWorld.gameplay)
    return false;
  eyeMatrices(views[currentEye], {frameWorld.x, frameWorld.y, frameWorld.z},
              frameWorld.yaw, frameWorld.units, v, p);
  return true;
}
void copyEye(RT64::RenderWorker *w, RT64::RenderTarget *target, int i) {
  if (!begun)
    return;
  if (!target || target->isEmpty())
    throw std::runtime_error("empty XR render target");
  auto &sw = swaps[i];
  XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  XR(xrAcquireSwapchainImage(sw.handle, &ai, &sw.index));
  sw.acquired = true;
  XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wi.timeout = XR_INFINITE_DURATION;
  XR(xrWaitSwapchainImage(sw.handle, &wi));
  auto *src =
      dynamic_cast<plume::VulkanTexture *>(target->getResolvedTexture());
  if (!src)
    throw std::runtime_error("XR source is not Vulkan");
  w->commandList->begin();
  w->commandList->barriers(plume::RenderBarrierStage::GRAPHICS,
                           plume::RenderTextureBarrier(
                               src, plume::RenderTextureLayout::COPY_SOURCE));
  w->commandList->end();
  w->execute();
  w->wait();
  vkcheck(vkResetCommandBuffer(command, 0));
  VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkcheck(vkBeginCommandBuffer(command, &cb));
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
      VK_QUEUE_FAMILY_IGNORED;
  barrier.image = sw.images[sw.index].image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  VkImageBlit blit{};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstSubresource = blit.srcSubresource;
  blit.srcOffsets[1] = {(int)target->width, (int)target->height, 1};
  blit.dstOffsets[1] = {(int)sw.width, (int)sw.height, 1};
  vkCmdBlitImage(command, src->vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 barrier.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                 VK_FILTER_LINEAR);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);
  vkcheck(vkEndCommandBuffer(command));
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command;
  {
    std::lock_guard<std::mutex> lock(*queue->queue->mutex);
    vkcheck(vkQueueSubmit(queue->queue->vk, 1, &submit, fence));
  }
  vkcheck(vkWaitForFences(device->vk, 1, &fence, VK_TRUE, UINT64_MAX));
  vkcheck(vkResetFences(device->vk, 1, &fence));
  w->commandList->begin();
  w->commandList->barriers(plume::RenderBarrierStage::GRAPHICS,
                           plume::RenderTextureBarrier(
                               src, plume::RenderTextureLayout::SHADER_READ));
  w->commandList->end();
  w->execute();
  w->wait();
  XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  XR(xrReleaseSwapchainImage(sw.handle, &ri));
  sw.acquired = false;
}
void end() {
  currentEye = -1;
  if (!begun)
    return;
  XrCompositionLayerProjectionView pv[2] = {
      {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
      {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  for (int i = 0; i < 2; i++) {
    pv[i].pose = views[i].pose;
    pv[i].fov = views[i].fov;
    pv[i].subImage.swapchain = swaps[i].handle;
    pv[i].subImage.imageRect.extent = {(int)swaps[i].width,
                                       (int)swaps[i].height};
  }
  XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  layer.space = local;
  layer.viewCount = 2;
  layer.views = pv;
  XrCompositionLayerQuad menu{XR_TYPE_COMPOSITION_LAYER_QUAD};
  menu.space = headSpace;
  menu.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
  menu.subImage = pv[0].subImage;
  menu.pose.orientation.w = 1;
  menu.pose.position = {0, 0, -2};
  menu.size = {2.4f, 1.8f};
  std::vector<const XrCompositionLayerBaseHeader *> layers{
      frameWorld.gameplay
          ? reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer)
          : reinterpret_cast<XrCompositionLayerBaseHeader *>(&menu)};
  XrCompositionLayerQuad handLayers[2] = {{XR_TYPE_COMPOSITION_LAYER_QUAD},
                                          {XR_TYPE_COMPOSITION_LAYER_QUAD}};
  auto tracked = input();
  if (frameWorld.gameplay)
    for (int i = 0; i < 2; i++)
      if (tracked.handValid[i]) {
        auto &h = handLayers[i];
        h.space = local;
        h.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        h.pose = tracked.grip[i];
        h.size = {.055f, .09f};
        h.subImage.swapchain = handSwaps[i].handle;
        h.subImage.imageRect.extent = {16, 16};
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&h));
      }
  XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
  ei.displayTime = frame.predictedDisplayTime;
  ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  ei.layerCount = draw ? layers.size() : 0;
  ei.layers = layers.data();
  XR(xrEndFrame(session, &ei));
  begun = false;
  if (frames % 300 == 0)
    std::fprintf(stderr, "[XR] frames=%llu gameplay=%d hands=%d,%d\n",
                 (unsigned long long)frames, frameWorld.gameplay,
                 input().handValid[0], input().handValid[1]);
}
unsigned refreshRate() {
  return frame.predictedDisplayPeriod > 0
             ? unsigned(
                   std::lround(1000000000.0 / frame.predictedDisplayPeriod))
             : 90;
}
void abortFrame() {
  currentEye = -1;
  draw = false;
  for (auto &s : swaps)
    if (s.acquired) {
      XrSwapchainImageReleaseInfo r{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(s.handle, &r);
      s.acquired = false;
    }
  if (begun) {
    XrFrameEndInfo e{XR_TYPE_FRAME_END_INFO};
    e.displayTime = frame.predictedDisplayTime;
    e.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    xrEndFrame(session, &e);
    begun = false;
  }
  failed = true;
  std::lock_guard<std::mutex> lock(mutex);
  published = {};
}
void shutdown() {
  if (device && fence)
    vkDestroyFence(device->vk, fence, nullptr);
  if (device && pool)
    vkDestroyCommandPool(device->vk, pool, nullptr);
  for (auto &s : swaps)
    if (s.handle)
      xrDestroySwapchain(s.handle);
  for (auto &s : handSwaps)
    if (s.handle)
      xrDestroySwapchain(s.handle);
  for (int i = 0; i < 2; i++) {
    if (gripSpace[i])
      xrDestroySpace(gripSpace[i]);
    if (aimSpace[i])
      xrDestroySpace(aimSpace[i]);
  }
  if (headSpace)
    xrDestroySpace(headSpace);
  if (local)
    xrDestroySpace(local);
  if (session)
    xrDestroySession(session);
  if (actionSet)
    xrDestroyActionSet(actionSet);
  if (instance)
    xrDestroyInstance(instance);
  instance = XR_NULL_HANDLE;
  session = XR_NULL_HANDLE;
}
} // namespace jfgxr
extern "C" const char *jfg_xr_vk_extensions(int dev) {
  jfgxr::initInstance();
  return dev ? jfgxr::deviceExtensions.c_str()
             : jfgxr::instanceExtensions.c_str();
}
