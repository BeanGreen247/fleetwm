#pragma once

// The file manager window: tabs, address bar, navigation pane, the file list in every view mode, panes, menus, dialogs and
// the transfer panels. It draws with cairo into whatever context it is given and takes input as plain calls, so it runs the
// same in the Wayland client (apps/fleetfm), in a headless screenshot, and in the unit tests.

#include <cairo.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

#include "browser.hpp"
#include "file_ops.hpp"
#include "fleetkit.hpp"
#include "fm_settings.hpp"
#include "desktop_entry.hpp"
#include "icons.hpp"
#include "line_edit.hpp"
#include "mounts.hpp"
#include "nav_tree.hpp"
#include "search.hpp"
#include "transfer.hpp"
#include "trash.hpp"
#include "ui.hpp"
#include "ui_colors.hpp"
#include "view_metrics.hpp"
#include "view_style.hpp"

namespace fleetwm::fm {

// What the window needs from the outside world. Every member may be left empty (tests do).
struct Host {
  std::function<void()> redraw;                                   // something changed: draw again
  std::function<void(const std::string&)> set_title;
  std::function<void(std::function<void()>)> post;                // run on the main thread (thread-safe)
  std::function<void(int ms, std::function<void()>)> after;       // run once on the main thread after ms
  std::function<void(const std::string&)> watch;                  // watch this folder for changes
  std::function<void(std::function<void(const std::string&)>)> paste_text;
  std::function<void(const std::string&)> new_window;             // open a folder in another window
  std::function<void()> quit;
  std::function<double()> now;                                    // seconds, monotonic
  std::function<void(const std::string& text)> set_clipboard_text;
  // The system clipboard as files: what other file managers put there (x-special/gnome-copied-files, text/uri-list) and read.
  std::function<void(const std::vector<std::string>& paths, bool cut)> set_clipboard_files;
  std::function<void(std::function<void(const std::vector<std::string>& paths, bool cut)>)> read_clipboard_files;
  // Starts a drag of files out of the window (returns false when drag and drop is not available).
  std::function<bool(const std::vector<std::string>& paths, bool allow_move, std::function<void(bool performed, bool moved)>)> start_drag;
};

enum class Cmd {
  None, Back, Forward, Up, Reload, Home, ThisPc, NewTab, CloseTab, NextTab, PrevTab, ReopenTab, NewWindow, NewFolder, Cut, Copy, Paste, Delete,
  DeletePermanent, Rename, Properties, SelectAll, SelectNone, InvertSelection, Open, OpenInNewTab, OpenInNewWindow, OpenWith, Eject, Mount, ConnectServer,
  AddNextcloud, Disconnect, Settings, About, SetView, SetSort, ToggleSortDirection, ToggleHidden, ToggleExtensions, ToggleNav, ToggleDetails, TogglePreview,
  ToggleStatus, ToggleMenuBar, EmptyTrash, Restore, CopyPath, Checksums, SetStyle, FocusAddress, FocusSearch, ClearSearch, AddFavorite, MenuOrganize,
  MenuView, MenuSort, MenuStyle, MenuLayout, ShowTransfer, Quit, SortBy, SetGroup, OpenTerminal, UndoLast, DropMove, DropCopy, DropSymlink, DropHardlink, OpenWithApp, SendTo, CreateShortcut, Compress, Extract, CopyLocation, Undo, ScanNetwork
};

struct MenuItem {
  std::string label, shortcut;
  Cmd cmd = Cmd::None;
  int arg = 0;
  std::string sarg;
  bool enabled = true, checked = false, separator = false, radio = false, bold = false;
  std::vector<MenuItem> sub;  // a submenu: opens to the right when the pointer rests on the item
  static MenuItem sep() {
    MenuItem m;
    m.separator = true;
    return m;
  }
};

class FmWindow {
 public:
  // One copy or move running (or finished and not yet dismissed).
  struct Job {
    int id = 0;
    std::unique_ptr<Transfer> t;
    std::thread th;
    std::atomic<bool> done{false};
    TransferResult result;
    std::string title, from, to;
    std::string dest_path;                 // the full destination folder
    std::vector<std::string> sources;      // the full source paths
    HashAlgo verify_algo = HashAlgo::Sha256;
    double done_at = 0;
    bool verifying = false, move = false, finished_shown = false, details = false, eject_after = false;
    std::string eject_target;
    std::deque<double> speeds;
    double started = 0;
    // conflict question, asked from the worker, answered from the window
    std::mutex mu;
    std::condition_variable cv;
    bool asking = false, answered = false, apply_all = false;
    ConflictInfo question;
    Conflict answer = Conflict::KeepBoth;
    Conflict all_answer = Conflict::KeepBoth;
    bool have_all = false;
  };

