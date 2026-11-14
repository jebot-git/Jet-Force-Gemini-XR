#include "jfg_xr.h"
#include "plume_vulkan.h"
#include "xr_math.h"
#include "xr_target.h"
#include "xr_bitmap.h"
#include "xr_rig.h"
#include "xr_world.h"
#include "xr_draw.h"
#include <memory>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <chrono>
#ifdef __ANDROID__
#include <jni.h>
#endif
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
std::mutex mutex;
Input published;
World world, frameWorld, renderWorld;
WorldHistory worldHistory;
std::atomic<bool> firstPersonActive{false};
std::mutex rigMutex;
using PartMap=std::unordered_map<uint32_t,AvatarPart>;
std::shared_ptr<const PartMap> avatarParts=std::make_shared<PartMap>();
std::unordered_map<uint32_t,std::vector<uint8_t>> avatarMasks;
using BindingMap=std::unordered_map<uint32_t,HandBinding>;
std::shared_ptr<const BindingMap> handBindings=std::make_shared<BindingMap>();
struct Calibration { XrVector3f offset{}; XrQuaternionf rotation{0,0,0,1}; };
Calibration calibration, frameCalibration;
struct MenuData {
  bool open = false;
  std::vector<std::string> lines;
  int selected = 0;
  uint64_t revision = 0;
} menuData, frameMenu;
uint64_t overlayRevision=0;
#ifdef __ANDROID__
void *androidVm=nullptr,*androidActivity=nullptr;
void attachAndroidThread() {
  struct Attachment {
    JavaVM *vm=nullptr;
    ~Attachment() {if(vm)vm->DetachCurrentThread();}
  };
  thread_local Attachment attached;
  auto *vm=static_cast<JavaVM*>(androidVm);
  if(!vm)return;
  JNIEnv *env=nullptr;
  if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_EDETACHED) {
    if(vm->AttachCurrentThread(&env,nullptr)!=JNI_OK)throw std::runtime_error("Cannot attach XR thread to Android VM");
    attached.vm=vm;
  }
}
#endif
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
bool running = false, begun = false, draw = false;
std::atomic<bool> failed{false}, quit{false};
int currentEye = -1;
unsigned sourceWidth = 0, sourceHeight = 0;
EyeTarget eyeTarget;
unsigned worldProjections = 0;
uint64_t frames = 0;
XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
XrFrameState frame{XR_TYPE_FRAME_STATE};
struct Swap {
  XrSwapchain handle{};
  uint32_t width = 0, height = 0, index = 0;
  std::vector<XrSwapchainImageVulkanKHR> images;
  bool acquired = false;
} swaps[2], menuSwap;
BitmapFont menuFont;
bool menuFontLoaded = false;
VkBuffer menuBuffer = VK_NULL_HANDLE;
VkDeviceMemory menuMemory = VK_NULL_HANDLE;
uint64_t menuRevision = 0;
uint32_t hudDraws=0,screenEffectDraws=0;
bool hudCaptureWritten=false;
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
    std::vector<const char*> extensions{XR_KHR_VULKAN_ENABLE_EXTENSION_NAME};
#ifdef __ANDROID__
    if(!androidVm || !androidActivity)throw std::runtime_error("Android activity context is required");
    PFN_xrInitializeLoaderKHR initialize=nullptr;
    XR(xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",reinterpret_cast<PFN_xrVoidFunction*>(&initialize)));
    XrLoaderInitInfoAndroidKHR loader{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader.applicationVM=androidVm;loader.applicationContext=androidActivity;
    XR(initialize(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader)));
    extensions.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM=androidVm;androidInfo.applicationActivity=androidActivity;
#endif
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ci.applicationInfo.applicationName,
                "Jet Force Gemini XR prototype");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1,0,0); // Core 1.0 plus explicit KHR extensions.
    ci.enabledExtensionCount = uint32_t(extensions.size());
    ci.enabledExtensionNames = extensions.data();
