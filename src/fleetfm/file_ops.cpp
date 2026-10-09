#include "file_ops.hpp"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "child_signals.hpp"
#include "dir_listing.hpp"
#include "icons.hpp"
#include "locations.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

bool valid_file_name(const std::string& n, std::string* why) {
  auto bad = [&](const char* w) {
    if (why) *why = w;
    return false;
  };
  if (n.empty()) return bad("A file name cannot be empty.");
  if (n == "." || n == "..") return bad("That name is reserved.");
  if (n.size() > 255) return bad("The name is too long (255 bytes at most).");
  if (n.find('/') != std::string::npos) return bad("A file name cannot contain a slash (/).");
  if (n.find('\0') != std::string::npos) return bad("A file name cannot contain a null character.");
  return true;
}

std::string new_folder_name(const std::string& dir, const std::string& base) {
  std::error_code ec;
  if (!fs::exists(fs::path(dir) / base, ec)) return base;
  for (int i = 2;; ++i) {
    const std::string n = base + " (" + std::to_string(i) + ")";
    if (!fs::exists(fs::path(dir) / n, ec)) return n;
  }
}

bool make_dir(const std::string& path, std::string* err) {
  if (::mkdir(path.c_str(), 0777) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  return true;
}

bool rename_in_place(const std::string& path, const std::string& new_name, std::string* err) {
  if (!valid_file_name(new_name, err)) return false;
  const fs::path target = fs::path(path).parent_path() / new_name;
  if (target == fs::path(path)) return true;
  struct stat st;
  if (::lstat(target.c_str(), &st) == 0) {
    if (err) *err = "A file with that name already exists.";
    return false;
  }
  if (::rename(path.c_str(), target.c_str()) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  return true;
}

bool delete_tree(const std::string& path, std::string* err) {
  std::error_code ec;
  fs::remove_all(path, ec);
  if (ec) {
    if (err) *err = ec.message();
    return false;
  }
  return true;
}

TreeSize measure_tree(const std::string& path, const std::atomic<bool>* cancel) {
  TreeSize t;
  struct stat st;
  if (::lstat(path.c_str(), &st) != 0) return t;
  if (!S_ISDIR(st.st_mode)) {
    t.bytes = static_cast<uint64_t>(st.st_size);
    t.on_disk = static_cast<uint64_t>(st.st_blocks) * 512;
    t.files = 1;
    return t;
  }
  std::vector<std::string> stack = {path};
  while (!stack.empty()) {
    if (cancel && cancel->load()) break;
    const std::string dir = std::move(stack.back());
    stack.pop_back();
    DirListing l;
    if (!list_dir(dir, {false, true}, &l)) continue;
    for (const Entry& e : l.entries) {
      const std::string p = dir + "/" + std::string(l.name(e));
      if (e.kind == Kind::Dir) {
        ++t.folders;
        stack.push_back(p);
        continue;
      }
      struct stat s;
      if (::lstat(p.c_str(), &s) != 0) continue;
      ++t.files;
      t.bytes += static_cast<uint64_t>(s.st_size);
      t.on_disk += static_cast<uint64_t>(s.st_blocks) * 512;
    }
  }
  return t;
}

std::string mode_string(uint32_t m) {
  std::string s(10, '-');
  s[0] = S_ISDIR(m) ? 'd' : (S_ISLNK(m) ? 'l' : (S_ISCHR(m) ? 'c' : (S_ISBLK(m) ? 'b' : (S_ISFIFO(m) ? 'p' : (S_ISSOCK(m) ? 's' : '-')))));
  const char* rwx = "rwxrwxrwx";
  for (int i = 0; i < 9; ++i)
    if (m & (1u << (8 - i))) s[1 + i] = rwx[i];
  if (m & S_ISUID) s[3] = s[3] == 'x' ? 's' : 'S';
  if (m & S_ISGID) s[6] = s[6] == 'x' ? 's' : 'S';
  if (m & S_ISVTX) s[9] = s[9] == 'x' ? 't' : 'T';
  return s;
}

bool read_props(const std::string& path, FileProps* o) {
  struct stat st;
  if (::lstat(path.c_str(), &st) != 0) return false;
  o->path = path;
  o->name = fs::path(path).filename().string();
  if (o->name.empty()) o->name = path;
  o->mode = st.st_mode;
  o->mode_text = mode_string(st.st_mode);
  o->is_link = S_ISLNK(st.st_mode);
  if (o->is_link) {
    std::error_code ec;
    o->link_target = fs::read_symlink(path, ec).string();
    struct stat t;
    o->is_dir = ::stat(path.c_str(), &t) == 0 && S_ISDIR(t.st_mode);
  } else {
    o->is_dir = S_ISDIR(st.st_mode);
  }
  o->size = static_cast<uint64_t>(st.st_size);
  o->on_disk = static_cast<uint64_t>(st.st_blocks) * 512;
  o->modified = st.st_mtim.tv_sec;
  o->accessed = st.st_atim.tv_sec;
  o->changed = st.st_ctim.tv_sec;
  if (const passwd* pw = getpwuid(st.st_uid)) o->owner = pw->pw_name;
  else o->owner = std::to_string(st.st_uid);
  if (const group* gr = getgrgid(st.st_gid)) o->group = gr->gr_name;
  else o->group = std::to_string(st.st_gid);
  o->type = type_description(o->name, o->is_dir);
  return true;
}

namespace {
std::string link_name(const std::string& dir, const std::string& target) {
  const std::string base = fs::path(target).filename().string();
  const size_t dot = base.rfind('.');
  const bool ext = dot != std::string::npos && dot != 0;
  const std::string stem = ext ? base.substr(0, dot) : base, e = ext ? base.substr(dot) : "";
  std::error_code ec;
  std::string cand = stem + " - Link" + e;
  for (int i = 2; fs::exists(fs::symlink_status(fs::path(dir) / cand, ec)); ++i) cand = stem + " - Link (" + std::to_string(i) + ")" + e;
  return cand;
}
}  // namespace

bool make_symlink_to(const std::string& target, const std::string& dir, std::string* made, std::string* err) {
  const std::string path = dir + "/" + link_name(dir, target);
  if (::symlink(fs::absolute(target).lexically_normal().c_str(), path.c_str()) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  if (made) *made = path;
  return true;
}

bool make_hardlink_to(const std::string& target, const std::string& dir, std::string* made, std::string* err) {
  const std::string path = dir + "/" + link_name(dir, target);
  if (::link(target.c_str(), path.c_str()) != 0) {
    if (err) *err = errno == EPERM ? "A hard link cannot point to a folder" : (errno == EXDEV ? "A hard link must stay on the same drive" : std::strerror(errno));
    return false;
  }
  if (made) *made = path;
  return true;
}

bool spawn_detached(const std::vector<std::string>& argv) {
  if (argv.empty()) return false;
  std::vector<char*> args;
  for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  int err_pipe[2];  // the grandchild reports a failed exec here; a successful exec closes it (O_CLOEXEC)
  if (::pipe2(err_pipe, O_CLOEXEC) != 0) return false;
  const pid_t first = ::fork();
  if (first < 0) {
    ::close(err_pipe[0]);
    ::close(err_pipe[1]);
    return false;
  }
  if (first == 0) {
    reset_signals_for_exec();
    ::setsid();
    const pid_t second = ::fork();
    if (second != 0) _exit(second < 0 ? 1 : 0);  // the middle process leaves; the grandchild is adopted by init
    ::execvp(args[0], args.data());
    const int e = errno;
    const ssize_t w = ::write(err_pipe[1], &e, sizeof e);
    (void)w;
    _exit(127);
  }
  ::close(err_pipe[1]);
  int st = 0;
  while (::waitpid(first, &st, 0) < 0 && errno == EINTR) {}
  int child_errno = 0;
  const ssize_t n = ::read(err_pipe[0], &child_errno, sizeof child_errno);
  ::close(err_pipe[0]);
  return WIFEXITED(st) && WEXITSTATUS(st) == 0 && n <= 0;
}

bool open_default(const std::string& target) { return spawn_detached({"xdg-open", target}); }

std::vector<std::string> parse_path_list(const std::string& text) {
  std::vector<std::string> out;
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line.compare(0, 7, "file://") == 0) out.push_back(percent_decode(line.substr(7)));
    else if (line[0] == '/') out.push_back(line);
  }
  return out;
}

std::string make_uri_list(const std::vector<std::string>& paths) {
  std::string o;
  for (const std::string& p : paths) o += "file://" + percent_encode(p) + "\r\n";
  return o;
}

}  // namespace fleetwm::fm

