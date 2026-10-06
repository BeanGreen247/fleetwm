#include "wpa_backend.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>

#include "network_parse.hpp"
#include "system_devices.hpp"

namespace fleetwm::net {

namespace fs = std::filesystem;

namespace {

// One request/response conversation with a wpa_supplicant control socket (datagrams).
class Ctrl {
 public:
  explicit Ctrl(const std::string& path) {
    fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return;
    static std::atomic<int> counter{0};
    sockaddr_un local{};
    local.sun_family = AF_UNIX;
    // The supplicant needs a bound address to reply to: an abstract socket (leading NUL) needs no file.
    const std::string name = "fleetwm-wpa-" + std::to_string(getpid()) + "-" + std::to_string(counter++);
    std::memcpy(local.sun_path + 1, name.data(), name.size());
    const socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
    sockaddr_un remote{};
    remote.sun_family = AF_UNIX;
    if (path.size() >= sizeof remote.sun_path) {
      close_fd();
      return;
    }
    std::strcpy(remote.sun_path, path.c_str());
    if (bind(fd_, reinterpret_cast<sockaddr*>(&local), len) < 0 ||
        connect(fd_, reinterpret_cast<sockaddr*>(&remote), sizeof remote) < 0)
      close_fd();
  }
  ~Ctrl() { close_fd(); }
  Ctrl(const Ctrl&) = delete;
  Ctrl& operator=(const Ctrl&) = delete;
  bool ok() const { return fd_ >= 0; }

  // Sends `cmd`, returns the reply ("" on timeout). Unsolicited event lines ("<3>...") are skipped.
  std::string request(const std::string& cmd, int timeout_ms = 2000) {
    if (fd_ < 0 || send(fd_, cmd.data(), cmd.size(), 0) < 0) return "";
    for (;;) {
      pollfd p{fd_, POLLIN, 0};
      if (poll(&p, 1, timeout_ms) <= 0) return "";
      char buf[16384];
      const ssize_t n = recv(fd_, buf, sizeof buf, 0);
      if (n < 0) return "";
      if (n > 0 && buf[0] == '<') continue;
      return std::string(buf, static_cast<size_t>(n));
    }
  }
  bool ok_reply(const std::string& cmd, std::string* error) {
    const std::string r = request(cmd);
    if (r.rfind("OK", 0) == 0) return true;
    if (error) *error = r.empty() ? "wpa_supplicant did not answer" : "wpa_supplicant refused: " + r.substr(0, r.find('\n'));
    return false;
  }

