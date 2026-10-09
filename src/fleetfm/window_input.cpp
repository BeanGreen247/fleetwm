#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cmath>
#include <sys/stat.h>

#include <filesystem>

#include "file_ops.hpp"
#include "window.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {
constexpr uint32_t BTN_LEFT_ = 0x110, BTN_RIGHT_ = 0x111, BTN_MIDDLE_ = 0x112;
}

// ---------------------------------------------------------------------------------------------------------------------
// pointer
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::on_leave() {
  mx_ = my_ = -1;
  hover_item_ = -1;
  if (ui_ && dlg_ != Dlg::None) ui_->pointer_leave();
  schedule_redraw();
}

void FmWindow::on_focus(bool f) {
  focused_ = f;
  schedule_redraw();
}

void FmWindow::on_motion(double x, double y) {
  const Region* before = region_at(mx_, my_);
  const int kind_before = before ? static_cast<int>(before->kind) * 100000 + before->arg * 10 + before->arg2 : -1;
  const int item_before = hover_item_;
  mx_ = x;
  my_ = y;
  if (dlg_ != Dlg::None && ui_) {
    ui_->pointer_motion(x - dlg_rect_.x, y - dlg_rect_.y);
    schedule_redraw();
    return;
  }
  bool redraw = false;
  Browser& b = tab();
  if (menu_open()) {
    const Region* r = region_at(x, y);
    const int hot = r && r->kind == R::MenuItem ? r->arg : -1;
    if (r && r->kind == R::MenuItem && hot != menu_.hot) {
      menu_.hot = hot;
      redraw = true;
      // resting on an item with a submenu opens it; moving to another item of this level closes it
      if (hot >= 0 && hot < static_cast<int>(menu_.items.size()) && !menu_.items[hot].sub.empty() && menu_.items[hot].enabled) {
        sub_.items = menu_.items[hot].sub;
        sub_.parent = hot;
        sub_.hot = -1;
      } else {
        sub_.items.clear();
        sub_.parent = -1;
      }
    }
    const int shot = r && r->kind == R::SubItem ? r->arg : -1;
    if (r && r->kind == R::SubItem && shot != sub_.hot) {
      sub_.hot = shot;
      redraw = true;
    }
  }
  if (col_drag_ >= 0) {
    auto cols = active_columns();
    // the widths live in the settings list; map the visible index back to it
    int seen = -1;
    for (ColumnSetting& c : s_.columns) {
      if (!c.visible) continue;
      if (++seen == col_drag_) c.width = std::clamp(col_drag_w_ + static_cast<int>(x - col_drag_x_), 40, 900);
    }
    redraw = true;
  } else if (nav_split_) {
    s_.nav_width = std::clamp(static_cast<int>(x), 120, std::max(160, W_ / 2));
    redraw = true;
  } else if (vscroll_drag_) {
    const ViewMetrics m = metrics();
    const double track = lay_.content.h - m.header_h;
    const double thumb = std::max(24.0, track * track / std::max(1, m.content_h));
    const double frac = std::clamp((y - lay_.content.y - m.header_h - vscroll_off_) / std::max(1.0, track - thumb), 0.0, 1.0);
    b.scroll_y = static_cast<int>(frac * max_scroll_y(m));
    redraw = true;
  } else if (band_) {
    band_visible_ = true;
    const int x0 = static_cast<int>(std::min(drag_x0_, x)), y0 = static_cast<int>(std::min(drag_y0_, y));
    band_rect_ = {x0, y0, static_cast<int>(std::abs(x - drag_x0_)), static_cast<int>(std::abs(y - drag_y0_))};
    const ViewMetrics m = metrics();
    const Rect c = lay_.content;
    // the band is anchored in content space, so it keeps selecting what scrolled under it
    auto idx = indices_in_rect(m, static_cast<int>(drag_x0_ - c.x), static_cast<int>(drag_y0_ - c.y + (b.scroll_y - band_scroll_y_)), static_cast<int>(x - c.x), static_cast<int>(y - c.y), b.scroll_x, b.scroll_y);
    b.select_indices(idx, ctrl_down_ && !band_base_.empty());
    if (ctrl_down_) {
      for (int i : band_base_)
        if (i >= 0 && i < static_cast<int>(b.sel.size())) b.sel[i] = 1;
    }
    redraw = true;
    // auto-scroll near the edges
    if (y < c.y + 16) b.scroll_y = std::max(0, b.scroll_y - 12);
    else if (y > c.y + c.h - 16) b.scroll_y = std::min(max_scroll_y(m), b.scroll_y + 12);
  }
  if (press_item_ >= 0 && left_down_ && !item_drag_ && std::hypot(x - press_x_, y - press_y_) > 8) begin_item_drag(x, y);
  if (item_drag_) {
    const std::string t = drop_target_at(x, y);
    if (t != drop_hot_) drop_hot_ = t;
    redraw = true;
  }
  const Region* after = region_at(x, y);
  const int kind_after = after ? static_cast<int>(after->kind) * 100000 + after->arg * 10 + after->arg2 : -1;
  if (kind_after != kind_before) redraw = true;
  if (after && after->kind == R::Content) {
    const ViewMetrics m = metrics();
    const int hi = index_at(m, static_cast<int>(x - lay_.content.x), static_cast<int>(y - lay_.content.y), b.scroll_x, b.scroll_y);
    if (hi != item_before) redraw = true;
  } else if (item_before >= 0) {
    redraw = true;
  }
  if (redraw) schedule_redraw();
}

