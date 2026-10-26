#pragma once

#include <string>
#include <vector>

// Remote desktop settings (Settings -> Remote Desktop), saved per user in remote.toml (mode 0600, it holds
// the passwords). fleetwm-remote reads the same file and starts or stops its VNC and RDP servers when it changes.

namespace fleetwm {

struct RemoteConfig {
  bool vnc_enabled = false;
  int vnc_port = 5900;
  std::string vnc_password;  // VNC only looks at the first 8 characters
  bool vnc_tls = false;      // VeNCrypt (TLS); off = the classic VNC login, which not every viewer replaces
  int vnc_quality = 90;      // 1..100: lower drops colour detail and updates less often
  int vnc_max_kbps = 0;      // KB/s a viewer may use, 0 = no limit
  std::string vnc_resolution;  // largest picture sent, "1280x720"; empty = the screen's own size
  int vnc_depth = 0;           // 0 = decided by quality, 8, 16 or 24 bits
  bool rdp_enabled = false;
  int rdp_port = 3389;
  std::string rdp_password;
  bool rdp_tls = true;       // off = standard RDP security without encryption; many clients refuse that
  int rdp_quality = 90;
  int rdp_max_kbps = 0;
  std::string rdp_resolution;
  int rdp_depth = 0;           // 0 = decided by quality, 16 or 32 bits
  bool operator==(const RemoteConfig&) const = default;
};

constexpr int kVncPasswordLength = 8;
constexpr int kQualityMin = 1, kQualityMax = 100;
constexpr int kRdpPasswordLength = 16;

std::string remote_user_config_path();

// Loads remote.toml; a missing or broken file gives both servers off. Ports are clamped to 1..65535.
RemoteConfig load_remote_config();

// Writes remote.toml with mode 0600, creating the directory. Throws std::runtime_error on I/O failure.
void save_remote_config(const RemoteConfig& config);

// A random password of `length` characters from letters and digits without look-alikes (no 0/O, 1/l/I), taken from
// /dev/urandom. Throws std::runtime_error when the random source cannot be read.
std::string generate_remote_password(int length);

// The bandwidth limits Settings offers, in KB/s, 0 (no limit) first, and their text: "No limit", "128 KB/s (1 Mbit/s)".
const std::vector<int>& remote_bandwidth_choices();
std::string remote_bandwidth_label(int kbps);
// Index of the choice closest to `kbps` (0 for 0).
int nearest_bandwidth_index(int kbps);

// Render resolutions Settings offers: label and value ("" = the screen's own size, else "WIDTHxHEIGHT").
struct ResolutionChoice {
  const char* label;
  const char* value;
};
const std::vector<ResolutionChoice>& remote_resolution_choices();
// "1280x720" -> 1280, 720; false (and 0, 0) for "" or anything else.
bool parse_resolution(const std::string& text, int* w, int* h);
int nearest_resolution_index(const std::string& value);

// Colour depths Settings offers per protocol: label and bits (0 = automatic).
struct DepthChoice {
  const char* label;
  int bits;
};
const std::vector<DepthChoice>& remote_depth_choices(bool rdp);
int depth_index(bool rdp, int bits);

// The user name the RDP login expects: the login name of this account.
std::string remote_login_name();

// <XDG_RUNTIME_DIR>/fleetwm-remote.status, written by the daemon: lines "vnc=<listening|off|error text>",
// "rdp=...", "clients=<n>".
std::string remote_status_path();

}  // namespace fleetwm