 private:
  void close_fd() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
  }
  int fd_ = -1;
};

std::string quoted(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

class WpaBackend : public Backend {
 public:
  WpaBackend(std::string dir, std::string sys_net) : dir_(std::move(dir)), sys_net_(std::move(sys_net)) {}

  std::string socket_for(const std::string& device) const {
    const fs::path p = fs::path(dir_) / device;
    std::error_code ec;
    return fs::exists(p, ec) && access(p.c_str(), W_OK) == 0 ? p.string() : "";
  }

  Snapshot snapshot() override {
    Snapshot s;
    s.backend = "wpa_supplicant";
    s.devices = read_system_devices(sys_net_);
    fill_addresses(&s.devices);
    bool any_controlled = false;
    for (Device& d : s.devices) {
      d.controllable = false;  // until a supplicant answers for it
      if (d.kind != Kind::Wifi) continue;
      const std::string path = socket_for(d.name);
      if (path.empty()) continue;
      Ctrl c(path);
      if (!c.ok()) continue;
      any_controlled = true;
      d.controllable = true;
      fill_wifi(&d, c);
    }
    s.can_control = any_controlled;
    s.note = any_controlled ? "Wi-Fi is handled by wpa_supplicant. Ethernet is shown for information only." : "";
    return s;
  }

  void scan(const std::string& device) override {
    Ctrl c(socket_for(device));
    if (c.ok()) c.request("SCAN");
  }

  bool connect(const std::string& device, const std::string& ssid, const std::string& password, std::string* error) override {
    Ctrl c(socket_for(device));
    if (!c.ok()) {
      if (error) *error = "cannot reach wpa_supplicant for " + device + " (is your user in the netdev group?)";
      return false;
    }
    const std::vector<WpaNetwork> known = parse_wpa_list_networks(c.request("LIST_NETWORKS"));
    int id = -1;
    for (const WpaNetwork& n : known)
      if (n.ssid == ssid) id = n.id;
    if (id >= 0 && !password.empty()) {  // a new password replaces the old profile
      c.request("REMOVE_NETWORK " + std::to_string(id));
      id = -1;
    }
    if (id < 0) {
      const std::string added = c.request("ADD_NETWORK");
      id = added.empty() ? -1 : std::atoi(added.c_str());
      if (id < 0 || !std::isdigit(static_cast<unsigned char>(added[0]))) {
        if (error) *error = "wpa_supplicant would not add the network";
        return false;
      }
      const std::string n = std::to_string(id);
      if (!c.ok_reply("SET_NETWORK " + n + " ssid " + hex_encode(ssid), error)) return false;
      if (password.empty()) {
        if (!c.ok_reply("SET_NETWORK " + n + " key_mgmt NONE", error)) return false;
      } else {
        if (password.size() < 8 || password.size() > 63) {
          c.request("REMOVE_NETWORK " + n);
          if (error) *error = "a Wi-Fi password has 8 to 63 characters";
          return false;
        }
        if (!c.ok_reply("SET_NETWORK " + n + " psk " + quoted(password), error)) {
          c.request("REMOVE_NETWORK " + n);
          return false;
        }
      }
    }
    if (!c.ok_reply("SELECT_NETWORK " + std::to_string(id), error)) return false;
    c.request("ENABLE_NETWORK " + std::to_string(id));
    c.request("SAVE_CONFIG");  // only works when the supplicant allows it; harmless otherwise
    return true;
  }

  bool disconnect(const std::string& device, std::string* error) override {
    Ctrl c(socket_for(device));
    if (!c.ok()) {
      if (error) *error = "cannot reach wpa_supplicant for " + device;
      return false;
    }
    return c.ok_reply("DISCONNECT", error);
  }

  bool forget(const std::string& device, const std::string& ssid, std::string* error) override {
    Ctrl c(socket_for(device));
    if (!c.ok()) {
      if (error) *error = "cannot reach wpa_supplicant for " + device;
      return false;
    }
    bool removed = false;
    for (const WpaNetwork& n : parse_wpa_list_networks(c.request("LIST_NETWORKS")))
      if (n.ssid == ssid) removed |= c.request("REMOVE_NETWORK " + std::to_string(n.id)).rfind("OK", 0) == 0;
    c.request("SAVE_CONFIG");
    if (!removed && error) *error = "no saved network with that name";
    return removed;
  }

 private:
  void fill_wifi(Device* d, Ctrl& c) {
    const auto st = parse_key_values(c.request("STATUS"));
    const std::string state = st.count("wpa_state") ? st.at("wpa_state") : "";
    if (state == "COMPLETED") d->state = State::Connected;
    else if (state == "ASSOCIATING" || state == "ASSOCIATED" || state == "AUTHENTICATING" ||
             state == "4WAY_HANDSHAKE" || state == "GROUP_HANDSHAKE")
      d->state = State::Connecting;
    else if (state == "INTERFACE_DISABLED") d->state = State::Unavailable;
    else d->state = State::Disconnected;
    if (st.count("ssid")) d->connection = unescape_wpa_ssid(st.at("ssid"));
    d->access_points = parse_wpa_scan_results(c.request("SCAN_RESULTS"));
    const std::vector<WpaNetwork> known = parse_wpa_list_networks(c.request("LIST_NETWORKS"));
    for (AccessPoint& ap : d->access_points) {
      ap.active = d->state == State::Connected && ap.ssid == d->connection;
      ap.saved = std::any_of(known.begin(), known.end(), [&](const WpaNetwork& n) { return n.ssid == ap.ssid; });
      if (ap.active) d->signal = ap.strength;
    }
    // Saved networks that are not in range are still worth listing as "saved"? No: only what is in range.
  }

  std::string dir_, sys_net_;
};

}  // namespace

std::string wpa_dir(const std::string& arg) {
  const char* env = std::getenv("FLEETWM_WPA_DIR");
  return !arg.empty() ? arg : env && *env ? env : "/run/wpa_supplicant";
}

bool wpa_available(const std::string& ctrl_arg) {
  const std::string ctrl_dir = wpa_dir(ctrl_arg);
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(ctrl_dir, ec)) {
    const std::string n = e.path().filename().string();
    if (n.rfind("p2p-dev-", 0) == 0) continue;
    if (access(e.path().c_str(), W_OK) == 0) return true;
  }
  return false;
}

std::unique_ptr<Backend> make_wpa_backend(const std::string& ctrl_dir, const std::string& sys_net) {
  return std::make_unique<WpaBackend>(wpa_dir(ctrl_dir), sys_net);
}

}  // namespace fleetwm::net
