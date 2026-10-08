#pragma once

#include <string>
#include <vector>

namespace fleetwm {

// Splits a command line into words: whitespace separates, '...' and "..." group (the quotes are dropped), a backslash outside single
// quotes takes the next character literally. No expansion of any kind.
inline std::vector<std::string> split_command(const std::string& line) {
  std::vector<std::string> words;
  std::string cur;
  bool in_word = false;
  char quote = 0;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quote) {
      if (c == quote) quote = 0;
      else if (c == '\\' && quote == '"' && i + 1 < line.size()) cur += line[++i];
      else cur += c;
    } else if (c == '\'' || c == '"') {
      quote = c;
      in_word = true;
    } else if (c == '\\' && i + 1 < line.size()) {
      cur += line[++i];
      in_word = true;
    } else if (c == ' ' || c == '\t' || c == '\n') {
      if (in_word) words.push_back(cur);
      cur.clear();
      in_word = false;
    } else {
      cur += c;
      in_word = true;
    }
  }
  if (in_word) words.push_back(cur);
  return words;
}

// The inverse: one line that split_command() turns back into `words` (a word with anything but safe characters is single-quoted).
inline std::string join_command(const std::vector<std::string>& words) {
  std::string out;
  for (const std::string& w : words) {
    if (!out.empty()) out += ' ';
    const bool plain = !w.empty() && w.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-+=.,:/@%") == std::string::npos;
    if (plain) {
      out += w;
      continue;
    }
    out += '\'';
    for (const char c : w) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    out += '\'';
  }
  return out;
}

// The command line for the "open a terminal" shortcut. Plain `foot` is started with
// Fleetwm's bundled config (bigger font, the `~>` prompt) -- unless the user has their
// own ~/.config/foot/foot.ini, which always wins, or the bundled file is missing.
// Any other terminal command is split into words and run as set ("lestrix --lite" is the program lestrix with the argument --lite).
inline std::vector<std::string> terminal_argv(const std::string& command, const std::string& sysconf_dir,
                                              bool user_foot_config_exists, bool bundled_foot_config_exists) {
  if (command == "foot" && !user_foot_config_exists && bundled_foot_config_exists)
    return {"foot", "-c", sysconf_dir + "/foot.ini"};
  std::vector<std::string> words = split_command(command);
  if (words.empty()) words.push_back(command);
  return words;
}

}  // namespace fleetwm
