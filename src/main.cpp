#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <vector>
#include <filesystem>
#include <string>
#include <system_error>

#include "ultramodern/ultramodern.hpp"
#include "librecomp/game.hpp"


#include "input/input.hpp"
#include "input/controls.hpp"
#include "renderer/renderer.hpp"
#include "config/config.hpp"
#include "recomp_api/debug_ui_api.hpp"
#include "recomp_api/general_api.hpp"
#include "recomp_api/recomp_data_api.hpp"
#include "common/sdl.hpp"
#include "jfg_xr.h"
#include "xr_rom.h"
#include "jfg/jfg_update_check.h"

#include "runtime/audio.hpp"
#include "runtime/crash.hpp"
#include "runtime/gfx.hpp"
#include "runtime/mods.hpp"
#include "runtime/overlays.hpp"
#include "runtime/support.hpp"
#include "runtime/preload.hpp"
#include "runtime/rsp.hpp"
#include "runtime/threads.hpp"

const std::string version_string = JFG_VERSION_STRING; // JFG PORT: from version.txt (CMakeLists.txt)

extern "C" void recomp_entrypoint(uint8_t *rdram, recomp_context *ctx);
gpr get_entrypoint_address();
extern "C" void jfg_apply_asset_mods(void); // JFG PORT: src/jfg/jfg_assets.cpp
extern "C" void jfg_check_mod_replacements(void); // JFG PORT: src/jfg/jfg_mods.cpp
extern "C" void jfg_locale_start(void); // JFG PORT: src/jfg/jfg_locale.cpp
extern "C" int jfg_launch_arg(int argc, char** argv, int* i); // JFG PORT: src/jfg/jfg_launch.cpp
extern "C" int jfg_launch_ready(bool* skip_launcher); // JFG PORT: src/jfg/jfg_launch.cpp

static void custom_recomp_entrypoint(uint8_t *rdram, recomp_context *ctx) {
    printf("Initialized RDRAM at %p\n", rdram);

    dino::runtime::crash_register_rdram(rdram);
    dino::runtime::crash_register_thread_context(ctx);

    jfg_apply_asset_mods(); // JFG PORT: data replaced by mods (src/jfg/jfg_assets.cpp), once mods are loaded
    jfg_check_mod_replacements(); // JFG PORT: mods that turn port features off (src/jfg/jfg_mods.cpp)
    jfg_locale_start(); // English only, independent of saved locale files

    recomp_entrypoint(rdram, ctx);
}

// JFG port: boot-time globals the runtime's IPL3 emulation leaves unset (src/jfg/jfg_glue.cpp).
extern "C" void jfg_on_init(uint8_t* rdram, recomp_context* ctx);
// JFG port: mod support (src/jfg/jfg_mods.cpp): functions mods import, and the pre-linked ROM hooks regenerate from.
extern "C" void jfg_register_mod_exports(void);
std::vector<uint8_t> jfg_prelink_rom(std::span<const uint8_t> rom);

// JFG PORT: Jet Force Gemini's modified osCreateThread (libultra/src/os/createthread.c, JFGDIFFS) starts every
// thread with SR_FR set, i.e. 64-bit FPU registers where odd-numbered registers are independent, and clears it
// only for threads created with a negative id (the audio thread, whose libultra code needs FR = 0).
// The runtime replaces osCreateThread, so reproduce that rule here; game code that uses odd FPU registers
// (camera, collision and matrix maths) computes garbage otherwise.
static void custom_thread_create_callback(uint8_t* rdram, recomp_context* ctx) {
    dino::runtime::crash_register_thread_context(ctx);

    PTR(OSThread) self = ultramodern::this_thread();
    const bool fr = (self == NULLPTR) || (TO_PTR(OSThread, self)->id >= 0);
    if (fr) {
        ctx->f_odd = &ctx->f1.u32l;
        ctx->mips3_float_mode = true;
        ctx->status_reg |= 0x04000000u; // SR_FR
    }
}

