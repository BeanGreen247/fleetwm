#include "remote_usage.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fleetwm {

namespace fs = std::filesystem;

std::string remote_stats_dir() {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  return std::string(runtime && *runtime ? runtime : "/tmp") + "/fleetwm-remote";
}
std::string remote_stats_path() { return remote_stats_dir() + "/stats"; }

namespace {
const char* const kProtoName[2] = {"vnc", "rdp"};
}

std::string format_remote_live(const RemoteLive& live, time_t now) {
  std::ostringstream o;
  o << "time=" << static_cast<long long>(now) << "\n";
  for (int p = 0; p < 2; ++p) {
    const RemoteLiveProto& s = live.proto[p];
    o << kProtoName[p] << ".clients=" << s.clients << "\n"
      << kProtoName[p] << ".tx=" << s.tx << "\n"
      << kProtoName[p] << ".rx=" << s.rx << "\n"
      << kProtoName[p] << ".tx_rate=" << static_cast<long long>(s.tx_rate) << "\n"
      << kProtoName[p] << ".rx_rate=" << static_cast<long long>(s.rx_rate) << "\n"
      << kProtoName[p] << ".cpu_permille=" << static_cast<long long>(s.cpu_percent * 10 + 0.5) << "\n"
      << kProtoName[p] << ".ram=" << s.ram << "\n";
  }
  return o.str();
}

bool parse_remote_live(const std::string& text, RemoteLive* out, time_t* written) {
  RemoteLive live;
  bool have_time = false;
  long long t = 0;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);
    char* end = nullptr;
    const unsigned long long n = std::strtoull(val.c_str(), &end, 10);
    if (end == val.c_str()) continue;
    if (key == "time") {
      t = static_cast<long long>(n);
      have_time = true;
      continue;
    }
    for (int p = 0; p < 2; ++p) {
      const std::string prefix = std::string(kProtoName[p]) + ".";
      if (key.rfind(prefix, 0) != 0) continue;
      const std::string f = key.substr(prefix.size());
      RemoteLiveProto& s = live.proto[p];
      if (f == "clients") s.clients = static_cast<int>(n);
      else if (f == "tx") s.tx = n;
      else if (f == "rx") s.rx = n;
      else if (f == "tx_rate") s.tx_rate = static_cast<double>(n);
      else if (f == "rx_rate") s.rx_rate = static_cast<double>(n);
      else if (f == "cpu_permille") s.cpu_percent = static_cast<double>(n) / 10.0;
      else if (f == "ram") s.ram = n;
    }
  }
  if (!have_time) return false;
  live.valid = true;
  *out = live;
  *written = static_cast<time_t>(t);
  return true;
}

RemoteLive load_remote_live() {
  std::ifstream in(remote_stats_path());
  if (!in) return {};
  std::stringstream buf;
  buf << in.rdbuf();
  RemoteLive live;
  time_t written = 0;
  if (!parse_remote_live(buf.str(), &live, &written)) return {};
  if (std::time(nullptr) - written > 25) return {};  // the daemon writes every 10 s while a viewer is connected; longer means it died
  return live;
}

std::string remote_live_label(const RemoteLive& live) {
  const bool v = live.proto[kProtoVnc].clients > 0, r = live.proto[kProtoRdp].clients > 0;
  return v && r ? "VNC + RDP" : v ? "VNC" : r ? "RDP" : "";
}

std::string remote_format_bytes(double b) {
  char buf[48];
  if (b < 1024) std::snprintf(buf, sizeof buf, "%.0f B", b);
  else if (b < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024);
  else if (b < 1024.0 * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024 * 1024));
  else std::snprintf(buf, sizeof buf, "%.2f GB", b / (1024.0 * 1024 * 1024));
  return buf;
}

std::string remote_format_rate(double b) { return remote_format_bytes(b) + "/s"; }

std::string remote_format_duration(uint64_t s) {
  char buf[48];
  if (s < 60) std::snprintf(buf, sizeof buf, "%llu s", static_cast<unsigned long long>(s));
  else if (s < 3600) std::snprintf(buf, sizeof buf, "%llu min", static_cast<unsigned long long>(s / 60));
  else std::snprintf(buf, sizeof buf, "%llu h %02llu min", static_cast<unsigned long long>(s / 3600), static_cast<unsigned long long>((s % 3600) / 60));
  return buf;
}