  FmWindow(Host host, FmSettings settings, kit::Palette theme);
  ~FmWindow();
  FmWindow(const FmWindow&) = delete;
  FmWindow& operator=(const FmWindow&) = delete;

  // Opens the start location (settings, or `first` when given).
  void start(const std::string& first = {});
  void draw(cairo_t* cr, int w, int h);
  void on_motion(double x, double y);
  void on_button(double x, double y, uint32_t button, bool pressed);  // Linux button codes: 0x110 left, 0x111 right, 0x112 middle
  void on_scroll(double dx, double dy);
  void on_key(const kit::KeyEvent& ev);
  void on_leave();
  void on_focus(bool focused);
  void on_dir_changed();                                              // the watched folder changed
  void on_paste_text(const std::string& text);
  // Drag and drop onto the window (from this program or another one).
  bool on_drag_motion(double x, double y, uint32_t* action);  // true when a drop here would be accepted; *action: 1 copy, 2 move
  void on_drag_leave();
  void on_drop(double x, double y, const std::string& uri_list);

  // ---- state, for the host and the tests ----
  FmSettings& settings() { return s_; }
  const FmSettings& settings() const { return s_; }
  Rect item_screen_rect(int index) const;                              // where item `index` of the current tab is drawn (window coordinates)
  std::string dialog_name() const;                                    // "" or "settings", "about", "conflict" ...
  void show_settings_page(int page);
  const std::vector<std::unique_ptr<Job>>& jobs_for_test() const { return jobs_; }
  void close_dialog_for_test() { close_dialog(); }
  std::vector<std::string> menu_labels() const {
    std::vector<std::string> v;
    for (const MenuItem& m : menu_.items) v.push_back(m.separator ? "-" : m.label);
    return v;
  }
  std::vector<std::string> sub_labels(const std::string& of) const {
    std::vector<std::string> v;
    for (const MenuItem& m : menu_.items)
      if (m.label == of)
        for (const MenuItem& c : m.sub) v.push_back(c.separator ? "-" : c.label);
    return v;
  }
  const std::vector<std::pair<std::string, std::string>>& lan_hosts() const { return lan_hosts_; }
  size_t undo_depth() const { return undo_.size(); }
  Rect preview_rect() const { return lay_.preview; }
  Rect dialog_rect() const { return dlg_rect_; }
  Rect menu_rect() const { return {static_cast<int>(menu_.x), static_cast<int>(menu_.y), static_cast<int>(menu_.w), static_cast<int>(menu_.h)}; }
  void add_tab_for_test(const std::string& address) { add_tab(address); }
  void set_window_size(int w, int h) { s_.window_w = w; s_.window_h = h; }
  void save_session();                                                // remembers the last folder and the open tabs
  void screenshot_setup(const std::string& what);                     // headless screenshots: open a dialog or a menu
  void apply_settings();                                              // after settings() was edited: rebuild colours, layout, lists
  Browser& tab() { return tabs_[cur_]; }
  const Browser& tab() const { return tabs_[cur_]; }
  size_t tab_count() const { return tabs_.size(); }
  size_t current_tab() const { return cur_; }
  std::string title() const;
  bool busy() const;                                                  // loading, searching or transferring
  void run(Cmd c, int arg = 0, const std::string& sarg = {});
  void open_address(const std::string& address, bool new_tab = false, bool remember = true);
  void refresh_volumes();
  bool dialog_open() const { return dlg_ != Dlg::None; }
  bool menu_open() const { return !menu_.items.empty(); }
  int jobs_running() const;
  std::string status_text() const;
  const std::vector<Volume>& volumes() const { return volumes_; }
  const NavTree& nav() const { return nav_; }
  const Layout& layout() const { return lay_; }
  std::string last_message() const { return last_message_; }
  // Waits for background work to finish and applies its results (tests; the host calls post() results itself).
  void wait_idle(double timeout_s = 5.0);
  void set_volumes_for_test(std::vector<Volume> v) { volumes_ = std::move(v); nav_dirty_ = true; }
  void set_trash_for_test(const std::string& dir) { trash_ = Trash(dir); }
  CommandRunner* runner = nullptr;                                    // eject / mount helpers; null = the real ones

