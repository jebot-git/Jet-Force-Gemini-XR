// English-only compatibility API. Locale files, saved language choices and
// language-selection/debug overrides are deliberately not read.
#include "jfg_locale.h"
namespace jfg::locale {
const std::vector<Locale>& all() { static const std::vector<Locale> none;return none; }
const Locale* find(const std::string&) { return nullptr; }
std::string chosen() { return {}; }
void choose(const std::string&) {}
bool choice_saved() { return true; }
bool official(const std::string &id) { return id.empty() || id=="english"; }
std::vector<std::pair<std::string,std::string>> list() { return {{"","English"}}; }
void debug_pick() {}
std::string ui(const std::string &text) { return text; }
std::string ui_key(const std::string &text) { return text; }
std::string ui_breaks(const std::string &text) { return text; }
bool take_ui_switch(std::map<std::string,std::string> &old_ui,std::map<std::string,std::string> &new_ui) {
  old_ui.clear();new_ui.clear();return false;
}
std::string ui_in(const std::string&,const std::string &text) { return text; }
void hold_translation(bool) {}
bool translate_ui(std::string &out,const std::string &in) { out=in;return false; }
bool ui_has_accents() { return false; }
}
extern "C" void jfg_locale_build_registry() {}
void jfg_language_prompt_frame() {}
std::string jfg_locale_shown() { return {}; }
