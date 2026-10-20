#include "fleettoml.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <ostream>
#include <set>
#include <sstream>

namespace toml {

// ---------------------------------------------------------------------------------------------------------------------- node ---

node::node(table v) : type_(node_type::table), t_(std::make_unique<table>(std::move(v))) {}
node::node(array v) : type_(node_type::array), a_(std::make_unique<array>(std::move(v))) {}
node::node(const node& o)
    : type_(o.type_), b_(o.b_), i_(o.i_), d_(o.d_), s_(o.s_), t_(o.t_ ? std::make_unique<table>(*o.t_) : nullptr),
      a_(o.a_ ? std::make_unique<array>(*o.a_) : nullptr) {}
node::node(node&& o) noexcept = default;
node& node::operator=(const node& o) {
  if (this != &o) *this = node(o);
  return *this;
}
node& node::operator=(node&& o) noexcept = default;
node::~node() = default;

template <class T> std::optional<T> node::value() const {
  if constexpr (std::is_same_v<T, bool>) {
    if (type_ == node_type::boolean) return b_;
    if (type_ == node_type::integer) return i_ != 0;  // as toml++ does: an integer in a boolean field is true unless it is 0
    return std::nullopt;
  } else if constexpr (std::is_integral_v<T>) {
    if (type_ == node_type::integer) {
      if (i_ < static_cast<int64_t>(std::numeric_limits<T>::min()) && std::is_signed_v<T>) return std::nullopt;
      if (i_ < 0 && !std::is_signed_v<T>) return std::nullopt;
      if (std::is_unsigned_v<T> || sizeof(T) < sizeof(int64_t)) {
        if (static_cast<uint64_t>(i_) > static_cast<uint64_t>(std::numeric_limits<T>::max()) && i_ >= 0) return std::nullopt;
        if (std::is_signed_v<T> && i_ > static_cast<int64_t>(std::numeric_limits<T>::max())) return std::nullopt;
      }
      return static_cast<T>(i_);
    }
    if (type_ == node_type::boolean) return static_cast<T>(b_ ? 1 : 0);
    if (type_ == node_type::floating_point && std::isfinite(d_) && d_ == std::floor(d_) && d_ >= -9.2e18 && d_ <= 9.2e18) {
      const int64_t v = static_cast<int64_t>(d_);
      if (v >= static_cast<int64_t>(std::numeric_limits<T>::min()) && (v < 0 || static_cast<uint64_t>(v) <= static_cast<uint64_t>(std::numeric_limits<T>::max())))
        return static_cast<T>(v);
    }
    return std::nullopt;
  } else if constexpr (std::is_floating_point_v<T>) {
    if (type_ == node_type::floating_point) return static_cast<T>(d_);
    if (type_ == node_type::integer) return static_cast<T>(i_);
    if (type_ == node_type::boolean) return static_cast<T>(b_ ? 1 : 0);
    return std::nullopt;
  } else if constexpr (std::is_same_v<T, std::string>) {
    if (type_ == node_type::string) return s_;
    return std::nullopt;
  } else {
    static_assert(std::is_same_v<T, std::string_view>, "unsupported value<T>()");
    if (type_ == node_type::string) return std::string_view(s_);
    return std::nullopt;
  }
}
template std::optional<bool> node::value<bool>() const;
template std::optional<int> node::value<int>() const;
template std::optional<unsigned> node::value<unsigned>() const;
template std::optional<long> node::value<long>() const;
template std::optional<unsigned long> node::value<unsigned long>() const;
template std::optional<long long> node::value<long long>() const;
template std::optional<double> node::value<double>() const;
template std::optional<float> node::value<float>() const;
template std::optional<std::string> node::value<std::string>() const;
template std::optional<std::string_view> node::value<std::string_view>() const;

// --------------------------------------------------------------------------------------------------------------------- table ---

namespace {
bool key_less(const table::item& a, std::string_view b) { return std::string_view(a.first.text) < b; }
}  // namespace

node* table::find(std::string_view k) noexcept {
  auto it = std::lower_bound(items_.begin(), items_.end(), k, key_less);
  return it != items_.end() && it->first.text == k ? &it->second : nullptr;
}
const node* table::find(std::string_view k) const noexcept {
  auto it = std::lower_bound(items_.begin(), items_.end(), k, key_less);
  return it != items_.end() && it->first.text == k ? &it->second : nullptr;
}
void table::set(std::string_view k, node v) {
  auto it = std::lower_bound(items_.begin(), items_.end(), k, key_less);
  if (it != items_.end() && it->first.text == k) it->second = std::move(v);
  else items_.emplace(it, key{std::string(k)}, std::move(v));
}
bool table::insert_new(std::string_view k, node v) {
  auto it = std::lower_bound(items_.begin(), items_.end(), k, key_less);
  if (it != items_.end() && it->first.text == k) return false;
  items_.emplace(it, key{std::string(k)}, std::move(v));
  return true;
}
bool table::erase(std::string_view k) {
  auto it = std::lower_bound(items_.begin(), items_.end(), k, key_less);
  if (it == items_.end() || it->first.text != k) return false;
  items_.erase(it);
  return true;
}

// -------------------------------------------------------------------------------------------------------------------- parser ---

namespace {

struct Parser {
  std::string_view s;
  size_t i = 0;
  uint32_t line = 1;
  size_t line_start = 0;
  std::set<const table*> defined;      // tables given by a [header] (a second one is an error)
  std::set<const table*> closed;       // inline tables: nothing may be added later
  std::set<const array*> table_arrays;  // arrays made by [[headers]]

