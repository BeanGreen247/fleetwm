#include "window.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#include "file_ops.hpp"
#include "image.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {
double steady_now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

FmWindow::FmWindow(Host host, FmSettings settings, kit::Palette theme)
    : host_(std::move(host)), s_(std::move(settings)), theme_(std::move(theme)), trash_() {
  alive_ = std::make_shared<std::atomic<bool>>(true);
  if (s_.columns.empty()) s_.columns = default_columns();
  apply_style_colors();
  places_ = load_places();
  Browser b;
  new_tab_state(&b);
  tabs_.push_back(std::move(b));
  thumb_thread_ = std::thread([this] { thumb_worker(); });
  // Pick the checksum for verified copies now, off the main thread, so the first copy does not wait for the measurement (about 25 ms).
  std::thread([] { resolve_algo(HashAlgo::Auto); }).detach();
  load_folder_views();
}

FmWindow::~FmWindow() {
  *alive_ = false;
  search_cancel_ = true;
  props_cancel_ = true;
  {
    std::lock_guard<std::mutex> l(thumb_mu_);
    stopping_ = true;
    thumb_cv_.notify_all();
  }
  if (thumb_thread_.joinable()) thumb_thread_.join();
  if (search_thread_.joinable()) search_thread_.join();
  if (props_thread_.joinable()) props_thread_.join();
  if (vol_thread_.joinable()) vol_thread_.join();
  for (auto& l : loaders_)
    if (l->t.joinable()) l->t.join();
  for (auto& j : jobs_) {
    j->t->cancel();
    {
      std::lock_guard<std::mutex> l(j->mu);
      j->answered = true;
      j->answer = Conflict::Cancel;
      j->cv.notify_all();
    }
    if (j->th.joinable()) j->th.join();
  }
  for (auto& [k, surf] : thumbs_) cairo_surface_destroy(surf);
}

void FmWindow::post_(std::function<void()> fn) {
  auto alive = alive_;
  auto wrapped = [alive, fn = std::move(fn)] {
    if (*alive) fn();
  };
  if (host_.post) {
    host_.post(std::move(wrapped));
  } else {
    std::lock_guard<std::mutex> l(post_mu_);
    local_posts_.push_back(std::move(wrapped));
  }
}

bool FmWindow::pump_() {
  std::vector<std::function<void()>> run;
  {
    std::lock_guard<std::mutex> l(post_mu_);
    run.swap(local_posts_);
  }
  for (auto& f : run) f();
  return !run.empty();
}

bool FmWindow::busy() const {
  for (const Browser& b : tabs_)
    if (b.loading) return true;
  if (search_running_ || vol_busy_ || loads_running_ > 0) return true;
  if (loaders_active_ > 0) return true;
  for (const auto& j : jobs_)
    if (!j->done) return true;
  {
    std::lock_guard<std::mutex> l(const_cast<std::mutex&>(thumb_mu_));
    if (!thumb_queue_.empty()) return true;
  }
  return false;
}

int FmWindow::jobs_running() const {
  int n = 0;
  for (const auto& j : jobs_)
    if (!j->done) ++n;
  return n;
}

