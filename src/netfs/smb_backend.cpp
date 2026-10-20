#include <libsmbclient.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

#include "backend.hpp"
#include "parsers.hpp"

// SMB / CIFS shares through libsmbclient (Samba's client library). The URI is smb://server/share[/folder]; the mount root is that folder.

namespace fleetwm::netfs {

namespace {

const Target* g_target = nullptr;

void auth_cb(const char*, const char*, char* wg, int wglen, char* un, int unlen, char* pw, int pwlen) {
  if (!g_target) return;
  const std::string user = g_target->anonymous ? "" : g_target->user;
  std::snprintf(un, static_cast<size_t>(unlen), "%s", user.c_str());
  std::snprintf(pw, static_cast<size_t>(pwlen), "%s", g_target->anonymous ? "" : g_target->password.c_str());
  if (!g_target->domain.empty()) std::snprintf(wg, static_cast<size_t>(wglen), "%s", g_target->domain.c_str());
}

std::string errno_text(int e) {
  switch (e) {
    case EACCES:
    case EPERM: return "Wrong user name or password, or no permission";
    case ENOENT: return "Not found";
    case EEXIST: return "Already exists";
    case ENOTEMPTY: return "The folder is not empty";
    case ETIMEDOUT:
    case EHOSTUNREACH: return "The server did not answer in time";
    case ECONNREFUSED: return "The server refused the connection";
    case ENODEV:
    case ENXIO: return "The share does not exist";
    default: return std::strerror(e);
  }
}

class SmbBackend : public Backend {
 public:
  explicit SmbBackend(const Target& t) : t_(t) {
    base_ = t.base;
    while (base_.size() > 1 && base_.back() == '/') base_.pop_back();
    if (base_ == "/") base_.clear();
  }
  ~SmbBackend() override { close_read(); }

  bool connect(std::string* err) override {
    g_target = &t_;
    if (smbc_init(auth_cb, 0) != 0) {
      *err = errno_text(errno);
      return false;
    }
    const int d = smbc_opendir(url("/").c_str());
    if (d < 0) {
      *err = errno_text(errno);
      return false;
    }
    smbc_closedir(d);
    return true;
  }

  bool list(const std::string& path, std::vector<NetEntry>* out, std::string* err) override {
    out->clear();
    const int d = smbc_opendir(url(path).c_str());
    if (d < 0) {
      *err = errno_text(errno);
      return false;
    }
    while (const smbc_dirent* e = smbc_readdir(static_cast<unsigned>(d))) {
      const std::string name(e->name, e->namelen > 0 ? std::strlen(e->name) : 0);
      if (name.empty() || name == "." || name == "..") continue;
      if (e->smbc_type != SMBC_DIR && e->smbc_type != SMBC_FILE && e->smbc_type != SMBC_LINK && e->smbc_type != SMBC_FILE_SHARE) continue;
      NetEntry n;
      n.name = name;
      n.st.dir = e->smbc_type == SMBC_DIR || e->smbc_type == SMBC_FILE_SHARE;
      struct stat st {};
      if (smbc_stat(url(path == "/" ? "/" + name : path + "/" + name).c_str(), &st) == 0) fill(st, &n.st);
      out->push_back(std::move(n));
    }
    smbc_closedir(d);
    return true;
  }

  bool stat(const std::string& path, NetStat* out, bool* missing, std::string* err) override {
    *missing = false;
    struct stat st {};
    if (smbc_stat(url(path).c_str(), &st) != 0) {
      *missing = errno == ENOENT;
      *err = errno_text(errno);
      return false;
    }
    *out = NetStat{};
    fill(st, out);
    if (path == "/") out->dir = true;
    return true;
  }

  int64_t read(const std::string& path, uint64_t off, void* buf, size_t len, std::string* err) override {
    if (read_path_ != path) {
      close_read();
      read_fd_ = smbc_open(url(path).c_str(), O_RDONLY, 0);
      if (read_fd_ < 0) {
        *err = errno_text(errno);
        return -1;
      }
      read_path_ = path;
    }
    if (smbc_lseek(static_cast<unsigned>(read_fd_), static_cast<off_t>(off), SEEK_SET) < 0) {
      *err = errno_text(errno);
      close_read();
      return -1;
    }
    size_t got = 0;
    char* p = static_cast<char*>(buf);
    while (got < len) {
      const ssize_t n = smbc_read(static_cast<unsigned>(read_fd_), p + got, len - got);
      if (n < 0) {
        *err = errno_text(errno);
        close_read();
        return -1;
      }
      if (n == 0) break;
      got += static_cast<size_t>(n);
    }
    return static_cast<int64_t>(got);
  }

  bool put(const std::string& path, const std::string& local, std::string* err) override {
    close_read();
    FILE* in = std::fopen(local.c_str(), "rb");
    if (!in) {
      *err = "Cannot read the buffered file";
      return false;
    }
    const int fd = smbc_open(url(path).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
      *err = errno_text(errno);
      std::fclose(in);
      return false;
    }
    std::string chunk(256 * 1024, '\0');
    bool ok = true;
    size_t n;
    while (ok && (n = std::fread(chunk.data(), 1, chunk.size(), in)) > 0) {
      size_t done = 0;
      while (done < n) {
        const ssize_t w = smbc_write(static_cast<unsigned>(fd), chunk.data() + done, n - done);
        if (w < 0) {
          *err = errno_text(errno);
          ok = false;
          break;
        }
        done += static_cast<size_t>(w);
      }
    }
    smbc_close(static_cast<unsigned>(fd));
    std::fclose(in);
    return ok;
  }

  bool mkdir(const std::string& path, std::string* err) override { return check(smbc_mkdir(url(path).c_str(), 0755), err); }
  bool remove_file(const std::string& path, std::string* err) override {
    if (read_path_ == path) close_read();
    return check(smbc_unlink(url(path).c_str()), err);
  }
  bool remove_dir(const std::string& path, std::string* err) override { return check(smbc_rmdir(url(path).c_str()), err); }
  bool rename(const std::string& from, const std::string& to, std::string* err) override {
    close_read();
    if (smbc_rename(url(from).c_str(), url(to).c_str()) == 0) return true;
    if (errno == EEXIST || errno == EACCES) {  // SMB will not replace: remove the old file first
      struct stat st {};
      if (smbc_stat(url(to).c_str(), &st) == 0 && !S_ISDIR(st.st_mode) && smbc_unlink(url(to).c_str()) == 0 &&
          smbc_rename(url(from).c_str(), url(to).c_str()) == 0)
        return true;
    }
    *err = errno_text(errno);
    return false;
  }

 private:
  std::string url(const std::string& path) const {
    std::string p = base_ + (path == "/" ? "" : path);
    return "smb://" + t_.host + url_encode_path(p.empty() ? "/" : p);
  }

  static void fill(const struct stat& st, NetStat* n) {
    n->dir = S_ISDIR(st.st_mode);
    n->size = static_cast<uint64_t>(st.st_size);
    n->mtime = st.st_mtime;
  }

  bool check(int rc, std::string* err) {
    if (rc == 0) return true;
    *err = errno_text(errno);
    return false;
  }

  void close_read() {
    if (read_fd_ >= 0) smbc_close(static_cast<unsigned>(read_fd_));
    read_fd_ = -1;
    read_path_.clear();
  }

  Target t_;
  std::string base_;
  int read_fd_ = -1;
  std::string read_path_;
};

}  // namespace

std::unique_ptr<Backend> make_smb_backend(const Target& t) { return std::make_unique<SmbBackend>(t); }

}  // namespace fleetwm::netfs
