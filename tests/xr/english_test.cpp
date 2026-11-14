#include "jfg_locale.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
int main() {
  using namespace jfg::locale;
  setenv("JFG_LOCALE","french",1);
  choose("german");debug_pick();
  assert(chosen().empty() && all().empty() && find("german")==nullptr);
  const auto languages=list();
  assert(languages.size()==1 && languages[0].second=="English");
  assert(choice_saved() && ui("New Game")=="New Game");
  std::string out;
  assert(!translate_ui(out,"Start") && out=="Start");
  assert(ui_in("french","Resume")=="Resume");
  std::puts("PASS: English only; saved, API and environment language overrides ignored");
}
