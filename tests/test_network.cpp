#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "network_parse.hpp"
#include "system_devices.hpp"
#include "wpa_backend.hpp"

using namespace fleetwm::net;
namespace fs = std::filesystem;

TEST(NetworkParse, DbmToPercent) {
  EXPECT_EQ(dbm_to_percent(-30), 100);
  EXPECT_EQ(dbm_to_percent(-50), 100);
  EXPECT_EQ(dbm_to_percent(-70), 60);
  EXPECT_EQ(dbm_to_percent(-100), 0);
  EXPECT_EQ(dbm_to_percent(-120), 0);
}

TEST(NetworkParse, ProcWirelessGivesPercent) {
  const std::string text =
      "Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
      " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n"
      "wlan0: 0000   35.  -75.  -256        0      0      0      0      0        0\n";
  const auto q = parse_proc_wireless(text);
  ASSERT_EQ(q.count("wlan0"), 1u);
  EXPECT_EQ(q.at("wlan0"), 50);
}

TEST(NetworkParse, DefaultGateway) {
  const std::string route =
      "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
      "wlan0\t00000000\t0100A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n"
      "wlan0\t0000A8C0\t00000000\t0001\t0\t0\t600\t00FFFFFF\t0\t0\t0\n";
  EXPECT_EQ(parse_default_gateway(route, "wlan0"), "192.168.0.1");
  EXPECT_EQ(parse_default_gateway(route, "eth0"), "");
}

TEST(NetworkParse, ScanResultsKeepStrongestPerNameAndSkipHidden) {
  const std::string text =
      "bssid / frequency / signal level / flags / ssid\n"
      "aa:aa:aa:aa:aa:01\t2412\t-80\t[WPA2-PSK-CCMP][ESS]\tHome\n"
      "aa:aa:aa:aa:aa:02\t5180\t-55\t[WPA2-PSK-CCMP][ESS]\tHome\n"
      "bb:bb:bb:bb:bb:01\t2437\t-65\t[ESS]\tCafe\n"
      "cc:cc:cc:cc:cc:01\t2462\t-40\t[WPA2-PSK-CCMP][ESS]\t\n"
      "dd:dd:dd:dd:dd:01\t2462\t-90\t[WPA2-PSK-CCMP][ESS]\tCaf\\xc3\\xa9 2\n";
  const auto aps = parse_wpa_scan_results(text);
  ASSERT_EQ(aps.size(), 3u);
  EXPECT_EQ(aps[0].ssid, "Home");
  EXPECT_EQ(aps[0].bssid, "aa:aa:aa:aa:aa:02");
  EXPECT_TRUE(aps[0].secured);
  EXPECT_EQ(aps[1].ssid, "Cafe");
  EXPECT_FALSE(aps[1].secured);
  EXPECT_EQ(aps[2].ssid, "Caf\xc3\xa9 2");
}

TEST(NetworkParse, StatusAndNetworkList) {
  const auto st = parse_key_values("bssid=aa:bb\nssid=Home\nwpa_state=COMPLETED\nip_address=192.168.0.9\n");
  EXPECT_EQ(st.at("wpa_state"), "COMPLETED");
  EXPECT_EQ(st.at("ssid"), "Home");
  const auto nets = parse_wpa_list_networks("network id / ssid / bssid / flags\n0\tHome\tany\t[CURRENT]\n1\tWork\tany\t\n");
  ASSERT_EQ(nets.size(), 2u);
  EXPECT_TRUE(nets[0].current);
  EXPECT_EQ(nets[1].ssid, "Work");
  EXPECT_FALSE(nets[1].current);
}

TEST(NetworkParse, HexAndSpeedAndDescriptions) {
  EXPECT_EQ(hex_encode("Ab"), "4162");
  EXPECT_EQ(format_speed(0), "");
  EXPECT_EQ(format_speed(100), "100 Mbit/s");
  EXPECT_EQ(format_speed(1000), "1 Gbit/s");
  EXPECT_EQ(format_speed(2500), "2.5 Gbit/s");
  Device eth;
  eth.kind = Kind::Ethernet;
  eth.state = State::Unavailable;
  EXPECT_EQ(describe_device(eth), "Cable unplugged");
  eth.state = State::Connected;
  eth.speed_mbps = 1000;
  EXPECT_EQ(describe_device(eth), "Connected, 1 Gbit/s");
  Device wifi;
  wifi.kind = Kind::Wifi;
  wifi.state = State::Connected;
  wifi.connection = "Home";
  wifi.signal = 82;
  EXPECT_EQ(describe_device(wifi), "Connected to Home (82%)");
}