  [[noreturn]] void fail(const std::string& what) const {
    const uint32_t col = static_cast<uint32_t>(i - line_start + 1);
    throw parse_error(what, {{line, col}, {line, col}});
  }
  bool eof() const { return i >= s.size(); }
  char peek(size_t k = 0) const { return i + k < s.size() ? s[i + k] : '\0'; }
  void newline() {  // at a '\n' that was consumed
    ++line;
    line_start = i;
  }

  void skip_blank() {
    while (!eof() && (s[i] == ' ' || s[i] == '\t')) ++i;
  }
  void skip_comment() {
    if (peek() != '#') return;
    while (!eof() && s[i] != '\n') {
      if (s[i] == '\r' && peek(1) == '\n') break;
      const unsigned char c = static_cast<unsigned char>(s[i]);
      if ((c < 0x20 && c != '\t') || c == 0x7f) fail("control characters are not allowed in comments");
      ++i;
    }
  }
  // blanks, comments and line breaks (inside arrays)
  void skip_space_and_lines() {
    for (;;) {
      skip_blank();
      if (peek() == '#') skip_comment();
      if (peek() == '\n') {
        ++i;
        newline();
      } else if (peek() == '\r' && peek(1) == '\n') {
        i += 2;
        newline();
      } else {
        break;
      }
    }
  }
  void end_of_line() {
    skip_blank();
    skip_comment();
    if (eof()) return;
    if (peek() == '\n') {
      ++i;
      newline();
    } else if (peek() == '\r' && peek(1) == '\n') {
      i += 2;
      newline();
    } else {
      fail("expected the end of the line");
    }
  }

  static bool bare_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

  static void put_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  uint32_t hex_digits(int n) {
    uint32_t v = 0;
    for (int k = 0; k < n; ++k) {
      const char c = peek();
      uint32_t d;
      if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
      else fail("a hexadecimal digit was expected in the escape sequence");
      v = v * 16 + d;
      ++i;
    }
    if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) fail("not a Unicode scalar value");
    return v;
  }