#include <ctime>
#include <fstream>

namespace fleetwm::fm {

std::vector<RecentFile> read_recent(const std::string& xbel_path, size_t limit) {
  std::vector<RecentFile> out;
  std::ifstream f(xbel_path);
  if (!f) return out;
  std::string line;
  auto attr = [](const std::string& s, const std::string& name) {
    const std::string key = name + "=\"";
    const size_t p = s.find(key);
    if (p == std::string::npos) return std::string();
    const size_t e = s.find('"', p + key.size());
    return e == std::string::npos ? std::string() : s.substr(p + key.size(), e - p - key.size());
  };
  while (std::getline(f, line)) {
    const size_t b = line.find("<bookmark ");
    if (b == std::string::npos) continue;
    const std::string href = attr(line, "href");
    if (href.compare(0, 7, "file://") != 0) continue;
    RecentFile r;
    r.path = percent_decode(href.substr(7));
    std::string when = attr(line, "visited");
    if (when.empty()) when = attr(line, "modified");
    std::tm tm{};
    if (when.size() >= 19 && strptime(when.c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) r.modified = timegm(&tm);
    struct stat st;
    if (::stat(r.path.c_str(), &st) != 0) continue;
    out.push_back(std::move(r));
  }
  std::sort(out.begin(), out.end(), [](const RecentFile& a, const RecentFile& b) { return a.modified > b.modified; });
  if (out.size() > limit) out.resize(limit);
  return out;
}

}  // namespace fleetwm::fm

namespace fleetwm::fm {

std::string mime_type_for(std::string_view name, bool is_dir) {
  if (is_dir) return "inode/directory";
  static const struct {
    const char* ext;
    const char* mime;
  } table[] = {{"txt", "text/plain"}, {"log", "text/plain"}, {"md", "text/markdown"}, {"csv", "text/csv"}, {"html", "text/html"}, {"htm", "text/html"},
               {"css", "text/css"}, {"xml", "application/xml"}, {"json", "application/json"}, {"js", "text/javascript"}, {"c", "text/x-csrc"},
               {"h", "text/x-chdr"}, {"cpp", "text/x-c++src"}, {"hpp", "text/x-c++hdr"}, {"py", "text/x-python"}, {"sh", "application/x-shellscript"},
               {"pdf", "application/pdf"}, {"doc", "application/msword"}, {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
               {"xls", "application/vnd.ms-excel"}, {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
               {"odt", "application/vnd.oasis.opendocument.text"}, {"ods", "application/vnd.oasis.opendocument.spreadsheet"},
               {"ppt", "application/vnd.ms-powerpoint"}, {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
               {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"png", "image/png"}, {"gif", "image/gif"}, {"bmp", "image/bmp"}, {"webp", "image/webp"},
               {"svg", "image/svg+xml"}, {"tif", "image/tiff"}, {"tiff", "image/tiff"}, {"ico", "image/vnd.microsoft.icon"},
               {"mp3", "audio/mpeg"}, {"flac", "audio/flac"}, {"wav", "audio/x-wav"}, {"ogg", "audio/ogg"}, {"opus", "audio/ogg"}, {"m4a", "audio/mp4"},
               {"mp4", "video/mp4"}, {"mkv", "video/x-matroska"}, {"avi", "video/x-msvideo"}, {"mov", "video/quicktime"}, {"webm", "video/webm"},
               {"zip", "application/zip"}, {"tar", "application/x-tar"}, {"gz", "application/gzip"}, {"xz", "application/x-xz"}, {"bz2", "application/x-bzip2"},
               {"7z", "application/x-7z-compressed"}, {"rar", "application/vnd.rar"}, {"iso", "application/x-iso9660-image"}, {"deb", "application/vnd.debian.binary-package"},
               {"desktop", "application/x-desktop"}, {"appimage", "application/vnd.appimage"}};
  const std::string ext = extension_of(name);
  for (const auto& t : table)
    if (ext == t.ext) return t.mime;
  return "application/octet-stream";
}

bool have_program(const std::string& program) {
  if (program.find('/') != std::string::npos) return ::access(program.c_str(), X_OK) == 0;
  const char* path = std::getenv("PATH");
  std::stringstream ss(path ? path : "/usr/bin:/bin");
  std::string dir;
  while (std::getline(ss, dir, ':'))
    if (!dir.empty() && ::access((dir + "/" + program).c_str(), X_OK) == 0) return true;
  return false;
}

}  // namespace fleetwm::fm