TEST(NetworkParse, PrimaryDevicePrefersConnectedEthernetThenWifi) {
  Device eth, wifi;
  eth.kind = Kind::Ethernet;
  wifi.kind = Kind::Wifi;
  std::vector<Device> v{eth, wifi};
  EXPECT_EQ(primary_device(v)->kind, Kind::Wifi);  // none connected: the Wi-Fi card
  v[1].state = State::Connected;
  EXPECT_EQ(primary_device(v)->kind, Kind::Wifi);
  v[0].state = State::Connected;
  EXPECT_EQ(primary_device(v)->kind, Kind::Ethernet);
  EXPECT_EQ(primary_device({}), nullptr);
}

namespace {
struct FakeSys {
  fs::path root;
  FakeSys() {
    root = fs::temp_directory_path() / ("fleetwm-sys-" + std::to_string(getpid()));
    fs::remove_all(root);
    auto put = [](const fs::path& p, const std::string& v) {
      fs::create_directories(p.parent_path());
      std::ofstream(p) << v << "\n";
    };
    put(root / "eth0/address", "aa:bb:cc:dd:ee:01");
    put(root / "eth0/type", "1");
    put(root / "eth0/operstate", "up");
    put(root / "eth0/carrier", "1");
    put(root / "eth0/speed", "1000");
    fs::create_directories(root / "eth0/device");
    fs::create_symlink("/nowhere/r8169", root / "eth0/device/driver");
    put(root / "wlan0/address", "aa:bb:cc:dd:ee:02");
    put(root / "wlan0/type", "1");
    put(root / "wlan0/operstate", "down");
    fs::create_directories(root / "wlan0/device");
    fs::create_directories(root / "wlan0/wireless");
    fs::create_symlink("/nowhere/rtl8xxxu", root / "wlan0/device/driver");
    put(root / "lo/address", "00:00:00:00:00:00");
    put(root / "lo/type", "772");
    put(root / "tun0/type", "65534");
    put(root / "docker0/type", "1");  // virtual: no device/ directory
  }
  ~FakeSys() { fs::remove_all(root); }
};
}  // namespace

TEST(SystemDevices, ListsOnlyRealCardsEthernetFirst) {
  FakeSys sys;
  const auto devs = read_system_devices(sys.root.string());
  ASSERT_EQ(devs.size(), 2u);
  EXPECT_EQ(devs[0].name, "eth0");
  EXPECT_EQ(devs[0].kind, Kind::Ethernet);
  EXPECT_EQ(devs[0].state, State::Connected);
  EXPECT_EQ(devs[0].speed_mbps, 1000);
  EXPECT_EQ(devs[0].driver, "r8169");
  EXPECT_EQ(devs[1].name, "wlan0");
  EXPECT_EQ(devs[1].kind, Kind::Wifi);
  EXPECT_EQ(devs[1].state, State::Disconnected);
  EXPECT_EQ(devs[1].driver, "rtl8xxxu");
}

TEST(SystemDevices, UnpluggedCableIsUnavailable) {
  FakeSys sys;
  std::ofstream(sys.root / "eth0/operstate") << "down\n";
  std::ofstream(sys.root / "eth0/carrier") << "0\n";
  EXPECT_EQ(read_system_devices(sys.root.string())[0].state, State::Unavailable);
}

namespace {
// A pretend wpa_supplicant: answers on <dir>/wlan0 and records every command it received.
class FakeSupplicant {
 public:
  explicit FakeSupplicant(const fs::path& dir) {
    fs::create_directories(dir);
    path_ = (dir / "wlan0").string();
    fd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strcpy(a.sun_path, path_.c_str());
    bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a);
    timeval tv{0, 30000};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    thread_ = std::thread([this] { loop(); });
  }
  ~FakeSupplicant() {
    stop_ = true;
    thread_.join();
    close(fd_);
  }
  std::vector<std::string> commands() {
    std::lock_guard<std::mutex> l(m_);
    return cmds_;
  }
  std::string state = "COMPLETED";

 private:
  void loop() {
    while (!stop_) {
      char buf[1024];
      sockaddr_un from{};
      socklen_t len = sizeof from;
      const ssize_t n = recvfrom(fd_, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
      if (n <= 0) continue;
      const std::string cmd(buf, static_cast<size_t>(n));
      {
        std::lock_guard<std::mutex> l(m_);
        cmds_.push_back(cmd);
      }
      std::string reply = "OK\n";
      if (cmd == "STATUS") reply = "bssid=aa:aa:aa:aa:aa:02\nssid=Home\nwpa_state=" + state + "\n";
      else if (cmd == "SCAN_RESULTS")
        reply = "bssid / frequency / signal level / flags / ssid\n"
                "aa:aa:aa:aa:aa:02\t5180\t-55\t[WPA2-PSK-CCMP][ESS]\tHome\n"
                "bb:bb:bb:bb:bb:01\t2437\t-65\t[ESS]\tCafe\n";
      else if (cmd == "LIST_NETWORKS") reply = "network id / ssid / bssid / flags\n0\tHome\tany\t[CURRENT]\n";
      else if (cmd == "ADD_NETWORK") reply = "7\n";
      sendto(fd_, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&from), len);
    }
  }
  int fd_ = -1;
  std::string path_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::mutex m_;
  std::vector<std::string> cmds_;
};

