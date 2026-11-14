// Compatibility for engine/mod APIs formerly owned by desktop UI modules.
// Runtime input and presentation are exclusively OpenXR.
#include "input/input.hpp"
#include "config/config.hpp"
#include "jfg/jfg_xr_game.h"
#include "jfg_xr.h"
#include "xr_compat.hpp"
#include "xr_math.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
namespace { std::atomic<int> volume{100}; }
namespace recompui {
void message_box(const char *message) {std::fprintf(stderr,"[JFG XR] %s\n",message);}
void get_window_size(int &width,int &height) {
  unsigned w,h;jfgxr::renderSize(w,h);width=int(w);height=int(h);
}
void update_mod_list(bool) {}
}
namespace dino::config {
void reset_sound_settings() {volume=100;}
void set_main_volume(int value) {volume=std::clamp(value,0,100);}
int get_main_volume() {return volume.load();}
void open_quit_game_prompt(const std::string&) {jfgxr::requestExit();}
}
namespace jfg {
void overlay_size_now(float *size) {size[0]=320;size[1]=240;}
namespace overlay {float overlay_width() {return 320;}}
// Desktop overlay images are not part of the native renderer. Native game
// HUD display-list APIs remain available to mods.
void overlay_draw(int,float,float,float,float,uint32_t) {}
void overlay_set_image(int,int,int,std::vector<uint32_t>) {}
}
namespace dino::input {
const DefaultN64Mappings default_n64_keyboard_mappings{},default_n64_controller_mappings{};
std::string InputField::to_string() const {return {};}
float get_input_analog(const InputField&) {return 0;}
float get_input_analog(std::span<const InputField>) {return 0;}
bool get_input_digital(const InputField&) {return false;}
bool get_input_digital(std::span<const InputField>) {return false;}
void get_gyro_deltas(float *x,float *y) {*x=*y=0;}
void get_mouse_deltas(float *x,float *y) {*x=*y=0;}
void get_right_analog(float *x,float *y) {*x=jfgxr::input().turnX;*y=0;}
void set_right_analog_suppressed(bool) {}
void apply_joystick_deadzone(float x,float y,float *outX,float *outY) {
  *outX=jfgxr::stickDeadzone(x);*outY=jfgxr::stickDeadzone(y);
}
bool game_input_disabled() {const auto in=jfgxr::input();return !in.active || !in.focused || jfg::jfg_xr_menu_open();}
bool all_input_disabled() {return game_input_disabled();}
ultramodern::input::connected_device_info_t get_connected_device_info(int port) {
  using namespace ultramodern::input;
  return {port==0?Device::Controller:Device::None,Pak::None};
}
// Legacy control preferences are ignored: no keyboard, mouse, SDL controller
// scanner or desktop input device is initialized by this executable.
int get_rumble_strength() {return 0;}
int get_gyro_sensitivity() {return 0;}
int get_mouse_sensitivity() {return 0;}
int get_joystick_deadzone() {return 18;}
int get_joystick_range() {return 100;}
void set_rumble_strength(int) {}
void set_gyro_sensitivity(int) {}
void set_mouse_sensitivity(int) {}
void set_joystick_deadzone(int) {}
void set_joystick_range(int) {}
BackgroundInputMode get_background_input_mode() {return BackgroundInputMode::Off;}
void set_background_input_mode(BackgroundInputMode) {}
}
namespace dino::debug_ui {
bool game_has_input() {return !dino::input::game_input_disabled();}
}
