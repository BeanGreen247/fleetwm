#include "remote_config.hpp"

#include "fleettoml.hpp"

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

std::string remote_user_config_path() {
  return (config_internal::config_home() / "fleetwm" / "remote.toml").string();
}

std::string remote_status_path() {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  return std::string(runtime && *runtime ? runtime : "/tmp") + "/fleetwm-remote.status";
}

RemoteConfig load_remote_config() {
  RemoteConfig config;
  const fs::path path = remote_user_config_path();
  std::error_code ec;
  if (!fs::exists(path, ec)) return config;
  try {
    toml::table root = toml::parse_file(path.string());
    auto port = [](toml::table* t, const char* key, int* out) {
      if (auto* v = (*t)[key].as_integer()) *out = static_cast<int>(std::clamp<int64_t>(v->get(), 1, 65535));
    };
    auto picture = [](toml::table* t, std::string* resolution, int* depth, bool rdp) {
      if (auto v = (*t)["resolution"].value<std::string>()) {
        int w, h;
        if (parse_resolution(*v, &w, &h)) *resolution = *v;
      }
      if (auto* v = (*t)["depth"].as_integer()) {
        const int d = static_cast<int>(v->get());
        for (const DepthChoice& c : remote_depth_choices(rdp))
          if (c.bits == d) *depth = d;
      }
    };
    auto limits = [](toml::table* t, int* quality, int* kbps) {
      if (auto* v = (*t)["quality"].as_integer()) *quality = static_cast<int>(std::clamp<int64_t>(v->get(), kQualityMin, kQualityMax));
      if (auto* v = (*t)["max_kbps"].as_integer()) *kbps = static_cast<int>(std::clamp<int64_t>(v->get(), 0, 1000000));
    };
    if (auto* t = root["vnc"].as_table()) {
      if (auto v = (*t)["enabled"].value<bool>()) config.vnc_enabled = *v;
      port(t, "port", &config.vnc_port);
      if (auto v = (*t)["password"].value<std::string>()) config.vnc_password = *v;
      if (auto v = (*t)["tls"].value<bool>()) config.vnc_tls = *v;
      limits(t, &config.vnc_quality, &config.vnc_max_kbps);
      picture(t, &config.vnc_resolution, &config.vnc_depth, false);
    }
    if (auto* t = root["rdp"].as_table()) {
      if (auto v = (*t)["enabled"].value<bool>()) config.rdp_enabled = *v;
      port(t, "port", &config.rdp_port);
      if (auto v = (*t)["password"].value<std::string>()) config.rdp_password = *v;
      if (auto v = (*t)["tls"].value<bool>()) config.rdp_tls = *v;
      limits(t, &config.rdp_quality, &config.rdp_max_kbps);
      picture(t, &config.rdp_resolution, &config.rdp_depth, true);
    }
  } catch (const toml::parse_error&) {
    return RemoteConfig{};  // a broken file must never leave a server open with an unknown password
  }
  return config;
}

void save_remote_config(const RemoteConfig& config) {
  const fs::path path = remote_user_config_path();
  fs::create_directories(path.parent_path());
  toml::table vnc, rdp, root;
  vnc.insert_or_assign("enabled", config.vnc_enabled);
  vnc.insert_or_assign("port", static_cast<int64_t>(config.vnc_port));
  vnc.insert_or_assign("password", config.vnc_password);
  vnc.insert_or_assign("tls", config.vnc_tls);
  vnc.insert_or_assign("quality", static_cast<int64_t>(config.vnc_quality));
  vnc.insert_or_assign("max_kbps", static_cast<int64_t>(config.vnc_max_kbps));
  vnc.insert_or_assign("resolution", config.vnc_resolution);
  vnc.insert_or_assign("depth", static_cast<int64_t>(config.vnc_depth));
  rdp.insert_or_assign("enabled", config.rdp_enabled);
  rdp.insert_or_assign("port", static_cast<int64_t>(config.rdp_port));
  rdp.insert_or_assign("password", config.rdp_password);
  rdp.insert_or_assign("tls", config.rdp_tls);
  rdp.insert_or_assign("quality", static_cast<int64_t>(config.rdp_quality));
  rdp.insert_or_assign("max_kbps", static_cast<int64_t>(config.rdp_max_kbps));
  rdp.insert_or_assign("resolution", config.rdp_resolution);
  rdp.insert_or_assign("depth", static_cast<int64_t>(config.rdp_depth));
  root.insert_or_assign("vnc", vnc);
  root.insert_or_assign("rdp", rdp);

  // Write next to the target with mode 0600 from the first byte, then rename: the passwords are never readable by others.
  const fs::path tmp = path.string() + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "w");
  if (!f) throw std::runtime_error("failed to open " + tmp.string() + " for writing");
  fchmod(fileno(f), 0600);
  std::ostringstream text;  // toml output goes through a stream; render to a string first
  text << root << "\n";
  const std::string s = text.str();
  const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
  std::fclose(f);
  if (!ok) throw std::runtime_error("failed to write " + tmp.string());
  fs::rename(tmp, path);
}

