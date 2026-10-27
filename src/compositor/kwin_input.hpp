#pragma once

#include <memory>
#include <string>
#include <vector>

struct wlr_input_device;

namespace fleetwm {

class Server;

// The compositor speaks KWin's input device D-Bus interface (org.kde.KWin, /org/kde/KWin/InputDevice/eventN, interface org.kde.KWin.InputDevice) on the session bus.
// TuxThrottle switches the touchpad on and off, tap-to-click, natural scroll and disable-while-typing through exactly that interface, and so do other KDE-aware tools:
// under Fleetwm they now work unchanged. A value set from outside is kept for the session and put back after Settings -> Mouse re-applies its own settings, so the
// last word is never silently lost.
class KwinInput {
 public:
  explicit KwinInput(Server* server);
  ~KwinInput();
  KwinInput(const KwinInput&) = delete;
  KwinInput& operator=(const KwinInput&) = delete;

  // Connects to the session bus and claims org.kde.KWin; false (and no effect) when there is no session bus or another KWin owns the name.
  bool start();
  void device_added(wlr_input_device* device);
  void device_removed(wlr_input_device* device);
  // Puts the values that were set from outside back on `device` (after the mouse settings were applied).
  void reapply(wlr_input_device* device);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace fleetwm