  // At the opening quote of a basic string ("...") or a multi-line one ("""...""").
  std::string basic_string() {
    std::string out;
    const bool multi = peek(1) == '"' && peek(2) == '"';
    i += multi ? 3 : 1;
    if (multi) {  // a line break right after the opening quotes is dropped
      if (peek() == '\n') {
        ++i;
        newline();
      } else if (peek() == '\r' && peek(1) == '\n') {
        i += 2;
        newline();
      }
    }
    for (;;) {
      if (eof()) fail("unterminated string");
      const char c = s[i];
      if (c == '"') {
        if (!multi) {
          ++i;
          return out;
        }
        size_t run = 1;
        while (peek(run) == '"') ++run;
        if (run >= 3) {  // the closing quotes; up to two quotes right before them belong to the text
          if (run > 5) fail("too many quotes at the end of the string");
          out.append(run - 3, '"');
          i += run;
          return out;
        }
        out += c;
        ++i;
      } else if (c == '\\') {
        ++i;
        const char e = peek();
        ++i;
        switch (e) {
          case 'b': out += '\b'; break;
          case 't': out += '\t'; break;
          case 'n': out += '\n'; break;
          case 'f': out += '\f'; break;
          case 'r': out += '\r'; break;
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case 'u': put_utf8(out, hex_digits(4)); break;
          case 'U': put_utf8(out, hex_digits(8)); break;
          case ' ': case '\t': case '\n': case '\r':
            if (!multi) fail("invalid escape sequence");
            // a backslash at the end of a line: the line break and the white space after it are dropped
            --i;
            while (peek() == ' ' || peek() == '\t') ++i;
            if (peek() == '\r' && peek(1) == '\n') ++i;
            if (peek() != '\n') fail("invalid escape sequence");
            while (peek() == '\n' || peek() == ' ' || peek() == '\t' || (peek() == '\r' && peek(1) == '\n')) {
              if (peek() == '\r') ++i;
              if (peek() == '\n') {
                ++i;
                newline();
              } else {
                ++i;
              }
            }
            break;
          default: fail("invalid escape sequence");
        }
      } else if (c == '\n') {
        if (!multi) fail("a line break inside a string");
        out += c;
        ++i;
        newline();
      } else {
        const unsigned char u = static_cast<unsigned char>(c);
        if ((u < 0x20 && u != '\t' && !(multi && (u == '\r'))) || u == 0x7f) fail("control characters must be escaped in strings");
        out += c;
        ++i;
      }
    }
  }

  std::string literal_string() {
    std::string out;
    const bool multi = peek(1) == '\'' && peek(2) == '\'';
    i += multi ? 3 : 1;
    if (multi) {
      if (peek() == '\n') {
        ++i;
        newline();
      } else if (peek() == '\r' && peek(1) == '\n') {
        i += 2;
        newline();
      }
    }
    for (;;) {
      if (eof()) fail("unterminated string");
      const char c = s[i];
      if (c == '\'') {
        if (!multi) {
          ++i;
          return out;
        }
        size_t run = 1;
        while (peek(run) == '\'') ++run;
        if (run >= 3) {
          if (run > 5) fail("too many quotes at the end of the string");
          out.append(run - 3, '\'');
          i += run;
          return out;
        }
        out += c;
        ++i;
      } else if (c == '\n') {
        if (!multi) fail("a line break inside a string");
        out += c;
        ++i;
        newline();
      } else {
        const unsigned char u = static_cast<unsigned char>(c);
        if ((u < 0x20 && u != '\t' && !(multi && u == '\r')) || u == 0x7f) fail("control characters are not allowed in strings");
        out += c;
        ++i;
      }
    }
  }

  std::string parse_key_part() {
    skip_blank();
    const char c = peek();
    if (c == '"') return basic_string();
    if (c == '\'') return literal_string();
    const size_t b = i;
    while (!eof() && bare_char(s[i])) ++i;
    if (i == b) fail("a key was expected");
    return std::string(s.substr(b, i - b));
  }
  std::vector<std::string> parse_key_path() {
    std::vector<std::string> path;
    path.push_back(parse_key_part());
    for (;;) {
      skip_blank();
      if (peek() != '.') break;
      ++i;
      path.push_back(parse_key_part());
    }
    return path;
  }

  node parse_value(int depth) {
    if (depth > 128) fail("nesting is too deep");
    skip_blank();
    if (eof()) fail("a value was expected");
    const char c = s[i];
    if (c == '"') return node(basic_string());
    if (c == '\'') return node(literal_string());
    if (c == '[') return parse_array(depth);
    if (c == '{') return parse_inline_table(depth);
    return parse_scalar();
  }

