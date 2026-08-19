#include "ipc_client.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

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

}  // namespace
}  // namespace fleetwm