// array of supported GameEntry objects
std::vector<recomp::GameEntry> supported_games = {
    {
        // XXH3-64 of the US ROM (sha1 493ced9008dbe932d6e91179b68e8630cf23a023)
        .rom_hash = 0x44830C263C2DE108,
        .internal_name = "JET FORCE GEMINI    ",
        .game_id = u8"jfg-us",
        .mod_game_id = "jet-force-gemini",
        .save_type = recomp::SaveType::Flashram,
        .is_enabled = true,
        // JFG PORT: not compressed, but mods' hooks regenerate functions from this ROM, and the port recompiles
        // overlays pre-linked at private addresses (tools/prepare_overlays.py): jfg_prelink_rom gives the same copy.
        .decompression_routine = jfg_prelink_rom,
        .has_compressed_code = true,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = custom_recomp_entrypoint,
        .thread_create_callback = custom_thread_create_callback,
        .on_init_callback = jfg_on_init,
    },
};


namespace {
void stop_signal(int) {jfgxr::requestExit();}
void native_error(const char *message) { std::fprintf(stderr,"[JFG XR] %s\n",message); }
void poll_input() {}
bool input_state(int port,uint16_t *buttons,float *x,float *y) {
    if(port!=0)return false;
    *buttons=0;*x=*y=0;
    // The game-thread joyRead hook fills these from OpenXR poses/actions.
    return true;
}
void rumble(int,bool) {}
ultramodern::input::connected_device_info_t device_info(int port) {
    using namespace ultramodern::input;
    return {port==0?Device::Controller:Device::None,Pak::None};
}
ultramodern::renderer::WindowHandle no_window(ultramodern::gfx_callbacks_t::gfx_data_t) { return {}; }
}
int main(int argc,char **argv) {
    dino::runtime::crash_setup_handler();
    std::signal(SIGINT,stop_signal);
    std::signal(SIGTERM,stop_signal);
    const char *rom=nullptr;
    bool probe=false,paths_only=false,skip=true;
    std::string absolute_rom;
    for(int i=1;i<argc;i++) {
        const std::string arg=argv[i];
        if(arg=="--rom" && i+1<argc) {absolute_rom=jfgxr::pathUtf8(std::filesystem::absolute(std::filesystem::u8path(argv[++i])));rom=absolute_rom.c_str();}
        else if(arg=="--check-xr")probe=true;
        else if(arg=="--rom-paths")paths_only=true;
        else if(arg=="--help") {
            std::puts("Jet Force Gemini XR — native OpenXR, English only\nUsage: JetForceGeminiRecompiled [--rom USA.z64] [--check-xr] [--rom-paths]\nPut JetForceGemini.z64 in Downloads or roms beside the executable.\n--rom and JFG_ROM override discovery; --rom-paths lists locations without starting XR.\nA connected XR HMD is required. No desktop/window mode is available.");return 0;
        } else if(jfg_launch_arg(argc,argv,&i)) {}
        else {std::fprintf(stderr,"[JFG XR] Unsupported argument: %s\n",argv[i]);return EXIT_FAILURE;}
    }
    const auto candidates=jfgxr::romCandidates(dino::config::get_executable_folder_path(),jfgxr::downloadsFolder());
    if(!rom) {
        if(const char *override_path=std::getenv("JFG_ROM");override_path && *override_path)
            absolute_rom=jfgxr::pathUtf8(std::filesystem::absolute(std::filesystem::u8path(override_path)));
        else absolute_rom=jfgxr::pathUtf8(jfgxr::findRom(candidates));
        if(!absolute_rom.empty())rom=absolute_rom.c_str();
    }
    if(paths_only) {
        for(const auto &path:candidates)std::fprintf(stdout,"ROM location: %s\n",jfgxr::pathUtf8(path).c_str());
        std::fprintf(stdout,"Selected ROM: %s\n",rom?rom:"none (a previously imported ROM may still be available)");
        return EXIT_SUCCESS;
    }
    if(const int result=jfg_launch_ready(&skip);result>=0)return result;
    if(!jfgxr::requireHmd())return EXIT_FAILURE;
    if(probe) {jfgxr::shutdown();return EXIT_SUCCESS;}
#ifdef __ANDROID__
    // Android's document picker imports the owner's cartridge into app storage.
    while(rom && !std::filesystem::exists(rom) && !jfgxr::exitRequested())
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if(jfgxr::exitRequested())return EXIT_SUCCESS;
#endif
    std::error_code ec;
    std::filesystem::current_path(dino::config::get_executable_folder_path(),ec);
    recomp::Version project_version{};
    if(!recomp::Version::from_string(version_string,project_version)) {native_error("Invalid version string");return EXIT_FAILURE;}
    dino::runtime::PreloadContext preload_context;
    const bool preloaded=preload_executable(preload_context);
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    if(!dino::runtime::reset_audio(48000))return EXIT_FAILURE;
    recomp::register_config_path(dino::config::get_app_folder_path());
    for(const auto &game:supported_games)recomp::register_game(game);
    recomp::check_all_stored_roms();
    jfg_launcher_config_load(dino::config::get_app_folder_path());
    std::string saved_rom;
    if(!rom && !jfg_launcher_config_rom().empty() && std::filesystem::is_regular_file(std::filesystem::u8path(jfg_launcher_config_rom()),ec)) {saved_rom=jfg_launcher_config_rom();rom=saved_rom.c_str();}
    if(rom) {
        const auto error=recomp::select_rom(std::filesystem::u8path(rom),supported_games[0].game_id);
        if(error!=recomp::RomValidationError::Good) {native_error("Wrong or modified US cartridge dump");return EXIT_FAILURE;}
        recomp::check_all_stored_roms();
    }
    if(!recomp::is_rom_valid(supported_games[0].game_id)) {
        native_error("Put your US dump in Downloads/JetForceGemini.z64 or roms/JetForceGemini.z64 beside the executable. Use --rom-paths to see full paths, or --rom PATH to choose another file.");return EXIT_FAILURE;
    }
    jfg_register_mod_exports();
    dino::runtime::register_overlays();
    dino::config::load_config();
    recomp::rsp::callbacks_t rsp_callbacks{.get_rsp_microcode=dino::runtime::get_rsp_microcode};
    ultramodern::renderer::callbacks_t renderer_callbacks{.create_render_context=dino::renderer::create_render_context};
    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx=dino::runtime::create_gfx,.create_window=no_window,.update_gfx=dino::runtime::update_gfx};
    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples=dino::runtime::queue_samples,.get_frames_remaining=dino::runtime::get_frames_remaining,
        .set_frequency=dino::runtime::set_frequency};
    ultramodern::input::callbacks_t input_callbacks{
        .poll_input=poll_input,.get_input=input_state,.set_rumble=rumble,.get_connected_device_info=device_info};
    ultramodern::events::callbacks_t events_callbacks{.vi_callback=poll_input,.gfx_init_callback=poll_input};
    ultramodern::error_handling::callbacks_t errors{.message_box=native_error};
    ultramodern::threads::callbacks_t threads{.get_game_thread_name=dino::runtime::get_game_thread_name};
    dino::runtime::register_mods();
    recomp::start_game(supported_games[0].game_id);
    recomp::Configuration config{
        .project_version=project_version,.rsp_callbacks=rsp_callbacks,.renderer_callbacks=renderer_callbacks,
        .audio_callbacks=audio_callbacks,.input_callbacks=input_callbacks,.gfx_callbacks=gfx_callbacks,
        .events_callbacks=events_callbacks,.error_handling_callbacks=errors,.threads_callbacks=threads,
        .message_queue_control={.requeue_si=false}};
    recomp::start(config);
    const bool failure=jfgxr::hasError();
    jfgxr::shutdown();
    if(preloaded)release_preload(preload_context);
    return failure?EXIT_FAILURE:EXIT_SUCCESS;
}
