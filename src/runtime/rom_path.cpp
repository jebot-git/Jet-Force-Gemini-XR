#include "xr_rom.h"
#include <cstdlib>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif
namespace jfgxr {
std::filesystem::path downloadsFolder() {
#ifdef _WIN32
  PWSTR known=nullptr;
  if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads,0,nullptr,&known))) {
    std::filesystem::path result(known);CoTaskMemFree(known);return result;
  }
  if(const auto *home=_wgetenv(L"USERPROFILE"))return std::filesystem::path(home)/L"Downloads";
#elif !defined(__ANDROID__)
  const char *home=std::getenv("HOME");
  if(!home || !*home)return {};
  const auto *config=std::getenv("XDG_CONFIG_HOME");
  std::ifstream dirs((config && *config ? std::filesystem::path(config) : std::filesystem::path(home)/".config")/"user-dirs.dirs");
  std::string line;
  while(std::getline(dirs,line)) {
    const std::string prefix="XDG_DOWNLOAD_DIR=\"";
    if(line.compare(0,prefix.size(),prefix)!=0)continue;
    const auto end=line.find('"',prefix.size());
    if(end==std::string::npos)continue;
    auto value=line.substr(prefix.size(),end-prefix.size());
    if(value=="$HOME")value=home;
    else if(value.compare(0,6,"$HOME/")==0)value=std::string(home)+value.substr(5);
    // Read the path as data: never evaluate shell substitutions from this file.
    if(std::filesystem::path(value).is_absolute() && value.find_first_of("$`\\")==std::string::npos)return value;
  }
  return std::filesystem::path(home)/"Downloads";
#endif
  return {};
}
}