void FmWindow::on_scroll(double dx, double dy) {
  if (dlg_ != Dlg::None && ui_) {
    ui_->scroll(dy);
    schedule_redraw();
    return;
  }
  Browser& b = tab();
  if (menu_open()) return;
  const bool over_nav = lay_.nav.w > 0 && lay_.nav.contains(static_cast<int>(mx_), static_cast<int>(my_));
  if (over_nav) {
    nav_scroll_ = std::max(0.0, nav_scroll_ + dy * 3);
    schedule_redraw();
    return;
  }
  if (!lay_.content.contains(static_cast<int>(mx_), static_cast<int>(my_))) return;
  if (ctrl_down_ && std::abs(dy) > 0.5) {
    // Ctrl+wheel: the next bigger or smaller icon size, like Explorer
    static const ViewMode order[] = {ViewMode::List, ViewMode::Details, ViewMode::SmallIcons, ViewMode::MediumIcons, ViewMode::LargeIcons, ViewMode::ExtraLargeIcons};
    int cur = 1;
    for (int i = 0; i < 6; ++i)
      if (order[i] == b.mode) cur = i;
    cur = std::clamp(cur + (dy < 0 ? 1 : -1), 0, 5);
    set_view_mode(order[cur]);
    return;
  }
  const ViewMetrics m = metrics();
  const bool natural = false;
  const double k = natural ? -3 : 3;
  if (m.horizontal) b.scroll_x = std::clamp(b.scroll_x + static_cast<int>((dx + dy) * k), 0, max_scroll_x(m));
  else b.scroll_y = std::clamp(b.scroll_y + static_cast<int>(dy * k), 0, max_scroll_y(m));
  schedule_redraw();
}

void FmWindow::on_button(double x, double y, uint32_t button, bool pressed) {
  mx_ = x;
  my_ = y;
  if (dlg_ != Dlg::None && ui_) {
    ui_->pointer_button(x - dlg_rect_.x, y - dlg_rect_.y, button, pressed);
    schedule_redraw();
    return;
  }
  if (menu_open()) {
    if (!pressed) return;
    const Region* r = region_at(x, y);
    if (r && r->kind == R::SubItem && r->arg >= 0 && r->arg < static_cast<int>(sub_.items.size())) {
      const MenuItem it = sub_.items[r->arg];
      if (!it.enabled) return;
      close_menu();
      run(it.cmd, it.arg, it.sarg);
    } else if (r && r->kind == R::MenuItem && r->arg >= 0 && r->arg < static_cast<int>(menu_.items.size())) {
      const MenuItem it = menu_.items[r->arg];
      if (!it.enabled) return;
      if (!it.sub.empty()) {
        sub_.items = it.sub;
        sub_.parent = r->arg;
        sub_.hot = -1;
      } else {
        close_menu();
        run(it.cmd, it.arg, it.sarg);
      }
    } else if (!(r && (r->kind == R::MenuItem || r->kind == R::SubItem))) {
      close_menu();
    }
    schedule_redraw();
    return;
  }
  if (button == BTN_LEFT_) {
    if (pressed) left_press(x, y, 0);
    else left_release(x, y);
  } else if (button == BTN_RIGHT_ && pressed) {
    right_press(x, y);
  } else if (button == BTN_MIDDLE_ && pressed) {
    middle_press(x, y);
  }
  schedule_redraw();
}

