#pragma once

#include <string>

namespace fleetwm::tty {

// Reads a line with normal terminal echo (used for the username prompt).
// Returns false on EOF/error before any input was read.
bool read_line(std::string& out);

// Reads a line with echo disabled (used for the password prompt). Restores
// the terminal's original echo state on every exit path, including EOF and
// error, via an internal RAII guard. Returns false on EOF/error.
bool read_password(std::string& out);

void print(const std::string& text);
void print_line(const std::string& text);
void clear_screen();

}  // namespace fleetwm::tty
