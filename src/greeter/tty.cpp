#include "tty.hpp"

#include <termios.h>
#include <unistd.h>

#include <cstdio>

namespace fleetwm::tty {

namespace {

// Restores the terminal's original termios settings when it goes out of
// scope, so a read that hits EOF/error/signal can never leave the terminal
// stuck in no-echo mode.
class TermiosGuard {
 public:
  explicit TermiosGuard(bool disable_echo) {
    valid_ = tcgetattr(STDIN_FILENO, &original_) == 0;
    if (!valid_) {
      return;
    }
    if (disable_echo) {
      termios raw = original_;
      raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL);
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }
  }

  ~TermiosGuard() {
    if (valid_) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    }
  }

  TermiosGuard(const TermiosGuard&) = delete;
  TermiosGuard& operator=(const TermiosGuard&) = delete;

 private:
  termios original_{};
  bool valid_ = false;
};

bool read_line_impl(std::string& out) {
  out.clear();
  for (;;) {
    int c = std::fgetc(stdin);
    if (c == EOF) {
      return !out.empty();
    }
    if (c == '\n') {
      return true;
    }
    out.push_back(static_cast<char>(c));
  }
}

}  // namespace

bool read_line(std::string& out) {
  TermiosGuard guard(/*disable_echo=*/false);
  return read_line_impl(out);
}

bool read_password(std::string& out) {
  TermiosGuard guard(/*disable_echo=*/true);
  bool ok = read_line_impl(out);
  // The terminal never echoed the password, but the newline the user typed
  // to submit it did not print one either (ECHONL was also disabled) --
  // print it now so the next prompt starts on its own line.
  std::fputc('\n', stdout);
  std::fflush(stdout);
  return ok;
}

void print(const std::string& text) {
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fflush(stdout);
}

void print_line(const std::string& text) {
  print(text);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

void clear_screen() {
  // ANSI clear + cursor home. Plain escape codes only -- no ncurses.
  print("\x1b[2J\x1b[H");
}

}  // namespace fleetwm::tty
