#include "search.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace fleetwm::fm {

namespace {
inline char ifold(char c, bool cs) { return !cs && c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

// Glob with * and ?, over the whole text.
bool glob(std::string_view p, std::string_view t, bool cs) {
  size_t pi = 0, ti = 0, star = std::string_view::npos, mark = 0;
  while (ti < t.size()) {
    if (pi < p.size() && (p[pi] == '?' || ifold(p[pi], cs) == ifold(t[ti], cs))) {
      ++pi;
      ++ti;
    } else if (pi < p.size() && p[pi] == '*') {
      star = pi++;
      mark = ti;
    } else if (star != std::string_view::npos) {
      pi = star + 1;
      ti = ++mark;
    } else {
      return false;
    }
  }
  while (pi < p.size() && p[pi] == '*') ++pi;
  return pi == p.size();
}

bool contains(std::string_view hay, std::string_view needle, bool cs) {
  if (needle.empty()) return true;
  if (needle.size() > hay.size()) return false;
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    size_t j = 0;
    while (j < needle.size() && ifold(hay[i + j], cs) == ifold(needle[j], cs)) ++j;
    if (j == needle.size()) return true;
  }
  return false;
}

bool file_contains(const std::string& path, const std::string& q, bool cs, uint64_t size) {
  if (size == 0 || size > (4u << 20) || q.empty()) return false;
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOATIME);
  int f = fd >= 0 ? fd : ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (f < 0) return false;
  std::string buf(size, '\0');
  const ssize_t n = ::read(f, buf.data(), size);
  ::close(f);
  if (n <= 0) return false;
  buf.resize(static_cast<size_t>(n));
  if (std::memchr(buf.data(), 0, std::min<size_t>(buf.size(), 4096))) return false;  // binary
  return contains(buf, q, cs);
}
}  // namespace

bool name_matches(const SearchOptions& o, std::string_view name) {
  if (o.query.empty()) return false;
  const bool wild = o.query.find_first_of("*?") != std::string::npos;
  if (wild) return glob(o.query, name, o.case_sensitive) || (o.partial && glob("*" + o.query + "*", name, o.case_sensitive));
  return o.partial ? contains(name, o.query, o.case_sensitive)
                   : name.size() == o.query.size() && contains(name, o.query, o.case_sensitive);
}

size_t search_tree(const std::string& root, const SearchOptions& o, const std::atomic<bool>* cancel, const std::function<bool(const SearchHit&)>& on_hit) {
  size_t hits = 0;
  std::vector<std::string> stack = {""};
  while (!stack.empty()) {
    if (cancel && cancel->load()) break;
    const std::string rel = std::move(stack.back());
    stack.pop_back();
    DirListing l;
    if (!list_dir(rel.empty() ? root : root + "/" + rel, {true, o.hidden}, &l)) continue;
    sort_listing(&l, SortKey::Name);
    for (const Entry& e : l.entries) {
      if (cancel && cancel->load()) return hits;
      const std::string name(l.name(e));
      const std::string path = rel.empty() ? name : rel + "/" + name;
      bool match = name_matches(o, name);
      if (!match && o.contents && e.kind == Kind::File) match = file_contains(root + "/" + path, o.query, o.case_sensitive, e.size);
      if (match) {
        SearchHit h{path, e.link_to_dir ? Kind::Dir : e.kind, e.size, e.mtime};
        ++hits;
        if (!on_hit(h) || hits >= o.max_results) return hits;
      }
      if (o.subfolders && e.kind == Kind::Dir) stack.push_back(path);
    }
  }
  return hits;
}

}  // namespace fleetwm::fm
