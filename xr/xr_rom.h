#pragma once
#include <filesystem>
#include <vector>
#include <string>
namespace jfgxr {
inline std::string pathUtf8(const std::filesystem::path &path) {
  const auto bytes=path.u8string();return std::string(bytes.begin(),bytes.end());
}
// Discovery stays independent of the working directory and stored ROM cache.
inline std::vector<std::filesystem::path> romCandidates(const std::filesystem::path &executable,
                                                       const std::filesystem::path &downloads) {
  std::vector<std::filesystem::path> paths{executable/"roms"/"JetForceGemini.z64"};
  if(!downloads.empty()) {
    paths.push_back(downloads/"JetForceGemini.z64");
    paths.push_back(downloads/"Jet Force Gemini (USA).z64");
    paths.push_back(downloads/"baserom.us.z64");
  }
  paths.push_back(executable/"baserom.us.z64");
  return paths;
}
inline std::filesystem::path findRom(const std::vector<std::filesystem::path> &paths) {
  for(const auto &path:paths) {
    std::error_code error;
    if(std::filesystem::is_regular_file(path,error))return path;
  }
  return {};
}
std::filesystem::path downloadsFolder();
}
