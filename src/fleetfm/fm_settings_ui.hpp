#pragma once

#include <string>
#include <vector>

#include "desktop.hpp"
#include "fm_settings.hpp"
#include "ui.hpp"

// The file manager's and the desktop's settings as widgets, drawn in the Settings app (one place for every setting) and by the file
// manager's own Folder Options window when the Settings app is not installed. The caller owns the scroll region, the margins and the
// label width; these draw rows (label at the left, control to its right) and report what happened.

namespace fleetwm::fm {

struct FmSettingsResult {
  bool changed = false;        // something was edited: save
  bool style_changed = false;  // a style was picked: the file manager resets its view modes
  bool view_changed = false;   // the default view mode changed: the current tab follows
  bool regroup = false;        // Group by changed
  bool connect = false, nextcloud = false, clear_history = false;  // buttons on the pages
};

const std::vector<std::string>& fm_settings_pages();
FmSettingsResult draw_fm_settings_page(kit::Ui& ui, FmSettings& s, int page);
// Returns true when something was edited.
bool draw_desktop_settings(kit::Ui& ui, DesktopConfig& c);

}  // namespace fleetwm::fm