#ifdef __ANDROID__
    ci.next=&androidInfo;
#endif
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
void createSwap(Swap &s,unsigned width,unsigned height) {
  s.width=width;s.height=height;
  uint32_t count=0;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                    XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = swapchainFormat;
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
void initSession(RT64::RenderWorker *w) {
#ifdef __ANDROID__
  attachAndroidThread();
#endif
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
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(device->physicalDevice,&properties);
  const unsigned gpuLimit=std::min(properties.limits.maxImageDimension2D,unsigned(RT64::RenderTarget::MaxDimension));
  for(int i=0;i<2;i++) {
    const auto extent=eyeExtent(vc[i].recommendedImageRectWidth,vc[i].recommendedImageRectHeight,
                               vc[i].maxImageRectWidth,vc[i].maxImageRectHeight,gpuLimit);
    createSwap(swaps[i],extent.width,extent.height);
    std::fprintf(stderr,"[XR] eye=%d recommended=%ux%u submitted=%ux%u render_scale=%u\n",i,
                 vc[i].recommendedImageRectWidth,vc[i].recommendedImageRectHeight,
                 extent.width,extent.height,eyeRenderScale);
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
void loadBitmapFont() {
  if (!menuFontLoaded) {
    std::vector<std::string> paths;
    if (const char *font=std::getenv("JFG_XR_MENU_FONT")) paths.emplace_back(font);
    paths.emplace_back("assets/xr/menu.bdf");
    paths.emplace_back("/usr/local/share/fonts/domed/unifont-17.0.04.bdf");
    paths.emplace_back("/usr/share/fonts/gnu-unifont/unifont.bdf");
    for(const auto &path:paths) if(menuFont.load(path)) { menuFontLoaded=true; break; }
    if(!menuFontLoaded) throw std::runtime_error("XR settings require a bitmap font; set JFG_XR_MENU_FONT to a BDF file");
  }
}
void uploadBitmap(Swap &swap,VkBuffer &buffer,VkDeviceMemory &memory,uint64_t &uploadedRevision,
    uint64_t revision,std::vector<uint32_t> pixels,unsigned width,unsigned height) {
  if(swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB)
    for(auto &p:pixels)p=(p&0xff00ff00u)|((p&0xff)<<16)|((p>>16)&0xff);
  if (!swap.handle) {
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format=swapchainFormat; ci.sampleCount=1;
    ci.width=swap.width=width; ci.height=swap.height=height;
    ci.faceCount=ci.arraySize=ci.mipCount=1;
    XR(xrCreateSwapchain(session,&ci,&swap.handle));
    uint32_t n=0;
    XR(xrEnumerateSwapchainImages(swap.handle,0,&n,nullptr));
    swap.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    XR(xrEnumerateSwapchainImages(swap.handle,n,&n,
        reinterpret_cast<XrSwapchainImageBaseHeader *>(swap.images.data())));
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size=pixels.size()*sizeof(uint32_t); bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkcheck(vkCreateBuffer(device->vk,&bi,nullptr,&buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device->vk,buffer,&requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(device->physicalDevice,&properties);
    uint32_t type=properties.memoryTypeCount;
    for(uint32_t i=0;i<properties.memoryTypeCount;i++)
      if((requirements.memoryTypeBits&(1u<<i)) &&
          (properties.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
          (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {type=i;break;}
    if(type==properties.memoryTypeCount)throw std::runtime_error("No host-visible coherent memory for XR settings");
    VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mi.allocationSize=requirements.size; mi.memoryTypeIndex=type;
    vkcheck(vkAllocateMemory(device->vk,&mi,nullptr,&memory));
    vkcheck(vkBindBufferMemory(device->vk,buffer,memory,0));
  }
  void *mapped=nullptr;
  vkcheck(vkMapMemory(device->vk,memory,0,pixels.size()*sizeof(uint32_t),0,&mapped));
  std::memcpy(mapped,pixels.data(),pixels.size()*sizeof(uint32_t));
  vkUnmapMemory(device->vk,memory);
  XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  XR(xrAcquireSwapchainImage(swap.handle,&acquire,&swap.index));
  swap.acquired=true;
  XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait.timeout=XR_INFINITE_DURATION;
  XR(xrWaitSwapchainImage(swap.handle,&wait));
  vkcheck(vkResetCommandBuffer(command,0));
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkcheck(vkBeginCommandBuffer(command,&begin));
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  barrier.image=swap.images[swap.index].image;
  barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,0,nullptr,0,nullptr,1,&barrier);
  VkBufferImageCopy copy{};
  copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
  copy.imageExtent={width,height,1};
  vkCmdCopyBufferToImage(command,buffer,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0,0,nullptr,0,nullptr,1,&barrier);
  vkcheck(vkEndCommandBuffer(command));
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount=1; submit.pCommandBuffers=&command;
  {
    std::lock_guard<std::mutex> lock(*queue->queue->mutex);
    vkcheck(vkQueueSubmit(queue->queue->vk,1,&submit,fence));
  }
  vkcheck(vkWaitForFences(device->vk,1,&fence,VK_TRUE,UINT64_MAX));
  vkcheck(vkResetFences(device->vk,1,&fence));
  XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  XR(xrReleaseSwapchainImage(swap.handle,&release));
  swap.acquired=false;
  uploadedRevision=revision;
}
void uploadMenu() {
  if(!frameMenu.open || (menuSwap.handle && menuRevision==frameMenu.revision))return;
  loadBitmapFont();
  auto pixels=menuBitmap(menuFont,frameMenu.lines,frameMenu.selected);
  uploadBitmap(menuSwap,menuBuffer,menuMemory,menuRevision,frameMenu.revision,std::move(pixels),1024,768);
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
  frameCalibration = calibration;
  frameMenu = menuData;
}
void pollSessionEvents() {
  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
  while(xrPollEvent(instance,&event)==XR_SUCCESS) {
    if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
      failed=true;quit=true;running=false;
      std::fprintf(stderr,"[JFG XR] XR runtime/HMD connection lost\n");
    } else if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      state=reinterpret_cast<XrEventDataSessionStateChanged *>(&event)->state;
      if(state==XR_SESSION_STATE_READY) {
        XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};
        info.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XR(xrBeginSession(session,&info));running=true;
      } else if(state==XR_SESSION_STATE_STOPPING) {
        XR(xrEndSession(session));running=false;quit=true;
        std::fprintf(stderr,"[JFG XR] HMD session stopped; closing application\n");
      } else if(state==XR_SESSION_STATE_EXITING) {
        running=false;quit=true;
      } else if(state==XR_SESSION_STATE_LOSS_PENDING) {
        running=false;failed=true;quit=true;
        std::fprintf(stderr,"[JFG XR] HMD session lost\n");
      }
    }
    event={XR_TYPE_EVENT_DATA_BUFFER};
  }
}
} // namespace
bool enabled() {
  return true;
}
#ifdef __ANDROID__
void setAndroidContext(void *vm,void *activity) {androidVm=vm;androidActivity=activity;}
#endif
bool exitRequested() {return failed.load() || quit.load();}
bool hasError() {return failed.load();}
void requestExit() {quit=true;}
bool requireHmd() {
  initInstance();
  if(failed || !instance || !systemId) {
    std::fprintf(stderr,"[JFG XR] A connected XR HMD and working OpenXR runtime are required. No desktop mode is available.\n");
    shutdown();return false;
  }
  return true;
}
bool initializeSession(RT64::RenderWorker *worker) {
  try {
    initSession(worker);
    if(!session)throw std::runtime_error("Could not create an HMD session");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!running && !exitRequested() && std::chrono::steady_clock::now()<deadline) {
      pollSessionEvents();
      if(!running)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!running)throw std::runtime_error("Connected XR HMD did not become ready; connect and wake the headset");
    std::fprintf(stderr,"[JFG XR] Native HMD session ready; no desktop surface\n");
    return true;
  } catch(const std::exception &error) {
    failed=true;std::fprintf(stderr,"[JFG XR] %s\n",error.what());return false;
  }
}
Input input() {
  std::lock_guard<std::mutex> lock(mutex);
  return published;
}
void setWorld(float x, float y, float z, float yaw, float units, bool game, const XrPosef &head) {
  std::lock_guard<std::mutex> lock(mutex);
  world = {x, y, z, yaw, units, game};
  if(game)worldHistory.add(world,head);
  firstPersonActive.store(game,std::memory_order_relaxed);
}
void setMenu(bool open, const std::vector<std::string> &lines, int selected) {
  std::lock_guard<std::mutex> lock(mutex);
  if(menuData.open==open && (!open || (menuData.selected==selected && menuData.lines==lines))) return;
  menuData.open=open; menuData.lines=lines; menuData.selected=selected; menuData.revision=++overlayRevision;
}



void resetScene() {
  std::lock_guard<std::mutex> lock(mutex);
  world.gameplay=false;worldHistory.clear();
  firstPersonActive.store(false,std::memory_order_relaxed);
  std::atomic_store(&handBindings,std::make_shared<const BindingMap>());
}
void setAvatarParts(uint32_t id, const std::vector<uint8_t> &visible) {
  std::lock_guard<std::mutex> lock(rigMutex);
  const auto previous=avatarMasks.find(id);
  if(previous!=avatarMasks.end() && previous->second==visible)return;
  // Keep recent model IDs for display lists already queued on the render thread.
  if(avatarMasks.size()>=8 && previous==avatarMasks.end())avatarMasks.clear();
  avatarMasks[id]=visible;
  auto parts=std::make_shared<PartMap>();
  for(const auto &avatar:avatarMasks)for(unsigned bone=0;bone<128;bone++) {
    (*parts)[boneId(avatar.first,bone)]=bone<avatar.second.size() && avatar.second[bone] ? AvatarPart::Tracked : AvatarPart::Hidden;
    (*parts)[(avatar.first*2654435761u)^(((bone*64u)^0x8000u)*40503u)]=AvatarPart::Hidden;
  }
  std::atomic_store(&avatarParts,std::shared_ptr<const PartMap>(parts));
}
AvatarPart avatarPart(uint32_t id) {
  if(!enabled() || !firstPersonActive.load(std::memory_order_relaxed))return AvatarPart::Ordinary;
  if(id==trackedWeaponId)return AvatarPart::Tracked;
  const auto parts=std::atomic_load(&avatarParts);
  const auto found=parts->find(id);
  if(found!=parts->end())return found->second;
  const auto bindings=std::atomic_load(&handBindings);
  return bindings->count(id)?AvatarPart::Tracked:AvatarPart::Ordinary;
}
void setCalibration(int x,int y,int z,int pitch,int yaw,int roll) {
  std::lock_guard<std::mutex> lock(mutex);
  calibration={{x*.001f,y*.001f,-z*.001f},calibrationRotation(float(pitch),float(yaw),float(roll))};
}
void setHandBindings(const std::vector<HandBinding> &bindings) {
  std::lock_guard<std::mutex> lock(rigMutex);
  auto next=std::make_shared<BindingMap>(*std::atomic_load(&handBindings));
  if(next->size()>2048)next->clear();
  for(const auto &binding:bindings)(*next)[binding.id]=binding;
  std::atomic_store(&handBindings,std::shared_ptr<const BindingMap>(next));
}
void deriveHandBinding(uint32_t id,uint32_t parent,const float *local) {
  const auto part=avatarPart(parent);
  if(part==AvatarPart::Hidden) {
    std::lock_guard<std::mutex> lock(rigMutex);
    auto parts=std::make_shared<PartMap>(*std::atomic_load(&avatarParts));
    (*parts)[id]=AvatarPart::Hidden;
    std::atomic_store(&avatarParts,std::shared_ptr<const PartMap>(parts));
  } else if(part==AvatarPart::Tracked) {
    const auto bindings=std::atomic_load(&handBindings);
    const auto found=bindings->find(parent);
    if(found==bindings->end())return;
    auto derived=found->second;
    Matrix matrix;std::copy(local,local+16,matrix.begin());
    derived.id=id;derived.local=multiplyMatrix(matrix,derived.local);
    setHandBindings({derived});
  }
}
bool trackedTransform(uint32_t id,float *matrix) {
  if(!renderingEye() || !renderWorld.gameplay)return false;
  const auto bindings=std::atomic_load(&handBindings);
  const auto found=bindings->find(id);
  if(found==bindings->end())return false;
  const auto &binding=found->second;
  if(!published.handValid[binding.hand])return false;
  const auto transformed=boundHandMatrix(binding,published.grip[binding.hand],published.aim[binding.hand].orientation,
      {renderWorld.x,renderWorld.y,renderWorld.z},renderWorld.yaw,renderWorld.units,
      frameCalibration.offset,frameCalibration.rotation);
  std::copy(transformed.begin(),transformed.end(),matrix);
  return true;
}
bool begin(RT64::RenderWorker *w) {
  if (!enabled() || failed)
    return false;
  try {
    initSession(w);
    if (!session)
      return false;
    pollSessionEvents();
    if (!running)
      return false;
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    XR(xrWaitFrame(session, &wi, &frame));
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    XR(xrBeginFrame(session, &bi));
    begun = true;
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
void eye(int i, RT64::RenderTarget *target, uint32_t address) {
  currentEye = i;
  eyeTarget.set(target, address);
  worldProjections = 0;
  hudDraws=screenEffectDraws=0;
  renderWorld = frameWorld;
}




bool renderingEye() { return currentEye >= 0; }
bool hudTarget(const RT64::RenderTarget *target) {
  return renderingEye() && target && eyeTarget.contains(target, target->addressForName);
}
void noteUiDraw(uint32_t mode,bool hud) {
  if(mode==uint32_t(DrawMode::ScreenEffect))++screenEffectDraws;
  else if(hud)++hudDraws;
}
void renderSize(unsigned &width, unsigned &height) {
  const auto &s = swaps[std::max(currentEye, 0)];
  const auto extent=imageExtent(s.width,s.height,frameWorld.gameplay);
  width = extent.width;
  height = extent.height;
}
void sourceSize(unsigned width, unsigned height) {
  sourceWidth = width; sourceHeight = height;
}
bool hudTransform(float *matrix) {
  if (currentEye < 0 || !frameWorld.gameplay || !published.headValid)
    return false;
  hudMatrix(views[currentEye], published.head, matrix);
  return true;
}
bool projection(float *v, float *p) {
  if (currentEye < 0 || !frameWorld.gameplay)
    return false;
  // Only the scene camera is replaced. Full-screen HUD/item animations may
  // also use perspective matrices, but have their own independent cameras.
  std::optional<World> recorded;
  {
    std::lock_guard<std::mutex> lock(mutex);
    recorded=worldHistory.match(v);
  }
  if(!recorded)return false;
  renderWorld=*recorded;
  ++worldProjections;
  eyeMatrices(views[currentEye], {recorded->x, recorded->y, recorded->z},
              recorded->yaw, recorded->units, v, p);
  return true;
}
void copyTarget(RT64::RenderWorker *w,RT64::RenderTarget *target,Swap &sw,unsigned width,unsigned height,bool nearest) {
  if (!begun)
    return;
  if (!target || target->isEmpty())
    throw std::runtime_error("empty XR render target");
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
  // Optional one-frame readback for inspecting real icon pixels and alpha.
  // It is disabled during ordinary play and never touches the cartridge/saves.
  const char *capturePath=std::getenv("JFG_XR_HUD_CAPTURE");
  const char *captureFrame=std::getenv("JFG_XR_CAPTURE_FRAME");
  const uint64_t captureAfter=captureFrame?std::strtoull(captureFrame,nullptr,10):0;
  const bool capture=capturePath && &sw==&swaps[0] && !hudCaptureWritten &&
      (captureFrame?frames>=captureAfter:frameWorld.gameplay) && hudDraws>0;
  VkBuffer captureBuffer=VK_NULL_HANDLE;
  VkDeviceMemory captureMemory=VK_NULL_HANDLE;
  const unsigned componentBytes=target->usesHDR?2:1;
  if(capture) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size=VkDeviceSize(width)*height*4*componentBytes;info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkcheck(vkCreateBuffer(device->vk,&info,nullptr,&captureBuffer));
    VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(device->vk,captureBuffer,&requirements);
    VkPhysicalDeviceMemoryProperties properties;vkGetPhysicalDeviceMemoryProperties(device->physicalDevice,&properties);
    uint32_t type=properties.memoryTypeCount;
    for(uint32_t n=0;n<properties.memoryTypeCount;n++)
      if((requirements.memoryTypeBits&(1u<<n)) &&
          (properties.memoryTypes[n].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
          (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {type=n;break;}
    if(type==properties.memoryTypeCount)throw std::runtime_error("No host memory for HUD readback");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
    vkcheck(vkAllocateMemory(device->vk,&allocation,nullptr,&captureMemory));
    vkcheck(vkBindBufferMemory(device->vk,captureBuffer,captureMemory,0));
  }
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
  if (!width || !height || width > target->width || height > target->height)
    throw std::runtime_error("XR source rectangle exceeds render target");
  blit.srcOffsets[1] = {(int)width, (int)height, 1};
  const auto extent=imageExtent(sw.width,sw.height,frameWorld.gameplay);
  blit.dstOffsets[1] = {(int)extent.width, (int)extent.height, 1};
  vkCmdBlitImage(command, src->vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 barrier.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                 nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
  if(capture) {
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
    vkCmdCopyImageToBuffer(command,src->vk,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,captureBuffer,1,&copy);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
  }
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
  if(capture) {
    void *mapped=nullptr;vkcheck(vkMapMemory(device->vk,captureMemory,0,VK_WHOLE_SIZE,0,&mapped));
    std::vector<uint32_t> rgba(size_t(width)*height);
    for(size_t pixel=0;pixel<rgba.size();pixel++) {
      uint8_t channels[4];
      for(unsigned channel=0;channel<4;channel++)channels[channel]=componentBytes==2?
          uint8_t(static_cast<const uint16_t*>(mapped)[pixel*4+channel]>>8):static_cast<const uint8_t*>(mapped)[pixel*4+channel];
      std::memcpy(&rgba[pixel],channels,4);
    }
    vkUnmapMemory(device->vk,captureMemory);vkDestroyBuffer(device->vk,captureBuffer,nullptr);vkFreeMemory(device->vk,captureMemory,nullptr);
    auto write=[&](const std::string &path,const std::vector<uint32_t> &pixels) {
      if(FILE *file=std::fopen(path.c_str(),"wb")) {
        std::fprintf(file,"P7\nWIDTH %u\nHEIGHT %u\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",width,height);
        std::fwrite(pixels.data(),sizeof(uint32_t),pixels.size(),file);std::fclose(file);
      }
    };
    // An early level fade can be completely black; keep waiting for a useful
    // image instead of recording the transition as evidence of visible UI.
    const auto coloured=std::count_if(rgba.begin(),rgba.end(),[](uint32_t p){return (p&0x00ffffffu)!=0;});
    if(coloured>1000) {
      write(capturePath,rgba);
      std::fprintf(stderr,"[XR] captured eye with original game UI %ux%u: %s\n",width,height,capturePath);
      hudCaptureWritten=true;
    }
  }
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
void copyEye(RT64::RenderWorker *worker,RT64::RenderTarget *target,int index,bool nearest) {
  copyTarget(worker,target,swaps[index],sourceWidth,sourceHeight,nearest);
}

void end() {
  currentEye = -1;
  eyeTarget.clear();
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
  const auto menuExtent=imageExtent(swaps[0].width,swaps[0].height,false);
  menu.subImage.imageRect.extent={(int)menuExtent.width,(int)menuExtent.height};
  menu.pose.orientation.w = 1;
  menu.pose.position = {0, 0, -hudDistance};
  menu.size = {uiWidth, uiHeight};
  std::vector<const XrCompositionLayerBaseHeader *> layers{
      frameWorld.gameplay
          ? reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer)
          : reinterpret_cast<XrCompositionLayerBaseHeader *>(&menu)};
  XrCompositionLayerQuad settings{XR_TYPE_COMPOSITION_LAYER_QUAD};
  if(frameMenu.open) {
    uploadMenu();
    settings.space=headSpace; settings.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    settings.pose.orientation.w=1; settings.pose.position={0,0,-uiDistance};
    settings.size={uiWidth,uiHeight};
    settings.subImage.swapchain=menuSwap.handle;
    settings.subImage.imageRect.extent={1024,768};
    layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&settings));
  }
  XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
  ei.displayTime = frame.predictedDisplayTime;
  ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  ei.layerCount = draw ? layers.size() : 0;
  ei.layers = layers.data();
  XR(xrEndFrame(session, &ei));
  begun = false;
  if (frames % 300 == 0)
    std::fprintf(stderr, "[XR] frames=%llu gameplay=%d hands=%d,%d source=%ux%u eye=%ux%u world_projections=%u hud=%u screen_fx=%u hud_distance=%.1f\n",
                 (unsigned long long)frames, frameWorld.gameplay,
                 input().handValid[0], input().handValid[1], sourceWidth, sourceHeight,
                 swaps[0].width, swaps[0].height, worldProjections,hudDraws,screenEffectDraws,hudDistance);
}
unsigned refreshRate() {
  return frame.predictedDisplayPeriod > 0
             ? unsigned(
                   std::lround(1000000000.0 / frame.predictedDisplayPeriod))
             : 90;
}
void abortFrame() {
  firstPersonActive.store(false,std::memory_order_relaxed);
  currentEye = -1;
  eyeTarget.clear();
  draw = false;
  if(menuSwap.acquired) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(menuSwap.handle,&release);
    menuSwap.acquired=false;
  }
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
  quit = true;
  std::lock_guard<std::mutex> lock(mutex);
  published = {};
}
void shutdown() {
#ifdef __ANDROID__
  attachAndroidThread();
#endif
  if(!instance && !device)return;
  if(device && menuBuffer) vkDestroyBuffer(device->vk,menuBuffer,nullptr);
  if(device && menuMemory) vkFreeMemory(device->vk,menuMemory,nullptr);
  if(menuSwap.handle) xrDestroySwapchain(menuSwap.handle);
  if (device && fence)
    vkDestroyFence(device->vk, fence, nullptr);
  if (device && pool)
    vkDestroyCommandPool(device->vk, pool, nullptr);
  for (auto &s : swaps)
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
  session=XR_NULL_HANDLE;systemId=0;actionSet=XR_NULL_HANDLE;
  local=headSpace=XR_NULL_HANDLE;
  for(int i=0;i<2;i++) {gripSpace[i]=aimSpace[i]=XR_NULL_HANDLE;swaps[i]={};}
  menuSwap={};menuBuffer=VK_NULL_HANDLE;menuMemory=VK_NULL_HANDLE;
  pool=VK_NULL_HANDLE;fence=VK_NULL_HANDLE;command=VK_NULL_HANDLE;
  device=nullptr;queue=nullptr;running=false;begun=false;
  session = XR_NULL_HANDLE;
}
} // namespace jfgxr
extern "C" const char *jfg_xr_vk_extensions(int dev) {
  jfgxr::initInstance();
  return dev ? jfgxr::deviceExtensions.c_str()
             : jfgxr::instanceExtensions.c_str();
}
