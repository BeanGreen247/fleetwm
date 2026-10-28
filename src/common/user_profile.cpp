#include "user_profile.hpp"

#include <pwd.h>
#include "fleettoml.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "config_paths.hpp"

namespace fleetwm {
namespace fs = std::filesystem;

namespace {
// The first letter of `word` as a UTF-8 string, skipping leading punctuation and digits; "" when it has none.
std::string first_letter(const std::string& word) {
  for (size_t i = 0; i < word.size();) {
    const unsigned char c = static_cast<unsigned char>(word[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (i + len > word.size()) len = 1;
    if (c < 0x80) {
      if (std::isalpha(c)) return std::string(1, static_cast<char>(std::toupper(c)));
    } else {
      return word.substr(i, len);  // a letter of another alphabet (the profile name is typed by a person, so it is not punctuation)
    }
    i += len;
  }
  return "";
}
}  // namespace

std::string profile_initials(const std::string& display_name) {
  std::vector<std::string> letters;
  std::string word;
  auto flush = [&] {
    if (word.empty()) return;
    const std::string l = first_letter(word);
    if (!l.empty()) letters.push_back(l);
    word.clear();
  };
  for (const char ch : display_name) {
    if (ch == ' ' || ch == '\t' || ch == '\n') flush();
    else word += ch;
  }
  flush();
  if (letters.empty()) return "F";
  if (letters.size() == 1) return letters.front();
  return letters.front() + letters.back();
}

std::string user_profile_path() { return (config_internal::config_home() / "fleetwm" / "profile.toml").string(); }

static std::string default_display_name() {
  const char* user = std::getenv("USER");
  if (user) {
    if (passwd* pw = getpwnam(user); pw && pw->pw_gecos) {
      std::string name(pw->pw_gecos);
      const size_t comma = name.find(',');
      if (comma != std::string::npos) name.resize(comma);
      if (!name.empty()) return name;
    }
    if (*user) return user;
  }
  return "Fleetwm user";
}

UserProfile load_user_profile() {
  UserProfile profile{default_display_name(), {}};
  std::error_code ec;
  if (!fs::exists(user_profile_path(), ec)) return profile;
  try {
    const toml::table root = toml::parse_file(user_profile_path());
    if (auto v = root["display_name"].value<std::string>(); v && !v->empty()) profile.display_name = *v;
    if (auto v = root["icon_path"].value<std::string>()) profile.icon_path = *v;
  } catch (const toml::parse_error&) {
  }
  return profile;
}

void save_user_profile(const UserProfile& profile) {
  const fs::path path = user_profile_path();
  fs::create_directories(path.parent_path());
  toml::table root;
  root.insert_or_assign("display_name", profile.display_name);
  root.insert_or_assign("icon_path", profile.icon_path);
  const fs::path tmp = path.string() + ".tmp";
  std::ofstream out(tmp);
  if (!out) throw std::runtime_error("cannot write " + tmp.string());
  out << root;
  out.close();
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) throw std::runtime_error("cannot save " + path.string() + ": " + ec.message());
}

}  // namespace fleetwm
