#include <cstdio>
#include <cstring>
#include <openxr/openxr.h>
int main() {
  const char *ext = "XR_KHR_vulkan_enable";
  XrInstanceCreateInfo c{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strcpy(c.applicationInfo.applicationName, "JFG XR runtime probe");
  c.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
  c.enabledExtensionCount = 1;
  c.enabledExtensionNames = &ext;
  XrInstance i;
  auto r = xrCreateInstance(&c, &i);
  if (XR_FAILED(r)) {
    std::printf("xrCreateInstance: %d\n", r);
    return 1;
  }
  XrInstanceProperties p{XR_TYPE_INSTANCE_PROPERTIES};
  xrGetInstanceProperties(i, &p);
  std::printf("Runtime: %s\n", p.runtimeName);
  XrSystemGetInfo g{XR_TYPE_SYSTEM_GET_INFO};
  g.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  XrSystemId s;
  r = xrGetSystem(i, &g, &s);
  std::printf("xrGetSystem: %d\n", r);
  xrDestroyInstance(i);
  return XR_FAILED(r) ? 2 : 0;
}
