#pragma once
#include "jfg_game.h"
#include "xr_draw.h"
namespace jfg {
inline thread_local jfgxr::DrawMode gameDrawMode=jfgxr::DrawMode::Normal;
class XrDrawScope {
  uint32_t cursor;
  jfgxr::DrawMode previous;
  void emit(jfgxr::DrawMode mode) {
    if(!valid_ptr(cursor))return;
    const uint32_t destination=rd32(cursor);
    if(!valid_ptr(destination) || !valid_ptr(destination+7))return;
    wr32(destination,jfgxr::drawModeMarker);
    wr32(destination+4,uint32_t(mode));
    wr32(cursor,destination+8);
  }
public:
  XrDrawScope(uint8_t *memory,uint32_t displayList,jfgxr::DrawMode mode)
      :cursor(displayList),previous(gameDrawMode) {
    g_rdram=memory;emit(mode);gameDrawMode=mode;
  }
  ~XrDrawScope() {emit(previous);gameDrawMode=previous;}
};
}
