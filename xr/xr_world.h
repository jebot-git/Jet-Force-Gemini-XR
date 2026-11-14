#pragma once
#include "xr_rig.h"
#include <deque>
#include <optional>
namespace jfgxr {
struct World {
  float x=0,y=0,z=0,yaw=0,units=35;
  bool gameplay=false;
};
// Rendering can lag simulation by several queued frames. Match the camera
// recorded in the display list, rather than applying the newest body's anchor
// to older geometry during movement/jumps.
class WorldHistory {
  struct Entry { World world; XrPosef head; };
  std::deque<Entry> entries;
public:
  void clear() {entries.clear();}
  void add(const World &world,const XrPosef &head) {
    entries.push_front({world,head});
    if(entries.size()>64)entries.pop_back();
  }
  std::optional<World> match(const float *view) const {
    Matrix original,inverse;
    std::copy(view,view+16,original.begin());
    if(!inverseAffine(original,inverse))return {};
    const std::array<float,3> camera{inverse[12],inverse[13],inverse[14]};
    const auto cameraForward=normalized(std::array<float,3>{-inverse[8],-inverse[9],-inverse[10]});
    float best=std::numeric_limits<float>::max();
    std::optional<World> result;
    for(const auto &entry:entries) {
      const auto &w=entry.world;
      const auto head=worldVector(entry.head.position.x,entry.head.position.y,entry.head.position.z,w.yaw);
      const auto forward=rotateVector(entry.head.orientation,0,0,-1);
      const auto direction=worldVector(forward[0],forward[1],forward[2],w.yaw);
      const float distance=std::hypot(camera[0]-w.x-head[0]*w.units,
          camera[1]-w.y-head[1]*w.units,camera[2]-w.z-head[2]*w.units);
      if(distance>w.units*.75f)continue;
      const float angle=std::hypot(cameraForward[0]-direction[0],cameraForward[1]-direction[1],cameraForward[2]-direction[2]);
      const float score=distance*distance+angle*angle*w.units*w.units*.1f;
      if(score<best) {
        best=score;result=w;
        // Include the renderer's interpolated simulation-camera translation.
        result->x=camera[0]-head[0]*w.units;
        result->y=camera[1]-head[1]*w.units;
        result->z=camera[2]-head[2]*w.units;
      }
    }
    return result;
  }
};
} // namespace jfgxr