 private:
  friend struct WindowTestAccess;

  // ----- regions: what is under the pointer, rebuilt every frame -----
  enum class R {
    None, Tool, Tab, TabClose, TabNew, NavItem, NavArrow, NavEject, Crumb, CrumbArrow, Address, Search, SearchClear, Head, HeadSep, Content, ScrollV,
    ScrollH, MenuItem, SubItem, DlgButton, Splitter, Details, Toast, JobBtn, MenuBar, Check
  };
  struct Region {
    Rect r;
    R kind;
    int arg = 0;
    int arg2 = 0;
  };

  enum class Dlg { None, Settings, About, Properties, Connect, Nextcloud, Conflict, ConfirmDelete, Message, Checksums, Errors, ConfirmEmptyTrash };

  struct ThumbKey {
    std::string path;
    int64_t mtime;
    int px;
    bool operator<(const ThumbKey& o) const { return std::tie(path, mtime, px) < std::tie(o.path, o.mtime, o.px); }
  };


  // ----- drawing (window_draw.cpp) -----
  void paint_chrome();
  void paint_menu_bar();
  void paint_toolbar();
  void paint_address_row();
  void paint_tabs();
  void paint_nav();
  void paint_content();
  void paint_details_view(const ViewMetrics& m);
  void paint_item(const ViewMetrics& m, int i, const ItemRect& r);
  void paint_computer_view();
  void paint_network_view();
  void paint_pane_and_status();
  void paint_preview();
  int tip_item_ = -1;
  double tip_since_ = 0;
  std::string preview_path_;
  int64_t preview_mtime_ = 0;
  std::vector<std::string> preview_lines_;
  void paint_overlays();
  void paint_menu();
  void paint_menu_level(const std::vector<MenuItem>& items, double* px_x, double* px_y, int hot, R kind, double* out_w, double* out_h, std::vector<Rect>* rows);
  void paint_transfers();
  void paint_dialog();
  void paint_scrollbars(const ViewMetrics& m);
  void paint_empty_message(const Rect& area, const std::string& text);
  // primitives
  double text(const std::string& s, double x, double y_mid, double px, const Color& c, bool bold = false);
  double text_w(const std::string& s, double px, bool bold = false);
  std::string fit(const std::string& s, double max_w, double px, bool bold = false);
  void box(double x, double y, double w, double h, double r, const Color& top, const Color& bot, const Color* border, double bw = 1);
  void button(const Rect& r, const std::string& label, bool hot, bool down, bool enabled = true, bool accent = false);
  void glyph(int kind, double x, double y, double size, const Color& c);
  void icon(IconKind k, double x, double y, int px);
  void add_region(const Rect& r, R kind, int arg = 0, int arg2 = 0);
  const Region* region_at(double x, double y) const;
  void use_clip(const Rect& r);