void FmWindow::left_press(double x, double y, uint32_t) {
  left_down_ = true;
  const Region* r = region_at(x, y);
  Browser& b = tab();
  const double now = now_();
  // clicking elsewhere ends text editing
  const bool in_addr = r && r->kind == R::Address, in_search = r && (r->kind == R::Search || r->kind == R::SearchClear);
  if (renaming_ && !(r && r->kind == R::Content)) commit_rename();
  if (renaming_ && r && r->kind == R::Content) {
    const ViewMetrics m = metrics();
    if (index_at(m, static_cast<int>(x - lay_.content.x), static_cast<int>(y - lay_.content.y), b.scroll_x, b.scroll_y) != rename_index_) commit_rename();
    else return;
  }
  if (!in_addr) addr_focus_ = false;
  if (!in_search) search_focus_ = false;
  if (!r) return;
  switch (r->kind) {
    case R::JobBtn: {
      if (r->arg < 0 || r->arg >= static_cast<int>(jobs_.size())) return;
      Job& j = *jobs_[r->arg];
      switch (r->arg2) {
        case 0: j.t->pause(!j.t->progress().paused); break;
        case 1: j.t->cancel(); break;
        case 2: j.details = !j.details; break;
        case 3: j.finished_shown = true; break;
        case 4:
          checksum_rows_.clear();
          for (const VerifiedFile& v : j.result.verified) checksum_rows_.emplace_back(v.checksum, v.destination);
          open_dialog(Dlg::Checksums);
          break;
        case 5:
          err_rows_ = j.result.errors;
          open_dialog(Dlg::Errors);
          break;
        default: break;
      }
      return;
    }
    case R::Tool: run(static_cast<Cmd>(r->arg), r->arg2); return;
    case R::MenuBar: {
      static const char* names[] = {"File", "Edit", "View", "Tools", "Help"};
      open_menu(menu_bar_items(r->arg), r->r.x, r->r.y + r->r.h);
      (void)names;
      return;
    }
    case R::Tab: select_tab(static_cast<size_t>(r->arg)); return;
    case R::TabClose: close_tab(static_cast<size_t>(r->arg)); return;
    case R::TabNew:
      run(Cmd::NewTab);
      return;
    case R::NavArrow:
      if (r->arg >= 0 && r->arg < static_cast<int>(nav_.rows().size())) {
        nav_.toggle(nav_.rows()[r->arg].key);
        nav_dirty_ = true;
      }
      return;
    case R::NavEject:
      if (r->arg >= 0 && r->arg < static_cast<int>(nav_.rows().size())) {
        const NavRow& row = nav_.rows()[r->arg];
        eject_volume_at(row.volume.mounted ? row.volume.mountpoint : row.volume.device);
      }
      return;
    case R::NavItem:
      if (r->arg >= 0 && r->arg < static_cast<int>(nav_.rows().size())) {
        const NavRow row = nav_.rows()[r->arg];
        if (!row.address.empty()) open_address(row.address);
      }
      return;
    case R::Crumb: {
      const auto c = crumbs();
      if (r->arg >= 0 && r->arg < static_cast<int>(c.size())) {
        if (static_cast<size_t>(r->arg) + 1 == c.size() && b.search.empty()) {
          // the last crumb: start editing the path, like clicking an Explorer address
          addr_focus_ = true;
          addr_edit_.set(b.address());
        } else {
          open_address(c[r->arg].second);
        }
      }
      return;
    }
    case R::CrumbArrow: {
      const auto c = crumbs();
      if (r->arg < 0 || r->arg >= static_cast<int>(c.size())) return;
      std::vector<MenuItem> items;
      const std::string& addr = c[r->arg].second;
      if (!addr.empty() && addr[0] == '/') {
        DirListing l;
        if (list_dir(addr, {true, s_.show_hidden}, &l)) {
          sort_listing(&l, SortKey::Name);
          int n = 0;
          for (const Entry& e : l.entries) {
            if (!l.is_dir(e)) continue;
            MenuItem m;
            m.label = std::string(l.name(e));
            m.cmd = Cmd::Open;
            m.sarg = (addr == "/" ? "" : addr) + "/" + m.label;
            m.arg = -1;
            items.push_back(m);
            if (++n >= 40) break;
          }
        }
      } else if (addr == "computer:///") {
        for (const Volume& v : volumes_) {
          if (!v.mounted) continue;
          MenuItem m;
          m.label = volume_display_name(v);
          m.cmd = Cmd::Open;
          m.sarg = v.mountpoint;
          m.arg = -1;
          items.push_back(m);
        }
      }
      if (!items.empty()) open_menu(std::move(items), r->r.x, r->r.y + r->r.h);
      return;
    }
    case R::Address:
      addr_focus_ = true;
      search_focus_ = false;
      addr_edit_.set(b.address());
      return;
    case R::Search:
      search_focus_ = true;
      addr_focus_ = false;
      return;
    case R::SearchClear:
      search_edit_.set("", false);
      start_search("");
      search_focus_ = false;
      return;
    case R::Head: {
      auto cols = active_columns();
      if (r->arg < 0 || r->arg >= static_cast<int>(cols.size())) return;
      const std::string& id = cols[r->arg].id;
      if (id == "name") toggle_sort(SortKey::Name);
      else if (id == "modified") toggle_sort(SortKey::Modified);
      else if (id == "type") toggle_sort(SortKey::Type);
      else if (id == "size") toggle_sort(SortKey::Size);
      return;
    }
    case R::HeadSep: {
      auto cols = active_columns();
      col_drag_ = r->arg;
      col_drag_x_ = x;
      col_drag_w_ = cols[r->arg].width;
      return;
    }
    case R::Splitter:
      nav_split_ = true;
      return;
    case R::ScrollV: {
      const ViewMetrics m = metrics();
      const double track = lay_.content.h - m.header_h;
      const double thumb = std::max(24.0, track * track / std::max(1, m.content_h));
      const double pos = r->arg2;
      const double rel = y - lay_.content.y - m.header_h;
      if (rel >= pos && rel <= pos + thumb) {
        vscroll_drag_ = true;
        vscroll_off_ = rel - pos;
      } else {
        b.scroll_y = std::clamp(b.scroll_y + (rel < pos ? -1 : 1) * (lay_.content.h - m.header_h), 0, max_scroll_y(m));
      }
      return;
    }
    case R::ScrollH: {
      const ViewMetrics m = metrics();
      b.scroll_x = std::clamp(b.scroll_x + (x < lay_.content.x + lay_.content.w / 2 ? -1 : 1) * lay_.content.w / 2, 0, max_scroll_x(m));
      return;
    }
    case R::Details: {  // tiles in Computer / Network
      const bool dbl = now - last_click_t_ < 0.45 && last_click_item_ == r->arg * 3 + r->arg2;
      last_click_t_ = now;
      last_click_item_ = r->arg * 3 + r->arg2;
      b.focus = r->arg2 == 0 ? r->arg : -1;
      if (!dbl) return;
      if (b.place == PlaceKind::Network) {
        if (r->arg == 10000) run(Cmd::ConnectServer);
        else if (r->arg == 10001) run(Cmd::AddNextcloud);
        else if (r->arg >= 20000 && r->arg - 20000 < static_cast<int>(lan_hosts_.size())) open_address(lan_hosts_[r->arg - 20000].second);
        else if (r->arg >= 0 && r->arg < static_cast<int>(places_.size())) open_address(places_[r->arg].uri);
      } else if (r->arg >= 0 && r->arg < static_cast<int>(volumes_.size())) {
        const Volume& v = volumes_[r->arg];
        if (v.mounted) open_address(v.mountpoint);
        else mount_device(v.device);
      }
      return;
    }
    case R::Content: break;
    default: return;
  }
  // ---- a click in the file list ----
  if (b.place == PlaceKind::Computer || b.place == PlaceKind::Network) return;
  const ViewMetrics m = metrics();
  const int cx = static_cast<int>(x - lay_.content.x), cy = static_cast<int>(y - lay_.content.y);
  const int i = index_at(m, cx, cy, b.scroll_x, b.scroll_y);
  const bool ctrl = ctrl_down_, shift = shift_down_;
  if (i >= 0) {
    const bool dbl = now - last_click_t_ < 0.45 && last_click_item_ == i && std::abs(x - last_click_x_) < 6 && std::abs(y - last_click_y_) < 6;
    last_click_t_ = now;
    last_click_item_ = i;
    last_click_x_ = x;
    last_click_y_ = y;
    if (dbl && !ctrl && !shift) {
      item_activate(i);
      last_click_item_ = -1;
      return;
    }
    if (s_.click_mode == ClickMode::Single && !ctrl && !shift) {
      b.select_only(i);
      item_activate(i);
      return;
    }
    if (ctrl) b.toggle(i);
    else if (shift) b.select_to(i);
    else {
      press_was_selected_ = b.sel[i] && b.selected_count() > 1;
      if (!b.sel[i]) b.select_only(i);
      else {
        b.focus = b.anchor = i;
      }
      press_item_ = i;
      press_x_ = x;
      press_y_ = y;
    }
    scroll_to_show(m, i, &b.scroll_x, &b.scroll_y);
  } else {
    if (!ctrl && !shift) b.clear_selection();
    band_ = true;
    band_base_ = ctrl ? b.selected() : std::vector<int>{};
    band_scroll_y_ = b.scroll_y;
    drag_x0_ = x;
    drag_y0_ = y;
    last_click_item_ = -1;
  }
}

