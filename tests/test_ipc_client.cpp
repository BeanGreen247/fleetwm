#include "ipc_client.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fleetwm {
namespace {

class IpcSocketPathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* existing = std::getenv("XDG_RUNTIME_DIR");
    had_old_value_ = existing != nullptr;
    if (had_old_value_) {
      old_value_ = existing;
    }
  }
  void TearDown() override {
    if (had_old_value_) {
      ::setenv("XDG_RUNTIME_DIR", old_value_.c_str(), 1);
    } else {
      ::unsetenv("XDG_RUNTIME_DIR");
    }
  }

  bool had_old_value_ = false;
  std::string old_value_;
};

TEST_F(IpcSocketPathTest, UsesXdgRuntimeDirWhenSet) {
  ::setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
  EXPECT_EQ(ipc_socket_path(), "/run/user/1000/fleetwm.sock");
}

TEST_F(IpcSocketPathTest, FallsBackToTmpWhenUnset) {
  ::unsetenv("XDG_RUNTIME_DIR");
  EXPECT_EQ(ipc_socket_path(), "/tmp/fleetwm.sock");
}

TEST_F(IpcSocketPathTest, FallsBackToTmpWhenEmpty) {
  ::setenv("XDG_RUNTIME_DIR", "", 1);
  EXPECT_EQ(ipc_socket_path(), "/tmp/fleetwm.sock");
}

TEST_F(IpcSocketPathTest, AlwaysEndsWithFleetwmSock) {
  ::setenv("XDG_RUNTIME_DIR", "/run/user/42", 1);
  EXPECT_NE(ipc_socket_path().find("fleetwm.sock"), std::string::npos);
}

TEST_F(IpcSocketPathTest, RuntimeDirWithTrailingSlashDoublesUpSlash) {
  // Not normalized -- ipc_socket_path() is a plain string concatenation,
  // so a trailing slash in XDG_RUNTIME_DIR produces a doubled slash
  // rather than being silently collapsed. Still a valid path (the OS
  // treats "//" the same as "/"), just documenting the actual behavior
  // so a future "let's normalize this" refactor doesn't accidentally
  // change the resolved path for the common no-trailing-slash case.
  ::setenv("XDG_RUNTIME_DIR", "/run/user/1000/", 1);
  EXPECT_EQ(ipc_socket_path(), "/run/user/1000//fleetwm.sock");
}

TEST(IpcClient, DefaultConstructedIsNotConnected) {
  IpcClient client;
  EXPECT_FALSE(client.is_connected());
  EXPECT_EQ(client.fd(), -1);
}

TEST(IpcClient, ConnectFailsGracefullyWhenNoSocketExists) {
  ::setenv("XDG_RUNTIME_DIR", "/nonexistent-fleetwm-test-dir", 1);
  IpcClient client;
  EXPECT_FALSE(client.connect());
  EXPECT_FALSE(client.is_connected());
  ::unsetenv("XDG_RUNTIME_DIR");
}

TEST(IpcClient, SendCommandOnUnconnectedClientFails) {
  IpcClient client;
  EXPECT_FALSE(client.send_command("LOCK"));
}