  // ----- input and commands (window_input.cpp) -----
  void left_press(double x, double y, uint32_t mods);
  void left_release(double x, double y);
  void right_press(double x, double y);
  void middle_press(double x, double y);
  void key_in_content(const kit::KeyEvent& ev);
  void key_in_field(const kit::KeyEvent& ev, LineEdit* e, bool* focused_flag, const std::function<void()>& on_enter, const std::function<void()>& on_change);
  void item_activate(int i);
  void open_menu(std::vector<MenuItem> items, double x, double y);
  void close_menu();
  void menu_for_selection(double x, double y);
  void menu_for_background(double x, double y);
  void menu_for_nav(int row, double x, double y);
  std::vector<MenuItem> organize_items();
  std::vector<MenuItem> view_items();
  std::vector<MenuItem> sort_items();
  std::vector<MenuItem> group_items();
  std::vector<MenuItem> style_items();
  void begin_rename();
  void commit_rename();
  void cancel_rename();
  void toggle_sort(SortKey k);
  void set_view_mode(ViewMode m);
  bool shift_down_ = false, ctrl_down_ = false, alt_down_ = false, alt_solo_ = false;
  int paste_target_ = 0;
  std::vector<int> band_base_;
  int band_scroll_y_ = 0;
  std::vector<MenuItem> menu_bar_items(int which);
  void toast(const std::string& t, double seconds = 3.0);

  // ----- locations and loading (window.cpp) -----
  void load_current(bool keep_scroll = false);
  void open_local(const std::string& path, bool remember = true);
  void open_special(PlaceKind k, bool remember = true);
  void open_remote(const Uri& u, bool remember = true);
  void finish_load(size_t tab_id, uint64_t gen, DirListing&& l, const std::string& err, bool full_stat = true);
  void request_full_stat(std::function<void()> then = nullptr);
  void start_search(const std::string& q);
  void stop_search();
  void add_tab(const std::string& address);
  void close_tab(size_t i);
  void select_tab(size_t i);
  std::string select_after_load_;
  bool rename_after_load_ = false;
  struct FolderView {
    ViewMode mode;
    SortKey key;
    bool ascending;
  };
  std::map<std::string, FolderView> folder_views_;
  void load_folder_views();
  void save_folder_views() const;
  static std::string folder_views_path();
  void remember_folder_view();
  void apply_folder_view();
  std::vector<MenuItem> toolbar_menu(Cmd c);
  // ---- window_extra.cpp: open with, send to, compress, undo, network discovery ----
  struct UndoOp {
    std::string label;
    std::function<std::string()> undo;  // returns "" when it worked, else why not
  };
  std::vector<UndoOp> undo_;
  void push_undo(std::string label, std::function<std::string()> fn);
  void undo_last();
  std::vector<kit::DesktopEntry> apps_;
  bool apps_loaded_ = false;
  std::vector<kit::DesktopEntry> open_with_list_;
  std::vector<MenuItem> open_with_items(const std::string& path, bool is_dir);
  std::vector<MenuItem> send_to_items();
  void run_archive(bool extract, int kind, const std::vector<std::string>& paths);
  void open_terminal_in(const std::string& dir);
  std::vector<std::pair<std::string, std::string>> lan_hosts_;
  bool lan_scanning_ = false;
  void scan_network();
  struct {
    std::vector<MenuItem> items;
    double x = 0, y = 0, w = 0, h = 0;
    int hot = -1, parent = -1;
  } sub_;
  void open_menu_at_tool(Cmd c, std::vector<MenuItem> items);
  std::vector<std::pair<std::string, std::string>> props_hashes_;
  void update_title();
  void rebuild_nav();
  void watch_current();
  void request_thumb(const std::string& path, int64_t mtime, int px);
  cairo_surface_t* thumb_for(const std::string& path, int64_t mtime, int px);
  void schedule_redraw();
  void load_recent();
  void load_trash();
  Browser& tab_by_id(size_t id) { return tabs_[id < tabs_.size() ? id : cur_]; }
  std::string home_dir() const;
  size_t history_limit() const { return s_.remember_history ? static_cast<size_t>(s_.history_size) : 0; }
  std::string describe_selection() const;
  bool is_external_destination(const std::string& path) const;

  // ----- file operations (window_ops.cpp) -----
  void copy_selection(bool cut);
  void paste_here(bool into_selected_folder = false);
  void start_transfer(std::vector<std::string> sources, const std::string& dest, bool move);
  void delete_selection(bool permanent);
  void do_delete(std::vector<std::string> paths, bool permanent);
  void new_folder(bool text_document = false);
  void eject_volume_at(const std::string& mountpoint_or_device);
  void mount_device(const std::string& device);
  void connect_to(const Uri& u, const Credentials& c, const std::string& name, bool save);
  void finish_job(Job* j);
  void reap_jobs();
  void answer_conflict(Job* j, Conflict c, bool all);
  void show_message(const std::string& title, const std::string& text, bool error = false);
  void show_properties();
  void compute_checksums();
  const Volume* volume_for_path(const std::string& path) const;