void FmWindow::left_release(double x, double y) {
  Browser& b = tab();
  left_down_ = false;
  if (item_drag_) {
    item_drag_ = false;
    const std::string target = drop_target_at(x, y);
    drop_hot_.clear();
    perform_drop(drag_paths_, target, alt_down_ ? 3 : (shift_down_ ? 2 : (ctrl_down_ ? 1 : 0)));
    drag_paths_.clear();
    press_item_ = -1;
    return;
  }
  if (col_drag_ >= 0) {
    col_drag_ = -1;
    save_settings();
  }
  if (nav_split_) {
    nav_split_ = false;
    save_settings();
  }
  vscroll_drag_ = false;
  if (band_) {
    band_ = false;
    band_visible_ = false;
  }
  if (press_item_ >= 0) {
    // a plain click on an item of a multi-selection selects just that one, on release
    const ViewMetrics m = metrics();
    const int i = index_at(m, static_cast<int>(x - lay_.content.x), static_cast<int>(y - lay_.content.y), b.scroll_x, b.scroll_y);
    if (press_was_selected_ && i == press_item_ && !ctrl_down_ && !shift_down_) b.select_only(i);
    press_item_ = -1;
  }
}

void FmWindow::right_press(double x, double y) {
  const Region* r = region_at(x, y);
  Browser& b = tab();
  if (!r) return;
  switch (r->kind) {
    case R::Tab: {
      std::vector<MenuItem> items;
      MenuItem a;
      a.label = "New tab";
      a.cmd = Cmd::NewTab;
      items.push_back(a);
      MenuItem c;
      c.label = "Close tab";
      c.cmd = Cmd::CloseTab;
      c.arg = r->arg;
      items.push_back(c);
      MenuItem w;
      w.label = "Open in new window";
      w.cmd = Cmd::OpenInNewWindow;
      w.sarg = tabs_[r->arg].address();
      items.push_back(w);
      open_menu(std::move(items), x, y);
      return;
    }
    case R::NavItem: menu_for_nav(r->arg, x, y); return;
    case R::Details:
      if (b.place == PlaceKind::Computer && r->arg >= 0 && r->arg < static_cast<int>(volumes_.size())) {
        const Volume& v = volumes_[r->arg];
        std::vector<MenuItem> items;
        MenuItem o;
        o.label = v.mounted ? "Open" : "Mount";
        o.cmd = v.mounted ? Cmd::Open : Cmd::Mount;
        o.sarg = v.mounted ? v.mountpoint : v.device;
        o.arg = -1;
        items.push_back(o);
        MenuItem t;
        t.label = "Open in new tab";
        t.cmd = Cmd::OpenInNewTab;
        t.sarg = v.mountpoint;
        t.enabled = v.mounted;
        items.push_back(t);
        items.push_back(MenuItem::sep());
        MenuItem e;
        e.label = v.kind == DriveKind::Network ? "Disconnect" : "Eject";
        e.cmd = Cmd::Eject;
        e.sarg = v.mounted ? v.mountpoint : v.device;
        e.enabled = (v.ejectable || v.kind == DriveKind::Network) && !v.system;
        items.push_back(e);
        open_menu(std::move(items), x, y);
      }
      return;
    case R::Content: break;
    default: return;
  }
  if (b.place == PlaceKind::Computer) return;
  if (b.place == PlaceKind::Network) {
    std::vector<MenuItem> items;
    MenuItem c;
    c.label = "Connect to server...";
    c.cmd = Cmd::ConnectServer;
    items.push_back(c);
    MenuItem n;
    n.label = "Add a Nextcloud account...";
    n.cmd = Cmd::AddNextcloud;
    items.push_back(n);
    open_menu(std::move(items), x, y);
    return;
  }
  const ViewMetrics m = metrics();
  const int i = index_at(m, static_cast<int>(x - lay_.content.x), static_cast<int>(y - lay_.content.y), b.scroll_x, b.scroll_y);
  if (i >= 0) {
    if (!b.sel[i]) b.select_only(i);
    menu_for_selection(x, y);
  } else {
    menu_for_background(x, y);
  }
}

