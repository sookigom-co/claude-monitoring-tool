#include "i18n.h"

namespace i18n {
namespace {

SCREEN_TEXT(I18N_STRINGS)
const char *const SCREEN[][LANG_COUNT] PROGMEM = {SCREEN_TEXT(I18N_ROW)};
static_assert(sizeof(SCREEN) / sizeof(SCREEN[0]) == static_cast<size_t>(Msg::Count), "one row per Msg");

}  // namespace

PGM_P text(Lang lang, Msg id) { return pick(SCREEN, static_cast<uint8_t>(id), lang); }

void copy(Lang lang, Msg id, char *out, size_t n) {
  if (!n) return;
  strncpy_P(out, text(lang, id), n - 1);
  out[n - 1] = '\0';
}

}  // namespace i18n
