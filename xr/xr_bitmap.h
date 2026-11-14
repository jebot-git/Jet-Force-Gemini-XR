#pragma once
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
namespace jfgxr {
// BDF bitmap glyphs, without outline-font substitution or font rasterization.
class BitmapFont {
  struct Glyph { int width=0, height=0, x=0, y=0, advance=8; std::vector<uint32_t> rows; };
  std::array<Glyph,128> glyphs;
public:
  bool load(const std::string &path) {
    std::ifstream in(path);
    if (!in) return false;
    glyphs={};
    std::array<bool,128> seen{};
    unsigned asciiCount=0;
    std::string line, word; Glyph glyph; int encoding=-1; bool bitmap=false;
    while (std::getline(in,line)) {
      std::istringstream fields(line); fields >> word;
      if (word=="STARTCHAR") { glyph={}; encoding=-1; bitmap=false; }
      else if (word=="ENCODING") fields >> encoding;
      else if (word=="DWIDTH") fields >> glyph.advance;
      else if (word=="BBX") fields >> glyph.width >> glyph.height >> glyph.x >> glyph.y;
      else if (word=="BITMAP") bitmap=true;
      else if (word=="ENDCHAR") {
        if(encoding>=32 && encoding<127 && glyph.width>=0 && glyph.width<=32 &&
            glyph.height>=0 && glyph.height<=32 && glyph.rows.size()==size_t(glyph.height)) {
          glyphs[encoding]=glyph;
          if(!seen[encoding]) { seen[encoding]=true;++asciiCount; }
          if(asciiCount==95)return true;
        }
        bitmap=false;
      } else if(bitmap && encoding>=32 && encoding<127 && glyph.width<=32 && glyph.height<=32) {
        try { glyph.rows.push_back(uint32_t(std::stoul(word,nullptr,16))); }
        catch(...) { return false; }
      }
    }
    return !glyphs['A'].rows.empty() && !glyphs['0'].rows.empty();
  }
  void text(std::vector<uint32_t> &pixels, unsigned width, unsigned height,
            int x, int baseline, const std::string &value, uint32_t colour, int scale=2) const {
    for(unsigned char c:value) {
      if(c>=128) c='?';
      const auto &g=glyphs[c];
      const int bits=((g.width+7)/8)*8;
      for(int y=0;y<g.height;y++)for(int bx=0;bx<g.width;bx++) {
        if(!(g.rows[y] & (uint32_t(1) << (bits-1-bx)))) continue;
        for(int dy=0;dy<scale;dy++)for(int dx=0;dx<scale;dx++) {
          int px=x+(g.x+bx)*scale+dx;
          int py=baseline-(g.y+g.height-y)*scale+dy;
          if(px>=0 && py>=0 && px<int(width) && py<int(height))pixels[py*width+px]=colour;
        }
      }
      x+=g.advance*scale;
    }
  }
};
inline std::vector<uint32_t> menuBitmap(const BitmapFont &font, const std::vector<std::string> &lines, int selected) {
  constexpr unsigned width=1024,height=768;
  std::vector<uint32_t> pixels(width*height,0xff211a15);
  font.text(pixels,width,height,48,65,"JET FORCE GEMINI - XR SETTINGS",0xfff1e6da);
  for(size_t i=0;i<lines.size();i++) {
    const int top=92+int(i)*43;
    if(top+40>680)break;
    if(int(i)==selected)for(int y=top;y<top+40;y++)for(int x=32;x<992;x++)pixels[y*width+x]=0xff6b4823;
    font.text(pixels,width,height,48,top+30,(int(i)==selected?"> ":"  ")+lines[i],0xfff1e6da);
  }
  font.text(pixels,width,height,48,729,"Left stick: select / adjust   Trigger: select",0xffc5b9ad);
  font.text(pixels,width,height,48,758,"Menu: resume   Changes are saved immediately",0xffc5b9ad,1);
  return pixels;
}
} // namespace jfgxr