void FmWindow::middle_press(double x, double y) {
  const Region* r = region_at(x, y);
  if (!r) return;
  Browser& b = tab();
  if (r->kind == R::Tab) {
    close_tab(static_cast<size_t>(r->arg));
  } else if (r->kind == R::NavItem && r->arg >= 0 && r->arg < static_cast<int>(nav_.rows().size())) {
    const std::string a = nav_.rows()[r->arg].address;
    if (!a.empty() && s_.middle_click_opens_tab) add_tab(a);
  } else if (r->kind == R::Crumb) {
    const auto c = crumbs();
    if (r->arg >= 0 && r->arg < static_cast<int>(c.size()) && s_.middle_click_opens_tab) add_tab(c[r->arg].second);
  } else if (r->kind == R::Content && s_.middle_click_opens_tab) {
    const ViewMetrics m = metrics();
    const int i = index_at(m, static_cast<int>(x - lay_.content.x), static_cast<int>(y - lay_.content.y), b.scroll_x, b.scroll_y);
    if (i >= 0 && b.is_dir_at(i)) add_tab(b.path_at(i));
  }
}

void FmWindow::item_activate(int i) {
  Browser& b = tab();
  if (i < 0 || i >= static_cast<int>(b.shown.size())) return;
  const std::string path = b.place == PlaceKind::Trash ? std::string() : b.path_at(i);
  if (b.place == PlaceKind::Trash) {
    show_properties();
    return;
  }
  if (b.is_dir_at(i)) {
    const bool new_tab = s_.open_folders_in == OpenIn::NewTab;
    if (s_.open_folders_in == OpenIn::NewWindow && host_.new_window) host_.new_window(path);
    else open_address(path, new_tab);
    return;
  }
  // a file: the default application. Executables are not run from here.
  open_default(path);
  toast("Opening " + b.label_at(i) + "...");
}

// ---------------------------------------------------------------------------------------------------------------------
// drag and drop
// ---------------------------------------------------------------------------------------------------------------------

// The folder a drop at (x, y) would go into: the folder item under the pointer, a navigation row, a tab, an address crumb, or the open
// folder when the pointer is over empty space. "" when nothing there takes files.
std::string FmWindow::drop_target_at(double x, double y, int* nav_row) const {
  if (nav_row) *nav_row = -1;
  const Region* r = region_at(x, y);
  if (!r) return {};
  const Browser& b = tab();
  auto local_dir = [](const std::string& a) { return !a.empty() && a[0] == '/' ? a : std::string(); };
  switch (r->kind) {
    case R::NavItem:
      if (r->arg >= 0 && r->arg < static_cast<int>(nav_.rows().size())) {
        if (nav_row) *nav_row = r->arg;
        return local_dir(nav_.rows()[r->arg].address);
      }
      return {};
    case R::Tab:
      if (r->arg >= 0 && r->arg < static_cast<int>(tabs_.size()) && tabs_[r->arg].place == PlaceKind::Local) return tabs_[r->arg].path;
      return {};
    case R::Crumb: {
      const auto c = crumbs();
      return r->arg >= 0 && r->arg < static_cast<int>(c.size()) ? local_dir(c[r->arg].second) : std::string();
    }
    case R::Content: {
      if (b.place != PlaceKind::Local) return {};
      const ViewMetrics m = metrics();
      const int i = index_at(m, static_cast<int>(x - lay_.content.x), static_cast<int>(y - lay_.content.y), b.scroll_x, b.scroll_y);
      if (i >= 0 && b.is_dir_at(i) && !b.sel[i]) return b.path_at(i);
      return b.search.empty() ? b.path : std::string();
    }
    default: return {};
  }
}

