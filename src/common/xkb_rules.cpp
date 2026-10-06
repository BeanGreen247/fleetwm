#include "xkb_rules.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fleetwm {

namespace fs = std::filesystem;

namespace {
std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
std::string trim(std::string s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  s.erase(0, a);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
  return s;
}
}  // namespace

std::vector<LayoutInfo> parse_xkb_rules(const std::string& text) {
  std::vector<LayoutInfo> layouts;
  std::map<std::string, std::vector<LayoutInfo>> variants;
  std::istringstream in(text);
  std::string line, section;
  while (std::getline(in, line)) {
    if (!line.empty() && line[0] == '!') {
      section = trim(line.substr(1));
      continue;
    }
    if (line.empty() || line[0] != ' ') continue;
    std::istringstream f(line);
    std::string name;
    f >> name;
    std::string rest;
    std::getline(f, rest);
    rest = trim(rest);
    if (name.empty()) continue;
    if (section == "layout") {
      layouts.push_back({name, "", rest});
    } else if (section == "variant") {
      const size_t colon = rest.find(':');  // "cz: Czech (QWERTY)"
      if (colon == std::string::npos) continue;
      variants[trim(rest.substr(0, colon))].push_back({trim(rest.substr(0, colon)), name, trim(rest.substr(colon + 1))});
    }
  }
  std::vector<LayoutInfo> out;
  for (const LayoutInfo& l : layouts) {
    out.push_back(l);
    for (const LayoutInfo& v : variants[l.layout]) out.push_back(v);
  }
  return out;
}

std::vector<LayoutInfo> load_xkb_layouts() {
  const char* env = std::getenv("FLEETWM_XKB_RULES");
  return parse_xkb_rules(slurp(env && *env ? env : "/usr/share/X11/xkb/rules/base.lst"));
}

std::string describe_layout(const std::vector<LayoutInfo>& all, const std::string& layout, const std::string& variant) {
  for (const LayoutInfo& l : all)
    if (l.layout == layout && l.variant == variant) return l.description;
  return variant.empty() ? layout : layout + " (" + variant + ")";
}

std::vector<LocaleInfo> parse_supported_locales(const std::string& text) {
  std::vector<LocaleInfo> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream f(line);
    std::string code, charset;
    if (!(f >> code >> charset) || charset != "UTF-8") continue;
    if (code.find(".UTF-8") == std::string::npos) continue;
    out.push_back({code, ""});
  }
  std::sort(out.begin(), out.end(), [](const LocaleInfo& a, const LocaleInfo& b) { return a.code < b.code; });
  out.erase(std::unique(out.begin(), out.end(), [](const LocaleInfo& a, const LocaleInfo& b) { return a.code == b.code; }), out.end());
  return out;
}

std::string locale_display_name(const std::string& text) {
  std::string language, territory;
  std::istringstream in(text);
  std::string line;
  auto quoted = [](const std::string& l, const char* key) -> std::string {
    if (l.rfind(key, 0) != 0) return "";
    const size_t a = l.find('"'), b = l.rfind('"');
    return a != std::string::npos && b > a ? l.substr(a + 1, b - a - 1) : "";
  };
  for (int i = 0; i < 60 && std::getline(in, line); ++i) {
    if (language.empty()) language = quoted(line, "language");
    if (territory.empty()) territory = quoted(line, "territory");
  }
  if (language.empty()) return "";
  return territory.empty() ? language : language + " (" + territory + ")";
}

std::vector<LocaleInfo> load_locales() {
  const char* env = std::getenv("FLEETWM_I18N_DIR");
  const std::string dir = env && *env ? env : "/usr/share/i18n";
  std::vector<LocaleInfo> out = parse_supported_locales(slurp(dir + "/SUPPORTED"));
  for (LocaleInfo& l : out) {
    const std::string base = l.code.substr(0, l.code.find('.'));  // "cs_CZ"
    l.name = locale_display_name(slurp(dir + "/locales/" + base));
  }
  return out;
}

}  // namespace fleetwm
