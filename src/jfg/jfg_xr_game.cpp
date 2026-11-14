#include "jfg_xr_game.h"
#include "jfg_fields.h"
#include "jfg_game.h"
#include "jfg_port.h"
#include "jfg_options.h"
#include "jfg_xr.h"
#include "xr_math.h"
#include "xr_posture.h"
#include "xr_menu.h"
#include "xr_rig.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
namespace jfg {
namespace {
float yaw = 0, units = 35;
bool initialized = false, turnHeld = false;
uint16_t previousButtons = 0;
uint32_t hero = 0;
XrVector3f origin{};
float anchor[3]{};
jfgxr::Input tracked;
jfgxr::Posture posture;
jfgxr::MenuInput xrMenu;
bool gamePauseRequested = false;
bool releaseGameInput = false;
std::array<float,3> gunMuzzle{36,-20,-35};
float gunScale = .26f;
std::array<float,3> gunForward=jfgxr::stockBarrelForward, gunUp=jfgxr::stockBarrelUp;
uint32_t posedObject=0, posedInstance=0, posedModel=0;
struct Rig {
  uint32_t object=0, model=0;
  int count=0;
  jfgxr::ArmBones arms;
  std::array<float,3> forward[2], up[2];
} rig;
float dead(float v) { return jfgxr::stickDeadzone(v); }
std::array<float, 3> rotate(float x, float y, float z) {
  return jfgxr::worldVector(x, y, z, yaw);
}

std::array<float, 3> forward(const XrQuaternionf &q) {
  return rotate(-2 * (q.x * q.z + q.w * q.y), -2 * (q.y * q.z - q.w * q.x),
                -(1 - 2 * (q.x * q.x + q.y * q.y)));
}
std::array<float, 3> point(const XrVector3f &p) {
  auto v = rotate(p.x, p.y, p.z);
  return {anchor[0] + v[0] * units, anchor[1] + v[1] * units,
          anchor[2] + v[2] * units};
}
void refreshAnchor() {
  if(!hero || !initialized || !tracked.headValid)return;
  const auto off=rotate(origin.x,origin.y,origin.z);
  const float drop=origin.y-tracked.head.position.y;
  anchor[0]=Obj::PosX::get(hero)-off[0]*units;
  anchor[1]=Obj::PosY::get(hero)+60-off[1]*units-posture.extraCameraDrop(drop)*units;
  anchor[2]=Obj::PosZ::get(hero)-off[2]*units;
}
void handMatrix(int hand, float scale, const std::array<float,3> &nativeForward,
                const std::array<float,3> &nativeUp, float *matrix) {
  jfgxr::HandBinding binding;
  binding.hand=hand;binding.scale=scale;binding.forward=nativeForward;binding.up=nativeUp;
  const auto result=jfgxr::boundHandMatrix(binding,tracked.grip[hand],tracked.aim[hand].orientation,
      {anchor[0],anchor[1],anchor[2]},yaw,units,
      {g.xr_weapon_x*.001f,g.xr_weapon_y*.001f,-g.xr_weapon_z*.001f},
      jfgxr::calibrationRotation(float(g.xr_weapon_pitch),float(g.xr_weapon_yaw),float(g.xr_weapon_roll)));
  std::copy(result.begin(),result.end(),matrix);
}
std::array<float,3> muzzlePoint() {
  float matrix[16];
  handMatrix(1,gunScale,gunForward,gunUp,matrix);
  std::array<float,3> p{};
  for(int c=0;c<3;c++)p[c]=matrix[12+c]+gunMuzzle[0]*matrix[c]+
      gunMuzzle[1]*matrix[4+c]+gunMuzzle[2]*matrix[8+c];
  return p;
}
void updateMenu() {
  const bool wasOpen=xrMenu.open;
  const int edit = xrMenu.update(tracked.focused, tracked.buttons & 0x1000,
      tracked.buttons & 0x2000, tracked.moveX, tracked.moveY, 13);
  if (xrMenu.open && edit) {
    const int direction = edit < 0 ? -1 : 1;
    if (xrMenu.selected == 0) g.xr_turn_mode = (g.xr_turn_mode + direction + 3) % 3;
    else if (xrMenu.selected == 1) apply_option("xr_snap_angle",g.xr_snap_angle+direction*15);
    else if (xrMenu.selected == 2) apply_option("xr_turn_speed",g.xr_turn_speed+direction*15);
    else if (xrMenu.selected < 9) {
      const char *keys[]={"xr_weapon_x","xr_weapon_y","xr_weapon_z","xr_weapon_pitch","xr_weapon_yaw","xr_weapon_roll"};
      const int values[]={g.xr_weapon_x,g.xr_weapon_y,g.xr_weapon_z,g.xr_weapon_pitch,g.xr_weapon_yaw,g.xr_weapon_roll};
      const int row=xrMenu.selected-3;
      apply_option(keys[row],values[row]+direction*(row<3?5:1));
    } else if (xrMenu.selected == 9 && edit == 2) {
      g.xr_weapon_x=g.xr_weapon_y=g.xr_weapon_z=0;
      g.xr_weapon_pitch=g.xr_weapon_yaw=g.xr_weapon_roll=0;
    } else if (xrMenu.selected == 10 && edit == 2) {
      xrMenu.open=false; gamePauseRequested=true;
    } else if (xrMenu.selected == 11 && edit == 2) xrMenu.open=false;
    else if(xrMenu.selected==12 && edit==2)jfgxr::requestExit();
    save_options();
  }
  if(wasOpen && !xrMenu.open) releaseGameInput=true;
  if(releaseGameInput && !(tracked.buttons&~0x1000u) &&
      std::abs(tracked.moveX)<.35f && std::abs(tracked.moveY)<.35f && std::abs(tracked.turnX)<.35f)
    releaseGameInput=false;
  static const char *modes[]={"Snap","Smooth","Off"};
  const auto number=[](int n,const char *suffix){return std::to_string(n)+suffix;};
  jfgxr::setMenu(xrMenu.open,{
      std::string("Turn mode: ")+modes[g.xr_turn_mode],
      "Snap angle: "+number(g.xr_snap_angle," degrees"),
      "Smooth speed: "+number(g.xr_turn_speed," deg/s"),
      "Weapon left/right: "+number(g.xr_weapon_x," mm"),
      "Weapon down/up: "+number(g.xr_weapon_y," mm"),
      "Weapon back/forward: "+number(g.xr_weapon_z," mm"),
      "Weapon pitch: "+number(g.xr_weapon_pitch," degrees"),
      "Weapon yaw: "+number(g.xr_weapon_yaw," degrees"),
      "Weapon roll: "+number(g.xr_weapon_roll," degrees"),
      "Reset weapon alignment", "Game pause menu", "Resume", "Quit game"},xrMenu.selected);
  jfgxr::setCalibration(g.xr_weapon_x,g.xr_weapon_y,g.xr_weapon_z,
      g.xr_weapon_pitch,g.xr_weapon_yaw,g.xr_weapon_roll);
}
} // namespace
bool jfg_xr_menu_open() { return jfgxr::enabled() && xrMenu.open; }
void jfg_xr_level_change() {
  // The native loader frees and reuses object/model memory. Do not let an
  // attachment or camera from the previous level follow those addresses.
  hero=posedObject=posedInstance=posedModel=0;
  rig={};initialized=false;
  gunScale=.26f;gunMuzzle={36,-20,-35};
  gunForward=jfgxr::stockBarrelForward;gunUp=jfgxr::stockBarrelUp;
  g_cam.player=0;g_cam.active=false;g_cam.shoulder=false;g_cam.overridden=false;
  jfgxr::resetScene();
}
bool jfg_xr_is_gun(uint32_t object, uint32_t matrix) {
  return jfgxr::enabled() && hero && object==hero && matrix==Obj::Data::get(hero)+0x78;
}
bool jfg_xr_controls(recomp_context *ctx) {
  using Clock = std::chrono::steady_clock;
  const auto now = Clock::now();
  static auto lastTurnTime = now;
  const float turnSeconds =
      std::chrono::duration<float>(now - lastTurnTime).count();
  lastTurnTime = now;
  load_options();
  tracked = jfgxr::input();
  if (!tracked.active) {
    xrMenu.open=false;
    jfgxr::setMenu(false,{},0);
    jfgxr::setWorld(anchor[0],anchor[1],anchor[2],yaw,units,false,tracked.head);
    posture.update(false, false, 0, tracked.crouchClick, false);
    hero=0;g_cam.player=0;g_cam.active=false;g_cam.shoulder=false;
    return true;
  }
  const bool game = g_rdram && rd32(ADDR_mainGameMode) == 0 && !in_cutscene() &&
                    rd8(ADDR_pauseMode) == 0 && rd8(ADDR_frontEndMode) != 17 &&
                    !menu_paused();
  hero = game ? player_object(ctx) : 0;
  updateMenu();
  g_cam.player = hero;
  g_cam.active = false;
  g_cam.shoulder = false;
  const uint32_t cam =
      ADDR_cameraActorArray + Camera::Size * (rd32(ADDR_cameraNo) & 3);
  if (!initialized && hero && tracked.headValid) {
    origin = tracked.head.position;
    yaw = -angle_from_s16(rd16(cam));
    if (const char *e = std::getenv("JFG_XR_UNITS_PER_METER"))
      units = std::clamp(std::strtof(e, nullptr), 10.f, 150.f);
    initialized = true;
  }
  const bool canTurn = tracked.focused && !xrMenu.open && !releaseGameInput && hero && tracked.headValid && initialized;
  if (g.xr_turn_mode == 1) {
    if (canTurn)
      yaw += jfgxr::smoothTurnDelta(tracked.turnX, float(g.xr_turn_speed), turnSeconds);
    turnHeld = false;
  } else if (g.xr_turn_mode == 0 && canTurn && std::abs(tracked.turnX) > .7f && !turnHeld) {
    yaw -= std::copysign(float(g.xr_snap_angle) * PI_F / 180, tracked.turnX);
    turnHeld = true;
  }
  if (!canTurn || g.xr_turn_mode == 2 || std::abs(tracked.turnX) < .35f)
    turnHeld = false;
  uint16_t b = tracked.focused && !xrMenu.open && !releaseGameInput ? (tracked.buttons & ~0x1000u) : 0;
  if (gamePauseRequested) { b = 0x1000; gamePauseRequested=false; }
  const bool postureActive = tracked.focused && !xrMenu.open && !releaseGameInput && hero && initialized;
  const float heightDrop =
      tracked.headValid ? origin.y - tracked.head.position.y : 0.f;
  const bool jump = postureActive && (tracked.jumpHeld || (b & 0x0008));
  if (posture.update(postureActive, tracked.headValid, heightDrop,
                     tracked.crouchClick, jump))
    b |= 0x0004; // Original C-down: crouch.
  if (postureActive && tracked.jumpHeld)
    b |= 0x0008; // Original C-up: jump / sustained jetpack input.
  float x = dead(tracked.moveX), y = dead(tracked.moveY);
  if (xrMenu.open || releaseGameInput) x=y=0;
  // Menus: trigger confirms, action cancels; face buttons keep A/B navigation.
  if (!hero) {
    if (b & 0x2000)
      b = (b & ~0x2000) | 0x8000;
    if (b & 8)
      b = (b & ~8) | 0x4000;
  }
  if (hero && tracked.headValid) {
    auto f = forward(tracked.head.orientation);
    float h = std::atan2(f[0], f[2]);
    float delta = h + angle_from_s16(rd16(cam));
    float c = std::cos(delta), s = std::sin(delta), ox = x;
    x = c * x - s * y;
    y = s * ox + c * y;
  }
  const uint32_t i = rd8(ADDR_sPlayerID) & 3u,
                 pad = ADDR_sControllerCurrData + 6 * i;
  wr16(pad, b);
  wr8(pad + 2, (uint8_t)(int8_t)std::lround(std::clamp(x, -1.f, 1.f) * 80));
  wr8(pad + 3, (uint8_t)(int8_t)std::lround(std::clamp(y, -1.f, 1.f) * 80));
  wr16(ADDR_buttonsPressed + 2 * i, b & ~previousButtons);
  previousButtons = b;
  if (hero) {
    refreshAnchor();
    const uint32_t data = Obj::Data::get(hero);
    if (valid_ptr(data))
      jfg_xr_aim(data);
  }
  if(!hero || !tracked.headValid) jfgxr::setWorld(anchor[0], anchor[1], anchor[2], yaw, units,false,tracked.head);
  return true;
}
void jfg_xr_aim(uint32_t data) {
  if (!hero || !tracked.active || !tracked.handValid[1] || !valid_ptr(data) ||
      data != Obj::Data::get(hero))
    return;
  refreshAnchor();
  auto f = forward(jfgxr::multiplyQuaternion(tracked.aim[1].orientation,
      jfgxr::calibrationRotation(float(g.xr_weapon_pitch),float(g.xr_weapon_yaw),float(g.xr_weapon_roll))));
  const float h = std::atan2(f[0], f[2]);
  const float pitch = std::asin(std::clamp(f[1], -1.f, 1.f));
  uint16_t ay = angle_to_s16(h - PI_F), ap = angle_to_s16(pitch);
  wr16(data + 0x1C6, ay);
  wr16(data + 0x1CA, ay);
  wr16(data + 0x1CC, ap);
  auto p = muzzlePoint();
  for (int i = 0; i < 3; i++)
    wrf(data + 0x544 + i * 4, p[i]);
  PlayerData::TargetLock::set(data, 0);
}
bool jfg_xr_camera(recomp_context *ctx) {
  if (!jfgxr::enabled() || !initialized || !hero || !tracked.headValid ||
      in_cutscene())
    return false;
  const uint32_t cam =
      ADDR_cameraActorArray + Camera::Size * (rd32(ADDR_cameraNo) & 3);
  for (uint32_t i = 0; i < Camera::Size; i += 4) {
    uint32_t v = rd32(cam + i);
    std::memcpy(&g_cam.saved[i], &v, 4);
  }
  g_cam.camera_addr = cam;
  g_cam.overridden = true;
  // The controls hook runs before simulation. Refresh from the final position
  // here so jump/locomotion cannot leave the camera a tick behind the model.
  refreshAnchor();
  jfgxr::setWorld(anchor[0],anchor[1],anchor[2],yaw,units,true,tracked.head);
  auto p = point(tracked.head.position), f = forward(tracked.head.orientation);
  wrf(cam + 0x0C, p[0]);
  wrf(cam + 0x10, p[1]);
  wrf(cam + 0x14, p[2]);
  wr16(cam, angle_to_s16(-std::atan2(f[0], f[2])));
  wr16(cam + 2, angle_to_s16(-std::asin(std::clamp(f[1], -1.f, 1.f))));
  wr16(cam + 4, 0);
  for (int i = 0; i < 3; i++)
    wrf(cam + 0x30 + i * 4, 0);
  return true;
}
bool jfg_xr_gun_matrix(uint32_t object, uint32_t matrix, int slot) {
  if (!jfgxr::enabled() || object != hero || !hero || !tracked.active ||
      !tracked.handValid[1] || !valid_ptr(matrix)) return false;
  const uint32_t children=rd32(hero+0x60);
  const uint32_t gun=valid_ptr(children)?rd32(children+4):0;
  const uint32_t models=valid_ptr(gun)?Obj::Models::get(gun):0;
  if (valid_ptr(models)) {
    const uint32_t instance=rd32(models+4*std::clamp(slot,0,8));
    const uint32_t muzzle=valid_ptr(instance)?rd32(instance+0x68):0;
    if (valid_ptr(muzzle)) {
      std::array<float,3> candidate{rdf(muzzle),rdf(muzzle+4),rdf(muzzle+8)};
      const float length=std::hypot(candidate[0],candidate[1],candidate[2]);
      if(std::isfinite(length) && length>.01f && length<10000)gunMuzzle=candidate;
    }
  }
  const float scale=std::hypot(rdf(matrix),rdf(matrix+4),rdf(matrix+8));
  if(std::isfinite(scale) && scale>.001f && scale<10)gunScale=scale;
  float result[16];
  handMatrix(1,gunScale,gunForward,gunUp,result);
  for(int i=0;i<16;i++)wrf(matrix+4*i,result[i]);
  return true;
}
void poseAvatar(uint32_t bones, int count) {
  if(!jfgxr::enabled() || !hero || posedObject!=hero || !valid_ptr(bones) || count<1 || count>128)return;
  const int modelIndex=Obj::ModelIndex::get(hero);
  uint32_t id=(((hero*2654435761u)^Obj::Def::get(hero))^(uint32_t(modelIndex)*2246822519u))|1u;
  if(id==0xffffffffu)id=1;
  std::vector<uint8_t> visible(count,0);
  const uint32_t attachment=valid_ptr(posedModel)?rd32(posedModel+0x30):0;
  const int wrist=valid_ptr(attachment)?int(rd16(attachment+2)):-1;
  if(wrist<2 || wrist>=count) { jfgxr::setAvatarParts(id,visible);return; }
  std::vector<jfgxr::Matrix> matrices(count);
  for(int bone=0;bone<count;bone++) {
    for(int i=0;i<16;i++)matrices[bone][i]=rdf(bones+bone*64+i*4);
    matrices[bone][3]=matrices[bone][7]=matrices[bone][11]=0;
    matrices[bone][15]=1;
  }
  const auto &right=matrices[wrist];
  gunScale=std::hypot(right[0],right[1],right[2]);
  if(!std::isfinite(gunScale) || gunScale<.001f || gunScale>10) {
    gunScale=.26f;jfgxr::setAvatarParts(id,visible);return;
  }
  if(rig.object!=hero || rig.model!=posedModel || rig.count!=count) {
    std::vector<int> parents;
    const uint32_t skeleton=rd32(posedModel+0x54);
    if(!valid_ptr(skeleton) || !valid_ptr(skeleton+16*count-1)) {
      // Loader metadata is not ready. Hide the local body and retry next pose.
      rig={};jfgxr::setAvatarParts(id,visible);jfgxr::setHandBindings({});return;
    }
    for(int bone=0;bone<count;bone++)parents.push_back(int(int8_t(rd8(skeleton+16*bone))));
    rig={};rig.object=hero;rig.model=posedModel;rig.count=count;
    for(int arm=0;arm<2;arm++) {rig.forward[arm]={0,0,-1};rig.up[arm]={0,1,0};}
    rig.arms=jfgxr::armBones(matrices,wrist,{Obj::PosX::get(hero),Obj::PosY::get(hero),Obj::PosZ::get(hero)},
                            angle_from_s16(Obj::Yaw::get(hero)),parents);
    rig.forward[1]=jfgxr::stockBarrelForward;
    rig.up[1]=jfgxr::stockBarrelUp;
    // Mirror the right hand's attachment axes into the left wrist. Both
    // hands then use the same physical aim convention, including at rest.
    const float bodyYaw=angle_from_s16(Obj::Yaw::get(hero));
    const std::array<float,3> lateral{std::cos(bodyYaw),0,-std::sin(bodyYaw)};
    auto mirrored=[&](const std::array<float,3> &axis) {
      auto world=jfgxr::matrixVector(axis,right);
      const float d=world[0]*lateral[0]+world[2]*lateral[2];
      for(int c=0;c<3;c++)world[c]-=2*d*lateral[c];
      return world;
    };
    if(rig.arms.wrist[0]>=0) {
      jfgxr::Matrix inverse;
      if(jfgxr::inverseAffine(matrices[rig.arms.wrist[0]],inverse)) {
        rig.forward[0]=jfgxr::normalized(jfgxr::matrixVector(mirrored(rig.forward[1]),inverse));
        rig.up[0]=jfgxr::normalized(jfgxr::matrixVector(mirrored(rig.up[1]),inverse));
      }
    }
    gunForward=rig.forward[1];gunUp=rig.up[1];
    std::fprintf(stderr,"[XR] first-person rig: %d bones, left wrist %d, right wrist %d\n",
        count,rig.arms.wrist[0],rig.arms.wrist[1]);
    std::fprintf(stderr,"[XR] weapon attachment axes: barrel 2,-1,-1; grip-to-barrel up -38,7,-83\n");
  }
  std::vector<jfgxr::HandBinding> bindings;
  jfgxr::HandBinding gunBinding;
  gunBinding.id=jfgxr::trackedWeaponId;gunBinding.hand=1;
  gunBinding.scale=gunScale;gunBinding.forward=gunForward;gunBinding.up=gunUp;
  bindings.push_back(gunBinding);
  for(int arm=0;arm<2;arm++) {
    const int hand=rig.arms.wrist[arm];
    if(hand<0 || hand>=count || !tracked.handValid[arm])continue;
    jfgxr::Matrix inverse,target;
    if(!jfgxr::inverseAffine(matrices[hand],inverse))continue;
    const float scale=std::hypot(matrices[hand][0],matrices[hand][1],matrices[hand][2]);
    handMatrix(arm,scale,rig.forward[arm],rig.up[arm],target.data());
    const auto delta=jfgxr::multiplyMatrix(inverse,target);
    for(int bone=0;bone<count;bone++)if(rig.arms.side[bone]==arm+1) {
      const auto posed=jfgxr::multiplyMatrix(matrices[bone],delta);
      for(int i=0;i<16;i++)wrf(bones+bone*64+i*4,posed[i]);
      visible[bone]=1;
      jfgxr::HandBinding binding;
      binding.id=jfgxr::boneId(id,bone);binding.hand=arm;binding.scale=scale;
      binding.forward=rig.forward[arm];binding.up=rig.up[arm];
      binding.local=jfgxr::multiplyMatrix(matrices[bone],inverse);
      bindings.push_back(binding);
    }
  }
  jfgxr::setAvatarParts(id,visible);
  jfgxr::setHandBindings(bindings);
}
} // namespace jfg

