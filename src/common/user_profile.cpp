#include "user_profile.hpp"

#include <pwd.h>
#include <toml++/toml.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fleetwm {
namespace fs = std::filesystem;

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
