#include "parsers.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace fleetwm::netfs {

namespace {

int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

uint64_t to_u64(std::string_view s) {
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') break;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  return v;
}

// "20240102030405" (UTC) -> epoch
int64_t parse_mlsd_time(std::string_view s) {
  if (s.size() < 14) return 0;
  std::tm tm{};
  tm.tm_year = static_cast<int>(to_u64(s.substr(0, 4))) - 1900;
  tm.tm_mon = static_cast<int>(to_u64(s.substr(4, 2))) - 1;
  tm.tm_mday = static_cast<int>(to_u64(s.substr(6, 2)));
  tm.tm_hour = static_cast<int>(to_u64(s.substr(8, 2)));
  tm.tm_min = static_cast<int>(to_u64(s.substr(10, 2)));
  tm.tm_sec = static_cast<int>(to_u64(s.substr(12, 2)));
  return static_cast<int64_t>(timegm(&tm));
}

int month_index(std::string_view m) {
  static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; ++i)
    if (m.size() >= 3 && strncasecmp(m.data(), names[i], 3) == 0) return i;
  return -1;
}

// Case-insensitive search for "<prefix:name" or "<name" opening tags; returns the element's text between > and </.
// `pos` moves past the element. Names compare without the namespace prefix.
bool next_element(std::string_view xml, std::string_view local, size_t* pos, std::string_view* text, size_t* tag_begin = nullptr) {
  size_t i = *pos;
  while ((i = xml.find('<', i)) != std::string_view::npos) {
    size_t j = i + 1;
    if (j < xml.size() && (xml[j] == '/' || xml[j] == '?' || xml[j] == '!')) {
      ++i;
      continue;
    }
    size_t e = j;
    while (e < xml.size() && xml[e] != '>' && xml[e] != ' ' && xml[e] != '/' && xml[e] != '\t' && xml[e] != '\n' && xml[e] != '\r') ++e;
    std::string_view qname = xml.substr(j, e - j);
    const size_t colon = qname.rfind(':');
    std::string_view ln = colon == std::string_view::npos ? qname : qname.substr(colon + 1);
    if (ln.size() == local.size() && strncasecmp(ln.data(), local.data(), local.size()) == 0) {
      const size_t gt = xml.find('>', e);
      if (gt == std::string_view::npos) return false;
      if (tag_begin) *tag_begin = i;
      if (xml[gt - 1] == '/') {  // empty element
        *text = std::string_view();
        *pos = gt + 1;
        return true;
      }
      // matching close tag: "</" + qname + ">"
      std::string close = "</" + std::string(qname);
      size_t c = gt + 1;
      std::string_view rest = xml.substr(c);
      size_t k = rest.find(close);
      if (k == std::string_view::npos) return false;
      *text = rest.substr(0, k);
      const size_t after = rest.find('>', k);
      *pos = c + (after == std::string_view::npos ? rest.size() : after + 1);
      return true;
    }
    i = e;
  }
  return false;
}

std::string xml_unescape(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '&') {
      auto take = [&](const char* ent, char ch) {
        const size_t n = std::strlen(ent);
        if (s.compare(i, n, ent) == 0) {
          out += ch;
          i += n - 1;
          return true;
        }
        return false;
      };
      if (take("&amp;", '&') || take("&lt;", '<') || take("&gt;", '>') || take("&quot;", '"') || take("&apos;", '\'')) continue;
    }
    out += s[i];
  }
  return out;
}

// href -> path part, decoded, no trailing slash
std::string href_path(std::string_view href) {
  std::string h = xml_unescape(trim(href));
  const size_t scheme = h.find("://");
  if (scheme != std::string::npos) {
    const size_t slash = h.find('/', scheme + 3);
    h = slash == std::string::npos ? "/" : h.substr(slash);
  }
  h = url_decode(h);
  while (h.size() > 1 && h.back() == '/') h.pop_back();
  return h;
}

bool fill_response(std::string_view resp, std::string* path, NetStat* st) {
  size_t p = 0;
  std::string_view t;
  if (!next_element(resp, "href", &p, &t)) return false;
  *path = href_path(t);
  *st = NetStat{};
  p = 0;
  if (next_element(resp, "getcontentlength", &p, &t)) st->size = to_u64(trim(t));
  p = 0;
  if (next_element(resp, "getlastmodified", &p, &t)) st->mtime = parse_http_date(trim(t));
  p = 0;
  if (next_element(resp, "resourcetype", &p, &t)) {
    size_t q = 0;
    std::string_view dummy;
    st->dir = next_element(t, "collection", &q, &dummy);
  }
  return true;
}

}  // namespace

std::string url_decode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
      out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string url_encode_path(std::string_view s) {
  static const char* digits = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += digits[c >> 4];
      out += digits[c & 15];
    }
  }
  return out;
}