  node parse_array(int depth) {
    ++i;  // [
    array a;
    for (;;) {
      skip_space_and_lines();
      if (peek() == ']') {
        ++i;
        return node(std::move(a));
      }
      a.push_back(parse_value(depth + 1));
      skip_space_and_lines();
      if (peek() == ',') {
        ++i;
        continue;
      }
      if (peek() == ']') {
        ++i;
        return node(std::move(a));
      }
      fail("expected ',' or ']' in the array");
    }
  }

  node parse_inline_table(int depth) {
    ++i;  // {
    table t;
    skip_blank();
    if (peek() == '}') {
      ++i;
      auto n = node(std::move(t));
      closed.insert(n.as_table());
      return n;
    }
    for (;;) {
      skip_blank();
      const std::vector<std::string> path = parse_key_path();
      skip_blank();
      if (peek() != '=') fail("'=' was expected after the key");
      ++i;
      table* cur = &t;
      for (size_t k = 0; k + 1 < path.size(); ++k) cur = descend(cur, path[k]);
      node v = parse_value(depth + 1);
      if (!cur->insert_new(path.back(), std::move(v))) fail("the key '" + path.back() + "' is defined twice");
      skip_blank();
      if (peek() == ',') {
        ++i;
        continue;
      }
      if (peek() == '}') {
        ++i;
        break;
      }
      fail("expected ',' or '}' in the inline table");
    }
    auto n = node(std::move(t));
    closed.insert(n.as_table());
    return n;
  }

  // The sub-table `name` of `t` for a dotted key or a header path, made when missing.
  table* descend(table* t, const std::string& name) {
    if (node* n = t->find(name)) {
      if (table* sub = n->as_table()) {
        if (closed.count(sub)) fail("the inline table '" + name + "' cannot be extended");
        return sub;
      }
      if (array* a = n->as_array(); a && table_arrays.count(a) && !a->empty()) return (*a)[a->size() - 1].as_table();
      fail("'" + name + "' is not a table");
    }
    t->insert_new(name, node(table{}));
    return t->find(name)->as_table();
  }

  node parse_scalar() {
    const size_t b = i;
    while (!eof()) {
      const char c = s[i];
      if (c == ' ' || c == '\t' || c == ',' || c == ']' || c == '}' || c == '#' || c == '\n' || c == '\r') break;
      ++i;
    }
    std::string_view tok = s.substr(b, i - b);
    if (tok.empty()) fail("a value was expected");
    if (tok == "true") return node(true);
    if (tok == "false") return node(false);
    // date and time values are kept as their text
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const bool date = tok.size() >= 10 && digit(tok[0]) && digit(tok[1]) && digit(tok[2]) && digit(tok[3]) && tok[4] == '-' && digit(tok[5]);
    const bool time = tok.size() >= 8 && digit(tok[0]) && digit(tok[1]) && tok[2] == ':' && digit(tok[3]);
    if (date || time) {
      auto num = [&](std::string_view t, size_t at, size_t n) {
        if (at + n > t.size()) return false;
        for (size_t k = 0; k < n; ++k)
          if (!digit(t[at + k])) return false;
        return true;
      };
      auto two = [&](std::string_view t, size_t at) { return (t[at] - '0') * 10 + (t[at + 1] - '0'); };
      // HH:MM:SS[.fraction][Z|+HH:MM|-HH:MM] from `at`; true when it ends the token
      auto valid_time = [&](std::string_view t, size_t at) {
        if (!(num(t, at, 2) && at + 2 < t.size() && t[at + 2] == ':' && num(t, at + 3, 2) && at + 5 < t.size() && t[at + 5] == ':' && num(t, at + 6, 2)))
          return false;
        if (two(t, at) > 23 || two(t, at + 3) > 59 || two(t, at + 6) > 60) return false;
        size_t k = at + 8;
        if (k < t.size() && t[k] == '.') {
          ++k;
          const size_t f = k;
          while (k < t.size() && digit(t[k])) ++k;
          if (k == f) return false;
        }
        if (k == t.size()) return true;
        if (t[k] == 'Z' || t[k] == 'z') return k + 1 == t.size();
        if ((t[k] == '+' || t[k] == '-') && num(t, k + 1, 2) && k + 3 < t.size() && t[k + 3] == ':' && num(t, k + 4, 2)) return k + 6 == t.size();
        return false;
      };
      bool date_ok = !date || (num(tok, 0, 4) && tok[4] == '-' && num(tok, 5, 2) && tok[7] == '-' && num(tok, 8, 2));
      if (date && date_ok) {
        const int year = two(tok, 0) * 100 + two(tok, 2), mon = two(tok, 5), day = two(tok, 8);
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        date_ok = mon >= 1 && mon <= 12 && day >= 1 && day <= days[mon - 1] + (mon == 2 && leap ? 1 : 0);
      }
      bool shape = date_ok;
      if (shape && date && tok.size() > 10) shape = (tok[10] == 'T' || tok[10] == 't') && valid_time(tok, 11);
      if (shape && time) shape = valid_time(tok, 0);
      if (!shape && date && tok.size() == 10) shape = date_ok;
      if (!shape && !(date && peek() == ' ' && digit(peek(1)) && tok.size() == 10 && date_ok)) fail("invalid date or time '" + std::string(tok) + "'");
      std::string text(tok);
      if (date && peek() == ' ' && digit(peek(1))) {  // "1979-05-27 07:32:00"
        ++i;
        const size_t b2 = i;
        while (!eof() && s[i] != ',' && s[i] != ']' && s[i] != '}' && s[i] != '#' && s[i] != '\n' && s[i] != '\r' && s[i] != ' ') ++i;
        if (!valid_time(s.substr(b2, i - b2), 0)) fail("invalid time in the date-time");
        text += ' ';
        text += s.substr(b2, i - b2);
      }
      return node(std::move(text));
    }
    return number(tok);
  }