// A real listening AF_UNIX socket at the path IpcClient/ipc_socket_path()
// resolves to, standing in for the compositor's own IPC server -- lets
// these tests exercise IpcClient's actual local-socket behavior (connect,
// send, line-buffered receive, disconnect detection) rather than only the
// already-covered "no server" failure path above.
class IpcClientWithServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("fleetwm-ipc-test-" +
            std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
            std::to_string(reinterpret_cast<uintptr_t>(this)));
    std::filesystem::create_directories(dir_);
    ::setenv("XDG_RUNTIME_DIR", dir_.string().c_str(), 1);

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd_, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string sock_path = ipc_socket_path();
    std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
    ASSERT_EQ(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listen_fd_, 1), 0);
  }

  void TearDown() override {
    if (server_fd_ >= 0) {
      ::close(server_fd_);
    }
    ::close(listen_fd_);
    ::unsetenv("XDG_RUNTIME_DIR");
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  // Accepts the connection IpcClient::connect() just initiated. Unix
  // domain SOCK_STREAM connects complete synchronously (no real
  // handshake), but poll briefly rather than assuming the first accept()
  // always wins the race, to keep this robust under load.
  int accept_client() {
    for (int attempt = 0; attempt < 50; ++attempt) {
      int fd = ::accept(listen_fd_, nullptr, nullptr);
      if (fd >= 0) {
        server_fd_ = fd;
        return fd;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return -1;
  }

  std::filesystem::path dir_;
  int listen_fd_ = -1;
  int server_fd_ = -1;
};

TEST_F(IpcClientWithServerTest, ConnectSucceedsWhenServerIsListening) {
  IpcClient client;
  EXPECT_TRUE(client.connect());
  EXPECT_TRUE(client.is_connected());
  EXPECT_GE(client.fd(), 0);
  EXPECT_GE(accept_client(), 0);
}

TEST_F(IpcClientWithServerTest, SendCommandAppendsMissingNewline) {
  IpcClient client;
  ASSERT_TRUE(client.connect());
  int server = accept_client();
  ASSERT_GE(server, 0);

  EXPECT_TRUE(client.send_command("WORKSPACE 3"));

  char buf[64] = {};
  ssize_t n = ::recv(server, buf, sizeof(buf) - 1, 0);
  ASSERT_GT(n, 0);
  EXPECT_STREQ(buf, "WORKSPACE 3\n");
}

TEST_F(IpcClientWithServerTest, SendCommandDoesNotDoubleNewline) {
  IpcClient client;
  ASSERT_TRUE(client.connect());
  int server = accept_client();
  ASSERT_GE(server, 0);

  EXPECT_TRUE(client.send_command("LOCK\n"));

  char buf[64] = {};
  ssize_t n = ::recv(server, buf, sizeof(buf) - 1, 0);
  ASSERT_GT(n, 0);
  EXPECT_STREQ(buf, "LOCK\n");
}

TEST_F(IpcClientWithServerTest, PollLinesDispatchesOneCompleteLine) {
  IpcClient client;
  ASSERT_TRUE(client.connect());
  int server = accept_client();
  ASSERT_GE(server, 0);

  ASSERT_EQ(::send(server, "FOCUSED 2\n", 10, 0), 10);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::vector<std::string> lines;
  client.poll_lines([&](const std::string& line) { lines.push_back(line); });
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "FOCUSED 2");
}

TEST_F(IpcClientWithServerTest, PollLinesDispatchesMultipleLinesFromOneRead) {
  IpcClient client;
  ASSERT_TRUE(client.connect());
  int server = accept_client();
  ASSERT_GE(server, 0);

  const char* msg = "FOCUSED 1\nWORKSPACE 2\nFLOATING 0\n";
  ASSERT_EQ(::send(server, msg, std::strlen(msg), 0), static_cast<ssize_t>(std::strlen(msg)));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::vector<std::string> lines;
  client.poll_lines([&](const std::string& line) { lines.push_back(line); });
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "FOCUSED 1");
  EXPECT_EQ(lines[1], "WORKSPACE 2");
  EXPECT_EQ(lines[2], "FLOATING 0");
}

TEST_F(IpcClientWithServerTest, PollLinesBuffersPartialLineAcrossCalls) {
  IpcClient client;
  ASSERT_TRUE(client.connect());
  int server = accept_client();
  ASSERT_GE(server, 0);

  ASSERT_EQ(::send(server, "WORKSP", 6, 0), 6);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::vector<std::string> lines;
  client.poll_lines([&](const std::string& line) { lines.push_back(line); });
  EXPECT_TRUE(lines.empty());  // no trailing newline yet -- must not fire early

  ASSERT_EQ(::send(server, "ACE 5\n", 6, 0), 6);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  client.poll_lines([&](const std::string& line) { lines.push_back(line); });
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "WORKSPACE 5");
}

TEST_F(IpcClientWithServerTest, PollLinesWithNoDataAvailableDoesNothing) {
  IpcClient client;
  ASSERT_TRUE(client.connect());
  ASSERT_GE(accept_client(), 0);

  std::vector<std::string> lines;
  client.poll_lines([&](const std::string& line) { lines.push_back(line); });
  EXPECT_TRUE(lines.empty());
  EXPECT_TRUE(client.is_connected());
}

TEST_F(IpcClientWithServerTest, ServerClosingConnectionDropsFdOnNextPoll) {
  IpcClient client;
  ASSERT_TRUE(client.connect());
  int server = accept_client();
  ASSERT_GE(server, 0);

  ::close(server);
  server_fd_ = -1;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  client.poll_lines([](const std::string&) {});
  EXPECT_FALSE(client.is_connected());
  EXPECT_EQ(client.fd(), -1);
}

TEST_F(IpcClientWithServerTest, SendCommandAfterServerClosesFailsOnceDetected) {
  IpcClient client;
  ASSERT_TRUE(client.connect());
  int server = accept_client();
  ASSERT_GE(server, 0);

  ::close(server);
  server_fd_ = -1;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  client.poll_lines([](const std::string&) {});  // detects the close, drops fd_

  EXPECT_FALSE(client.send_command("LOCK"));
}

}  // namespace
}  // namespace fleetwm
