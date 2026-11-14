#include "jfg_game.h"
#include "jfg_xr_draw.h"
#include <cassert>
#include <cstdio>
#include <vector>
namespace jfg {
uint8_t *g_rdram=nullptr;
uint32_t call_game(void (*)(uint8_t*,recomp_context*),recomp_context*,std::initializer_list<uint32_t>) {return 0;}
}
namespace {
constexpr uint32_t cursor=0x80001000,commands=0x80002000;
int glyphCalls=0,windowCalls=0;
void emit(uint32_t w0,uint32_t w1) {
  const auto output=jfg::rd32(cursor);
  jfg::wr32(output,w0);jfg::wr32(output+4,w1);jfg::wr32(cursor,output+8);
}
}
extern "C" void jfg_wrap_func_8006FD98_70998(uint8_t*,recomp_context*);
extern "C" void jfg_wrap_func_overlay_7_00001F80_1EFD0A8(uint8_t*,recomp_context*);
extern "C" void jfg_wrap_fxScreenEffect(uint8_t*,recomp_context*);
extern "C" void jfg_wrap_screenDraw(uint8_t*,recomp_context*);
extern "C" void frontSetLanguage(uint8_t*,recomp_context*) {}
#define EMPTY_ORIGINAL(name) extern "C" void name##_orig(uint8_t*,recomp_context*) {}
EMPTY_ORIGINAL(initFront)
EMPTY_ORIGINAL(frontSetLanguage)
EMPTY_ORIGINAL(setLanguage)
EMPTY_ORIGINAL(subtitleStart)
EMPTY_ORIGINAL(fontInit)
EMPTY_ORIGINAL(func_80071388_71F88)
extern "C" void func_8006FD98_70998_orig(uint8_t*,recomp_context* ctx) {
  ++glyphCalls;
  emit(0xfd100000,0x80004000);emit(0xe4000000,0x00010001);
  ctx->r2=123;
}
extern "C" void func_overlay_7_00001F80_1EFD0A8_orig(uint8_t* memory,recomp_context* ctx) {
  ++windowCalls;
  emit(0xf6000000,0x00100010); // original dialogue frame/background
  jfg_wrap_func_8006FD98_70998(memory,ctx);
}
extern "C" void fxScreenEffect_orig(uint8_t*,recomp_context*) {emit(0xe4000000,0x00010001);}
extern "C" void screenDraw_orig(uint8_t*,recomp_context*) {emit(0xf6000000,0);}
int main() {
  std::vector<uint8_t> memory(0x10000);jfg::g_rdram=memory.data();
  recomp_context ctx{};ctx.r4=cursor;
  jfg::wr32(cursor,commands);
  jfg_wrap_func_overlay_7_00001F80_1EFD0A8(memory.data(),&ctx);
  assert(windowCalls==1 && glyphCalls==1 && ctx.r2==123);
  assert(jfg::rd32(cursor)==commands+24);
  assert(jfg::rd32(commands)==0xf6000000);
  assert(jfg::rd32(commands+8)==0xfd100000 && jfg::rd32(commands+16)==0xe4000000);
  // Subsequent HUD text retains its native texture upload and glyph draw too.
  jfg_wrap_func_8006FD98_70998(memory.data(),&ctx);
  assert(glyphCalls==2 && jfg::rd32(cursor)==commands+40);
  assert(jfg::rd32(commands+24)==0xfd100000 && jfg::rd32(commands+32)==0xe4000000);
  // No hidden/replaced text markers are inserted into either command stream.
  for(uint32_t address=commands;address<commands+40;address+=8)
    assert(jfg::rd32(address)!=jfgxr::drawModeMarker);
  jfg::wr32(cursor,commands);
  {
    jfg::XrDrawScope parent(memory.data(),cursor,jfgxr::DrawMode::ScreenEffect);
    jfg_wrap_fxScreenEffect(memory.data(),&ctx);
    assert(jfg::rd32(jfg::rd32(cursor)-4)==1);
    jfg_wrap_screenDraw(memory.data(),&ctx);
  }
  assert(jfg::rd32(jfg::rd32(cursor)-4)==0);
  std::puts("PASS: original dialogue frame/background/glyph commands and subsequent HUD text remain unchanged; no text suppression markers");
}
