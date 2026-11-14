#include "xr_bitmap.h"
#include "xr_draw.h"
#include "xr_ui.h"
#include <cassert>
#include <cstdio>
#include <fstream>
int main(int argc,char **argv) {
  if(argc!=3)return 2;
  using Mode=jfgxr::DrawMode;
  assert(jfgxr::nativeUiSampling(true,true,false,false));
  assert(!jfgxr::nativeUiSampling(false,true,false,false));
  assert(!jfgxr::nativeUiSampling(true,false,false,false));
  assert(!jfgxr::nativeUiSampling(true,true,true,false));
  assert(!jfgxr::nativeUiSampling(true,true,false,true));
  assert(jfgxr::hudDraw(false,true,false,false,Mode::Normal));
  assert(!jfgxr::hudDraw(false,true,false,false,Mode::ScreenEffect));
  assert(!jfgxr::hudDraw(true,true,false,false,Mode::Normal));
  assert(!jfgxr::hudDraw(false,false,true,true,Mode::Normal));
  assert(jfgxr::hudDistance==3.5f && jfgxr::uiDistance==3.f);
  assert(jfgxr::hudWidth>=4096 && jfgxr::hudHeight>=3072);
  // Native glyphs, icons and frames all remain in the original UI pass.
  jfgxr::BitmapFont font;
  assert(!font.load("/nonexistent/jfg-menu-font.bdf"));
  assert(font.load(argv[1]));
  const std::vector<std::string> lines={"Turn mode: Snap","Snap angle: 30 degrees","Smooth speed: 90 deg/s",
      "Weapon left/right: 0 mm","Weapon down/up: 0 mm","Weapon back/forward: 0 mm",
      "Weapon pitch: 0 degrees","Weapon yaw: 0 degrees","Weapon roll: 0 degrees",
      "Reset weapon alignment","Game pause menu","Resume","Quit game"};
  auto pixels=jfgxr::menuBitmap(font,lines,0);
  assert(pixels.size()==1024*768);
  size_t textPixels=0;
  for(auto pixel:pixels)if(pixel==0xfff1e6da)++textPixels;
  assert(textPixels>10000 && textPixels<200000);
  auto moved=jfgxr::menuBitmap(font,lines,11);
  assert(pixels!=moved); // selection is visibly updated
  std::ofstream image(argv[2],std::ios::binary);
  image<<"P6\n1024 768\n255\n";
  for(auto p:pixels) {
    const char rgb[]={char(p&255),char((p>>8)&255),char((p>>16)&255)};
    image.write(rgb,3);
  }
  assert(image.good());
  std::puts("PASS: original UI glyphs/icons/frames retained; high-resolution intermediate scale, HUD at 3.5m, existing XR settings at 3m");
}
