// fleetwm-remote: the remote desktop server of the session. Serves the screen over VNC (RFB 3.8) and RDP (TLS security)
// when Settings -> Remote Desktop has them switched on, and plays the remote keyboard and mouse back through the
// compositor's virtual input protocols. No external VNC or RDP server is involved.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "capture.hpp"
#include "config_paths.hpp"
#include "hub.hpp"
#include "malloc_tuning.hpp"
#include "rdp.hpp"
#include "tls.hpp"
#include "remote_config.hpp"
#include "remote_usage.hpp"
#include "vnc.hpp"

namespace {

volatile sig_atomic_t g_quit = 0;
void on_signal(int) { g_quit = 1; }

constexpr int kMaxClientsPerProtocol = 4;
constexpr int kStatsIntervalSeconds = 10;  // how often the bar and Settings get fresh numbers (a change is written at once)

struct Listener {
  int fd = -1;
  int port = 0;
  std::string error;  // why it is not listening, shown in Settings
  void close_fd() {
    if (fd >= 0) close(fd);
    fd = -1;
  }
};

// Listens on all addresses (IPv6 socket that also takes IPv4), or IPv4 only when the machine has no IPv6.
int open_listener(int port, std::string* error) {
  int fd = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  int one = 1;
  if (fd >= 0) {
    int off = 0;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    sockaddr_in6 a6 = {};
    a6.sin6_family = AF_INET6;
    a6.sin6_addr = in6addr_any;
    a6.sin6_port = htons(static_cast<uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&a6), sizeof a6) == 0 && listen(fd, 8) == 0) return fd;
    if (errno != EAFNOSUPPORT) {
      *error = std::strerror(errno);
      close(fd);
      return -1;
    }
    close(fd);
  }
  fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    *error = std::strerror(errno);
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a4 = {};
  a4.sin_family = AF_INET;
  a4.sin_addr.s_addr = htonl(INADDR_ANY);
  a4.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(fd, reinterpret_cast<sockaddr*>(&a4), sizeof a4) != 0 || listen(fd, 8) != 0) {
    *error = std::strerror(errno);
    close(fd);
    return -1;
  }
  return fd;
}

void write_status(const Listener& vnc, bool vnc_on, const Listener& rdp, bool rdp_on, int clients) {
  auto line = [](const char* name, const Listener& l, bool on) {
    std::string s = std::string(name) + "=";
    if (!on) return s + "off\n";
    return s + (l.fd >= 0 ? "listening:" + std::to_string(l.port) : "error:" + l.error) + "\n";
  };
  const std::string path = fleetwm::remote_status_path();
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp);
    out << line("vnc", vnc, vnc_on) << line("rdp", rdp, rdp_on) << "clients=" << clients << "\n";
  }
  std::filesystem::rename(tmp, path);
}

}  // namespace