  node number(std::string_view tok) {
    // underscores may only sit between digits
    std::string clean;
    clean.reserve(tok.size());
    const bool hex = tok.size() > 2 && tok[0] == '0' && tok[1] == 'x';
    auto digit_of = [&](char c) { return hex ? std::isxdigit(static_cast<unsigned char>(c)) != 0 : (c >= '0' && c <= '9'); };
    for (size_t k = 0; k < tok.size(); ++k) {
      if (tok[k] == '_') {
        const bool ok = k > 0 && k + 1 < tok.size() && digit_of(tok[k - 1]) && digit_of(tok[k + 1]);
        if (!ok) fail("misplaced '_' in the number");
      } else {
        clean += tok[k];
      }
    }
    std::string_view t = clean;
    bool neg = false;
    std::string_view body = t;
    if (!body.empty() && (body[0] == '+' || body[0] == '-')) {
      neg = body[0] == '-';
      body.remove_prefix(1);
    }
    if (body == "inf") return node(neg ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
    if (body == "nan") return node(std::numeric_limits<double>::quiet_NaN());
    if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'o' || body[1] == 'b') && t.size() == body.size()) {
      const int base = body[1] == 'x' ? 16 : (body[1] == 'o' ? 8 : 2);
      uint64_t v = 0;
      const auto r = std::from_chars(body.data() + 2, body.data() + body.size(), v, base);
      if (r.ec != std::errc() || r.ptr != body.data() + body.size() || v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        fail("invalid or out of range integer");
      return node(static_cast<int64_t>(v));
    }
    if (body.empty() || !(std::isdigit(static_cast<unsigned char>(body[0])))) fail("unrecognized value '" + std::string(tok) + "'");
    const bool is_float = body.find_first_of(".eE") != std::string_view::npos;
    if (body.size() > 1 && body[0] == '0' && std::isdigit(static_cast<unsigned char>(body[1]))) fail("leading zeros are not allowed in a number");
    if (!is_float) {
      uint64_t v = 0;
      const auto r = std::from_chars(body.data(), body.data() + body.size(), v, 10);
      if (r.ec != std::errc() || r.ptr != body.data() + body.size()) fail("invalid integer '" + std::string(tok) + "'");
      if (!neg && v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) fail("integer out of range");
      if (neg && v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1) fail("integer out of range");
      return node(neg ? static_cast<int64_t>(0 - v) : static_cast<int64_t>(v));
    }
    // digits around the point; "1." and ".5" are not floats in TOML
    const size_t dot = body.find('.');
    if (dot != std::string_view::npos && (dot + 1 >= body.size() || !std::isdigit(static_cast<unsigned char>(body[dot + 1])))) fail("invalid float");
    double d = 0;
    const auto r = std::from_chars(body.data(), body.data() + body.size(), d);
    if (r.ec != std::errc() || r.ptr != body.data() + body.size()) fail("invalid float '" + std::string(tok) + "'");
    return node(neg ? -d : d);
  }

