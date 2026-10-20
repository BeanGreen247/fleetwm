#pragma once

// A small TOML reader and writer with the part of toml++'s interface the project uses, in namespace `toml` so the config code reads as
// before. It exists so the build needs no external TOML library (and no git clone of one). Supported: comments, bare and quoted keys, dotted
// keys, [tables], [[arrays of tables]], inline tables, arrays (nested, multi-line, trailing commas), basic and literal strings (also the
// multi-line forms), integers (decimal, hex, octal, binary, underscores), floats (inf, nan), booleans. A date or time is kept as a string
// holding its text. Tables are kept sorted by key, and printed the way toml++ printed them (values first, then [sub-tables], literal
// single-quoted strings), so files written before and after the switch look alike.

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace toml {

struct source_position {
  uint32_t line = 0, column = 0;
};
struct source_region {
  source_position begin, end;
};

class parse_error : public std::runtime_error {
 public:
  parse_error(std::string description, source_region where)
      : std::runtime_error(description), description_(std::move(description)), where_(where) {}
  std::string_view description() const noexcept { return description_; }
  const source_region& source() const noexcept { return where_; }

 private:
  std::string description_;
  source_region where_;
};

class node;
class table;
class array;
template <class N> class node_view_t;
using node_view = node_view_t<node>;
using const_node_view = node_view_t<const node>;

enum class node_type : uint8_t { boolean, integer, floating_point, string, table, array };

class node {
 public:
  node() = default;
  node(bool v) : type_(node_type::boolean), b_(v) {}
  node(int64_t v) : type_(node_type::integer), i_(v) {}
  node(double v) : type_(node_type::floating_point), d_(v) {}
  node(std::string v) : type_(node_type::string), s_(std::move(v)) {}
  node(table v);
  node(array v);
  node(const node& o);
  node(node&& o) noexcept;
  node& operator=(const node& o);
  node& operator=(node&& o) noexcept;
  ~node();

  node_type type() const noexcept { return type_; }
  bool is_table() const noexcept { return type_ == node_type::table; }
  bool is_array() const noexcept { return type_ == node_type::array; }
  bool is_string() const noexcept { return type_ == node_type::string; }
  bool is_integer() const noexcept { return type_ == node_type::integer; }
  bool is_boolean() const noexcept { return type_ == node_type::boolean; }
  bool is_floating_point() const noexcept { return type_ == node_type::floating_point; }

  table* as_table() noexcept { return type_ == node_type::table ? t_.get() : nullptr; }
  const table* as_table() const noexcept { return type_ == node_type::table ? t_.get() : nullptr; }
  array* as_array() noexcept { return type_ == node_type::array ? a_.get() : nullptr; }
  const array* as_array() const noexcept { return type_ == node_type::array ? a_.get() : nullptr; }

  // toml++'s as_integer(): the node itself when it is an integer (and only then), read with get().
  node* as_integer() noexcept { return type_ == node_type::integer ? this : nullptr; }
  const node* as_integer() const noexcept { return type_ == node_type::integer ? this : nullptr; }
  int64_t get() const noexcept { return i_; }

  // The value as T, or nullopt when the node is another kind. As in toml++: a number or a boolean converts to the other numeric types
  // when it fits (an integer in a bool field is true when not zero); a string only ever gives a string.
  template <class T> std::optional<T> value() const;
  template <class T> T value_or(T fallback) const {
    const auto v = value<T>();
    return v ? *v : fallback;
  }
  std::string_view as_string_view() const noexcept { return s_; }  // valid for a string node

 private:
  friend class table;
  friend class array;
  node_type type_ = node_type::boolean;
  bool b_ = false;
  int64_t i_ = 0;
  double d_ = 0;
  std::string s_;
  std::unique_ptr<table> t_;
  std::unique_ptr<array> a_;
};

template <class T> node make_node(T&& v);

// A key as the table stores it; `str()` is toml++'s name for the text.
struct key {
  std::string text;
  std::string_view str() const noexcept { return text; }
  operator std::string_view() const noexcept { return text; }
};

// A reference that may point at nothing: looking up a missing key, or going on from one, gives an empty view and never throws.
template <class N> class node_view_t {
 public:
  node_view_t() = default;
  node_view_t(N* p) : p_(p) {}
  explicit operator bool() const noexcept { return p_ != nullptr; }
  N* node() const noexcept { return p_; }
  template <class T> std::optional<T> value() const { return p_ ? p_->template value<T>() : std::nullopt; }
  template <class T> T value_or(T fallback) const { return p_ ? p_->template value_or<T>(fallback) : fallback; }
  auto as_table() const noexcept { return p_ ? p_->as_table() : static_cast<decltype(p_->as_table())>(nullptr); }
  auto as_array() const noexcept { return p_ ? p_->as_array() : static_cast<decltype(p_->as_array())>(nullptr); }
  auto as_integer() const noexcept { return p_ ? p_->as_integer() : static_cast<decltype(p_->as_integer())>(nullptr); }
  node_view_t operator[](std::string_view k) const;

 private:
  N* p_ = nullptr;
};

class table {
 public:
  using item = std::pair<key, node>;
  using iterator = std::vector<item>::iterator;
  using const_iterator = std::vector<item>::const_iterator;

  table() = default;
  node_view operator[](std::string_view k) noexcept { return node_view(find(k)); }
  const_node_view operator[](std::string_view k) const noexcept { return const_node_view(find(k)); }
  bool contains(std::string_view k) const noexcept { return find(k) != nullptr; }
  size_t size() const noexcept { return items_.size(); }
  bool empty() const noexcept { return items_.empty(); }
  iterator begin() noexcept { return items_.begin(); }
  iterator end() noexcept { return items_.end(); }
  const_iterator begin() const noexcept { return items_.begin(); }
  const_iterator end() const noexcept { return items_.end(); }
  node* find(std::string_view k) noexcept;
  const node* find(std::string_view k) const noexcept;

  template <class T> void insert_or_assign(std::string_view k, T&& v) { set(k, make_node(std::forward<T>(v))); }
  bool erase(std::string_view k);
  // Parser use: false when the key is already there.
  bool insert_new(std::string_view k, node v);

 private:
  void set(std::string_view k, node v);
  std::vector<item> items_;  // sorted by key
};

class array {
 public:
  array() = default;
  size_t size() const noexcept { return items_.size(); }
  bool empty() const noexcept { return items_.empty(); }
  node& operator[](size_t i) noexcept { return items_[i]; }
  const node& operator[](size_t i) const noexcept { return items_[i]; }
  std::vector<node>::iterator begin() noexcept { return items_.begin(); }
  std::vector<node>::iterator end() noexcept { return items_.end(); }
  std::vector<node>::const_iterator begin() const noexcept { return items_.begin(); }
  std::vector<node>::const_iterator end() const noexcept { return items_.end(); }
  template <class T> void push_back(T&& v) { items_.push_back(make_node(std::forward<T>(v))); }

 private:
  std::vector<node> items_;
};

// Turns a C++ value into a node: bool, any integer, any floating point, strings, table, array, node.
template <class T> node make_node(T&& v) {
  using U = std::remove_cv_t<std::remove_reference_t<T>>;
  if constexpr (std::is_same_v<U, bool>) return node(static_cast<bool>(v));
  else if constexpr (std::is_integral_v<U>) return node(static_cast<int64_t>(v));
  else if constexpr (std::is_floating_point_v<U>) return node(static_cast<double>(v));
  else if constexpr (std::is_convertible_v<U, std::string_view> && !std::is_same_v<U, node>) return node(std::string(std::string_view(v)));
  else return node(std::forward<T>(v));
}

template <class N> node_view_t<N> node_view_t<N>::operator[](std::string_view k) const {
  if (!p_) return {};
  auto* t = p_->as_table();
  return t ? (*t)[k] : node_view_t{};
}

// Reading. A file that cannot be opened is a parse_error too (the config loaders treat both as "use the defaults").
table parse(std::string_view text);
table parse_file(const std::string& path);

// Writing, in toml++'s layout.
std::ostream& operator<<(std::ostream& out, const table& t);
std::string to_string(const table& t);

}  // namespace toml
