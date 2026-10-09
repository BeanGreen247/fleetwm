#include "config_backup.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <toml++/toml.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "config_paths.hpp"
#include "version.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

namespace {

constexpr const char* kTop = "fleetwm";
constexpr const char* kManifest = "manifest.toml";
constexpr rlim_t kMaxFileBytes = 1u << 20;  // a config file is a few KB; tar is stopped from writing more than this per file

// Runs argv (no shell), returns the exit status (-1 when it did not exit normally) and its stdout+stderr.
int run_tar(const std::vector<std::string>& argv, std::string* out) {
  int fds[2];
  if (pipe(fds) != 0) return -1;
  const pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    const int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) dup2(devnull, STDIN_FILENO);
    close(fds[0]);
    close(fds[1]);
    signal(SIGPIPE, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    const rlimit lim{kMaxFileBytes, kMaxFileBytes};
    setrlimit(RLIMIT_FSIZE, &lim);
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }
  close(fds[1]);
  char buf[4096];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof buf)) > 0) out->append(buf, static_cast<size_t>(n));
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string first_line(const std::string& s) {
  const auto end = s.find('\n');
  return s.substr(0, end);
}

// A scratch folder next to the config folder (same file system, outside the watched folder), removed on scope exit.
struct Scratch {
  fs::path path;
  explicit Scratch(const fs::path& base) {
    std::error_code ec;
    fs::create_directories(base, ec);
    std::string tmpl = (base / "fleetwm-config.XXXXXX").string();
    if (mkdtemp(tmpl.data())) path = tmpl;
  }
  ~Scratch() {
    if (path.empty()) return;
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;
};

bool is_listed(const std::vector<std::string>& v, const std::string& name) { return std::find(v.begin(), v.end(), name) != v.end(); }

std::string stamp(const char* fmt, bool utc) {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  if (utc) gmtime_r(&t, &tm);
  else localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, fmt, &tm);
  return buf;
}

BackupResult fail(std::string msg) {
  BackupResult r;
  r.message = std::move(msg);
  return r;
}

// "" when the file is a valid toml document, else the parser's complaint.
std::string toml_problem(const fs::path& p) {
  try {
    (void)toml::parse_file(p.string());
  } catch (const toml::parse_error& e) {
    std::ostringstream os;
    os << e.description() << " (line " << e.source().begin.line << ")";
    return os.str();
  }
  return "";
}

}  // namespace

const std::vector<std::string>& backup_files() {
  static const std::vector<std::string> files = {
      "theme.toml",   "bar.toml",   "wallpaper.toml", "outputs.toml", "keybinds.toml",       "power.toml",        "keyboard.toml",
      "mouse.toml",   "default_apps.toml", "desktop.toml", "fleetfm.toml", "fleetfm-views.toml", "fleetfm-places.toml",
  };
  return files;
}

const std::vector<std::string>& machine_specific_files() {
  static const std::vector<std::string> files = {"outputs.toml", "fleetfm-places.toml"};
  return files;
}

std::string config_backup_dir() { return (config_internal::config_home() / kTop).string(); }

BackupResult export_config(const std::string& archive, const BackupOptions& opt) {
  try {
    const fs::path dir = config_backup_dir();
    Scratch scratch(config_internal::config_home());
    if (scratch.path.empty()) return fail("Cannot create a scratch folder in " + config_internal::config_home().string());
    const fs::path stage = scratch.path / kTop;
    fs::create_directories(stage);

    BackupResult r;
    for (const std::string& name : backup_files()) {
      const fs::path src = dir / name;
      std::error_code ec;
      if (!fs::is_regular_file(src, ec)) continue;
      if (!opt.include_machine_specific && is_listed(machine_specific_files(), name)) {
        r.skipped.push_back(name + " (machine specific)");
        continue;
      }
      if (const std::string why = toml_problem(src); !why.empty()) {
        r.skipped.push_back(name + " (not valid toml: " + why + ")");
        continue;
      }
      fs::copy_file(src, stage / name);
      r.files.push_back(name);
    }
    if (r.files.empty()) return fail("Nothing to export: " + dir.string() + " has no configuration files");

    toml::array names;
    for (const auto& f : r.files) names.push_back(f);
    toml::table manifest;
    manifest.insert_or_assign("format", kConfigBackupFormat);
    manifest.insert_or_assign("fleetwm_version", version_string());
    manifest.insert_or_assign("created", stamp("%Y-%m-%dT%H:%M:%SZ", true));
    manifest.insert_or_assign("machine_specific_included", opt.include_machine_specific);
    manifest.insert_or_assign("files", std::move(names));
    {
      std::ofstream out(stage / kManifest);
      out << manifest << "\n";
      if (!out) return fail("Cannot write the manifest");
    }

    const fs::path final_path = archive;
    const std::string tmp = archive + ".tmp";
    std::string log;
    const int rc = run_tar({"tar", "--sort=name", "--mtime=@0", "--owner=0", "--group=0", "--numeric-owner", "-czf", tmp, "-C",
                            scratch.path.string(), kTop},
                           &log);
    if (rc != 0) {
      std::error_code ec;
      fs::remove(tmp, ec);
      return fail("tar failed: " + (log.empty() ? std::string("exit ") + std::to_string(rc) : first_line(log)));
    }
    std::error_code ec;
    fs::rename(tmp, final_path, ec);
    if (ec) {
      fs::remove(tmp, ec);
      return fail("Cannot write " + archive + ": " + ec.message());
    }
    r.ok = true;
    r.message = "Exported " + std::to_string(r.files.size()) + " files to " + archive;
    if (!r.skipped.empty()) r.message += " (" + std::to_string(r.skipped.size()) + " left out)";
    return r;
  } catch (const std::exception& e) {
    return fail(std::string("Export failed: ") + e.what());
  }
}

