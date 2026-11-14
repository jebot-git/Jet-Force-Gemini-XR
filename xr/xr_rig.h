#pragma once
#include "xr_math.h"
#include <cstdint>
#include <limits>
#include <vector>
namespace jfgxr {
using Matrix = std::array<float,16>;
// The stock pistol's four muzzle-ring vertices lie on 2*x-y-z = 126
// (one unit of vertex quantization). These are attachment-space axes, shared
// by the hand and held weapons; the idle wrist animation is not an aim pose.
constexpr std::array<float,3> stockBarrelForward{2,-1,-1};
// Roll is fixed by the grip-to-barrel plane, not an arbitrary perpendicular.
// Project muzzle (36,-20,-35) off forward (2,-1,-1): (-38,7,-83)/6.
constexpr std::array<float,3> stockBarrelUp{-38,7,-83};
inline Matrix multiplyMatrix(const Matrix &a, const Matrix &b) {
  Matrix out{};
  for(int r=0;r<4;r++)for(int c=0;c<4;c++)for(int k=0;k<4;k++)out[r*4+c]+=a[r*4+k]*b[k*4+c];
  return out;
}
inline bool inverseAffine(const Matrix &m, Matrix &out) {
  for(float value:m)if(!std::isfinite(value))return false;
  const float determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
  if(!std::isfinite(determinant) || std::abs(determinant)<1e-8f) return false;
  out={};
  out[0]=(m[5]*m[10]-m[6]*m[9])/determinant;
  out[1]=(m[2]*m[9]-m[1]*m[10])/determinant;
  out[2]=(m[1]*m[6]-m[2]*m[5])/determinant;
  out[4]=(m[6]*m[8]-m[4]*m[10])/determinant;
  out[5]=(m[0]*m[10]-m[2]*m[8])/determinant;
  out[6]=(m[2]*m[4]-m[0]*m[6])/determinant;
  out[8]=(m[4]*m[9]-m[5]*m[8])/determinant;
  out[9]=(m[1]*m[8]-m[0]*m[9])/determinant;
  out[10]=(m[0]*m[5]-m[1]*m[4])/determinant;
  for(int c=0;c<3;c++)out[12+c]=-(m[12]*out[c]+m[13]*out[4+c]+m[14]*out[8+c]);
  out[15]=1;
  return true;
}
inline std::array<float,3> matrixVector(const std::array<float,3> &v, const Matrix &m) {
  return {v[0]*m[0]+v[1]*m[4]+v[2]*m[8], v[0]*m[1]+v[1]*m[5]+v[2]*m[9], v[0]*m[2]+v[1]*m[6]+v[2]*m[10]};
}
inline std::array<float,3> normalized(std::array<float,3> v) {
  const float length=std::hypot(v[0],v[1],v[2]);
  if(length>1e-6f)for(auto &x:v)x/=length;
  return v;
}
inline uint32_t boneId(uint32_t objectId, unsigned bone) {
  return (objectId*2654435761u)^((bone*64u)*40503u);
}
struct ArmBones {
  std::array<uint8_t,128> side{}; // 0 hidden body, 1 left arm, 2 right arm
  int wrist[2] = {-1,-1};
};
struct HandBinding {
  uint32_t id=0;
  int hand=1;
  float scale=.26f;
  std::array<float,3> forward{0,0,-1}, up{0,1,0};
  Matrix local{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};
inline Matrix boundHandMatrix(const HandBinding &binding, XrPosef grip,
                               XrQuaternionf aim, const std::array<float,3> &anchor,
                               float yaw, float units, const XrVector3f &offset,
                               const XrQuaternionf &rotation) {
  if(binding.hand==1) {
    const auto translation=rotateVector(grip.orientation,offset.x,offset.y,offset.z);
    grip.position.x+=translation[0];grip.position.y+=translation[1];grip.position.z+=translation[2];
    aim=multiplyQuaternion(aim,rotation);
  }
  Matrix hand;
  heldWeaponMatrix(grip,aim,anchor,yaw,units,binding.scale,binding.forward,hand.data(),binding.up);
  return multiplyMatrix(binding.local,hand);
}
// Native rigs lay each arm's shoulder, forearm and hand consecutively. Locate
// the other hand by mirroring the documented gun attachment about the torso.
// Cache the result per model: jump/crouch animation must never change the mask.
inline ArmBones armBones(const std::vector<Matrix> &bones, int rightWrist,
                          const std::array<float,3> &root, float bodyYaw,
                          const std::vector<int> &parents={}) {
  ArmBones result;
  if(rightWrist<2 || rightWrist>=int(bones.size()) || bones.size()>128)return result;
  result.wrist[1]=rightWrist;
  const float rx=std::cos(bodyYaw),rz=-std::sin(bodyYaw);
  const auto &hand=bones[rightWrist];
  const float lateral=(hand[12]-root[0])*rx+(hand[14]-root[2])*rz;
  const std::array<float,3> mirror{hand[12]-2*lateral*rx,hand[13],hand[14]-2*lateral*rz};
  const bool hierarchy=parents.size()==bones.size();
  // Match a sibling shoulder/forearm/wrist chain under the same torso joint.
  // Loading and previously reposed matrices can put the torso nearest the
  // mirrored hand; geometry alone cannot identify a hand in those frames.
  if(hierarchy && (parents[rightWrist]!=rightWrist-1 ||
      parents[rightWrist-1]!=rightWrist-2))return result;
  float best=std::numeric_limits<float>::max();
  for(int i=2;i<int(bones.size());i++) {
    if(std::abs(i-rightWrist)<=2)continue;
    if(hierarchy && (parents[i]!=i-1 || parents[i-1]!=i-2 ||
        parents[i-2]!=parents[rightWrist-2]))continue;
    const auto &m=bones[i];
    const float side=(m[12]-root[0])*rx+(m[14]-root[2])*rz;
    if(!hierarchy && (side*lateral>=0 || m[13]<root[1]+(hand[13]-root[1])*.6f))continue;
    const float distance=std::hypot(m[12]-mirror[0],m[13]-mirror[1],m[14]-mirror[2]);
    if(distance<best) { best=distance;result.wrist[0]=i; }
  }
  for(int arm=0;arm<2;arm++)if(result.wrist[arm]>=2)
    for(int bone=result.wrist[arm]-2;bone<=result.wrist[arm];bone++) {
      const auto &m=bones[bone];
      const float side=(m[12]-root[0])*rx+(m[14]-root[2])*rz;
      if(hierarchy || bone==result.wrist[arm] ||
          (std::abs(side)>std::max(1.f,std::abs(lateral)*.25f) && m[13]>root[1]+(hand[13]-root[1])*.6f))
        result.side[bone]=uint8_t(arm+1);
    }
  return result;
}
} // namespace jfgxr