void FmWindow::perform_drop(const std::vector<std::string>& paths, const std::string& dest, int forced) {
  if (paths.empty() || dest.empty()) return;
  std::vector<std::string> use;
  for (const std::string& p : paths) {
    const fs::path parent = fs::path(p).parent_path();
    if (p == dest) continue;
    if (forced != 1 && parent == fs::path(dest)) continue;  // moving into the folder it is already in
    use.push_back(p);
  }
  if (use.empty()) return;
  if (forced == 0) {
    // A plain drop asks what is meant, like Nautilus, Dolphin and Explorer's right-button drag. Ctrl copies, Shift moves, Alt links, without asking.
    drop_paths_ = use;
    std::vector<MenuItem> items;
    auto add = [&](const char* label, Cmd c) {
      MenuItem m;
      m.label = label;
      m.cmd = c;
      m.sarg = dest;
      items.push_back(m);
    };
    add("Copy here", Cmd::DropCopy);  // the default: first, and already highlighted so Enter takes it
    add("Move here", Cmd::DropMove);
    add("Create symbolic link here", Cmd::DropSymlink);
    add("Create hard link here", Cmd::DropHardlink);
    items.push_back(MenuItem::sep());
    MenuItem cancel;
    cancel.label = "Cancel";
    items.push_back(cancel);
    open_menu(std::move(items), mx_, my_);
    menu_.hot = 0;
    return;
  }
  bool move;
  if (forced == 3) {
    run(Cmd::DropSymlink, 0, dest);
    return;
  }
  if (forced == 1) move = false;
  else if (forced == 2) move = true;
  else {
    struct stat a, d;
    move = ::stat(use[0].c_str(), &a) == 0 && ::stat(dest.c_str(), &d) == 0 && a.st_dev == d.st_dev;  // same drive: move, like Explorer
  }
  start_transfer(use, dest, move);
}

bool FmWindow::on_drag_motion(double x, double y, uint32_t* action) {
  mx_ = x;
  my_ = y;
  const std::string target = drop_target_at(x, y);
  if (target != drop_hot_) {
    drop_hot_ = target;
    schedule_redraw();
  }
  if (action) *action = shift_down_ ? 2 : 1;  // copy unless Shift: the menu after the drop asks for the real choice
  return !target.empty();
}

void FmWindow::on_drag_leave() {
  drop_hot_.clear();
  schedule_redraw();
}

void FmWindow::on_drop(double x, double y, const std::string& uri_list) {
  const std::string target = drop_target_at(x, y);
  drop_hot_.clear();
  const std::vector<std::string> paths = parse_path_list(uri_list);
  perform_drop(paths, target, alt_down_ ? 3 : (shift_down_ ? 2 : (ctrl_down_ ? 1 : 0)));
  drag_paths_.clear();
  schedule_redraw();
}