std::string generate_remote_password(int length) {
  static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";  // 57 characters
  constexpr unsigned kLimit = 256 - 256 % (sizeof(kAlphabet) - 1);  // reject the top bytes so every character is equally likely
  std::FILE* f = std::fopen("/dev/urandom", "rb");
  if (!f) throw std::runtime_error("cannot read /dev/urandom");
  std::string out;
  while (static_cast<int>(out.size()) < length) {
    unsigned char b;
    if (std::fread(&b, 1, 1, f) != 1) {
      std::fclose(f);
      throw std::runtime_error("cannot read /dev/urandom");
    }
    if (b < kLimit) out += kAlphabet[b % (sizeof(kAlphabet) - 1)];
  }
  std::fclose(f);
  return out;
}

const std::vector<int>& remote_bandwidth_choices() {
  static const std::vector<int> kChoices = {0, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
  return kChoices;
}

std::string remote_bandwidth_label(int kbps) {
  if (kbps <= 0) return "No limit";
  const double mbit = kbps * 8.0 / 1000.0;
  char buf[64];
  if (mbit < 1) std::snprintf(buf, sizeof buf, "%d KB/s (%d kbit/s)", kbps, kbps * 8);
  else std::snprintf(buf, sizeof buf, "%d KB/s (%.0f Mbit/s)", kbps, mbit);
  return buf;
}

int nearest_bandwidth_index(int kbps) {
  const auto& c = remote_bandwidth_choices();
  if (kbps <= 0) return 0;
  size_t best = 1;
  for (size_t i = 1; i < c.size(); ++i)
    if (std::abs(c[i] - kbps) < std::abs(c[best] - kbps)) best = i;
  return static_cast<int>(best);
}

const std::vector<ResolutionChoice>& remote_resolution_choices() {
  static const std::vector<ResolutionChoice> kChoices = {
      {"Same as the screen", ""}, {"1920 x 1080", "1920x1080"}, {"1600 x 900", "1600x900"}, {"1366 x 768", "1366x768"}, {"1280 x 720", "1280x720"},
      {"1024 x 768", "1024x768"}, {"960 x 540", "960x540"},     {"800 x 600", "800x600"},   {"640 x 480", "640x480"},   {"480 x 270", "480x270"},
  };
  return kChoices;
}

bool parse_resolution(const std::string& text, int* w, int* h) {
  *w = *h = 0;
  const size_t x = text.find('x');
  if (x == std::string::npos || x == 0 || x + 1 >= text.size()) return false;
  for (size_t i = 0; i < text.size(); ++i)
    if (i != x && !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
  if (x > 5 || text.size() - x - 1 > 5) return false;
  const int a = std::atoi(text.substr(0, x).c_str()), b = std::atoi(text.substr(x + 1).c_str());
  if (a < 160 || b < 90 || a > 16384 || b > 16384) return false;
  *w = a;
  *h = b;
  return true;
}

int nearest_resolution_index(const std::string& value) {
  const auto& c = remote_resolution_choices();
  for (size_t i = 0; i < c.size(); ++i)
    if (value == c[i].value) return static_cast<int>(i);
  return 0;
}

const std::vector<DepthChoice>& remote_depth_choices(bool rdp) {
  static const std::vector<DepthChoice> kVnc = {{"Automatic (from quality)", 0}, {"8-bit (256 colours)", 8}, {"16-bit", 16}, {"24-bit (full colour)", 24}};
  static const std::vector<DepthChoice> kRdp = {{"Automatic (from quality)", 0}, {"16-bit", 16}, {"32-bit (full colour)", 32}};
  return rdp ? kRdp : kVnc;
}

int depth_index(bool rdp, int bits) {
  const auto& c = remote_depth_choices(rdp);
  for (size_t i = 0; i < c.size(); ++i)
    if (c[i].bits == bits) return static_cast<int>(i);
  return 0;
}

std::string remote_login_name() {
  if (const passwd* pw = getpwuid(getuid())) return pw->pw_name;
  const char* user = std::getenv("USER");
  return user ? user : "user";
}

}  // namespace fleetwm
