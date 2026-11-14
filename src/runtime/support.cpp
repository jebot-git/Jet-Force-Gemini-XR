#include "support.hpp"
#include "config/config.hpp"
#include <cstdio>
namespace dino::runtime {
std::filesystem::path get_program_path() {return dino::config::get_executable_folder_path();}
std::filesystem::path get_asset_path(const char *asset) {return get_program_path()/"assets"/asset;}
void open_file_dialog(std::function<void(bool,const std::filesystem::path&)> callback) {callback(false,{});}
void open_file_dialog_multiple(std::function<void(bool,const std::list<std::filesystem::path>&)> callback) {callback(false,{});}
void show_error_message_box(const char *title,const char *message) {std::fprintf(stderr,"[JFG XR] %s: %s\n",title,message);}
}
