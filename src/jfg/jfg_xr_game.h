#pragma once
#include "recomp.h"
#include <cstdint>
namespace jfg {
bool jfg_xr_controls(recomp_context *);
bool jfg_xr_camera(recomp_context *);
bool jfg_xr_hide(uint32_t object);
void jfg_xr_aim(uint32_t data);
} // namespace jfg