struct WpaFixture : ::testing::Test {
  FakeSys sys;
  fs::path ctrl = fs::temp_directory_path() / ("fleetwm-wpa-" + std::to_string(getpid()));
  void SetUp() override { fs::remove_all(ctrl); }
  void TearDown() override { fs::remove_all(ctrl); }
};
}  // namespace

TEST_F(WpaFixture, AvailableOnlyWithASocket) {
  EXPECT_FALSE(wpa_available(ctrl.string()));
  FakeSupplicant sup(ctrl);
  EXPECT_TRUE(wpa_available(ctrl.string()));
}

TEST_F(WpaFixture, SnapshotShowsConnectionAndNetworksInRange) {
  FakeSupplicant sup(ctrl);
  auto backend = make_wpa_backend(ctrl.string(), sys.root.string());
  const Snapshot s = backend->snapshot();
  EXPECT_TRUE(s.can_control);
  ASSERT_EQ(s.devices.size(), 2u);
  const Device& w = s.devices[1];
  EXPECT_EQ(w.state, State::Connected);
  EXPECT_EQ(w.connection, "Home");
  EXPECT_EQ(w.signal, 90);
  ASSERT_EQ(w.access_points.size(), 2u);
  EXPECT_TRUE(w.access_points[0].active);
  EXPECT_TRUE(w.access_points[0].saved);
  EXPECT_EQ(w.access_points[1].ssid, "Cafe");
  EXPECT_FALSE(w.access_points[1].secured);
  EXPECT_FALSE(w.access_points[1].saved);
}

TEST_F(WpaFixture, ConnectingToANewSecuredNetworkAddsAProfileAndSelectsIt) {
  FakeSupplicant sup(ctrl);
  auto backend = make_wpa_backend(ctrl.string(), sys.root.string());
  std::string err;
  ASSERT_TRUE(backend->connect("wlan0", "Work", "hunter2hunter2", &err)) << err;
  const auto cmds = sup.commands();
  EXPECT_NE(std::find(cmds.begin(), cmds.end(), "SET_NETWORK 7 ssid 576f726b"), cmds.end());
  EXPECT_NE(std::find(cmds.begin(), cmds.end(), "SET_NETWORK 7 psk \"hunter2hunter2\""), cmds.end());
  EXPECT_NE(std::find(cmds.begin(), cmds.end(), "SELECT_NETWORK 7"), cmds.end());
}

TEST_F(WpaFixture, OpenNetworkUsesNoKeyManagement) {
  FakeSupplicant sup(ctrl);
  auto backend = make_wpa_backend(ctrl.string(), sys.root.string());
  std::string err;
  ASSERT_TRUE(backend->connect("wlan0", "Cafe", "", &err)) << err;
  const auto cmds = sup.commands();
  EXPECT_NE(std::find(cmds.begin(), cmds.end(), "SET_NETWORK 7 key_mgmt NONE"), cmds.end());
}

TEST_F(WpaFixture, SavedNetworkIsSelectedWithoutAPassword) {
  FakeSupplicant sup(ctrl);
  auto backend = make_wpa_backend(ctrl.string(), sys.root.string());
  std::string err;
  ASSERT_TRUE(backend->connect("wlan0", "Home", "", &err)) << err;
  const auto cmds = sup.commands();
  EXPECT_NE(std::find(cmds.begin(), cmds.end(), "SELECT_NETWORK 0"), cmds.end());
  EXPECT_EQ(std::find(cmds.begin(), cmds.end(), "ADD_NETWORK"), cmds.end());
}

TEST_F(WpaFixture, ShortPasswordIsRefusedAndNothingIsLeftBehind) {
  FakeSupplicant sup(ctrl);
  auto backend = make_wpa_backend(ctrl.string(), sys.root.string());
  std::string err;
  EXPECT_FALSE(backend->connect("wlan0", "Work", "short", &err));
  EXPECT_NE(err.find("8 to 63"), std::string::npos);
  const auto cmds = sup.commands();
  EXPECT_NE(std::find(cmds.begin(), cmds.end(), "REMOVE_NETWORK 7"), cmds.end());
}

TEST_F(WpaFixture, DisconnectAndForget) {
  FakeSupplicant sup(ctrl);
  auto backend = make_wpa_backend(ctrl.string(), sys.root.string());
  std::string err;
  EXPECT_TRUE(backend->disconnect("wlan0", &err));
  EXPECT_TRUE(backend->forget("wlan0", "Home", &err));
  EXPECT_FALSE(backend->forget("wlan0", "Nope", &err));
  const auto cmds = sup.commands();
  EXPECT_NE(std::find(cmds.begin(), cmds.end(), "DISCONNECT"), cmds.end());
  EXPECT_NE(std::find(cmds.begin(), cmds.end(), "REMOVE_NETWORK 0"), cmds.end());
}
