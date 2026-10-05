#pragma once

// Default-application associations per the freedesktop mime-apps spec,
// GLib-free: reads mimeapps.list / mimeinfo.cache and writes the user's
// ~/.config/mimeapps.list (what GAppInfo's set_as_default does).

#include <string>
#include <vector>

namespace fleetwm::kit {

// Desktop-file ids associated with `mime` (user "Added Associations" first,
// then the system mimeinfo.cache; "Removed Associations" excluded).
std::vector<std::string> mime_apps_for(const std::string& mime);

// The user's/system default desktop id for `mime`, or "" if none is set.
std::string mime_default_for(const std::string& mime);

// Makes `desktop_id` the default for `mime` and records the association.
bool mime_set_default(const std::string& mime, const std::string& desktop_id);

}  // namespace fleetwm::kit