int64_t parse_http_date(std::string_view s) {
  s = trim(s);
  std::string z(s);
  std::tm tm{};
  if (strptime(z.c_str(), "%a, %d %b %Y %H:%M:%S", &tm)) return static_cast<int64_t>(timegm(&tm));
  std::memset(&tm, 0, sizeof tm);
  if (strptime(z.c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) return static_cast<int64_t>(timegm(&tm));
  return 0;
}

std::vector<NetEntry> parse_mlsd(std::string_view text) {
  std::vector<NetEntry> out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    std::string_view line = text.substr(pos, eol - pos);
    pos = eol + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    const size_t sp = line.find(' ');
    if (line.empty() || sp == std::string_view::npos) continue;
    std::string_view facts = line.substr(0, sp);
    NetEntry e;
    e.name = std::string(line.substr(sp + 1));
    bool skip = false;
    size_t f = 0;
    while (f < facts.size()) {
      size_t semi = facts.find(';', f);
      if (semi == std::string_view::npos) semi = facts.size();
      std::string_view fact = facts.substr(f, semi - f);
      f = semi + 1;
      const size_t eq = fact.find('=');
      if (eq == std::string_view::npos) continue;
      std::string key(fact.substr(0, eq));
      std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
      std::string_view val = fact.substr(eq + 1);
      if (key == "type") {
        std::string v(val);
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
        if (v == "cdir" || v == "pdir") skip = true;
        e.st.dir = v == "dir";
      } else if (key == "size") {
        e.st.size = to_u64(val);
      } else if (key == "modify") {
        e.st.mtime = parse_mlsd_time(val);
      } else if (key == "unix.mode") {
        e.st.mode = static_cast<uint32_t>(std::strtoul(std::string(val).c_str(), nullptr, 8));
      }
    }
    if (skip || e.name.empty() || e.name == "." || e.name == "..") continue;
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<NetEntry> parse_unix_list(std::string_view text, int64_t now) {
  std::vector<NetEntry> out;
  std::tm now_tm{};
  const time_t nt = static_cast<time_t>(now);
  gmtime_r(&nt, &now_tm);
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    std::string_view line = text.substr(pos, eol - pos);
    pos = eol + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    if (line.size() < 10 || (line[0] != 'd' && line[0] != '-' && line[0] != 'l')) continue;
    // mode links owner group size month day time-or-year name   (name may hold spaces)
    std::string_view rest = line;
    std::string_view tok[8];
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
      while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
      const size_t sp = rest.find(' ');
      if (sp == std::string_view::npos) {
        ok = false;
        break;
      }
      tok[i] = rest.substr(0, sp);
      rest.remove_prefix(sp);
    }
    if (!ok) continue;
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    if (rest.empty()) continue;
    NetEntry e;
    std::string name(rest);
    if (line[0] == 'l') {
      const size_t arrow = name.find(" -> ");
      if (arrow != std::string::npos) name.resize(arrow);
    }
    if (name == "." || name == "..") continue;
    e.name = name;
    e.st.dir = line[0] == 'd';
    e.st.size = to_u64(tok[4]);
    uint32_t mode = 0;
    for (int i = 1; i < 10 && i < static_cast<int>(tok[0].size()); ++i) mode = (mode << 1) | (tok[0][i] != '-' ? 1 : 0);
    e.st.mode = mode;
    const int mon = month_index(tok[5]);
    if (mon >= 0) {
      std::tm tm{};
      tm.tm_mon = mon;
      tm.tm_mday = static_cast<int>(to_u64(tok[6]));
      if (tok[7].find(':') != std::string_view::npos) {
        tm.tm_hour = static_cast<int>(to_u64(tok[7]));
        tm.tm_min = static_cast<int>(to_u64(tok[7].substr(tok[7].find(':') + 1)));
        tm.tm_year = now_tm.tm_year;
        if (mon > now_tm.tm_mon || (mon == now_tm.tm_mon && tm.tm_mday > now_tm.tm_mday + 1)) tm.tm_year -= 1;
      } else {
        tm.tm_year = static_cast<int>(to_u64(tok[7])) - 1900;
      }
      e.st.mtime = static_cast<int64_t>(timegm(&tm));
    }
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<NetEntry> parse_propfind(std::string_view xml, const std::string& dir_path) {
  std::string self = dir_path;
  while (self.size() > 1 && self.back() == '/') self.pop_back();
  std::vector<NetEntry> out;
  size_t pos = 0;
  std::string_view resp;
  while (next_element(xml, "response", &pos, &resp)) {
    std::string path;
    NetStat st;
    if (!fill_response(resp, &path, &st)) continue;
    if (path == self) continue;
    const size_t slash = path.rfind('/');
    NetEntry e;
    e.name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (e.name.empty()) continue;
    e.st = st;
    out.push_back(std::move(e));
  }
  return out;
}

bool parse_propfind_one(std::string_view xml, NetStat* st) {
  size_t pos = 0;
  std::string_view resp;
  if (!next_element(xml, "response", &pos, &resp)) return false;
  std::string path;
  return fill_response(resp, &path, st);
}

}  // namespace fleetwm::netfs