void FmWindow::begin_item_drag(double, double) {
  Browser& b = tab();
  drag_paths_.clear();
  if (b.place != PlaceKind::Local && b.place != PlaceKind::Remote) return;
  for (int i : b.selected()) drag_paths_.push_back(b.path_at(i));
  if (drag_paths_.empty()) return;
  if (host_.start_drag) {
    auto alive = alive_;
    if (host_.start_drag(drag_paths_, true, [this, alive](bool, bool) {
          if (!*alive) return;
          drag_paths_.clear();
          schedule_redraw();
        })) {
      press_item_ = -1;
      return;
    }
  }
  item_drag_ = true;  // no compositor drag and drop: the window follows the pointer itself
  press_item_ = -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::on_key(const kit::KeyEvent& ev) {
  shift_down_ = ev.mods & kit::kShift;
  ctrl_down_ = ev.mods & kit::kCtrl;
  alt_down_ = ev.mods & kit::kAlt;
  if (!ev.pressed) {
    if (ev.sym == XKB_KEY_Alt_L && s_.menu_bar == MenuBar::Alt && alt_solo_) {
      menu_bar_shown_ = !menu_bar_shown_;
      schedule_redraw();
    }
    return;
  }
  alt_solo_ = ev.sym == XKB_KEY_Alt_L;
  if (dlg_ != Dlg::None && ui_) {
    if (ev.sym == XKB_KEY_Escape && !ui_->modal_open()) {
      if (dlg_ == Dlg::Conflict && conflict_job_) answer_conflict(conflict_job_, Conflict::Cancel, false);
      else close_dialog();
    } else {
      ui_->key(ev);
    }
    schedule_redraw();
    return;
  }
  if (menu_open()) {
    const bool in_sub = !sub_.items.empty() && sub_.hot >= 0;
    std::vector<MenuItem>& level = in_sub ? sub_.items : menu_.items;
    int& hot = in_sub ? sub_.hot : menu_.hot;
    const int n = static_cast<int>(level.size());
    auto step = [&](int d) {
      int h = hot;
      for (int k = 0; k < n; ++k) {
        h = (h + d + n) % n;
        if (!level[h].separator && level[h].enabled) break;
      }
      hot = h;
    };
    if (ev.sym == XKB_KEY_Escape) {
      if (in_sub) sub_.hot = -1, sub_.items.clear(), sub_.parent = -1;
      else close_menu();
    } else if (ev.sym == XKB_KEY_Down) step(1);
    else if (ev.sym == XKB_KEY_Up) step(-1);
    else if (ev.sym == XKB_KEY_Right && menu_.hot >= 0 && !in_sub && !menu_.items[menu_.hot].sub.empty()) {
      sub_.items = menu_.items[menu_.hot].sub;
      sub_.parent = menu_.hot;
      sub_.hot = 0;
    } else if (ev.sym == XKB_KEY_Left && in_sub) {
      sub_.items.clear();
      sub_.parent = sub_.hot = -1;
    } else if ((ev.sym == XKB_KEY_Return || ev.sym == XKB_KEY_KP_Enter) && hot >= 0) {
      const MenuItem it = level[hot];
      if (!it.sub.empty()) {
        sub_.items = it.sub;
        sub_.parent = hot;
        sub_.hot = 0;
      } else {
        close_menu();
        run(it.cmd, it.arg, it.sarg);
      }
    }
    schedule_redraw();
    return;
  }
  if (renaming_) {
    key_in_field(ev, &rename_edit_, &renaming_, [this] { commit_rename(); }, [] {});
    if (ev.sym == XKB_KEY_Escape) cancel_rename();
    schedule_redraw();
    return;
  }
  if (addr_focus_) {
    key_in_field(ev, &addr_edit_, &addr_focus_, [this] {
      addr_focus_ = false;
      open_address(addr_edit_.text);
    }, [] {});
    schedule_redraw();
    return;
  }
  if (search_focus_) {
    key_in_field(ev, &search_edit_, &search_focus_, [this] { start_search(search_edit_.text); }, [this] {
      const std::string q = search_edit_.text;
      search_pending_ = q;
      if (q.empty()) {
        start_search("");
        return;
      }
      if (!s_.search_subfolders) {
        start_search(q);
        return;
      }
      auto alive = alive_;
      auto go = [this, alive, q] {
        if (*alive && search_pending_ == q) start_search(q);
      };
      if (host_.after) host_.after(220, go);
      else go();
    });
    schedule_redraw();
    return;
  }
  key_in_content(ev);
  schedule_redraw();
}

void FmWindow::key_in_field(const kit::KeyEvent& ev, LineEdit* e, bool* focused, const std::function<void()>& on_enter, const std::function<void()>& on_change) {
  const bool shift = ev.mods & kit::kShift, ctrl = ev.mods & kit::kCtrl;
  switch (ev.sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: on_enter(); return;
    case XKB_KEY_Escape:
      *focused = false;
      if (e == &search_edit_) {
        search_edit_.set("", false);
        start_search("");
      }
      return;
    case XKB_KEY_Left: e->left(shift, ctrl); return;
    case XKB_KEY_Right: e->right(shift, ctrl); return;
    case XKB_KEY_Home: e->home(shift); return;
    case XKB_KEY_End: e->end(shift); return;
    case XKB_KEY_BackSpace: e->backspace(ctrl), on_change(); return;
    case XKB_KEY_Delete: e->del(ctrl), on_change(); return;
    default: break;
  }
  if (ctrl) {
    if (ev.sym == XKB_KEY_a || ev.sym == XKB_KEY_A) e->select_all();
    else if ((ev.sym == XKB_KEY_v || ev.sym == XKB_KEY_V) && host_.paste_text) {
      paste_target_ = e == &addr_edit_ ? 1 : (e == &search_edit_ ? 2 : 3);
      host_.paste_text([this](const std::string& t) { on_paste_text(t); });
    } else if (ev.sym == XKB_KEY_x || ev.sym == XKB_KEY_X) {
      e->cut();
      on_change();
    } else if (ev.sym == XKB_KEY_l || ev.sym == XKB_KEY_L) {
      addr_focus_ = true;
      addr_edit_.set(tab().address());
    }
    return;
  }
  if (!ev.utf8.empty() && static_cast<unsigned char>(ev.utf8[0]) >= 0x20 && ev.utf8[0] != 0x7f) {
    e->insert(ev.utf8);
    on_change();
  }
}

void FmWindow::on_paste_text(const std::string& text) {
  if (text.empty()) return;
  if (paste_target_ && (addr_focus_ || search_focus_ || renaming_)) {
    std::string t = text;
    t.erase(std::remove(t.begin(), t.end(), '\n'), t.end());
    t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
    LineEdit& e = addr_focus_ ? addr_edit_ : (search_focus_ ? search_edit_ : rename_edit_);
    e.insert(t);
    if (search_focus_) {
      search_pending_ = e.text;
      start_search(e.text);
    }
    paste_target_ = 0;
    schedule_redraw();
    return;
  }
  const std::vector<std::string> paths = parse_path_list(text);
  if (!paths.empty()) {
    const Browser& b = tab();
    if (b.place == PlaceKind::Local || b.place == PlaceKind::Remote) start_transfer(paths, b.path, false);
  }
}

void FmWindow::key_in_content(const kit::KeyEvent& ev) {
  Browser& b = tab();
  const bool shift = ev.mods & kit::kShift, ctrl = ev.mods & kit::kCtrl, alt = ev.mods & kit::kAlt;
  const xkb_keysym_t k = ev.sym;
  auto lower = [](xkb_keysym_t s) { return s >= XKB_KEY_A && s <= XKB_KEY_Z ? s + 32 : s; };
  const xkb_keysym_t lk = lower(k);
  if (ctrl) {
    switch (lk) {
      case XKB_KEY_z: run(Cmd::Undo); return;
      case XKB_KEY_c: run(Cmd::Copy); return;
      case XKB_KEY_x: run(Cmd::Cut); return;
      case XKB_KEY_v: run(Cmd::Paste); return;
      case XKB_KEY_a: run(Cmd::SelectAll); return;
      case XKB_KEY_i: run(Cmd::InvertSelection); return;
      case XKB_KEY_n: shift ? run(Cmd::NewFolder) : run(Cmd::NewWindow); return;
      case XKB_KEY_t: shift ? run(Cmd::ReopenTab) : run(Cmd::NewTab); return;
      case XKB_KEY_w: run(Cmd::CloseTab, static_cast<int>(cur_)); return;
      case XKB_KEY_l: run(Cmd::FocusAddress); return;
      case XKB_KEY_e:
      case XKB_KEY_f: run(Cmd::FocusSearch); return;
      case XKB_KEY_r: run(Cmd::Reload); return;
      case XKB_KEY_h: run(Cmd::ToggleHidden); return;
      case XKB_KEY_q: run(Cmd::Quit); return;
      case XKB_KEY_comma: run(Cmd::Settings); return;
      case XKB_KEY_1: run(Cmd::SetView, static_cast<int>(ViewMode::ExtraLargeIcons)); return;
      case XKB_KEY_2: run(Cmd::SetView, static_cast<int>(ViewMode::LargeIcons)); return;
      case XKB_KEY_3: run(Cmd::SetView, static_cast<int>(ViewMode::MediumIcons)); return;
      case XKB_KEY_4: run(Cmd::SetView, static_cast<int>(ViewMode::SmallIcons)); return;
      case XKB_KEY_5: run(Cmd::SetView, static_cast<int>(ViewMode::List)); return;
      case XKB_KEY_6: run(Cmd::SetView, static_cast<int>(ViewMode::Details)); return;
      case XKB_KEY_7: run(Cmd::SetView, static_cast<int>(ViewMode::Tiles)); return;
      case XKB_KEY_8: run(Cmd::SetView, static_cast<int>(ViewMode::Content)); return;
      default: break;
    }
    if (k == XKB_KEY_Tab || k == XKB_KEY_ISO_Left_Tab) {
      run(shift || k == XKB_KEY_ISO_Left_Tab ? Cmd::PrevTab : Cmd::NextTab);
      return;
    }
    if (k == XKB_KEY_Page_Down) return run(Cmd::NextTab);
    if (k == XKB_KEY_Page_Up) return run(Cmd::PrevTab);
  }
  if (alt) {
    if (k == XKB_KEY_Left) return run(Cmd::Back);
    if (k == XKB_KEY_Right) return run(Cmd::Forward);
    if (k == XKB_KEY_Up) return run(Cmd::Up);
    if (k == XKB_KEY_Home) return run(Cmd::Home);
    if (k == XKB_KEY_Return || k == XKB_KEY_KP_Enter) return run(Cmd::Properties);
    if (k == XKB_KEY_d || k == XKB_KEY_D) return run(Cmd::FocusAddress);
  }
  switch (k) {
    case XKB_KEY_F5: run(Cmd::Reload); return;
    case XKB_KEY_F2: run(Cmd::Rename); return;
    case XKB_KEY_F3: run(Cmd::FocusSearch); return;
    case XKB_KEY_F4: run(Cmd::FocusAddress); return;
    case XKB_KEY_F10: menu_bar_shown_ = !menu_bar_shown_; return;
    case XKB_KEY_F1: run(Cmd::About); return;
    case XKB_KEY_Delete:
    case XKB_KEY_KP_Delete: run(shift ? Cmd::DeletePermanent : Cmd::Delete); return;
    case XKB_KEY_BackSpace: run(Cmd::Back); return;
    case XKB_KEY_Escape:
      if (!b.search.empty() || !search_edit_.text.empty()) {
        search_edit_.set("", false);
        start_search("");
      } else {
        b.clear_selection();
      }
      return;
    case XKB_KEY_Menu: menu_for_selection(lay_.content.x + 60, lay_.content.y + 60); return;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
      if (b.selected_count() >= 1) run(Cmd::Open);
      else if (b.focus >= 0) item_activate(b.focus);
      return;
    default: break;
  }
  const ViewMetrics m = metrics();
  Nav nav;
  bool is_nav = true;
  switch (k) {
    case XKB_KEY_Up: nav = Nav::Up; break;
    case XKB_KEY_Down: nav = Nav::Down; break;
    case XKB_KEY_Left: nav = Nav::Left; break;
    case XKB_KEY_Right: nav = Nav::Right; break;
    case XKB_KEY_Page_Up: nav = Nav::PageUp; break;
    case XKB_KEY_Page_Down: nav = Nav::PageDown; break;
    case XKB_KEY_Home: nav = Nav::Home; break;
    case XKB_KEY_End: nav = Nav::End; break;
    default: is_nav = false; nav = Nav::Down; break;
  }
  if (is_nav && !b.shown.empty()) {
    const int to = navigate(m, b.focus, nav);
    if (to >= 0) {
      if (ctrl) b.set_focus(to);
      else if (shift) b.select_to(to);
      else b.select_only(to);
      scroll_to_show(m, to, &b.scroll_x, &b.scroll_y);
    }
    return;
  }
  if (k == XKB_KEY_space && ctrl && b.focus >= 0) {
    b.toggle(b.focus);
    return;
  }
  // type to select (or to search, when the setting says so)
  if (!ev.utf8.empty() && !ctrl && !alt && static_cast<unsigned char>(ev.utf8[0]) >= 0x20) {
    if (s_.type_ahead == TypeAhead::Search) {
      search_focus_ = true;
      search_edit_.insert(ev.utf8);
      search_pending_ = search_edit_.text;
      start_search(search_edit_.text);
      return;
    }
    const int hit = b.type_ahead(ev.utf8, now_());
    if (hit >= 0) scroll_to_show(m, hit, &b.scroll_x, &b.scroll_y);
  }
}

}  // namespace fleetwm::fm
