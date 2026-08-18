#pragma once

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fleetwm::testutil {

// Points XDG_CONFIG_HOME at a fresh, unique temp directory for the
// lifetime of the test and removes it afterwards -- every config module
// under test (theme, wallpaper, default_apps, keybinds, bar) resolves its
// user config path through $XDG_CONFIG_HOME/fleetwm/<name>.toml, so this
// is what actually isolates each test from the real user's config and
// from other tests running in the same process.
class ScopedConfigHome : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("fleetwm-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    std::filesystem::create_directories(dir_);
    ::setenv("XDG_CONFIG_HOME", dir_.string().c_str(), 1);
  }

  void TearDown() override {
    ::unsetenv("XDG_CONFIG_HOME");
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::filesystem::path dir_;
};

}  // namespace fleetwm::testutil