std::string remote_live_tooltip(const RemoteLive& live) {
  std::string t = "Remote desktop";
  for (int p = 0; p < 2; ++p) {
    const RemoteLiveProto& s = live.proto[p];
    if (s.clients <= 0) continue;
    char cpu[32];
    std::snprintf(cpu, sizeof cpu, "%.1f%%", s.cpu_percent);
    t += std::string("\n\n") + (p == kProtoVnc ? "VNC" : "RDP") + ": " + std::to_string(s.clients) + (s.clients == 1 ? " viewer" : " viewers");
    t += "\nSent " + remote_format_bytes(static_cast<double>(s.tx)) + ", received " + remote_format_bytes(static_cast<double>(s.rx));
    t += "\nNow: up " + remote_format_rate(s.tx_rate) + ", down " + remote_format_rate(s.rx_rate);
    t += std::string("\nCPU ") + cpu + ", memory " + remote_format_bytes(static_cast<double>(s.ram));
  }
  return t + "\n\nUp is the picture sent to the viewer, down is its keys and mouse.\nClick to open Remote Desktop settings";
}

// ---- the tally ----

std::string usage_path() {
  const char* state = std::getenv("XDG_STATE_HOME");
  const char* home = std::getenv("HOME");
  fs::path base = state && *state ? fs::path(state) : fs::path(home && *home ? home : "/tmp") / ".local" / "state";
  return (base / "fleetwm" / "remote-usage.txt").string();
}

std::string usage_day_key(time_t t) {
  tm lt;
  localtime_r(&t, &lt);
  char buf[16];
  std::strftime(buf, sizeof buf, "%Y-%m-%d", &lt);
  return buf;
}

UsageHistory load_usage() {
  UsageHistory h;
  std::ifstream in(usage_path());
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string day, proto;
    DayUsage u;
    if (!(ls >> day >> proto >> u.tx >> u.rx >> u.active_s >> u.cpu_ms)) continue;
    const int p = proto == "vnc" ? 0 : proto == "rdp" ? 1 : -1;
    if (p < 0) continue;
    if (day == "total") h.all_time[static_cast<size_t>(p)] = u;
    else h.days[day][static_cast<size_t>(p)] = u;
  }
  return h;
}

void save_usage(const UsageHistory& h) {
  const fs::path path = usage_path();
  fs::create_directories(path.parent_path());
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("failed to open " + tmp.string() + " for writing");
    for (int p = 0; p < 2; ++p) {
      const DayUsage& u = h.all_time[static_cast<size_t>(p)];
      out << "total " << kProtoName[p] << " " << u.tx << " " << u.rx << " " << u.active_s << " " << u.cpu_ms << "\n";
    }
    for (const auto& [day, arr] : h.days)
      for (int p = 0; p < 2; ++p) {
        const DayUsage& u = arr[static_cast<size_t>(p)];
        if (u.tx || u.rx || u.active_s || u.cpu_ms) out << day << " " << kProtoName[p] << " " << u.tx << " " << u.rx << " " << u.active_s << " " << u.cpu_ms << "\n";
      }
    if (!out) throw std::runtime_error("failed to write " + tmp.string());
  }
  fs::rename(tmp, path);
}

void add_usage(UsageHistory* h, const std::string& day, int proto, const DayUsage& delta) {
  h->days[day][static_cast<size_t>(proto)] += delta;
  h->all_time[static_cast<size_t>(proto)] += delta;
  while (h->days.size() > static_cast<size_t>(kUsageKeepDays)) h->days.erase(h->days.begin());  // keys sort by date
}

UsageAverage average_usage(const UsageHistory& h, int proto, int days, time_t now) {
  UsageAverage a;
  if (days <= 0) return a;
  DayUsage sum;
  for (int i = 0; i < days; ++i) {
    auto it = h.days.find(usage_day_key(now - static_cast<time_t>(i) * 86400));
    if (it != h.days.end()) sum += it->second[static_cast<size_t>(proto)];
  }
  a.tx = static_cast<double>(sum.tx) / days;
  a.rx = static_cast<double>(sum.rx) / days;
  a.active_s = static_cast<double>(sum.active_s) / days;
  a.cpu_s = static_cast<double>(sum.cpu_ms) / 1000.0 / days;
  return a;
}

}  // namespace fleetwm
