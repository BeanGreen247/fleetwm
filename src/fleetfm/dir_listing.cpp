#include "dir_listing.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "natural_sort.hpp"

namespace fleetwm::fm {

namespace {
struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

Kind kind_from_mode(mode_t m) {
  if (S_ISDIR(m)) return Kind::Dir;
  if (S_ISREG(m)) return Kind::File;
  if (S_ISLNK(m)) return Kind::Symlink;
  return Kind::Other;
}
Kind kind_from_dtype(unsigned char t) {
  switch (t) {
    case DT_DIR: return Kind::Dir;
    case DT_REG: return Kind::File;
    case DT_LNK: return Kind::Symlink;
    default: return Kind::Other;
  }
}
}  // namespace

bool list_dir(const std::string& path, const ListOptions& opt, DirListing* out, std::string* err) {
  out->entries.clear();
  out->arena.clear();
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  static thread_local std::vector<char> buf(64 * 1024);
  for (;;) {
    const long n = ::syscall(SYS_getdents64, fd, buf.data(), buf.size());
    if (n < 0) {
      if (errno == EINTR) continue;
      if (err) *err = std::strerror(errno);
      ::close(fd);
      return false;
    }
    if (n == 0) break;
    for (long pos = 0; pos < n;) {
      const auto* d = reinterpret_cast<const linux_dirent64*>(buf.data() + pos);
      pos += d->d_reclen;
      const char* nm = d->d_name;
      if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
      const bool hidden = nm[0] == '.';
      if (hidden && !opt.show_hidden) continue;
      Entry e;
      e.name_off = static_cast<uint32_t>(out->arena.size());
      e.name_len = static_cast<uint32_t>(std::strlen(nm));
      e.src = static_cast<uint32_t>(out->entries.size());
      e.hidden = hidden;
      e.kind = kind_from_dtype(d->d_type);
      if (opt.stat || d->d_type == DT_UNKNOWN || d->d_type == DT_LNK) {
        struct statx sx;
        if (::statx(fd, nm, AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC, STATX_TYPE | STATX_MODE | STATX_SIZE | STATX_MTIME, &sx) != 0)
          continue;  // gone since the directory was read
        e.has_stat = true;
        e.mode = sx.stx_mode;
        e.kind = kind_from_mode(sx.stx_mode);
        e.size = sx.stx_size;
        e.mtime = sx.stx_mtime.tv_sec;
        if (e.kind == Kind::Symlink) {
          struct statx tx;
          if (::statx(fd, nm, AT_STATX_DONT_SYNC, STATX_TYPE, &tx) == 0 && S_ISDIR(tx.stx_mode)) e.link_to_dir = true;
        }
      }
      out->arena.append(nm, e.name_len);
      out->entries.push_back(e);
    }
  }
  ::close(fd);
  return true;
}

bool stat_entry(const std::string& dir, const std::string& name, Entry* e) {
  const std::string p = dir.empty() || dir.back() != '/' ? dir + "/" + name : dir + name;
  struct statx sx;
  if (::statx(AT_FDCWD, p.c_str(), AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC, STATX_TYPE | STATX_MODE | STATX_SIZE | STATX_MTIME, &sx) != 0) return false;
  e->has_stat = true;
  e->mode = sx.stx_mode;
  e->kind = kind_from_mode(sx.stx_mode);
  e->size = sx.stx_size;
  e->mtime = sx.stx_mtime.tv_sec;
  if (e->kind == Kind::Symlink) {
    struct statx tx;
    e->link_to_dir = ::statx(AT_FDCWD, p.c_str(), AT_STATX_DONT_SYNC, STATX_TYPE, &tx) == 0 && S_ISDIR(tx.stx_mode);
  }
  return true;
}

std::string extension_of(std::string_view name) {
  const size_t dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) return {};
  std::string ext(name.substr(dot + 1));
  for (char& c : ext)
    if (c >= 'A' && c <= 'Z') c += 32;
  return ext;
}

void sort_listing(DirListing* l, SortKey key, bool ascending, bool folders_first) {
  sort_entries(*l, &l->entries, key, ascending, folders_first);
}

