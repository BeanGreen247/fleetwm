#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "backend.hpp"
#include "parsers.hpp"
#include "vfs.hpp"

// fleetwm-netfs: mounts an FTP, FTPS, SFTP or WebDAV server as an ordinary folder so every file manager feature (listing, copy with
// verification, search, thumbnails) works on it unchanged, the way GVfs's FUSE folders do but without GVfs.
//   fleetwm-netfs [--user U] [--anonymous] [--known-hosts FILE] [--foreground] URI MOUNTPOINT
// The password is read from the first line of standard input (never from the command line). Exit status 2 and a one-line message on
// standard error when the login fails; 0 once the mount is up (the process then stays behind in the background).

namespace {

using namespace fleetwm::netfs;

Vfs* g_vfs = nullptr;

void fill_stat(const NetStat& n, struct stat* st) {
  std::memset(st, 0, sizeof *st);
  st->st_uid = getuid();
  st->st_gid = getgid();
  const mode_t perm = n.mode ? (n.mode & 0777) : (n.dir ? 0755 : 0644);
  st->st_mode = (n.dir ? S_IFDIR : S_IFREG) | perm;
  st->st_nlink = n.dir ? 2 : 1;
  st->st_size = static_cast<off_t>(n.size);
  st->st_mtim.tv_sec = n.mtime;
  st->st_atim.tv_sec = n.mtime;
  st->st_ctim.tv_sec = n.mtime;
  st->st_blksize = 4096;
  st->st_blocks = static_cast<blkcnt_t>((n.size + 511) / 512);
}

int op_getattr(const char* path, struct stat* st, fuse_file_info*) {
  NetStat n;
  const int r = g_vfs->getattr(path, &n);
  if (r != 0) return r;
  fill_stat(n, st);
  return 0;
}

int op_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t, fuse_file_info*, fuse_readdir_flags) {
  std::vector<NetEntry> v;
  const int r = g_vfs->readdir(path, &v);
  if (r != 0) return r;
  filler(buf, ".", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
  filler(buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
  for (const NetEntry& e : v) {
    struct stat st;
    fill_stat(e.st, &st);
    filler(buf, e.name.c_str(), &st, 0, static_cast<fuse_fill_dir_flags>(0));
  }
  return 0;
}

int op_open(const char* path, fuse_file_info* fi) {
  if ((fi->flags & O_ACCMODE) == O_RDONLY) {
    NetStat n;
    const int r = g_vfs->getattr(path, &n);
    if (r != 0) return r;
    if (n.dir) return -EISDIR;
    fi->fh = 0;
    fi->keep_cache = 0;
    return 0;
  }
  uint64_t h = 0;
  const int r = g_vfs->open_write(path, (fi->flags & O_TRUNC) != 0, false, &h);
  if (r != 0) return r;
  fi->fh = h;
  return 0;
}

int op_create(const char* path, mode_t, fuse_file_info* fi) {
  uint64_t h = 0;
  const int r = g_vfs->open_write(path, true, true, &h);
  if (r != 0) return r;
  fi->fh = h;
  return 0;
}

int op_read(const char* path, char* buf, size_t len, off_t off, fuse_file_info* fi) {
  const int64_t n = fi->fh ? g_vfs->read_handle(fi->fh, static_cast<uint64_t>(off), buf, len)
                           : g_vfs->read(path, static_cast<uint64_t>(off), buf, len);
  return static_cast<int>(n);
}

int op_write(const char*, const char* buf, size_t len, off_t off, fuse_file_info* fi) {
  if (!fi->fh) return -EBADF;
  return static_cast<int>(g_vfs->write(fi->fh, static_cast<uint64_t>(off), buf, len));
}

int op_flush(const char*, fuse_file_info* fi) { return fi->fh ? g_vfs->flush(fi->fh) : 0; }
int op_fsync(const char*, int, fuse_file_info* fi) { return fi->fh ? g_vfs->flush(fi->fh) : 0; }
int op_release(const char*, fuse_file_info* fi) {
  if (fi->fh) g_vfs->release(fi->fh);
  return 0;
}

int op_truncate(const char* path, off_t size, fuse_file_info* fi) {
  if (fi && fi->fh) return g_vfs->truncate_handle(fi->fh, static_cast<uint64_t>(size));
  return g_vfs->truncate_path(path, static_cast<uint64_t>(size));
}

int op_mkdir(const char* path, mode_t) { return g_vfs->mkdir(path); }
int op_unlink(const char* path) { return g_vfs->unlink(path); }
int op_rmdir(const char* path) { return g_vfs->rmdir(path); }

int op_rename(const char* from, const char* to, unsigned flags) {
  if (flags & RENAME_NOREPLACE) {
    NetStat n;
    if (g_vfs->getattr(to, &n) == 0) return -EEXIST;
  }
  return g_vfs->rename(from, to);
}

int op_utimens(const char*, const timespec[2], fuse_file_info*) { return 0; }
int op_chmod(const char*, mode_t, fuse_file_info*) { return 0; }
int op_chown(const char*, uid_t, gid_t, fuse_file_info*) { return 0; }

int op_statfs(const char*, struct statvfs* s) {
  std::memset(s, 0, sizeof *s);
  s->f_bsize = s->f_frsize = 4096;
  s->f_blocks = s->f_bfree = s->f_bavail = (1ull << 40) / 4096;  // the protocols cannot report free space; do not block copies
  s->f_namemax = 255;
  return 0;
}

struct Uri {
  std::string scheme, host, path;
  int port = 0;
  std::string user;
};

bool parse(const std::string& s, Uri* u) {
  const size_t sc = s.find("://");
  if (sc == std::string::npos) return false;
  u->scheme = s.substr(0, sc);
  std::string rest = s.substr(sc + 3);
  const size_t slash = rest.find('/');
  std::string auth = slash == std::string::npos ? rest : rest.substr(0, slash);
  u->path = slash == std::string::npos ? "/" : url_decode(rest.substr(slash));
  const size_t at = auth.rfind('@');
  if (at != std::string::npos) {
    u->user = url_decode(auth.substr(0, at));
    auth = auth.substr(at + 1);
  }
  if (!auth.empty() && auth.front() == '[') {
    const size_t rb = auth.find(']');
    if (rb == std::string::npos) return false;
    u->host = auth.substr(1, rb - 1);
    if (rb + 2 < auth.size() && auth[rb + 1] == ':') u->port = std::atoi(auth.c_str() + rb + 2);
  } else {
    const size_t colon = auth.rfind(':');
    if (colon != std::string::npos) {
      u->port = std::atoi(auth.c_str() + colon + 1);
      auth = auth.substr(0, colon);
    }
    u->host = auth;
  }
  return !u->host.empty();
}

}  // namespace

int main(int argc, char** argv) {
  std::string user, known_hosts, uri_text, mountpoint;
  bool anonymous = false, foreground = false, debug = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--version" || a == "-V") {
      std::puts("fleetwm-netfs " FLEETWM_VERSION);
      return 0;
    }
    if (a == "--help" || a == "-h") {
      std::puts("usage: fleetwm-netfs [--user NAME] [--anonymous] [--known-hosts FILE] [--foreground] URI MOUNTPOINT\n"
                "URI: sftp://, ftp://, ftps:// or dav(s):// followed by host[:port]/path. The password is read from the first line of standard input.");
      return 0;
    }
    if (a == "--user" && i + 1 < argc) user = argv[++i];
    else if (a == "--known-hosts" && i + 1 < argc) known_hosts = argv[++i];
    else if (a == "--anonymous") anonymous = true;
    else if (a == "--foreground") foreground = true;
    else if (a == "--debug") debug = foreground = true;
    else if (uri_text.empty()) uri_text = a;
    else if (mountpoint.empty()) mountpoint = a;
  }
  Uri u;
  if (uri_text.empty() || mountpoint.empty() || !parse(uri_text, &u)) {
    std::fputs("fleetwm-netfs: need URI and MOUNTPOINT (see --help)\n", stderr);
    return 64;
  }
  Target t;
  t.scheme = u.scheme;
  t.host = u.host;
  t.port = u.port;
  t.user = user.empty() ? u.user : user;
  t.base = u.path;
  t.anonymous = anonymous;
  t.known_hosts = known_hosts;
  if (!anonymous) {
    std::string pw;
    std::getline(std::cin, pw);
    t.password = pw;
  }
  std::unique_ptr<Backend> be = make_backend(t);
  if (!be) {
    std::fprintf(stderr, "fleetwm-netfs: protocol '%s' is not built in\n", t.scheme.c_str());
    return 3;
  }
  std::string err;
  if (!be->connect(&err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 2;
  }
  Vfs vfs(std::move(be));
  g_vfs = &vfs;

  fuse_operations ops{};
  ops.getattr = op_getattr;
  ops.readdir = op_readdir;
  ops.open = op_open;
  ops.create = op_create;
  ops.read = op_read;
  ops.write = op_write;
  ops.flush = op_flush;
  ops.fsync = op_fsync;
  ops.release = op_release;
  ops.truncate = op_truncate;
  ops.mkdir = op_mkdir;
  ops.unlink = op_unlink;
  ops.rmdir = op_rmdir;
  ops.rename = op_rename;
  ops.utimens = op_utimens;
  ops.chmod = op_chmod;
  ops.chown = op_chown;
  ops.statfs = op_statfs;

  const std::string fsname = "fsname=" + u.scheme + "://" + u.host;
  std::vector<std::string> args = {"fleetwm-netfs", mountpoint, "-s", "-o", fsname + ",subtype=fleetwm-netfs,auto_unmount"};
  if (foreground) args.push_back("-f");
  if (debug) args.push_back("-d");
  std::vector<char*> cargs;
  for (std::string& s : args) cargs.push_back(s.data());
  cargs.push_back(nullptr);
  return fuse_main(static_cast<int>(args.size()), cargs.data(), &ops, nullptr);
}
