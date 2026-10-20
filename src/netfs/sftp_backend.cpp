#include <arpa/inet.h>
#include <fcntl.h>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

#include "backend.hpp"

// SFTP through libssh2 over one TCP connection. The host key is trusted on first use and remembered in a small file; a changed
// key refuses the connection (the same rule as ssh's known_hosts, without its hashing).

namespace fleetwm::netfs {

namespace {

class SftpBackend : public Backend {
 public:
  explicit SftpBackend(const Target& t) : t_(t) {
    base_ = t.base;
    while (base_.size() > 1 && base_.back() == '/') base_.pop_back();
    if (base_ == "/") base_.clear();
  }
  ~SftpBackend() override { close_all(); }

  bool connect(std::string* err) override {
    static const int init = libssh2_init(0);
    (void)init;
    const int port = t_.port ? t_.port : 22;
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(t_.host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
      *err = "The server name could not be found";
      return false;
    }
    for (addrinfo* a = res; a; a = a->ai_next) {
      sock_ = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (sock_ < 0) continue;
      timeval tv{15, 0};
      setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
      setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
      if (::connect(sock_, a->ai_addr, a->ai_addrlen) == 0) break;
      ::close(sock_);
      sock_ = -1;
    }
    freeaddrinfo(res);
    if (sock_ < 0) {
      *err = "The server refused the connection";
      return false;
    }
    session_ = libssh2_session_init();
    libssh2_session_set_blocking(session_, 1);
    libssh2_session_set_timeout(session_, 60000);
    if (libssh2_session_handshake(session_, sock_) != 0) {
      *err = "The SSH handshake failed: " + last_error();
      return false;
    }
    if (!check_host_key(port, err)) return false;
    const std::string user = t_.user.empty() ? current_user() : t_.user;
    char* methods = libssh2_userauth_list(session_, user.c_str(), static_cast<unsigned>(user.size()));
    const std::string allowed = methods ? methods : "";
    bool authed = libssh2_userauth_authenticated(session_) != 0;  // "none" accepted
    if (!authed && !t_.password.empty() && allowed.find("password") != std::string::npos)
      authed = libssh2_userauth_password(session_, user.c_str(), t_.password.c_str()) == 0;
    if (!authed && !t_.password.empty() && allowed.find("keyboard-interactive") != std::string::npos) {
      static thread_local const char* pw = nullptr;
      pw = t_.password.c_str();
      authed = libssh2_userauth_keyboard_interactive(session_, user.c_str(),
                   [](const char*, int, const char*, int, int n, const LIBSSH2_USERAUTH_KBDINT_PROMPT*,
                      LIBSSH2_USERAUTH_KBDINT_RESPONSE* resp, void**) {
                     for (int i = 0; i < n; ++i) {
                       resp[i].text = strdup(pw);
                       resp[i].length = static_cast<unsigned>(std::strlen(pw));
                     }
                   }) == 0;
    }
    if (!authed && allowed.find("publickey") != std::string::npos) {
      const char* home = std::getenv("HOME");
      for (const char* key : {"id_ed25519", "id_ecdsa", "id_rsa"}) {
        if (!home) break;
        const std::string priv = std::string(home) + "/.ssh/" + key;
        if (access(priv.c_str(), R_OK) != 0) continue;
        if (libssh2_userauth_publickey_fromfile(session_, user.c_str(), nullptr, priv.c_str(),
                                                t_.password.empty() ? nullptr : t_.password.c_str()) == 0) {
          authed = true;
          break;
        }
      }
    }
    if (!authed) {
      *err = "Wrong user name or password";
      return false;
    }
    sftp_ = libssh2_sftp_init(session_);
    if (!sftp_) {
      *err = "The server has no SFTP service: " + last_error();
      return false;
    }
    return true;
  }

