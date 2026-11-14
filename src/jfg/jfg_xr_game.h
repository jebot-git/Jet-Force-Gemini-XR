#pragma once
#include "recomp.h"
#include <cstdint>
namespace jfg {
bool jfg_xr_controls(recomp_context *);
bool jfg_xr_camera(recomp_context *);
void jfg_xr_aim(uint32_t data);
bool jfg_xr_gun_matrix(uint32_t object, uint32_t matrix, int slot);
bool jfg_xr_menu_open();
void jfg_xr_level_change();
bool jfg_xr_is_gun(uint32_t object, uint32_t matrix);
} // namespace jfg