  // ----- dialogs (window_dialogs.cpp) -----
  void dialog_settings(kit::Ui& ui, double w, double h);
  void dialog_about(kit::Ui& ui, double w, double h);
  void dialog_properties(kit::Ui& ui, double w, double h);
  void dialog_connect(kit::Ui& ui, double w, double h);
  void dialog_nextcloud(kit::Ui& ui, double w, double h);
  void dialog_conflict(kit::Ui& ui, double w, double h);
  void dialog_confirm_delete(kit::Ui& ui, double w, double h);
  void dialog_message(kit::Ui& ui, double w, double h);
  void dialog_checksums(kit::Ui& ui, double w, double h);
  void dialog_errors(kit::Ui& ui, double w, double h);
  void dialog_confirm_empty_trash(kit::Ui& ui, double w, double h);
  void open_dialog(Dlg d);
  void close_dialog();
  void save_settings();

  // ----- members -----
  Host host_;
  FmSettings s_;
  kit::Palette theme_;
  Colors col_;
  const StyleSpec* style_ = nullptr;
  IconCache icons_{192};
  std::vector<Browser> tabs_;
  size_t cur_ = 0;
  std::vector<std::string> closed_tabs_;
  std::vector<Volume> volumes_;
  std::vector<SavedPlace> places_;
  NavTree nav_;
  bool nav_dirty_ = true;
  Trash trash_;
  std::vector<TrashItem> trash_items_;

  cairo_t* cr_ = nullptr;
  int W_ = 0, H_ = 0;
  Layout lay_;
  bool focused_ = true;
  std::vector<Region> regions_, prev_regions_;
  double mx_ = -1, my_ = -1;
  Region hot_{};
  int hover_item_ = -1;
  double nav_scroll_ = 0;
  Rect dlg_rect_{};
  bool band_visible_ = false;
  Rect band_rect_{};
  bool search_box_open_ = false;
  bool computer_hot_ = false;
  double now_() const;
  struct ToolItem {
    Cmd cmd = Cmd::None;
    int arg = 0;
    int glyph = 0;
    std::string label;
    bool enabled = true, dropdown = false, right = false, sep_before = false, toggled = false;
  };
  std::vector<ToolItem> tool_items() const;
  void tool_button(Rect r, const ToolItem& t, bool icon_only);
  void paint_breadcrumbs(const Rect& r);
  std::vector<std::pair<std::string, std::string>> crumbs() const;
  void paint_column_headers(const ViewMetrics& m);
  std::string cell_text(const std::string& col, int i) const;
  std::vector<ColumnSetting> active_columns() const;
  ViewMetrics metrics() const;
  int icon_px() const;
  int row_h() const;
  int font_px() const;
  void paint_job_card(Job& j, Rect r, int index);
  void paint_progress(const Rect& r, double frac, bool warn);
  void paint_scroll_thumb(const Rect& track, double content, double view, double offset, bool vertical, int arg);

  // text fields
  LineEdit addr_edit_, search_edit_, rename_edit_;
  bool addr_focus_ = false, search_focus_ = false, renaming_ = false;
  int rename_index_ = -1;
  std::string rename_path_;
  std::string search_pending_;
  uint64_t search_gen_ = 0;
  std::atomic<bool> search_cancel_{false};
  std::thread search_thread_;