  bool list(const std::string& path, std::vector<NetEntry>* out, std::string* err) override {
    out->clear();
    const std::string p = abs(path);
    LIBSSH2_SFTP_HANDLE* d = libssh2_sftp_opendir(sftp_, p.c_str());
    if (!d) {
      *err = sftp_error();
      return false;
    }
    char name[1024];
    LIBSSH2_SFTP_ATTRIBUTES a;
    for (;;) {
      const int n = libssh2_sftp_readdir(d, name, sizeof name, &a);
      if (n < 0) {
        *err = sftp_error();
        libssh2_sftp_closedir(d);
        return false;
      }
      if (n == 0) break;
      const std::string nm(name, static_cast<size_t>(n));
      if (nm == "." || nm == "..") continue;
      NetEntry e;
      e.name = nm;
      fill(a, &e.st);
      if (LIBSSH2_SFTP_S_ISLNK(a.permissions)) {  // follow the link for its type and size
        LIBSSH2_SFTP_ATTRIBUTES t;
        if (libssh2_sftp_stat(sftp_, (p + (p == "/" ? "" : "/") + nm).c_str(), &t) == 0) fill(t, &e.st);
      }
      out->push_back(std::move(e));
    }
    libssh2_sftp_closedir(d);
    return true;
  }

  bool stat(const std::string& path, NetStat* st, bool* missing, std::string* err) override {
    *missing = false;
    LIBSSH2_SFTP_ATTRIBUTES a;
    if (libssh2_sftp_stat(sftp_, abs(path).c_str(), &a) != 0) {
      *missing = libssh2_session_last_errno(session_) == LIBSSH2_ERROR_SFTP_PROTOCOL &&
                 libssh2_sftp_last_error(sftp_) == LIBSSH2_FX_NO_SUCH_FILE;
      *err = sftp_error();
      return false;
    }
    *st = NetStat{};
    fill(a, st);
    if (path == "/") st->dir = true;
    return true;
  }