void FmWindow::wait_idle(double timeout_s) {
  const double end = steady_now() + timeout_s;
  while (steady_now() < end) {
    const bool ran = pump_();
    reap_jobs();
    if (!ran && !busy()) {
      bool queued;
      {
        std::lock_guard<std::mutex> l(post_mu_);
        queued = !local_posts_.empty();
      }
      if (!queued) return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void FmWindow::schedule_redraw() {
  if (host_.redraw) host_.redraw();
}

void FmWindow::apply_style_colors() {
  style_ = &style_spec(s_.style);
  col_ = make_colors(s_.style, s_.colour_scheme, theme_);
  font_cache_.clear();
}

void FmWindow::reload_config(FmSettings fresh, kit::Palette theme) {
  const bool style_changed = fresh.style != s_.style;
  const bool regroup = fresh.group_by != s_.group_by;
  fresh.last_location = s_.last_location;
  fresh.open_tabs = s_.open_tabs;
  fresh.window_w = s_.window_w;
  fresh.window_h = s_.window_h;
  s_ = std::move(fresh);
  theme_ = std::move(theme);
  if (ui_) ui_->set_palette(theme_);
  for (Browser& t : tabs_) {
    if (style_changed) t.mode = s_.default_view;
    if (regroup) t.group_by = s_.group_by;
  }
  apply_settings();
}

void FmWindow::apply_settings() {
  if (s_.columns.empty()) s_.columns = default_columns();
  apply_style_colors();
  for (Browser& b : tabs_) {
    b.show_hidden = s_.show_hidden;
    b.folders_first = s_.folders_first;
    b.rebuild();
  }
  nav_dirty_ = true;
  update_title();
  schedule_redraw();
}

void FmWindow::new_tab_state(Browser* b) const {
  b->id = const_cast<FmWindow*>(this)->next_tab_id_++;
  b->mode = s_.default_view;
  b->sort_key = s_.sort_key;
  b->group_by = s_.group_by;
  b->type_label = [](std::string_view n, bool d) { return type_description(n, d); };
  b->ascending = s_.sort_ascending;
  b->folders_first = s_.folders_first;
  b->show_hidden = s_.show_hidden;
}

std::string FmWindow::home_dir() const {
  const char* h = std::getenv("HOME");
  return h && *h ? h : "/";
}

std::string FmWindow::title() const {
  const Browser& b = tab();
  std::string name;
  switch (b.place) {
    case PlaceKind::Computer: name = "Computer"; break;
    case PlaceKind::Network: name = "Network"; break;
    case PlaceKind::Trash: name = "Trash"; break;
    case PlaceKind::Recent: name = "Recent Places"; break;
    default: {
      if (!b.search.empty()) {
        name = "Search Results in " + fs::path(b.path).filename().string();
        break;
      }
      const std::string& p = b.place == PlaceKind::Remote ? b.uri : b.path;
      if (s_.show_full_path_in_title) name = p;
      else if (b.place == PlaceKind::Remote) {
        const Uri u = parse_uri(b.uri);
        name = u.path.empty() || u.path == "/" ? u.host : fs::path(u.path).filename().string();
      } else {
        name = b.path == "/" ? "/" : fs::path(b.path).filename().string();
      }
    }
  }
  return name + " - File Manager";
}

void FmWindow::update_title() {
  if (host_.set_title) host_.set_title(title());
}

// ---------------------------------------------------------------------------------------------------------------------
// start, tabs
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::start(const std::string& first) {
  refresh_volumes();
  std::string where = first;
  if (where.empty()) {
    switch (s_.startup) {
      case Startup::ThisPc: where = "computer:///"; break;
      case Startup::Home: where = home_dir(); break;
      case Startup::LastLocation: where = s_.last_location.empty() ? home_dir() : s_.last_location; break;
      case Startup::Custom: where = s_.startup_path.empty() ? home_dir() : s_.startup_path; break;
    }
  }
  if (first.empty() && s_.restore_tabs && !s_.open_tabs.empty()) {
    bool firstTab = true;
    for (const std::string& a : s_.open_tabs) {
      if (firstTab) {
        open_address(a, false);
        firstTab = false;
      } else {
        add_tab(a);
      }
    }
    select_tab(0);
  } else {
    open_address(where, false);
  }
  if (host_.after && !refresh_timer_) {
    refresh_timer_ = true;
    auto tick = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = tick;
    auto alive = alive_;
    *tick = [this, weak, alive] {
      if (!*alive) return;
      if (focused_) refresh_volumes();
      if (auto t = weak.lock()) host_.after(3000, [t] { (*t)(); });
    };
    tick_keep_ = tick;
    host_.after(3000, [tick] { (*tick)(); });
  }
}

void FmWindow::add_tab(const std::string& address) {
  Browser b;
  new_tab_state(&b);
  b.place = PlaceKind::Local;
  tabs_.push_back(std::move(b));
  cur_ = tabs_.size() - 1;
  open_address(address.empty() ? home_dir() : address, false);
}

void FmWindow::select_tab(size_t i) {
  if (i >= tabs_.size()) return;
  cur_ = i;
  stop_search();
  update_title();
  watch_current();
  nav_dirty_ = true;
  addr_focus_ = search_focus_ = false;
  schedule_redraw();
}

void FmWindow::close_tab(size_t i) {
  if (i >= tabs_.size()) return;
  if (tabs_.size() == 1) {
    if (s_.close_window_with_last_tab && host_.quit) host_.quit();
    return;
  }
  closed_tabs_.push_back(tabs_[i].address());
  if (closed_tabs_.size() > 20) closed_tabs_.erase(closed_tabs_.begin());
  tabs_.erase(tabs_.begin() + static_cast<long>(i));
  if (cur_ >= tabs_.size()) cur_ = tabs_.size() - 1;
  else if (i < cur_) --cur_;
  update_title();
  watch_current();
  nav_dirty_ = true;
  schedule_redraw();
}

// ---------------------------------------------------------------------------------------------------------------------
// locations and loading
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::open_address(const std::string& address, bool new_tab, bool remember) {
  if (new_tab) {
    add_tab(address);
    return;
  }
  if (address.compare(0, 6, "mount:") == 0) {
    mount_device(address.substr(6));
    return;
  }
  const std::string cwd = tab().place == PlaceKind::Local ? tab().path : home_dir();
  const Location loc = parse_location(address, home_dir(), cwd);
  switch (loc.kind) {
    case PlaceKind::Local: {
      std::error_code ec;
      struct stat st;
      if (::stat(loc.path.c_str(), &st) != 0) {
        show_message("File Manager", "'" + loc.path + "' can't be found. Check the spelling and try again.", true);
        return;
      }
      if (!S_ISDIR(st.st_mode)) {
        open_default(loc.path);
        return;
      }
      open_local(loc.path, remember);
      return;
    }
    case PlaceKind::Remote: open_remote(loc.uri, remember); return;
    case PlaceKind::Unknown:
      show_message("File Manager", "The address '" + address + "' is not understood. Try a folder path, or an address like smb://server/share or sftp://host.", true);
      return;
    default: open_special(loc.kind, remember); return;
  }
}

void FmWindow::open_local(const std::string& path, bool remember) {
  Browser& b = tab();
  stop_search();
  search_edit_.set("", false);
  b.filter.clear();
  if (remember && !(b.place == PlaceKind::Local && b.path == path)) b.remember_current(history_limit());
  b.place = PlaceKind::Local;
  b.path = path;
  b.uri.clear();
  b.scroll_x = b.scroll_y = 0;
  b.focus = b.anchor = -1;
  b.clear_selection();
  addr_focus_ = false;
  load_current();
  update_title();
  watch_current();
  nav_dirty_ = true;
  s_.last_location = path;
}

void FmWindow::open_special(PlaceKind k, bool remember) {
  Browser& b = tab();
  stop_search();
  search_edit_.set("", false);
  b.filter.clear();
  if (remember) b.remember_current(history_limit());
  b.place = k;
  b.path = k == PlaceKind::Trash ? trash_.home_trash() + "/files" : "/";
  b.uri.clear();
  b.scroll_x = b.scroll_y = 0;
  b.error.clear();
  b.set_listing(DirListing{});
  load_current();
  update_title();
  watch_current();
  nav_dirty_ = true;
}

void FmWindow::open_remote(const Uri& u, bool remember) {
  const std::string root = gvfs_root(static_cast<unsigned>(::getuid()));
  std::string existing = find_mount_dir(root, u);
  if (existing.empty()) existing = root + "/" + gvfs_mount_dir_name(u);
  const std::string dir = existing;
  auto enter = [this, u, remember](const std::string& dir) {
    Browser& b = tab();
    stop_search();
    if (remember) b.remember_current(history_limit());
    b.place = PlaceKind::Remote;
    std::string sub = u.path;
    if (u.scheme == "smb") {  // the share is part of the mount folder name, the rest is below it
      std::string p = u.path;
      if (!p.empty() && p[0] == '/') p.erase(0, 1);
      const size_t slash = p.find('/');
      sub = slash == std::string::npos ? "" : p.substr(slash);
    } else if (u.scheme == "davs" || u.scheme == "dav") {
      sub.clear();
    }
    b.path = dir + sub;
    b.uri = uri_to_string(u);
    b.scroll_x = b.scroll_y = 0;
    b.clear_selection();
    load_current();
    update_title();
    watch_current();
    nav_dirty_ = true;
  };
  std::error_code ec;
  if (fs::exists(dir, ec)) {
    enter(dir);
    return;
  }
  // Not mounted yet: ask GVfs (it may prompt for a password; the Connect dialog collects one first).
  connecting_ = true;
  schedule_redraw();
  auto alive = alive_;
  CommandRunner* r = runner ? runner : &system_runner();
  spawn_loader([this, u, r, alive, enter] {
    Credentials c;
    c.user = u.user;
    c.password = u.password;
    const MountOutcome o = mount_location(u, c, *r, gvfs_root(static_cast<unsigned>(::getuid())));
    post_([this, o, enter, u] {
      connecting_ = false;
      if (o.ok) {
        refresh_volumes();
        enter(o.path);
      } else {
        // Ask for credentials in the Connect dialog, prefilled.
        connect_uri_ = uri_to_string(u);
        connect_user_ = u.user;
        connect_error_ = o.error;
        open_dialog(Dlg::Connect);
      }
      schedule_redraw();
    });
  });
}

void FmWindow::load_current(bool keep_scroll) {
  Browser& b = tab();
  const int sx = b.scroll_x, sy = b.scroll_y;
  switch (b.place) {
    case PlaceKind::Computer:
      refresh_volumes();
      b.error.clear();
      b.loading = false;
      b.set_listing(DirListing{});
      return;
    case PlaceKind::Network:
      b.error.clear();
      b.loading = false;
      b.set_listing(DirListing{});
      scan_network();
      return;
    case PlaceKind::Trash: load_trash(); return;
    case PlaceKind::Recent: load_recent(); return;
    default: break;
  }
  b.loading = true;
  const uint64_t gen = ++b.generation;
  const uint64_t id = b.id;
  const std::string path = b.path;
  ++loads_running_;
  auto alive = alive_;
  // Sizes and dates are only read for the rows that are drawn, unless the order needs them all (sort by size or date).
  const bool full_stat = b.sort_key == SortKey::Size || b.sort_key == SortKey::Modified;
  spawn_loader([this, id, gen, path, alive, keep_scroll, sx, sy, full_stat] {
    DirListing l;
    std::string err;
    list_dir(path, {full_stat, true}, &l, &err);
    post_([this, id, gen, l = std::move(l), err, keep_scroll, sx, sy, full_stat]() mutable {
      --loads_running_;
      finish_load(id, gen, std::move(l), err, full_stat);
      if (keep_scroll) {
        for (Browser& b : tabs_)
          if (b.id == id) {
            b.scroll_x = sx;
            b.scroll_y = sy;
          }
      }
    });
  });
}

void FmWindow::spawn_loader(std::function<void()> fn) {
  std::lock_guard<std::mutex> lk(load_mu_);
  // Threads that finished are joined here so the list does not grow with every folder visited.
  while (!loaders_.empty() && loaders_.front()->done) {
    if (loaders_.front()->t.joinable()) loaders_.front()->t.join();
    loaders_.pop_front();
  }
  auto l = std::make_unique<Loader>();
  Loader* raw = l.get();
  ++loaders_active_;
  l->t = std::thread([this, raw, fn = std::move(fn)] {
    fn();
    raw->done = true;
    --loaders_active_;
  });
  loaders_.push_back(std::move(l));
}

void FmWindow::finish_load(size_t id, uint64_t gen, DirListing&& l, const std::string& err, bool full_stat) {
  for (Browser& b : tabs_) {
    if (b.id != id) continue;
    if (b.generation != gen) return;
    b.loading = false;
    b.error = err;
    b.stat_complete = full_stat;
    if (err.empty()) b.set_listing(std::move(l));
    else b.set_listing(DirListing{});
    const ViewMetrics m = compute_metrics(b.mode, static_cast<int>(b.shown.size()), std::max(1, lay_.content.w), std::max(1, lay_.content.h), s_.icon_px ? s_.icon_px : view_mode_icon_px(b.mode),
                                          s_.row_height ? s_.row_height : style_->row_h, s_.font_px ? s_.font_px : style_->font_px, style_->header_h, 0, &b.group_starts);
    b.scroll_y = std::clamp(b.scroll_y, 0, max_scroll_y(m));
    b.scroll_x = std::clamp(b.scroll_x, 0, max_scroll_x(m));
    if (!select_after_load_.empty() && b.id == tab().id) {
      for (size_t i = 0; i < b.shown.size(); ++i)
        if (b.name_at(static_cast<int>(i)) == select_after_load_) {
          b.select_only(static_cast<int>(i));
          scroll_to_show(m, static_cast<int>(i), &b.scroll_x, &b.scroll_y);
          if (rename_after_load_) {
            rename_after_load_ = false;
            begin_rename();
          }
        }
      select_after_load_.clear();
    }
    apply_folder_view();
  }
  schedule_redraw();
}

void FmWindow::on_dir_changed() {
  if (reload_pending_) return;
  reload_pending_ = true;
  auto alive = alive_;
  auto go = [this, alive] {
    if (!*alive) return;
    reload_pending_ = false;
    Browser& b = tab();
    if (b.place == PlaceKind::Computer) refresh_volumes();
    else if (b.search.empty() && !b.loading) load_current(true);
  };
  if (host_.after) host_.after(150, go);
  else go();
}

void FmWindow::watch_current() {
  const Browser& b = tab();
  if ((b.place == PlaceKind::Local || b.place == PlaceKind::Remote || b.place == PlaceKind::Trash) && host_.watch) host_.watch(b.path);
}

void FmWindow::load_trash() {
  Browser& b = tab();
  std::vector<std::string> mounts;
  for (const Volume& v : volumes_)
    if (v.mounted && v.kind != DriveKind::Network && !v.mountpoint.empty()) mounts.push_back(v.mountpoint);
  trash_items_ = trash_.list(mounts);
  DirListing l;
  for (const TrashItem& it : trash_items_) {
    Entry e;
    e.name_off = static_cast<uint32_t>(l.arena.size());
    e.name_len = static_cast<uint32_t>(it.name.size());
    e.kind = it.is_dir ? Kind::Dir : Kind::File;
    e.has_stat = true;
    e.src = static_cast<uint32_t>(l.entries.size());
    e.size = it.size;
    std::tm tm{};
    if (strptime(it.deleted.c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) e.mtime = mktime(&tm);
    l.arena += it.name;
    l.entries.push_back(e);
  }
  b.loading = false;
  b.error.clear();
  b.set_listing(std::move(l));
}

void FmWindow::load_recent() {
  Browser& b = tab();
  const char* xdg = std::getenv("XDG_DATA_HOME");
  const std::string xbel = (xdg && *xdg ? std::string(xdg) : home_dir() + "/.local/share") + "/recently-used.xbel";
  DirListing l;
  for (const RecentFile& r : read_recent(xbel)) {
    struct stat st;
    if (::stat(r.path.c_str(), &st) != 0) continue;
    Entry e;
    e.name_off = static_cast<uint32_t>(l.arena.size());
    e.name_len = static_cast<uint32_t>(r.path.size());
    e.kind = S_ISDIR(st.st_mode) ? Kind::Dir : Kind::File;
    e.has_stat = true;
    e.src = static_cast<uint32_t>(l.entries.size());
    e.size = static_cast<uint64_t>(st.st_size);
    e.mtime = r.modified ? r.modified : st.st_mtim.tv_sec;
    l.arena += r.path;
    l.entries.push_back(e);
  }
  b.loading = false;
  b.error.clear();
  b.set_listing(std::move(l));
  b.sort_key = SortKey::Modified;
  b.ascending = false;
  b.folders_first = false;
  b.rebuild();
}

// Reads size and date of every entry off the main thread (a sort by size or date, a selection that reaches past the visible rows).
void FmWindow::request_full_stat(std::function<void()> then) {
  Browser& b = tab();
  if (b.stat_complete) {
    if (then) then();
    return;
  }
  if (b.stat_pending) return;
  b.stat_pending = true;
  std::vector<std::string> names;
  names.reserve(b.raw.entries.size());
  for (const Entry& e : b.raw.entries) names.emplace_back(b.raw.name(e));
  const uint64_t id = b.id, gen = b.generation;
  const std::string dir = b.path;
  spawn_loader([this, id, gen, dir, names = std::move(names), then = std::move(then)]() mutable {
    struct Stat {
      uint32_t mode;
      uint64_t size;
      int64_t mtime;
      Kind kind;
      bool link_to_dir, ok;
    };
    std::vector<Stat> out(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
      Entry e;
      const bool ok = stat_entry(dir, names[i], &e);
      out[i] = {e.mode, e.size, e.mtime, e.kind, e.link_to_dir, ok};
    }
    post_([this, id, gen, out = std::move(out), then = std::move(then)]() mutable {
      for (Browser& b : tabs_) {
        if (b.id != id) continue;
        b.stat_pending = false;
        if (b.generation != gen || out.size() != b.raw.entries.size()) continue;
        for (size_t i = 0; i < out.size(); ++i) {
          Entry& r = b.raw.entries[i];
          if (!out[i].ok) continue;
          r.has_stat = true;
          r.mode = out[i].mode;
          r.size = out[i].size;
          r.mtime = out[i].mtime;
          r.kind = out[i].kind;
          r.link_to_dir = out[i].link_to_dir;
        }
        b.stat_complete = true;
        b.rebuild();
      }
      if (then) then();
      schedule_redraw();
    });
  });
}

// ---------------------------------------------------------------------------------------------------------------------
// volumes and the navigation pane
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::refresh_volumes() {
  if (vol_busy_.exchange(true)) return;
  if (vol_thread_.joinable()) vol_thread_.join();
  vol_thread_ = std::thread([this] {
    std::vector<Volume> v = list_volumes();
    post_([this, v = std::move(v)]() mutable {
      vol_busy_ = false;
      bool same = v.size() == volumes_.size();
      for (size_t i = 0; same && i < v.size(); ++i)
        same = v[i].device == volumes_[i].device && v[i].mountpoint == volumes_[i].mountpoint && v[i].free == volumes_[i].free && v[i].mounted == volumes_[i].mounted;
      if (same) return;
      volumes_ = std::move(v);
      nav_dirty_ = true;
      schedule_redraw();
    });
  });
}

void FmWindow::rebuild_nav() {
  NavInput in;
  in.settings = &s_;
  in.style = s_.style;
  in.volumes = volumes_;
  in.places = places_;
  in.home = home_dir();
  in.current = tab().place == PlaceKind::Local || tab().place == PlaceKind::Remote ? tab().path : tab().address();
  in.subfolders = [](const std::string& dir) {
    std::vector<std::string> out;
    DirListing l;
    if (!list_dir(dir, {true, false}, &l)) return out;
    sort_listing(&l, SortKey::Name);
    for (const Entry& e : l.entries)
      if (l.is_dir(e)) out.emplace_back(l.name(e));
    return out;
  };
  nav_.build(in);
  nav_dirty_ = false;
}

const Volume* FmWindow::volume_for_path(const std::string& path) const {
  const Volume* best = nullptr;
  size_t best_len = 0;
  for (const Volume& v : volumes_) {
    if (!v.mounted || v.mountpoint.empty()) continue;
    const std::string& m = v.mountpoint;
    const bool inside = path == m || m == "/" || (path.compare(0, m.size(), m) == 0 && path[m.size()] == '/');
    if (inside && m.size() >= best_len) {
      best = &v;
      best_len = m.size();
    }
  }
  return best;
}

bool FmWindow::is_external_destination(const std::string& path) const {
  const Volume* v = volume_for_path(path);
  if (!v) return path.compare(0, gvfs_root(static_cast<unsigned>(::getuid())).size(), gvfs_root(static_cast<unsigned>(::getuid()))) == 0;
  return v->kind == DriveKind::Removable || v->kind == DriveKind::Network || v->kind == DriveKind::Optical;
}

// ---------------------------------------------------------------------------------------------------------------------
// search
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::stop_search() {
  search_cancel_ = true;
  ++search_gen_;
  if (search_thread_.joinable()) search_thread_.join();
  search_running_ = false;
  Browser& b = tab();
  if (!b.search.empty()) b.search.clear();
}

void FmWindow::start_search(const std::string& q) {
  Browser& b = tab();
  if (b.place != PlaceKind::Local && b.place != PlaceKind::Remote) return;
  search_cancel_ = true;
  if (search_thread_.joinable()) search_thread_.join();
  const uint64_t gen = ++search_gen_;
  if (q.empty()) {
    const bool was_search = !b.search.empty();
    b.search.clear();
    b.filter.clear();
    if (was_search) load_current(false);
    else b.rebuild();
    return;
  }
  if (!s_.search_subfolders) {
    b.search.clear();
    b.filter = q;
    b.rebuild();
    return;
  }
  b.filter.clear();
  b.search = q;
  b.set_listing(DirListing{});
  b.loading = true;
  search_cancel_ = false;
  search_running_ = true;
  SearchOptions o;
  o.query = q;
  o.subfolders = true;
  o.partial = s_.search_partial;
  o.hidden = s_.search_hidden || s_.show_hidden;
  o.contents = s_.search_in_contents;
  o.case_sensitive = s_.search_case_sensitive;
  o.max_results = static_cast<size_t>(s_.search_max_results);
  const std::string root = b.path;
  const uint64_t id = b.id;
  search_thread_ = std::thread([this, o, root, gen, id] {
    auto batch = std::make_shared<std::vector<SearchHit>>();
    double last = steady_now();
    auto flush = [&](bool final_batch) {
      auto chunk = batch;
      batch = std::make_shared<std::vector<SearchHit>>();
      post_([this, chunk, gen, id, final_batch] {
        if (gen != search_gen_) return;
        for (Browser& b : tabs_) {
          if (b.id != id) continue;
          DirListing l = std::move(b.raw);
          for (const SearchHit& h : *chunk) {
            Entry e;
            e.name_off = static_cast<uint32_t>(l.arena.size());
            e.name_len = static_cast<uint32_t>(h.path.size());
            e.kind = h.kind;
            e.has_stat = true;
            e.src = static_cast<uint32_t>(l.entries.size());
            e.size = h.size;
            e.mtime = h.mtime;
            const size_t slash = h.path.rfind('/');
            e.hidden = (slash == std::string::npos ? h.path[0] : h.path[slash + 1]) == '.';
            l.arena += h.path;
            l.entries.push_back(e);
          }
          b.set_listing(std::move(l));
          if (final_batch) {
            b.loading = false;
            search_running_ = false;
          }
        }
        schedule_redraw();
      });
    };
    search_tree(root, o, &search_cancel_, [&](const SearchHit& h) {
      batch->push_back(h);
      const double t = steady_now();
      if (batch->size() >= 200 || (t - last > 0.15 && !batch->empty())) {
        flush(false);
        last = t;
      }
      return !search_cancel_.load();
    });
    if (!search_cancel_) flush(true);
  });
}

// ---------------------------------------------------------------------------------------------------------------------
// thumbnails
// ---------------------------------------------------------------------------------------------------------------------

cairo_surface_t* FmWindow::thumb_for(const std::string& path, int64_t mtime, int px) {
  ThumbKey k{path, mtime, px};
  std::lock_guard<std::mutex> l(thumb_mu_);
  auto it = thumbs_.find(k);
  return it == thumbs_.end() ? nullptr : it->second;
}

void FmWindow::request_thumb(const std::string& path, int64_t mtime, int px) {
  ThumbKey k{path, mtime, px};
  std::lock_guard<std::mutex> l(thumb_mu_);
  if (thumbs_.count(k) || thumb_pending_.count(k)) return;
  thumb_pending_[k] = true;
  thumb_queue_.push_back(k);
  thumb_cv_.notify_one();
}

void FmWindow::thumb_worker() {
  for (;;) {
    ThumbKey k;
    {
      std::unique_lock<std::mutex> l(thumb_mu_);
      thumb_cv_.wait(l, [&] { return stopping_ || !thumb_queue_.empty(); });
      if (stopping_) return;
      k = thumb_queue_.front();
      thumb_queue_.pop_front();
    }
    cairo_surface_t* out = nullptr;
    struct stat st;
    if (::stat(k.path.c_str(), &st) == 0 && static_cast<uint64_t>(st.st_size) <= static_cast<uint64_t>(s_.thumbnail_max_mb) * 1024 * 1024) {
      const std::string ext = extension_of(k.path);
      kit::Image img = ext == "svg" ? kit::load_svg(k.path, k.px * 2) : kit::load_image(k.path);
      if (img.ok()) {
        cairo_surface_t* src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, img.width, img.height);
        unsigned char* d = cairo_image_surface_get_data(src);
        const int stride = cairo_image_surface_get_stride(src);
        for (int y = 0; y < img.height; ++y) {
          uint32_t* row = reinterpret_cast<uint32_t*>(d + y * stride);
          const uint8_t* in = img.rgba.data() + static_cast<size_t>(y) * img.width * 4;
          for (int x = 0; x < img.width; ++x, in += 4) {
            const unsigned a = in[3];
            row[x] = (a << 24) | ((in[0] * a / 255) << 16) | ((in[1] * a / 255) << 8) | (in[2] * a / 255);
          }
        }
        cairo_surface_mark_dirty(src);
        const double scale = std::min(1.0, std::min(static_cast<double>(k.px) / img.width, static_cast<double>(k.px) / img.height));
        const int tw = std::max(1, static_cast<int>(img.width * scale)), th = std::max(1, static_cast<int>(img.height * scale));
        out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
        cairo_t* cr = cairo_create(out);
        cairo_scale(cr, static_cast<double>(tw) / img.width, static_cast<double>(th) / img.height);
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(src);
      }
    }
    post_([this, k, out] {
      std::lock_guard<std::mutex> l(thumb_mu_);
      thumb_pending_.erase(k);
      if (!out) {
        thumbs_[k] = nullptr;
        return;
      }
      thumbs_[k] = out;
      thumb_lru_.push_back(k);
      thumb_bytes_ += static_cast<size_t>(cairo_image_surface_get_width(out)) * cairo_image_surface_get_height(out) * 4;
      while (thumb_bytes_ > (64u << 20) && !thumb_lru_.empty()) {
        const ThumbKey old = thumb_lru_.front();
        thumb_lru_.pop_front();
        auto it = thumbs_.find(old);
        if (it != thumbs_.end() && it->second) {
          thumb_bytes_ -= static_cast<size_t>(cairo_image_surface_get_width(it->second)) * cairo_image_surface_get_height(it->second) * 4;
          cairo_surface_destroy(it->second);
          thumbs_.erase(it);
        }
      }
      schedule_redraw();
    });
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// status text
// ---------------------------------------------------------------------------------------------------------------------

std::string FmWindow::describe_selection() const {
  const Browser& b = tab();
  const int n = b.selected_count();
  if (n == 0) return std::to_string(b.shown.size()) + (b.shown.size() == 1 ? " item" : " items");
  std::string s = std::to_string(n) + (n == 1 ? " item selected" : " items selected");
  bool complete = true;
  const uint64_t bytes = b.selected_bytes(&complete);
  if (!complete) {
    s += "   ...";  // sizes of items that were never drawn are being read in the background (draw() asks for that)
  } else if (bytes > 0) {
    s += "   " + format_size(bytes);
  }
  return s;
}

std::string FmWindow::status_text() const { return describe_selection(); }

}  // namespace fleetwm::fm
