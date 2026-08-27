#include "i18n.h"
#include "pet.h"
#include <Preferences.h>

Lang gLang = LANG_RECOVERY;

static constexpr uint16_t MEDAL_NAME_BASE = STR_COUNT;
static constexpr uint16_t MEDAL_LABEL_BASE = STR_COUNT + MED_COUNT;
static constexpr uint16_t MEDAL_DESC_BASE = STR_COUNT + MED_COUNT * 2;

const char *T(StrId id) { return uiString((uint16_t)id); }
const char *medalName(int i) {
  return i >= 0 && i < MED_COUNT ? uiString(MEDAL_NAME_BASE + i) : "?";
}
const char *medalLabel(int i) {
  return i >= 0 && i < MED_COUNT ? uiString(MEDAL_LABEL_BASE + i) : "?";
}
const char *medalDesc(int i) {
  return i >= 0 && i < MED_COUNT ? uiString(MEDAL_DESC_BASE + i) : "?";
}
const char *natureName(NatureId nature) {
  static_assert(S_NATURE_QUIRKY - S_NATURE_HARDY + 1 == NATURE_COUNT,
                "nature strings must match NatureId");
  return natureValid(nature) ? T((StrId)(S_NATURE_HARDY + (uint8_t)nature)) : "?";
}

uint8_t langCount() { return uiLocaleCount(); }
const char *langCode(Lang l) { return uiLocaleInfo(l).locale; }
const char *langLabel(Lang l) { return uiLocaleInfo(l).shortLabel; }
bool langIsCjk(Lang l) { return l < uiLocaleCount() && uiLocaleInfo(l).isCjk; }
const char *langDisplayName(Lang l) {
  const UiLocaleInfo &info = uiLocaleInfo(l);
  if (!info.displayName[0]) return "English";
  // GLUE: bitmap UI packs cannot draw the Chinese pack's native self-name.
  // Remove this fallback when UI pack metadata gains a cross-font ASCII name.
  if (info.isCjk && !langIsCjk(gLang) && !strncmp(info.locale, "zh-CN", 16))
    return "Chinese";
  return info.displayName;
}

bool loadLang() {
  contentBegin();
  Preferences prefs;
  prefs.begin("tamapoke", true);
  char saved[16] = {};
  prefs.getString("locale", saved, sizeof(saved));
  int8_t selected = saved[0] ? uiFindLocale(saved) : -1;
  prefs.end();
  if (selected >= 0 && uiActivateLocale((uint8_t)selected)) {
    gLang = (Lang)selected;
    return true;
  }
  selected = uiLocaleCount() ? 0 : -1;
  if (selected >= 0 && uiActivateLocale((uint8_t)selected)) {
    gLang = (Lang)selected;
    return false;
  }
  selected = uiFindLocale("en-US");
  if (selected >= 0 && uiActivateLocale((uint8_t)selected)) {
    gLang = (Lang)selected;
    return false;
  }
  selected = uiActiveLocale();
  if (selected >= 0 && uiActivateLocale((uint8_t)selected)) gLang = (Lang)selected;
  return false;
}

bool setLang(Lang l) {
  if (l >= uiLocaleCount() || !uiActivateLocale(l)) return false;
  gLang = l;
  Preferences prefs;
  prefs.begin("tamapoke", false);
  prefs.putString("locale", uiActiveLocaleCode());
  prefs.end();
  return true;
}