#include "jfg_hooks.h"
// The barrel API is shared by the original weapons: supply the tracked aim
// origin.
JFG_WRAP(controlGetGunBarrelPos) {
  using namespace jfg;
  if (jfgxr::enabled() && hero && (uint32_t)ctx->r4 == hero &&
      tracked.handValid[1]) {
    refreshAnchor();
    const auto p = muzzlePoint();
    const uint32_t out[3] = {(uint32_t)ctx->r6, (uint32_t)ctx->r7,
                             rd32((uint32_t)ctx->r29 + 0x10)};
    for (int i = 0; i < 3; i++)
      if (valid_ptr(out[i]))
        wrf(out[i], p[i]);
    return;
  }
  controlGetGunBarrelPos_orig(rdram, ctx);
}
JFG_WRAP(controlUpdateWeapon) {
  using namespace jfg;
  if (jfgxr::enabled() && (uint32_t)ctx->r4 == hero)
    jfg_xr_aim((uint32_t)ctx->r5);
  controlUpdateWeapon_orig(rdram, ctx);
}
JFG_WRAP(modGenAnimMatrices) {
  using namespace jfg;
  const uint32_t previousObject=posedObject, previousInstance=posedInstance, previousModel=posedModel;
  posedInstance=(uint32_t)ctx->r4; posedModel=(uint32_t)ctx->r5; posedObject=(uint32_t)ctx->r6;
  modGenAnimMatrices_orig(rdram,ctx);
  posedObject=previousObject; posedInstance=previousInstance; posedModel=previousModel;
}
JFG_WRAP(camConvertMatrixList) {
  jfg::g_rdram=rdram;
  jfg::poseAvatar((uint32_t)ctx->r4,(int)ctx->r5);
  camConvertMatrixList_orig(rdram,ctx);
}
