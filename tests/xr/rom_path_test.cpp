#include "xr_rom.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
int main() {
  namespace fs=std::filesystem;
  const auto root=fs::temp_directory_path()/("jfg-rom-paths-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto executable=root/fs::u8path("Portable game ž");
  const auto downloads=root/fs::u8path("Downloads ž");
  fs::create_directories(executable/"roms");fs::create_directories(downloads);
  const auto paths=jfgxr::romCandidates(executable,downloads);
  assert(jfgxr::findRom(paths).empty());
  // A folder with the canonical name must never be mistaken for a cartridge.
  fs::create_directory(paths[0]);assert(jfgxr::findRom(paths).empty());fs::remove(paths[0]);
  std::ofstream(paths[2]).put('x'); // original US dump filename, no renaming needed
  assert(jfgxr::findRom(paths)==paths[2]);
  std::ofstream(paths[1]).put('x');assert(jfgxr::findRom(paths)==paths[1]);
  std::ofstream(paths[0]).put('x');assert(jfgxr::findRom(paths)==paths[0]);
  assert(fs::u8path(jfgxr::pathUtf8(paths[0]))==paths[0]);
  const auto legacy=jfgxr::romCandidates(executable,{});
  assert(legacy.size()==2 && legacy.back()==executable/"baserom.us.z64");
  fs::remove_all(root);
  // Discover the real OS folder without creating it or touching its contents.
  const auto actual=jfgxr::downloadsFolder();assert(actual.empty() || actual.is_absolute());
  std::puts("PASS: portable/Downloads ROM discovery, filename aliases, precedence, missing files, Unicode paths and OS Downloads location");
}