  table run() {
    table root;
    table* cur = &root;
    for (;;) {
      skip_blank();
      if (eof()) break;
      const char c = s[i];
      if (c == '#') {
        skip_comment();
        end_of_line();
        continue;
      }
      if (c == '\n' || (c == '\r' && peek(1) == '\n')) {
        end_of_line();
        continue;
      }
      if (c == '[') {
        const bool aot = peek(1) == '[';
        i += aot ? 2 : 1;
        const std::vector<std::string> path = parse_key_path();
        skip_blank();
        if (peek() != ']') fail("']' was expected after the table name");
        ++i;
        if (aot) {
          if (peek() != ']') fail("']]' was expected");
          ++i;
        }
        table* t = &root;
        for (size_t k = 0; k + 1 < path.size(); ++k) t = descend(t, path[k]);
        const std::string& last = path.back();
        if (aot) {
          node* n = t->find(last);
          array* a = nullptr;
          if (n) {
            a = n->as_array();
            if (!a || !table_arrays.count(a)) fail("'" + last + "' is not an array of tables");
          } else {
            t->insert_new(last, node(array{}));
            a = t->find(last)->as_array();
            table_arrays.insert(a);
          }
          a->push_back(table{});
          cur = (*a)[a->size() - 1].as_table();
          defined.insert(cur);
        } else {
          node* n = t->find(last);
          if (n) {
            table* sub = n->as_table();
            if (!sub) {
              array* a = n->as_array();
              if (a && table_arrays.count(a)) fail("'" + last + "' is an array of tables");
              fail("'" + last + "' is not a table");
            }
            if (defined.count(sub) || closed.count(sub)) fail("the table '" + last + "' is defined twice");
            cur = sub;
          } else {
            t->insert_new(last, node(table{}));
            cur = t->find(last)->as_table();
          }
          defined.insert(cur);
        }
        end_of_line();
        continue;
      }
      // key = value
      const std::vector<std::string> path = parse_key_path();
      skip_blank();
      if (peek() != '=') fail("'=' was expected after the key");
      ++i;
      table* t = cur;
      for (size_t k = 0; k + 1 < path.size(); ++k) t = descend(t, path[k]);
      node v = parse_value(0);
      if (closed.count(t)) fail("an inline table cannot be extended");
      if (!t->insert_new(path.back(), std::move(v))) fail("the key '" + path.back() + "' is defined twice");
      end_of_line();
    }
    return root;
  }
};

}  // namespace

table parse(std::string_view text) {
  // a byte order mark at the start is not part of the document
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF)
    text.remove_prefix(3);
  Parser p{text};
  return p.run();
}

table parse_file(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) throw parse_error("File could not be opened for reading", {});
  std::string data;
  char buf[16384];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
  std::fclose(f);
  return parse(data);
}

// -------------------------------------------------------------------------------------------------------------------- printer ---