void sort_entries(const DirListing& names, std::vector<Entry>* entries, SortKey key, bool ascending, bool folders_first) {
  const size_t n = entries->size();
  if (n < 2) return;
  // One key per entry, built once: the sort then compares bytes instead of parsing numbers in every comparison.
  std::string arena;
  arena.reserve(n * 24);
  std::vector<uint32_t> off(n + 1);
  for (size_t i = 0; i < n; ++i) {
    off[i] = static_cast<uint32_t>(arena.size());
    const Entry& e = (*entries)[i];
    if (key == SortKey::Type) {
      arena += extension_of(names.name(e));
      arena.push_back('\0');
    }
    append_natural_key(names.name(e), &arena);
  }
  off[n] = static_cast<uint32_t>(arena.size());
  std::vector<uint32_t> idx(n);
  for (size_t i = 0; i < n; ++i) idx[i] = static_cast<uint32_t>(i);
  const Entry* es = entries->data();
  auto bytes = [&](uint32_t i) { return std::string_view(arena).substr(off[i], off[i + 1] - off[i]); };
  auto cmp = [&](uint32_t a, uint32_t b) {
    const Entry &ea = es[a], &eb = es[b];
    if (folders_first) {
      const bool da = names.is_dir(ea), db = names.is_dir(eb);
      if (da != db) return da;  // folders stay on top whatever the direction
    }
    int r = 0;
    if (key == SortKey::Size) r = ea.size < eb.size ? -1 : (ea.size > eb.size ? 1 : 0);
    else if (key == SortKey::Modified) r = ea.mtime < eb.mtime ? -1 : (ea.mtime > eb.mtime ? 1 : 0);
    if (r == 0) {
      const int c = bytes(a).compare(bytes(b));
      r = c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    if (r == 0) r = natural_compare(names.name(ea), names.name(eb));  // names that differ only in case or leading zeros
    if (r == 0) return a < b;                                         // equal in every way: keep the listing's order
    return ascending ? r < 0 : r > 0;
  };
  if (key == SortKey::Name) {
    // Most comparisons are decided by the first bytes of the two keys. Sorting 24-byte records that carry the first 16 bytes keeps the sort inside
    // the cache; the entries and the key arena are only touched when two prefixes are equal. 8 bytes were not enough (names like IMG_00123 share
    // the first 7: 1.3 million fallbacks per sort of 100,000 names; 16 bytes: 18,000). perflog/2026-10-09/dirbench, findings section 20.
    struct Rec {
      uint64_t prefix, prefix2;
      uint32_t i;
      uint32_t folder;  // 1 when folders sort first and this one is a folder
    };
    std::vector<Rec> recs(n);
    for (size_t i = 0; i < n; ++i) {
      const std::string_view k = bytes(static_cast<uint32_t>(i));
      uint64_t p = 0, p2 = 0;
      for (size_t b = 0; b < 8; ++b) p = (p << 8) | (b < k.size() ? static_cast<unsigned char>(k[b]) : 0u);
      for (size_t b = 8; b < 16; ++b) p2 = (p2 << 8) | (b < k.size() ? static_cast<unsigned char>(k[b]) : 0u);
      recs[i] = {p, p2, static_cast<uint32_t>(i), folders_first && names.is_dir(es[i]) ? 1u : 0u};
    }
    std::sort(recs.begin(), recs.end(), [&](const Rec& a, const Rec& b) {
      if (a.folder != b.folder) return a.folder > b.folder;
      if (a.prefix != b.prefix) return ascending ? a.prefix < b.prefix : a.prefix > b.prefix;
      if (a.prefix2 != b.prefix2) return ascending ? a.prefix2 < b.prefix2 : a.prefix2 > b.prefix2;
      return cmp(a.i, b.i);
    });
    for (size_t i = 0; i < n; ++i) idx[i] = recs[i].i;
  } else {
    std::sort(idx.begin(), idx.end(), cmp);
  }
  std::vector<Entry> out;
  out.reserve(n);
  for (uint32_t i : idx) out.push_back(es[i]);
  entries->swap(out);
}

int find_prefix(const DirListing& l, std::string_view prefix, int from) { return find_prefix(l, l.entries, prefix, from); }

int find_prefix(const DirListing& l, const std::vector<Entry>& entries, std::string_view prefix, int from) {
  if (prefix.empty()) return -1;
  const int n = static_cast<int>(entries.size());
  for (int k = 0; k < n; ++k) {
    const int i = (from + k) % n;
    std::string_view nm = l.name(entries[i]);
    if (nm.size() < prefix.size()) continue;
    bool ok = true;
    for (size_t j = 0; j < prefix.size() && ok; ++j) {
      unsigned char a = nm[j], b = prefix[j];
      if (a >= 'A' && a <= 'Z') a += 32;
      if (b >= 'A' && b <= 'Z') b += 32;
      ok = a == b;
    }
    if (ok) return i;
  }
  return -1;
}

}  // namespace fleetwm::fm
