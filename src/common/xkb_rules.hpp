#pragma once

// The names of keyboard layouts and variants ("Czech", "English (US, intl., with dead keys)")
// from xkeyboard-config's base.lst, and the locales the system can generate.

#include <string>
#include <vector>

namespace fleetwm {

struct LayoutInfo {
  std::string layout;       // "cz"
  std::string variant;      // "" for the plain layout
  std::string description;  // "Czech" / "Czech (QWERTY)"
};

// Parses the "! layout" and "! variant" sections of base.lst text; plain layouts first in
// file order, each followed by its variants.
std::vector<LayoutInfo> parse_xkb_rules(const std::string& base_lst_text);
std::vector<LayoutInfo> load_xkb_layouts();  // from /usr/share/X11/xkb/rules/base.lst ($FLEETWM_XKB_RULES for tests)

// Description for one layout/variant, or the plain names when unknown.
std::string describe_layout(const std::vector<LayoutInfo>& all, const std::string& layout, const std::string& variant);

struct LocaleInfo {
  std::string code;  // "cs_CZ.UTF-8"
  std::string name;  // "Czech (Czechia)"
};
// UTF-8 locales from SUPPORTED text, sorted by code, name left empty.
std::vector<LocaleInfo> parse_supported_locales(const std::string& supported_text);
// language / territory from a locale definition file's header.
std::string locale_display_name(const std::string& locale_file_text);
std::vector<LocaleInfo> load_locales();  // from /usr/share/i18n ($FLEETWM_I18N_DIR for tests)

}  // namespace fleetwm
