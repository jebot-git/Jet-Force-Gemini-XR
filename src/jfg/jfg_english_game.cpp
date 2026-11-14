#include "jfg_game.h"
#include "jfg_hooks.h"
#include "jfg_xr_draw.h"
#include <cstdio>
namespace {
constexpr uint32_t language=0x800ff386,frontText=0x800a51c8;
}
extern "C" void frontSetLanguage(uint8_t*,recomp_context*);
extern "C" void jfg_locale_start() { std::fprintf(stderr,"[JFG XR] English only\n"); }
extern "C" void jfg_locale_tick(recomp_context *ctx) {
  if(jfg::g_rdram && jfg::rd32(frontText) && jfg::rd8(language)!=0)
    jfg::call_game(frontSetLanguage,ctx,{0});
}
extern "C" void jfg_locale_alloc(recomp_context*) {}
JFG_WRAP(initFront) { initFront_orig(rdram,ctx); }
JFG_WRAP(frontSetLanguage) {
  jfg::g_rdram=rdram;ctx->r4=0;frontSetLanguage_orig(rdram,ctx);jfg::wr8(language,0);
}
JFG_WRAP(setLanguage) {
  jfg::g_rdram=rdram;ctx->r4=0;setLanguage_orig(rdram,ctx);jfg::wr8(language,0);
}
JFG_WRAP(subtitleStart) { subtitleStart_orig(rdram,ctx); }
JFG_WRAP(fontInit) { fontInit_orig(rdram,ctx); }
// Keep every original glyph, window, frame, colour and layout command.
// The renderer increases the native UI target resolution; no text is replaced.
JFG_WRAP(func_8006FD98_70998) { func_8006FD98_70998_orig(rdram,ctx); }
JFG_WRAP(func_overlay_7_00001F80_1EFD0A8) { func_overlay_7_00001F80_1EFD0A8_orig(rdram,ctx); }
JFG_WRAP(func_80071388_71F88) { func_80071388_71F88_orig(rdram,ctx); }
// Screen effects cover each eye rather than being compressed onto the HUD.
JFG_WRAP(fxScreenEffect) {
  jfg::XrDrawScope scope(rdram,uint32_t(ctx->r4),jfgxr::DrawMode::ScreenEffect);
  fxScreenEffect_orig(rdram,ctx);
}
JFG_WRAP(screenDraw) {
  jfg::XrDrawScope scope(rdram,uint32_t(ctx->r4),jfgxr::DrawMode::ScreenEffect);
  screenDraw_orig(rdram,ctx);
}