  // mouse interaction
  // dragging files
  bool item_drag_ = false, left_down_ = false;
  double press_x_ = 0, press_y_ = 0;
  std::vector<std::string> drag_paths_;
  std::vector<std::string> drop_paths_;  // files waiting for the answer of the "Move / Copy / Link" menu
  std::string drop_hot_;  // the folder (or address) the pointer is over while dragging, highlighted
  std::string drop_target_at(double x, double y, int* nav_row = nullptr) const;
  void perform_drop(const std::vector<std::string>& paths, const std::string& dest, int forced);
  void begin_item_drag(double x, double y);
  bool dragging_ = false, band_ = false, sel_drag_ = false;
  double drag_x0_ = 0, drag_y0_ = 0;
  int press_item_ = -1;
  bool press_was_selected_ = false;
  int col_drag_ = -1;
  double col_drag_x_ = 0;
  int col_drag_w_ = 0;
  bool nav_split_ = false;
  bool vscroll_drag_ = false;
  double vscroll_off_ = 0;
  double last_click_t_ = 0, last_click_x_ = 0, last_click_y_ = 0;
  int last_click_item_ = -1;

  // menus
  struct {
    std::vector<MenuItem> items;
    double x = 0, y = 0, w = 0, h = 0;
    int hot = -1;
    std::vector<Rect> rows;
  } menu_;
  bool menu_bar_shown_ = false;

  // internal clipboard (the system clipboard is read through Host::paste_text)
  std::vector<std::string> clip_;
  bool clip_cut_ = false;

  // dialogs
  Dlg dlg_ = Dlg::None;
  std::unique_ptr<kit::Ui> ui_;
  int settings_page_ = 0;
  double settings_scroll_ = 0;
  double checksum_scroll_ = 0;
  std::string compare_hash_;
  std::string dlg_title_, dlg_text_;
  bool dlg_error_ = false;
  FileProps props_;
  TreeSize props_size_;
  std::atomic<bool> props_cancel_{false};
  std::thread props_thread_;
  std::mutex props_mu_;
  std::vector<std::pair<std::string, std::string>> checksum_rows_;
  bool checksum_running_ = false;
  std::vector<TransferError> err_rows_;
  std::string connect_uri_, connect_user_, connect_pass_, connect_domain_, connect_name_;
  bool connect_save_ = true, connect_anon_ = false, connect_reveal_ = false;
  int connect_proto_ = 0;
  std::string nc_server_, nc_user_, nc_pass_;
  bool nc_reveal_ = false;
  std::string connect_error_;
  bool connecting_ = false;
  ConflictInfo conflict_;
  Job* conflict_job_ = nullptr;
  bool conflict_all_ = false;
  std::vector<std::string> delete_paths_;
  bool delete_permanent_ = false;
  std::string last_message_;
  std::string toast_;
  double toast_until_ = 0;

  // transfers
  std::vector<std::unique_ptr<Job>> jobs_;
  int next_job_ = 1;
  bool job_poll_ = false;

  // thumbnails
  std::map<ThumbKey, cairo_surface_t*> thumbs_;
  std::map<ThumbKey, bool> thumb_pending_;
  std::mutex thumb_mu_;
  std::deque<ThumbKey> thumb_lru_;
  size_t thumb_bytes_ = 0;

  // font metrics cache: px -> (ascent, height)
  std::map<int, std::pair<double, double>> font_cache_;
  std::map<std::string, double> width_cache_;
  bool stopping_ = false;

  // background work
  std::shared_ptr<std::atomic<bool>> alive_;
  uint64_t next_tab_id_ = 1;
  std::mutex post_mu_;
  std::vector<std::function<void()>> local_posts_;
  void post_(std::function<void()> fn);
  bool pump_();
  struct Loader {
    std::thread t;
    std::atomic<bool> done{false};
  };
  std::mutex load_mu_;
  std::deque<std::unique_ptr<Loader>> loaders_;
  void spawn_loader(std::function<void()> fn);
  std::atomic<int> loads_running_{0};
  std::atomic<int> loaders_active_{0};
  std::atomic<bool> search_running_{false};
  std::thread vol_thread_;
  std::atomic<bool> vol_busy_{false};
  std::thread thumb_thread_;
  std::condition_variable thumb_cv_;
  std::deque<ThumbKey> thumb_queue_;
  bool reload_pending_ = false;
  bool refresh_timer_ = false;
  std::shared_ptr<std::function<void()>> tick_keep_, job_tick_keep_;
  void apply_style_colors();
  void thumb_worker();
  void new_tab_state(Browser* b) const;
};

}  // namespace fleetwm::fm