  int64_t read(const std::string& path, uint64_t off, void* buf, size_t len, std::string* err) override {
    if (read_path_ != path) {
      close_read();
      read_ = libssh2_sftp_open(sftp_, abs(path).c_str(), LIBSSH2_FXF_READ, 0);
      if (!read_) {
        *err = sftp_error();
        return -1;
      }
      read_path_ = path;
    }
    libssh2_sftp_seek64(read_, off);
    size_t got = 0;
    char* p = static_cast<char*>(buf);
    while (got < len) {
      const ssize_t n = libssh2_sftp_read(read_, p + got, len - got);
      if (n < 0) {
        *err = sftp_error();
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
    std::ifstream in(local, std::ios::binary);
    if (!in) {
      *err = "Cannot read the buffered file";
      return false;
    }
    LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open(sftp_, abs(path).c_str(), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                                               LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!h) {
      *err = sftp_error();
      return false;
    }
    std::string chunk(256 * 1024, '\0');
    bool ok = true;
    while (in) {
      in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
      size_t n = static_cast<size_t>(in.gcount()), done = 0;
      while (done < n) {
        const ssize_t w = libssh2_sftp_write(h, chunk.data() + done, n - done);
        if (w < 0) {
          *err = sftp_error();
          ok = false;
          break;
        }
        done += static_cast<size_t>(w);
      }
      if (!ok) break;
    }
    libssh2_sftp_close(h);
    return ok;
  }

  bool mkdir(const std::string& path, std::string* err) override {
    return check(libssh2_sftp_mkdir(sftp_, abs(path).c_str(), 0755), err);
  }
  bool remove_file(const std::string& path, std::string* err) override {
    if (read_path_ == path) close_read();
    return check(libssh2_sftp_unlink(sftp_, abs(path).c_str()), err);
  }
  bool remove_dir(const std::string& path, std::string* err) override {
    return check(libssh2_sftp_rmdir(sftp_, abs(path).c_str()), err);
  }
  bool rename(const std::string& from, const std::string& to, std::string* err) override {
    close_read();
    return check(libssh2_sftp_rename_ex(sftp_, abs(from).c_str(), static_cast<unsigned>(abs(from).size()), abs(to).c_str(),
                                         static_cast<unsigned>(abs(to).size()),
                                         LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE),
                 err);
  }

 private:
  std::string abs(const std::string& path) const {
    std::string p = base_ + (path == "/" ? "" : path);
    return p.empty() ? "/" : p;
  }

  static void fill(const LIBSSH2_SFTP_ATTRIBUTES& a, NetStat* st) {
    if (a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
      st->dir = LIBSSH2_SFTP_S_ISDIR(a.permissions);
      st->mode = a.permissions & 0777;
    }
    if (a.flags & LIBSSH2_SFTP_ATTR_SIZE) st->size = a.filesize;
    if (a.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) st->mtime = static_cast<int64_t>(a.mtime);
  }

  static std::string current_user() {
    const char* u = std::getenv("USER");
    return u ? u : "root";
  }

  std::string last_error() const {
    char* m = nullptr;
    int n = 0;
    libssh2_session_last_error(session_, &m, &n, 0);
    return m ? m : "unknown error";
  }

  std::string sftp_error() const {
    if (libssh2_session_last_errno(session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
      switch (libssh2_sftp_last_error(sftp_)) {
        case LIBSSH2_FX_NO_SUCH_FILE: return "Not found";
        case LIBSSH2_FX_PERMISSION_DENIED: return "Permission denied";
        case LIBSSH2_FX_FILE_ALREADY_EXISTS: return "Already exists";
        case LIBSSH2_FX_DIR_NOT_EMPTY: return "The folder is not empty";
        case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM: return "The server is out of space";
        default: break;
      }
    }
    return last_error();
  }

  bool check(int rc, std::string* err) {
    if (rc == 0) return true;
    *err = sftp_error();
    return false;
  }

  bool check_host_key(int port, std::string* err) {
    const char* raw = libssh2_hostkey_hash(session_, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!raw) {
      *err = "The server sent no host key";
      return false;
    }
    static const char* digits = "0123456789abcdef";
    std::string fp;
    for (int i = 0; i < 32; ++i) {
      const unsigned char c = static_cast<unsigned char>(raw[i]);
      fp += digits[c >> 4];
      fp += digits[c & 15];
    }
    const std::string id = t_.host + ":" + std::to_string(port);
    if (t_.known_hosts.empty()) return true;
    std::string line, seen;
    {
      std::ifstream in(t_.known_hosts);
      while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string h, f;
        ss >> h >> f;
        if (h == id) seen = f;
      }
    }
    if (seen.empty()) {
      std::ofstream out(t_.known_hosts, std::ios::app);
      out << id << " " << fp << "\n";
      return true;
    }
    if (seen != fp) {
      *err = "The host key of " + id + " has changed since the last connection; remove it from " + t_.known_hosts + " if that is expected";
      return false;
    }
    return true;
  }

  void close_read() {
    if (read_) libssh2_sftp_close(read_);
    read_ = nullptr;
    read_path_.clear();
  }

  void close_all() {
    close_read();
    if (sftp_) libssh2_sftp_shutdown(sftp_);
    if (session_) {
      libssh2_session_disconnect(session_, "bye");
      libssh2_session_free(session_);
    }
    if (sock_ >= 0) ::close(sock_);
    sftp_ = nullptr;
    session_ = nullptr;
    sock_ = -1;
  }

  Target t_;
  std::string base_;
  int sock_ = -1;
  LIBSSH2_SESSION* session_ = nullptr;
  LIBSSH2_SFTP* sftp_ = nullptr;
  LIBSSH2_SFTP_HANDLE* read_ = nullptr;
  std::string read_path_;
};

}  // namespace

std::unique_ptr<Backend> make_sftp_backend(const Target& t) { return std::make_unique<SftpBackend>(t); }

}  // namespace fleetwm::netfs