int main() {
  fleetwm::tune_malloc_for_low_rss();  // the session threads allocate and free whole pictures; give the memory back
  signal(SIGPIPE, SIG_IGN);
  struct sigaction sa = {};
  sa.sa_handler = on_signal;
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGHUP, &sa, nullptr);

  // One daemon per user session.
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  const std::string lock_path = std::string(runtime && *runtime ? runtime : "/tmp") + "/fleetwm-remote.lock";
  const int lock_fd = open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) return 0;

  fleetwm::RemoteConfig cfg = fleetwm::load_remote_config();
  if (!cfg.vnc_enabled && !cfg.rdp_enabled) return 0;  // started at login with nothing switched on

  fleetremote::Hub hub;
  fleetremote::Capture capture(hub);
  if (!capture.connect()) return 1;

  const std::string tls_dir = (fleetwm::config_internal::config_home() / "fleetwm" / "remote").string();
  SSL_CTX* tls = nullptr;
  const std::string rdp_user = fleetwm::remote_login_name();

  Listener vnc, rdp;
  bool vnc_on = false, rdp_on = false;
  fleetwm::RemoteConfig live;  // what the listeners were opened with
  struct stat last_st = {};
  bool have_st = false;
  int last_clients = -1;

  auto apply = [&](const fleetwm::RemoteConfig& c) {
    // Quality and bandwidth are read live by the sessions: no reconnect.
    hub.vnc_tuning.quality = c.vnc_quality;
    hub.vnc_tuning.max_kbps = c.vnc_max_kbps;
    hub.vnc_tuning.fps = c.vnc_fps;
    hub.rdp_tuning.fps = c.rdp_fps;
    hub.rdp_tuning.quality = c.rdp_quality;
    hub.rdp_tuning.max_kbps = c.rdp_max_kbps;
    int rw = 0, rh = 0;
    fleetwm::parse_resolution(c.vnc_resolution, &rw, &rh);
    hub.vnc_tuning.res_w = rw;
    hub.vnc_tuning.res_h = rh;
    hub.vnc_tuning.depth = c.vnc_depth;
    fleetwm::parse_resolution(c.rdp_resolution, &rw, &rh);
    const bool rdp_picture_changed = hub.rdp_tuning.res_w != rw || hub.rdp_tuning.res_h != rh || hub.rdp_tuning.depth != c.rdp_depth;
    hub.rdp_tuning.res_w = rw;
    hub.rdp_tuning.res_h = rh;
    hub.rdp_tuning.depth = c.rdp_depth;
    if (rdp_on && rdp_picture_changed) hub.kick("rdp");  // an RDP picture size and depth are fixed when the viewer connects
    if ((c.vnc_enabled && c.vnc_tls) || (c.rdp_enabled && c.rdp_tls)) {
      if (!tls) tls = fleetremote::make_tls_context(tls_dir);
    }
    // VNC
    if (vnc_on && (!c.vnc_enabled || c.vnc_port != live.vnc_port || c.vnc_password != live.vnc_password || c.vnc_tls != live.vnc_tls)) {
      vnc.close_fd();
      hub.kick("vnc");
    }
    vnc_on = c.vnc_enabled;
    if (vnc_on && vnc.fd < 0) {
      vnc.port = c.vnc_port;
      vnc.error.clear();
      if (c.vnc_password.empty()) vnc.error = "no password set";
      else if (c.vnc_tls && !tls) vnc.error = "cannot create the TLS certificate";
      else vnc.fd = open_listener(vnc.port, &vnc.error);
    }
    // RDP
    if (rdp_on && (!c.rdp_enabled || c.rdp_port != live.rdp_port || c.rdp_password != live.rdp_password || c.rdp_tls != live.rdp_tls)) {
      rdp.close_fd();
      hub.kick("rdp");
    }
    rdp_on = c.rdp_enabled;
    if (rdp_on && rdp.fd < 0) {
      rdp.port = c.rdp_port;
      rdp.error.clear();
      if (c.rdp_password.empty()) rdp.error = "no password set";
      else if (c.rdp_tls && !tls) rdp.error = "cannot create the TLS certificate";
      else rdp.fd = open_listener(rdp.port, &rdp.error);
    }
    live = c;
    write_status(vnc, vnc_on, rdp, rdp_on, hub.count());
  };
  apply(cfg);

  auto accept_one = [&](Listener& l, const char* proto) {
    const int fd = accept4(l.fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) return;
    if (hub.count(proto) >= kMaxClientsPerProtocol) {
      close(fd);
      return;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
    fleetremote::ServeOptions opt;
    if (std::string(proto) == "vnc") {
      opt.password = live.vnc_password;
      opt.tls = live.vnc_tls ? tls : nullptr;
      std::thread([&hub, fd, opt] { fleetremote::serve_vnc(hub, fd, opt); }).detach();
    } else {
      opt.user = rdp_user;
      opt.password = live.rdp_password;
      opt.tls = live.rdp_tls ? tls : nullptr;
      std::thread([&hub, fd, opt] { fleetremote::serve_rdp(hub, fd, opt); }).detach();
    }
  };

  // The config file is watched with inotify, so an idle daemon sleeps until something happens; if the directory cannot be
  // watched it checks the file once a second instead.
  const std::string cfg_path = fleetwm::remote_user_config_path();
  const std::string cfg_dir = std::filesystem::path(cfg_path).parent_path().string();
  const int inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  const bool watching = inotify_fd >= 0 && inotify_add_watch(inotify_fd, cfg_dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE) >= 0;

  // Reloads the config when the file changed; true when both servers are now off and the daemon should end.
  auto reload_if_changed = [&]() {
    struct stat st = {};
    const bool ok = stat(cfg_path.c_str(), &st) == 0;
    if (ok == have_st && (!ok || (st.st_mtim.tv_sec == last_st.st_mtim.tv_sec && st.st_mtim.tv_nsec == last_st.st_mtim.tv_nsec))) return false;
    have_st = ok;
    last_st = st;
    cfg = fleetwm::load_remote_config();
    apply(cfg);
    return !cfg.vnc_enabled && !cfg.rdp_enabled;
  };
  reload_if_changed();  // a change made between the first load and the watch

  const char* const kProto[2] = {"vnc", "rdp"};
  fleetwm::UsageHistory usage = fleetwm::load_usage();
  bool usage_dirty = false;
  auto last_save = std::chrono::steady_clock::now();
  fleetremote::Hub::ProtoTotals prev[2];
  auto prev_time = std::chrono::steady_clock::now();
  double active_frac[2] = {0, 0};
  bool stats_on = false;  // a viewer is connected: tick every ten seconds (and at once when one comes or goes)
  auto next_tick = std::chrono::steady_clock::now();

  auto write_live = [&](const fleetwm::RemoteLive& live) {
    const std::string dir = fleetwm::remote_stats_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    chmod(dir.c_str(), 0700);
    const std::string path = fleetwm::remote_stats_path(), tmp = path + ".tmp";
    {
      std::ofstream out(tmp);
      out << fleetwm::format_remote_live(live, std::time(nullptr));
    }
    std::filesystem::rename(tmp, path, ec);
  };
  auto save_usage_now = [&]() {
    if (!usage_dirty) return;
    try {
      fleetwm::save_usage(usage);
      usage_dirty = false;
    } catch (const std::exception& e) {
      std::fprintf(stderr, "fleetwm-remote: cannot save the usage tally: %s\n", e.what());
      usage_dirty = false;  // do not retry every second
    }
    last_save = std::chrono::steady_clock::now();
  };
  // Takes the numbers: rates and CPU over the time since the last tick, the new bytes into today's tally.
  auto stats_tick = [&]() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::max(0.001, std::chrono::duration<double>(now - prev_time).count());
    fleetwm::RemoteLive live;
    live.valid = true;
    const std::string day = fleetwm::usage_day_key(std::time(nullptr));
    for (int p = 0; p < 2; ++p) {
      const auto cur = hub.totals(kProto[p]);
      const uint64_t dtx = cur.tx >= prev[p].tx ? cur.tx - prev[p].tx : 0, drx = cur.rx >= prev[p].rx ? cur.rx - prev[p].rx : 0;
      const uint64_t dcpu = cur.cpu_ns >= prev[p].cpu_ns ? cur.cpu_ns - prev[p].cpu_ns : 0;
      fleetwm::RemoteLiveProto& l = live.proto[p];
      l.clients = cur.clients;
      l.tx = cur.tx;
      l.rx = cur.rx;
      l.tx_rate = static_cast<double>(dtx) / dt;
      l.rx_rate = static_cast<double>(drx) / dt;
      l.cpu_percent = static_cast<double>(dcpu) / (dt * 1e9) * 100.0;
      l.ram = cur.mem;
      fleetwm::DayUsage d;
      d.tx = dtx;
      d.rx = drx;
      d.cpu_ms = dcpu / 1000000;
      if (cur.clients > 0) {
        active_frac[p] += dt;
        d.active_s = static_cast<uint64_t>(active_frac[p]);
        active_frac[p] -= static_cast<double>(d.active_s);
      }
      if (d.tx || d.rx || d.cpu_ms || d.active_s) {
        fleetwm::add_usage(&usage, day, p, d);
        usage_dirty = true;
      }
      prev[p] = cur;
    }
    prev_time = now;
    write_live(live);
    if (now - last_save >= std::chrono::seconds(60)) save_usage_now();
    next_tick = now + std::chrono::seconds(kStatsIntervalSeconds);
  };
  // A viewer arrived or the last one left.
  auto stats_clients_changed = [&](int clients) {
    if (clients > 0 && !stats_on) {
      stats_on = true;
      for (int p = 0; p < 2; ++p) prev[p] = hub.totals(kProto[p]);
      prev_time = std::chrono::steady_clock::now();
      stats_tick();
    } else if (clients == 0 && stats_on) {
      stats_on = false;
      stats_tick();  // the last bytes, then zero viewers so the bar hides its indicator
      save_usage_now();
    }
  };

  while (!g_quit) {
    pollfd fds[6] = {{capture.fd(), POLLIN, 0}, {hub.input_fd(), POLLIN, 0}, {hub.ctl_fd(), POLLIN, 0},
                     {vnc.fd, POLLIN, 0},       {rdp.fd, POLLIN, 0},       {watching ? inotify_fd : -1, POLLIN, 0}};
    int timeout = watching ? -1 : 1000;
    if (stats_on) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(next_tick - std::chrono::steady_clock::now()).count();
      const int ms = left > 0 ? static_cast<int>(left) : 0;
      timeout = timeout < 0 ? ms : std::min(timeout, ms);
    }
    const int retry = capture.retry_timeout_ms();
    if (retry >= 0) timeout = timeout < 0 ? retry : std::min(timeout, retry);
    if (poll(fds, 6, timeout) < 0 && errno != EINTR) break;
    if (fds[0].revents & (POLLERR | POLLHUP)) {
      std::fprintf(stderr, "fleetwm-remote: lost the compositor\n");
      break;
    }
    if (fds[0].revents & POLLIN && !capture.dispatch()) break;
    if (fds[1].revents & POLLIN) {
      for (const fleetremote::InputEvent& ev : hub.drain_input()) capture.handle_input(ev);
      capture.flush();
    }
    if (fds[2].revents & POLLIN) {
      uint64_t drain;
      [[maybe_unused]] ssize_t r = read(hub.ctl_fd(), &drain, sizeof drain);
      capture.set_active(hub.count() > 0);
      const int clients = hub.count();
      if (clients != last_clients) {
        last_clients = clients;
        write_status(vnc, vnc_on, rdp, rdp_on, clients);
      }
      stats_clients_changed(clients);
    }
    if (fds[3].revents & POLLIN) accept_one(vnc, "vnc");
    if (fds[4].revents & POLLIN) accept_one(rdp, "rdp");
    capture.retry_if_due();
    if (stats_on && std::chrono::steady_clock::now() >= next_tick) stats_tick();
    bool check = !watching;
    if (fds[5].revents & POLLIN) {
      char buf[4096];
      while (read(inotify_fd, buf, sizeof buf) > 0) {}
      check = true;
    }
    if (check && reload_if_changed()) break;  // both switched off in Settings: nothing left to do
  }

  hub.kick_all();
  for (int i = 0; i < 60 && hub.count() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  vnc.close_fd();
  rdp.close_fd();
  if (stats_on) stats_tick();
  save_usage_now();
  if (inotify_fd >= 0) close(inotify_fd);
  std::remove(fleetwm::remote_status_path().c_str());
  std::remove(fleetwm::remote_stats_path().c_str());
  if (hub.count() > 0) _exit(0);  // a session thread is stuck in a blocking call; leave it to the OS
  return 0;
}
