#pragma once
#include <algorithm>
#include <stdexcept>
namespace jfgxr {
// Preserve the original 4:3 canvas and its finite stereo distance.
constexpr float uiWidth=3.6f,uiHeight=2.7f,uiDistance=3.f;
constexpr float hudDistance=3.5f;
constexpr unsigned eyeRenderScale=2;
constexpr unsigned hudWidth=4096,hudHeight=3072;
struct ImageExtent { unsigned width,height; };
inline ImageExtent eyeExtent(unsigned width,unsigned height,unsigned maxWidth,
                              unsigned maxHeight,unsigned gpuLimit) {
  // Reject unsupported sizes instead of silently advertising a 2x image that
  // was clamped or downsampled before it reached the compositor.
  if(!width || !height || width>std::min(maxWidth,gpuLimit)/eyeRenderScale ||
      height>std::min(maxHeight,gpuLimit)/eyeRenderScale)
    throw std::runtime_error("OpenXR/GPU limits cannot support twice the recommended eye dimensions");
  return {width*eyeRenderScale,height*eyeRenderScale};
}
inline ImageExtent imageExtent(unsigned width,unsigned height,bool gameplay) {
  if(gameplay)return {width,height};
  // Fit the original 4:3 image inside the swapchain. Submit this same rectangle
  // for the menu quad, avoiding a stretch to eye aspect followed by a squeeze.
  const unsigned unit=std::min(width/4,height/3);
  return {unit*4,unit*3};
}
}