namespace {

void append_string(std::string& out, std::string_view s) {
  bool literal_ok = true;
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (c == '\'' || (u < 0x20 && c != '\t') || u == 0x7f) {
      literal_ok = false;
      break;
    }
  }
  if (literal_ok) {
    out += '\'';
    out += s;
    out += '\'';
    return;
  }
  out += '"';
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (u < 0x20 || u == 0x7f) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04X", u);
          out += b;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void append_key(std::string& out, std::string_view k) {
  bool bare = !k.empty();
  for (char c : k)
    if (!Parser::bare_char(c)) {
      bare = false;
      break;
    }
  if (bare) out += k;
  else append_string(out, k);
}

void append_double(std::string& out, double d) {
  if (std::isnan(d)) {
    out += "nan";
    return;
  }
  if (std::isinf(d)) {
    out += d < 0 ? "-inf" : "inf";
    return;
  }
  char buf[40];
  const auto r = std::to_chars(buf, buf + sizeof buf, d);
  const std::string_view v(buf, static_cast<size_t>(r.ptr - buf));
  out += v;
  if (v.find_first_of(".eE") == std::string_view::npos) out += ".0";
}

void append_int(std::string& out, int64_t v) {
  char buf[24];
  const auto r = std::to_chars(buf, buf + sizeof buf, v);
  out.append(buf, static_cast<size_t>(r.ptr - buf));
}

void append_inline(std::string& out, const node& n);

void append_inline_table(std::string& out, const table& t) {
  if (t.empty()) {
    out += "{}";
    return;
  }
  out += "{ ";
  bool first = true;
  for (const auto& [k, v] : t) {
    if (!first) out += ", ";
    first = false;
    append_key(out, k.str());
    out += " = ";
    append_inline(out, v);
  }
  out += " }";
}

void append_inline(std::string& out, const node& n) {
  switch (n.type()) {
    case node_type::boolean: out += *n.value<bool>() ? "true" : "false"; break;
    case node_type::integer: append_int(out, n.get()); break;
    case node_type::floating_point: append_double(out, *n.value<double>()); break;
    case node_type::string: append_string(out, n.as_string_view()); break;
    case node_type::table: append_inline_table(out, *n.as_table()); break;
    case node_type::array: {
      const array& a = *n.as_array();
      if (a.empty()) {
        out += "[]";
        break;
      }
      out += "[ ";
      bool first = true;
      for (const node& e : a) {
        if (!first) out += ", ";
        first = false;
        append_inline(out, e);
      }
      out += " ]";
    } break;
  }
}

bool is_table_array(const node& n) {
  const array* a = n.as_array();
  if (!a || a->empty()) return false;
  for (const node& e : *a)
    if (!e.is_table()) return false;
  return true;
}

void print_table(std::string& out, const table& t, std::string& path, bool* first_block, bool is_array_element) {
  // A table that holds only sub-tables needs no header of its own: "[a.b]" says "[a]" exists (toml++ left it out the same way).
  bool has_value = false, has_sub = false;
  for (const auto& [k, v] : t) (v.is_table() || is_table_array(v) ? has_sub : has_value) = true;
  const bool has_header = !path.empty() && (has_value || !has_sub || is_array_element);
  if (has_header) {
    if (!*first_block) out += '\n';
    *first_block = false;
    out += is_array_element ? "[[" : "[";
    out += path;
    out += is_array_element ? "]]\n" : "]\n";
  }
  bool wrote_any = false;
  for (const auto& [k, v] : t) {
    if (v.is_table() || is_table_array(v)) continue;
    append_key(out, k.str());
    out += " = ";
    append_inline(out, v);
    out += '\n';
    wrote_any = true;
  }
  if (wrote_any) *first_block = false;
  // sub-tables first, then arrays of tables, each in key order (the order toml++ wrote)
  for (int pass = 0; pass < 2; ++pass) {
    for (const auto& [k, v] : t) {
      const bool sub_table = pass == 0 && v.is_table();
      const bool sub_array = pass == 1 && is_table_array(v);
      if (!sub_table && !sub_array) continue;
      const size_t keep = path.size();
      if (!path.empty()) path += '.';
      append_key(path, k.str());
      if (sub_table) {
        print_table(out, *v.as_table(), path, first_block, false);
      } else {
        for (const node& e : *v.as_array()) print_table(out, *e.as_table(), path, first_block, true);
      }
      path.resize(keep);
    }
  }
}

}  // namespace

std::string to_string(const table& t) {
  std::string out, path;
  bool first = true;
  print_table(out, t, path, &first, false);
  if (!out.empty() && out.back() == '\n') out.pop_back();  // toml++ ends the document without a final line break
  return out;
}

std::ostream& operator<<(std::ostream& out, const table& t) {
  const std::string s = to_string(t);
  return out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

}  // namespace toml