BackupResult import_config(const std::string& archive, const BackupOptions& opt) {
  try {
    std::error_code ec;
    if (!fs::is_regular_file(archive, ec)) return fail("Cannot find " + archive);

    // 1. Names: only the top folder, the manifest and known files may be inside.
    std::string listing;
    if (run_tar({"tar", "-tzf", archive}, &listing) != 0) return fail("Not a readable tar.gz archive: " + archive);
    std::vector<std::string> members;
    std::istringstream ls(listing);
    for (std::string line; std::getline(ls, line);) {
      if (line.empty()) continue;
      if (line == std::string(kTop) + "/" || line == kTop) continue;
      const std::string prefix = std::string(kTop) + "/";
      if (line.rfind(prefix, 0) != 0) return fail("Unexpected entry in the archive: " + line);
      const std::string name = line.substr(prefix.size());
      if (name != kManifest && !is_listed(backup_files(), name)) return fail("Unexpected entry in the archive: " + line);
      members.push_back(name);
    }
    if (std::find(members.begin(), members.end(), kManifest) == members.end()) return fail("Not a fleetwm configuration archive (no manifest.toml)");

    // 2. Unpack into a scratch folder and check everything there.
    Scratch scratch(config_internal::config_home());
    if (scratch.path.empty()) return fail("Cannot create a scratch folder in " + config_internal::config_home().string());
    std::string log;
    if (run_tar({"tar", "-xzf", archive, "-C", scratch.path.string(), "--no-same-owner", "--no-same-permissions"}, &log) != 0)
      return fail("Cannot unpack the archive: " + (log.empty() ? std::string("tar failed") : first_line(log)));
    const fs::path stage = scratch.path / kTop;
    for (const std::string& name : members) {
      const fs::path p = stage / name;
      if (!fs::is_regular_file(fs::symlink_status(p, ec)) || fs::file_size(p, ec) > kMaxFileBytes) return fail(name + " in the archive is not an ordinary small file");
    }

    toml::table manifest;
    try {
      manifest = toml::parse_file((stage / kManifest).string());
    } catch (const toml::parse_error& e) {
      return fail(std::string("manifest.toml is damaged: ") + std::string(e.description()));
    }
    const auto format = manifest["format"].value<int64_t>();
    if (!format || *format < 1) return fail("manifest.toml has no valid format number");
    if (*format > kConfigBackupFormat)
      return fail("This archive was made by a newer fleetwm (format " + std::to_string(*format) + ", this build reads up to " +
                  std::to_string(kConfigBackupFormat) + "). Update fleetwm and try again.");
    // Older formats are upgraded here when there is one; format 1 is the first.

    BackupResult r;
    std::vector<std::string> todo;
    for (const std::string& name : members) {
      if (name == kManifest) continue;
      if (!opt.include_machine_specific && is_listed(machine_specific_files(), name)) {
        r.skipped.push_back(name + " (machine specific)");
        continue;
      }
      if (const std::string why = toml_problem(stage / name); !why.empty()) return fail(name + " is not valid toml: " + why + ". Nothing was changed.");
      todo.push_back(name);
    }
    if (todo.empty()) return fail("The archive holds no configuration files to import");

    // 3. Keep the current folder, then replace file by file (rename, so watchers see a finished file).
    const fs::path dir = config_backup_dir();
    if (fs::is_directory(dir, ec)) {
      std::string bak = dir.string() + ".bak-" + stamp("%Y%m%d-%H%M%S", false);
      for (int n = 2; fs::exists(bak, ec); ++n) bak = dir.string() + ".bak-" + stamp("%Y%m%d-%H%M%S", false) + "-" + std::to_string(n);
      fs::copy(dir, bak, fs::copy_options::recursive | fs::copy_options::copy_symlinks);
      r.backup_dir = bak;
    }
    try {
      fs::create_directories(dir);
      for (const std::string& name : todo) {
        const fs::path tmp = dir / (name + ".import-tmp");
        fs::copy_file(stage / name, tmp, fs::copy_options::overwrite_existing);
        fs::rename(tmp, dir / name);
        r.files.push_back(name);
      }
    } catch (const std::exception& e) {
      return fail(std::string("Import stopped: ") + e.what() + (r.backup_dir.empty() ? "" : ". The previous folder is in " + r.backup_dir));
    }

    r.ok = true;
    r.message = "Imported " + std::to_string(r.files.size()) + " files";
    if (const auto v = manifest["fleetwm_version"].value<std::string>(); v && *v != version_string()) r.message += " (made by fleetwm " + *v + ")";
    if (!r.backup_dir.empty()) r.message += ". Previous settings kept in " + r.backup_dir;
    return r;
  } catch (const std::exception& e) {
    return fail(std::string("Import failed: ") + e.what());
  }
}

}  // namespace fleetwm
