#include "xr_math.h"
#include "xr_posture.h"
#include "xr_target.h"
#include "xr_rig.h"
#include "xr_menu.h"
#include "xr_world.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
std::array<float, 4> multiply(std::array<float, 4> v, const float *m) {
  std::array<float, 4> o{};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      o[j] += v[i] * m[i * 4 + j];
  return o;
}
bool near(float a, float b) { return std::abs(a - b) < .0001f; }
int main() {
  // Both rendered and submitted dimensions must survive a 2x request; reject
  // runtimes/GPUs that cannot supply it, including unsigned overflow inputs.
  const auto doubled=jfgxr::eyeExtent(2160,2376,8192,8192,16384);
  assert(doubled.width==4320 && doubled.height==4752);
  for(const auto limits:std::array<std::array<unsigned,5>,4>{{
      {2160,2376,4096,8192,16384},{2160,2376,8192,8192,4096},
      {0,2376,8192,8192,16384},{~0u,2376,~0u,~0u,~0u}}}) {
    bool rejected=false;
    try {jfgxr::eyeExtent(limits[0],limits[1],limits[2],limits[3],limits[4]);}
    catch(const std::runtime_error &) {rejected=true;}
    assert(rejected);
  }
  for(const auto extent:std::array<jfgxr::ImageExtent,3>{{doubled,{4000,2000},{4321,4753}}}) {
    const auto menu=jfgxr::imageExtent(extent.width,extent.height,false);
    assert(menu.width<=extent.width && menu.height<=extent.height);
    assert(menu.width*3==menu.height*4);
    const auto eye=jfgxr::imageExtent(extent.width,extent.height,true);
    assert(eye.width==extent.width && eye.height==extent.height);
  }
  const auto originalMenu=jfgxr::imageExtent(2160,2376,false);
  const auto doubledMenu=jfgxr::imageExtent(doubled.width,doubled.height,false);
  assert(doubledMenu.width==2*originalMenu.width && doubledMenu.height==2*originalMenu.height);

  int output=0, unrelated=0;
  jfgxr::EyeTarget target;
  assert(!target.contains(&output,0x1000));
  target.set(&output,0x1000);
  assert(target.contains(&output,0x1000));
  // The second N64 buffer must not disable HUD projection on alternate frames.
  target.set(&output,0x2000);
  assert(target.contains(&output,0x1000));
  assert(target.contains(&unrelated,0x2000)); // MSAA intermediate for this buffer
  assert(!target.contains(&unrelated,0x1000));
  target.clear();
  assert(!target.contains(&output,0x2000));
  jfgxr::MenuInput menu;
  assert(menu.update(true,true,false,0,0,12)==0 && menu.open);
  menu.update(true,true,false,0,0,12); // centre after opening before navigation
  assert(menu.update(true,true,false,1,0,12)==1);
  assert(menu.update(true,true,false,1,0,12)==0); // held stick edits once
  menu.update(true,false,false,0,0,12);
  menu.update(true,false,false,0,-1,12);
  assert(menu.selected==1);
  menu.update(true,false,false,0,0,12);
  assert(menu.update(true,false,true,0,0,12)==2);
  assert(menu.update(true,false,true,0,0,12)==0); // no repeated activation
  menu.update(false,true,false,0,0,12);
  menu.update(true,true,false,0,0,12);
  assert(menu.open); // focus return must not toggle
  menu.update(true,false,false,0,0,12);
  menu.update(true,true,false,0,0,12);
  assert(!menu.open);
  XrView e{XR_TYPE_VIEW};
  e.pose.orientation.w = 1;
  e.fov = {-.8f, .8f, .8f, -.8f};
  float v[16], p[16];
  jfgxr::eyeMatrices(e, {0, 0, 0}, 0, 35, v, p);
  auto forward = multiply({0, 0, 35, 1}, v);
  assert(near(forward[2], -35));
  auto clip = multiply(forward, p);
  assert(clip[3] > 0 && near(clip[0], 0) && near(clip[1], 0));
  e.pose.position.x = -.032f;
  jfgxr::eyeMatrices(e, {0, 0, 0}, 0, 35, v, p);
  auto left = multiply({0, 0, 35, 1}, v);
  e.pose.position.x = .032f;
  jfgxr::eyeMatrices(e, {0, 0, 0}, 0, 35, v, p);
  auto right = multiply({0, 0, 35, 1}, v);
  assert(near(left[0] - right[0], .064f * 35));
  e.pose.position = {.2f, .4f, -.3f};
  jfgxr::eyeMatrices(e, {10, 20, 30}, .7f, 35, v, p);
  auto pos = jfgxr::worldVector(.2f, .4f, -.3f, .7f);
  auto eye =
      multiply({10 + pos[0] * 35, 20 + pos[1] * 35, 30 + pos[2] * 35, 1}, v);
  assert(near(eye[0], 0) && near(eye[1], 0) && near(eye[2], 0));
  const std::array<float,3> headWorld{10+pos[0]*35,20+pos[1]*35,30+pos[2]*35};
  assert(jfgxr::sceneCamera(v,headWorld,35));
  jfgxr::WorldHistory history;
  XrPosef recordedHead=e.pose;
  jfgxr::World recordedWorld{10,20,30,.7f,35,true};
  history.add(recordedWorld,recordedHead);
  history.add({110,70,130,.7f,35,true},recordedHead); // simulation is already ahead
  const auto matched=history.match(v);
  assert(matched && near(matched->x,10) && near(matched->y,20) && near(matched->z,30));
  history.clear();
  assert(!history.match(v)); // no camera survives a level unload
  float independentCamera[16]{};
  independentCamera[0]=independentCamera[5]=independentCamera[10]=independentCamera[15]=1;
  assert(!jfgxr::sceneCamera(independentCamera,{400,80,-130},35));
  assert(!history.match(independentCamera)); // independent HUD camera is not a scene
  e.pose.orientation = {0, 0, std::sin(.3f), std::cos(.3f)};
  jfgxr::eyeMatrices(e, {0, 0, 0}, 0, 35, v, p);
  assert(std::abs(v[1]) > .5f); // roll must survive
  // A HUD centre has the disparity of a plane at 3m, rather than zero
  // disparity at infinity or the original near-clipped screen coordinates.
  XrPosef head{{0,0,0,1},{0,0,0}};
  e.pose.orientation = head.orientation;
  e.pose.position = {-.032f,0,0};
  float hud[16];
  jfgxr::hudMatrix(e,head,hud);
  auto hudLeft=multiply({0,0,.8f,1},hud);
  e.pose.position.x=.032f;
  jfgxr::hudMatrix(e,head,hud);
  auto hudRight=multiply({0,0,.8f,1},hud);
  assert(near(hudLeft[3],jfgxr::hudDistance) && near(hudRight[3],jfgxr::hudDistance));
  assert(near(hudLeft[0]/hudLeft[3]-hudRight[0]/hudRight[3], .064f/(jfgxr::hudDistance*std::tan(.8f))));
  auto corner=multiply({1,1,0,1},hud);
  assert(near(corner[1]/corner[3],jfgxr::uiHeight*.5f/(jfgxr::hudDistance*std::tan(.8f))));
  // The plane remains centred under yaw, pitch, roll and positional tracking.
  head.orientation={0,std::sin(.35f),0,std::cos(.35f)};
  head.position={.2f,1.6f,-.4f};
  e.pose=head;
  jfgxr::hudMatrix(e,head,hud);
  auto hudCentre=multiply({0,0,0,1},hud);
  assert(near(hudCentre[0],0) && near(hudCentre[1],0) && near(hudCentre[3],jfgxr::hudDistance));
  // Independently reconstruct the optical ray of each HUD corner. Use two
  // different asymmetric frusta, eye offsets, cant, head translation and roll;
  // a shared/centred frustum or swapped eye pose must fail these comparisons.
  head={jfgxr::calibrationRotation(13,27,9),{.3f,1.7f,-.5f}};
  for(int eyeIndex=0;eyeIndex<2;eyeIndex++) {
    XrView optical{XR_TYPE_VIEW};
    optical.pose=head;
    const auto offset=jfgxr::rotateVector(head.orientation,eyeIndex?.032f:-.032f,0,0);
    optical.pose.position.x+=offset[0]; optical.pose.position.y+=offset[1]; optical.pose.position.z+=offset[2];
    optical.pose.orientation=jfgxr::multiplyQuaternion(head.orientation,jfgxr::calibrationRotation(0,eyeIndex?3.f:-2.f,0));
    optical.fov=eyeIndex ? XrFovf{-.84f,.73f,.81f,-.64f} : XrFovf{-.71f,.85f,.77f,-.69f};
    jfgxr::hudMatrix(optical,head,hud);
    const auto q=optical.pose.orientation;
    const XrQuaternionf inverse{-q.x,-q.y,-q.z,q.w};
    for(const auto point:std::array<std::array<float,2>,3>{{{-1,-1},{0,0},{1,1}}}) {
      auto physical=jfgxr::rotateVector(head.orientation,point[0]*jfgxr::uiWidth*.5f,point[1]*jfgxr::uiHeight*.5f,-jfgxr::hudDistance);
      physical[0]+=head.position.x-optical.pose.position.x;
      physical[1]+=head.position.y-optical.pose.position.y;
      physical[2]+=head.position.z-optical.pose.position.z;
      const auto local=jfgxr::rotateVector(inverse,physical[0],physical[1],physical[2]);
      const float l=std::tan(optical.fov.angleLeft),r=std::tan(optical.fov.angleRight);
      const float b=std::tan(optical.fov.angleDown),t=std::tan(optical.fov.angleUp);
      const auto projected=multiply({point[0],point[1],0,1},hud);
      assert(projected[3]>0 && near(projected[3],-local[2]));
      assert(near(projected[0]/projected[3],(2*local[0]/-local[2]-r-l)/(r-l)));
      assert(near(projected[1]/projected[3],(2*local[1]/-local[2]-t-b)/(t-b)));
    }
  }
  // Attachment-space barrel direction must follow the controller, including
  // at a level's initial idle pose. Muzzle offset is a point, not an axis.
  float gun[16];
  const std::array<float,3> localMuzzle{36,-20,-35};
  XrPosef grip{{0,0,0,1},{.2f,1.2f,-.4f}};
  jfgxr::heldWeaponMatrix(grip,grip.orientation,{10,20,30},0,35,.26f,
      jfgxr::stockBarrelForward,gun,jfgxr::stockBarrelUp);
  auto barrel=multiply({2,-1,-1,0},gun);
  assert(near(barrel[0],0) && near(barrel[1],0) && barrel[2]>0);
  auto muzzle=multiply({localMuzzle[0],localMuzzle[1],localMuzzle[2],1},gun);
  // The barrel sits ABOVE the grip and in its vertical plane. The old
  // arbitrary "up" mapped this measured offset sideways by about 90 degrees.
  assert(muzzle[1]>gun[13]+3.f);
  assert(std::abs(muzzle[0]-gun[12])<.001f);
  // A shot origin is the actual native muzzle transformed by the draw matrix.
  for(int c=0;c<3;c++)assert(near(muzzle[c],gun[12+c]+36*gun[c]-20*gun[4+c]-35*gun[8+c]));
  assert(near(gun[12],3) && near(gun[13],62) && near(gun[14],44));
  for(const auto &rotation:std::array<XrQuaternionf,3>{jfgxr::calibrationRotation(0,40,0),
      jfgxr::calibrationRotation(30,15,-10),jfgxr::calibrationRotation(-45,-30,20)}) {
    float transformed[16];
    jfgxr::heldWeaponMatrix(grip,rotation,{10,20,30},.7f,35,.26f,
        jfgxr::stockBarrelForward,transformed,jfgxr::stockBarrelUp);
    const auto direction=jfgxr::normalized(jfgxr::matrixVector(jfgxr::stockBarrelForward,
        jfgxr::Matrix{transformed[0],transformed[1],transformed[2],0,
        transformed[4],transformed[5],transformed[6],0,transformed[8],transformed[9],transformed[10],0,0,0,0,1}));
    const auto xrForward=jfgxr::rotateVector(rotation,0,0,-1);
    const auto expected=jfgxr::worldVector(xrForward[0],xrForward[1],xrForward[2],.7f);
    for(int c=0;c<3;c++)assert(near(direction[c],expected[c]));
  }
  // Rotation must preserve scale and handedness (no mirrored weapon mesh).
  const float det=gun[0]*(gun[5]*gun[10]-gun[6]*gun[9])-gun[1]*(gun[4]*gun[10]-gun[6]*gun[8])+gun[2]*(gun[4]*gun[9]-gun[5]*gun[8]);
  assert(near(det,.26f*.26f*.26f));
  // A weapon's grip orientation must not depend on its muzzle position.
  jfgxr::Matrix native, nativeInverse;
  jfgxr::heldWeaponMatrix(grip,grip.orientation,{10,20,30},0,35,.26f,{0,0,-1},native.data());
  assert(jfgxr::inverseAffine(native,nativeInverse));
  const auto identity=jfgxr::multiplyMatrix(native,nativeInverse);
  for(int row=0;row<4;row++)for(int col=0;col<4;col++)assert(near(identity[row*4+col],row==col?1.f:0.f));
  jfgxr::Matrix controller;
  grip.position={.4f,1.5f,-.7f};
  jfgxr::heldWeaponMatrix(grip,jfgxr::calibrationRotation(15,30,-10),{10,20,30},.4f,35,.26f,{0,0,-1},controller.data());
  const auto relocated=jfgxr::multiplyMatrix(native,jfgxr::multiplyMatrix(nativeInverse,controller));
  for(int i=0;i<16;i++)assert(near(relocated[i],controller[i]));
  std::vector<jfgxr::Matrix> bones(25);
  for(auto &m:bones) {m[0]=m[5]=m[10]=.26f;m[15]=1;}
  const std::array<std::array<float,3>,6> armPoints={{{12,45,0},{18,35,-5},{20,30,-10},{-12,45,0},{-18,35,-5},{-20,30,-10}}};
  for(int bone=4;bone<=9;bone++)for(int c=0;c<3;c++)bones[bone][12+c]=armPoints[bone-4][c];
  bones[10][13]=60; // head
  bones[11][13]=38; // torso
  const auto arms=jfgxr::armBones(bones,6,{0,0,0},0);
  assert(arms.wrist[0]==9 && arms.wrist[1]==6);
  for(int bone=0;bone<25;bone++)assert(bool(arms.side[bone])==(bone>=4 && bone<=9));
  for(auto &m:bones)m[13]+=20; // jump: body moves, mask does not become a head/torso mask
  const auto jumping=jfgxr::armBones(bones,6,{0,20,0},0);
  assert(arms.side==jumping.side);
  assert(jfgxr::armBones(bones,200,{0,20,0},0).wrist[1]==-1);
  const std::vector<int> parents={-1,0,1,2,3,4,5,3,7,8,2,0,11,12,13,14,11,16,17,18,19};
  std::vector<jfgxr::Matrix> stockBones(bones.begin(),bones.begin()+21);
  // A loading/reposed torso sits exactly where geometric mirroring expects
  // the left wrist. Only the skeleton can distinguish it from an arm.
  stockBones[3]=stockBones[9];stockBones[9][12]+=100;
  const auto structural=jfgxr::armBones(stockBones,6,{0,20,0},0,parents);
  assert(structural.wrist[0]==9 && structural.wrist[1]==6 && !structural.side[3]);
  for(int bone=0;bone<21;bone++)assert(bool(structural.side[bone])==(bone>=4 && bone<=9));
  // Predicted hand transforms must remain fixed relative to the tracked eyes
  // when simulation translates/jumps; queued avatar matrices are irrelevant.
  jfgxr::HandBinding binding;
  const XrPosef predictedGrip{{0,0,0,1},{.3f,1.2f,-.6f}};
  XrView predictedEye{XR_TYPE_VIEW};
  predictedEye.pose={{0,0,0,1},{0,1.6f,0}};
  predictedEye.fov={-.8f,.8f,.8f,-.8f};
  const XrVector3f calibrationOffset{.025f,-.01f,-.035f};
  const auto calibration=jfgxr::calibrationRotation(5,10,-3);
  const auto handA=jfgxr::boundHandMatrix(binding,predictedGrip,predictedGrip.orientation,
      {10,20,30},0,35,calibrationOffset,calibration);
  jfgxr::eyeMatrices(predictedEye,{10,20,30},0,35,v,p);
  const auto relativeA=multiply({handA[12],handA[13],handA[14],1},v);
  const auto handB=jfgxr::boundHandMatrix(binding,predictedGrip,predictedGrip.orientation,
      {110,40,0},0,35,calibrationOffset,calibration);
  jfgxr::eyeMatrices(predictedEye,{110,40,0},0,35,v,p);
  const auto relativeB=multiply({handB[12],handB[13],handB[14],1},v);
  for(int c=0;c<4;c++)assert(near(relativeA[c],relativeB[c]));
  auto newerGrip=predictedGrip;newerGrip.position.x+=.1f;
  const auto handNew=jfgxr::boundHandMatrix(binding,newerGrip,newerGrip.orientation,
      {10,20,30},0,35,calibrationOffset,calibration);
  assert(near(handNew[12]-handA[12],-3.5f)); // newest tracking, not the old game tick
  assert(near(jfgxr::stickDeadzone(.1f), 0) &&
         near(jfgxr::stickDeadzone(1), 1) &&
         near(jfgxr::stickDeadzone(-1), -1));
  // One second of turning must have the same angle at 30 and 90 updates/sec.
  float turn30 = 0, turn90 = 0;
  for (int i = 0; i < 30; ++i)
    turn30 += jfgxr::smoothTurnDelta(1, 90, 1.f / 30);
  for (int i = 0; i < 90; ++i)
    turn90 += jfgxr::smoothTurnDelta(1, 90, 1.f / 90);
  assert(near(turn30, -3.14159265358979323846f / 2));
  assert(near(turn30, turn90));
  assert(near(jfgxr::smoothTurnDelta(.1f, 90, .05f), 0));
  assert(near(jfgxr::smoothTurnDelta(-1, 90, .05f),
              -jfgxr::smoothTurnDelta(1, 90, .05f)));
  assert(near(jfgxr::smoothTurnDelta(1, 90, 10),
              jfgxr::smoothTurnDelta(1, 90, .1f)));
  jfgxr::Posture posture;
  assert(!posture.update(true, true, 0, false, false));
  assert(posture.update(true, true, 0, true, false));
  assert(
      posture.update(true, true, 0, true, false)); // Held click toggles once.
  assert(near(posture.extraCameraDrop(0), .30f));
  assert(near(posture.extraCameraDrop(.2f), .10f));
  assert(near(posture.extraCameraDrop(.4f), 0));
  assert(posture.update(true, true, 0, false, false));
  assert(!posture.update(true, true, 0, true, false)); // Second click stands.
  assert(posture.update(true, true, .26f, false, false));
  assert(posture.update(true, true, .20f, false, false)); // Hysteresis.
  assert(!posture.update(true, true, .14f, false, false));
  assert(posture.update(true, true, .26f, true, false));  // Both sources.
  assert(!posture.update(true, true, .26f, false, true)); // Jump wins.
  assert(!posture.toggled);
  assert(
      posture.update(true, true, .26f, false, false)); // Still physically low.
  assert(!posture.update(true, false, .26f, false, false)); // Tracking lost.
  assert(!posture.update(false, true, 0, true, false));
  assert(!posture.update(true, true, 0, true, false)); // No focus-return click.
  std::puts("PASS: alternating framebuffer HUD identity, controller menu edges, native-arm mask/reposing, "
            "stereo transforms, HUD plane disparity/tracking, held weapon/muzzle, stick deadzone, smooth turning, crouch "
            "toggle/height hysteresis/camera offset/jump priority/focus");
}
