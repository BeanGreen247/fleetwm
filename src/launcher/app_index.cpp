#include "app_index.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace fleetwm::launcher {

namespace {

const std::unordered_map<std::string, std::string>& category_labels() {
  static const std::unordered_map<std::string, std::string> kLabels = {
      {"WebBrowser", "Web Browser"},     {"TerminalEmulator", "Terminal"},
      {"Utility", "Utility"},            {"Development", "Developer Tool"},
      {"Game", "Game"},                  {"Graphics", "Graphics"},
      {"AudioVideo", "Media"},           {"Office", "Office"},
  };
  return kLabels;
}

std::string category_hint_from(const char* categories_csv) {
  if (categories_csv == nullptr) {
    return "Application";
  }
  std::stringstream ss(categories_csv);
  std::string category;
  while (std::getline(ss, category, ';')) {
    auto it = category_labels().find(category);
    if (it != category_labels().end()) {
      return it->second;
    }
  }
  return "Application";
}

std::string to_lower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  return out;
}

}  // namespace

AppIndex::AppIndex() {
  GList* infos = g_app_info_get_all();
  for (GList* l = infos; l != nullptr; l = l->next) {
    auto* info = static_cast<GAppInfo*>(l->data);
    if (!g_app_info_should_show(info)) {
      g_object_unref(info);
      continue;
    }
    if (!G_IS_DESKTOP_APP_INFO(info)) {
      g_object_unref(info);
      continue;
    }

    auto* desktop_info = G_DESKTOP_APP_INFO(info);

    AppEntry entry;
    const char* display_name = g_app_info_get_display_name(info);
    entry.name = display_name != nullptr ? display_name : "";
    const char* description = g_app_info_get_description(info);
    entry.secondary_text = description != nullptr ? description : "";

    const char* categories = g_desktop_app_info_get_categories(desktop_info);
    entry.category_hint = category_hint_from(categories);

    entry.info = desktop_info;  // ownership transferred to entries_
    if (!entry.name.empty()) {
      entries_.push_back(std::move(entry));
    } else {
      g_object_unref(info);
    }
  }
  g_list_free(infos);

  std::sort(entries_.begin(), entries_.end(),
            [](const AppEntry& a, const AppEntry& b) { return a.name < b.name; });
}

AppIndex::~AppIndex() {
  for (AppEntry& entry : entries_) {
    g_object_unref(entry.info);
  }
}

std::vector<const AppEntry*> AppIndex::search(const std::string& query) const {
  std::vector<const AppEntry*> results;
  if (query.empty()) {
    results.reserve(entries_.size());
    for (const AppEntry& entry : entries_) {
      results.push_back(&entry);
    }
    return results;
  }

  std::string needle = to_lower(query);
  std::vector<std::pair<size_t, const AppEntry*>> scored;

  for (const AppEntry& entry : entries_) {
    std::string name_lower = to_lower(entry.name);
    size_t pos = name_lower.find(needle);
    if (pos == std::string::npos) {
      std::string secondary_lower = to_lower(entry.secondary_text);
      pos = secondary_lower.find(needle);
      if (pos != std::string::npos) {
        pos += name_lower.size();  // rank name matches ahead of comment matches
      }
    }
    if (pos != std::string::npos) {
      scored.emplace_back(pos, &entry);
    }
  }

  std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first < b.first;
    }
    return a.second->name < b.second->name;
  });

  results.reserve(scored.size());
  for (const auto& [pos, entry] : scored) {
    results.push_back(entry);
  }
  return results;
}

}  // namespace fleetwm::launcher
