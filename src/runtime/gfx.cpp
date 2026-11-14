#include "gfx.hpp"
#include "common/sdl.hpp"
#include "ultramodern/ultramodern.hpp"
#include "jfg_xr.h"
#include <stdexcept>
namespace dino::runtime {
ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    // SDL supplies audio and an event pump only. No video subsystem, native
    // window, display connection, file dialog or desktop input is created.
    if(SDL_InitSubSystem(SDL_INIT_EVENTS|SDL_INIT_TIMER)<0)throw std::runtime_error(SDL_GetError());
    return {};
}
ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t,int,int) {return {};}
SDL_Window *get_window() {return nullptr;}
void update_gfx(void*) {
    SDL_Event event;
    while(SDL_PollEvent(&event))if(event.type==SDL_QUIT)jfgxr::requestExit();
    if(jfgxr::exitRequested())ultramodern::quit();
}
}
