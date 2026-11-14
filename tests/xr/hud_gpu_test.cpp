#include "xr_math.h"
#include "shared/rt64_raster_params.h"
#include <vulkan/vulkan.h>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
static void check(VkResult r) {
  if(r!=VK_SUCCESS)throw std::runtime_error("Vulkan error "+std::to_string(r));
}
int main(int argc,char **argv) {
  if(argc!=2)return 2;
  static_assert(offsetof(interop::RasterParams,xrHudRows)==32);
  static_assert(sizeof(interop::RasterParams)==96);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName="JFG XR offscreen HUD regression";app.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ii.pApplicationInfo=&app;
  VkInstance instance;check(vkCreateInstance(&ii,nullptr,&instance));
  uint32_t count=0;check(vkEnumeratePhysicalDevices(instance,&count,nullptr));
  if(!count)throw std::runtime_error("No Vulkan device");
  std::vector<VkPhysicalDevice> devices(count);
  check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
  VkPhysicalDevice gpu=devices[0];
  VkPhysicalDeviceProperties props{};
  for(auto candidate:devices) {
    vkGetPhysicalDeviceProperties(candidate,&props);
    if(props.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {gpu=candidate;break;}
  }
  vkGetPhysicalDeviceProperties(gpu,&props);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());
  uint32_t family=0;
  while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;
  if(family==count)throw std::runtime_error("No compute queue");
  float priority=1;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
  VkDevice device;check(vkCreateDevice(gpu,&di,nullptr,&device));
  VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size=10*4*sizeof(float);bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  VkBuffer buffer;check(vkCreateBuffer(device,&bi,nullptr,&buffer));
  VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(device,buffer,&requirements);
  VkPhysicalDeviceMemoryProperties memoryProperties;vkGetPhysicalDeviceMemoryProperties(gpu,&memoryProperties);
  uint32_t type=0;
  for(;type<memoryProperties.memoryTypeCount;type++) {
    const auto flags=memoryProperties.memoryTypes[type].propertyFlags;
    if((requirements.memoryTypeBits&(1u<<type)) &&
       (flags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
       (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))break;
  }
  if(type==memoryProperties.memoryTypeCount)throw std::runtime_error("No coherent host memory");
  VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mi.allocationSize=requirements.size;mi.memoryTypeIndex=type;
  VkDeviceMemory memory;check(vkAllocateMemory(device,&mi,nullptr,&memory));
  check(vkBindBufferMemory(device,buffer,memory,0));
  VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
  VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  li.bindingCount=1;li.pBindings=&binding;
  VkDescriptorSetLayout descriptorLayout;check(vkCreateDescriptorSetLayout(device,&li,nullptr,&descriptorLayout));
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.maxSets=1;pi.poolSizeCount=1;pi.pPoolSizes=&size;
  VkDescriptorPool descriptorPool;check(vkCreateDescriptorPool(device,&pi,nullptr,&descriptorPool));
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool=descriptorPool;ai.descriptorSetCount=1;ai.pSetLayouts=&descriptorLayout;
  VkDescriptorSet descriptor;check(vkAllocateDescriptorSets(device,&ai,&descriptor));
  VkDescriptorBufferInfo info{buffer,0,bi.size};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet=descriptor;write.dstBinding=0;write.descriptorCount=1;
  write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&info;
  vkUpdateDescriptorSets(device,1,&write,0,nullptr);
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(interop::RasterParams)};
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pli.setLayoutCount=1;pli.pSetLayouts=&descriptorLayout;pli.pushConstantRangeCount=1;pli.pPushConstantRanges=&range;
  VkPipelineLayout layout;check(vkCreatePipelineLayout(device,&pli,nullptr,&layout));
  std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
  if(!file)throw std::runtime_error("Cannot read shader");
  const size_t bytes=size_t(file.tellg());
  std::vector<uint32_t> code((bytes+3)/4);
  file.seekg(0);file.read(reinterpret_cast<char *>(code.data()),bytes);
  VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};si.codeSize=bytes;si.pCode=code.data();
  VkShaderModule shader;check(vkCreateShaderModule(device,&si,nullptr,&shader));
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"CSMain",nullptr};
  ci.layout=layout;
  VkPipeline pipeline;check(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline));
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family;
  VkCommandPool pool;check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));
  VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  commands.commandPool=pool;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=1;
  VkCommandBuffer command;check(vkAllocateCommandBuffers(device,&commands,&command));
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(command,&begin));
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
  vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&descriptor,0,nullptr);
  std::array<float,40> expected{};
  XrPosef head{jfgxr::multiplyQuaternion(jfgxr::calibrationRotation(10,25,8),{0,0,0,1}),{.2f,1.6f,-.3f}};
  for(int eye=0;eye<2;eye++) {
    XrView view{XR_TYPE_VIEW};view.pose=head;
    const auto ipd=jfgxr::rotateVector(head.orientation,eye?.032f:-.032f,0,0);
    view.pose.position.x+=ipd[0];view.pose.position.y+=ipd[1];view.pose.position.z+=ipd[2];
    view.pose.orientation=jfgxr::multiplyQuaternion(head.orientation,jfgxr::calibrationRotation(0,eye?2.f:-2.f,0));
    view.fov={-.72f,.84f,.81f,-.65f};
    float matrix[16];jfgxr::hudMatrix(view,head,matrix);
    interop::RasterParams params{};params.renderIndex=eye*5;
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)params.xrHudRows[r][c]=matrix[r*4+c];
    vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),&params);
    vkCmdDispatch(command,1,1,1);
    const float samples[3][4]={{0,0,.2f,1},{1,1,.5f,1},{0,0,.8f,1}};
    for(int sample=0;sample<3;sample++) {
      auto *out=&expected[(eye*5+sample)*4];
      for(int c=0;c<4;c++)out[c]=samples[sample][0]*matrix[c]+samples[sample][1]*matrix[4+c]+matrix[12+c];
      out[2]=(.0001f+samples[sample][2]*.0001f)*out[3];
    }
    auto *continuous=&expected[(eye*5+3)*4];continuous[0]=.2f;continuous[1]=.3f;
    auto *snapped=&expected[(eye*5+4)*4];snapped[0]=.2f-12*.16f;snapped[1]=.3f-11*.18f;
  }
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
  check(vkEndCommandBuffer(command));
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
  check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));check(vkQueueWaitIdle(queue));
  void *mapped;check(vkMapMemory(device,memory,0,bi.size,0,&mapped));
  const auto *actual=static_cast<const float *>(mapped);
  for(size_t i=0;i<expected.size();i++) {
    if(!std::isfinite(actual[i]) || std::abs(actual[i]-expected[i])>.0001f) {
      std::fprintf(stderr,"HUD shader mismatch at %zu: GPU %.8f, CPU %.8f\n",i,actual[i],expected[i]);return 1;
    }
  }
  assert(actual[2]/actual[3]<actual[10]/actual[11]);
  std::printf("PASS: actual HUD shader / push-constant layout, canted asymmetric eyes, UI depth order and continuous original-glyph UVs (%s)\n",props.deviceName);
  vkUnmapMemory(device,memory);
  vkDestroyCommandPool(device,pool,nullptr);vkDestroyPipeline(device,pipeline,nullptr);
  vkDestroyShaderModule(device,shader,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);
  vkDestroyDescriptorPool(device,descriptorPool,nullptr);vkDestroyDescriptorSetLayout(device,descriptorLayout,nullptr);
  vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,memory,nullptr);
  vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
}
