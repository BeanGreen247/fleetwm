#include "session.hpp"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fleetwm::session {

namespace {

constexpr const char* kCompositorPath = "/usr/local/bin/fleetwm";

// Builds the child's environment: a minimal hardcoded floor, then whatever
// PAM provided layered on top (so PAM-provided values, e.g. from
// pam_systemd, can override the floor but the floor is always present).
std::vector<std::string> build_env(const passwd* pw,
                                    const std::vector<std::string>& pam_envlist) {
  std::vector<std::string> env;
  env.push_back(std::string("HOME=") + pw->pw_dir);
  env.push_back(std::string("USER=") + pw->pw_name);
  env.push_back(std::string("LOGNAME=") + pw->pw_name);
  env.push_back(std::string("SHELL=") + pw->pw_shell);
  env.emplace_back("PATH=/usr/local/bin:/usr/bin:/bin");
  env.emplace_back("XDG_SESSION_TYPE=wayland");
  // Same rationale as fleetwm-greeter@.service's own copy of this var
  // (see packaging/fleetwm-greeter@.service): on hardware/VMs with no
  // hardware-accelerated EGL, wlroots refuses to fall back to llvmpipe
  // unless this is set. Harmless no-op when real GPU accel exists.
  env.emplace_back("WLR_RENDERER_ALLOW_SOFTWARE=1");
  // GTK4's GSK defaults to a GL renderer, which drags in Mesa's full
  // GL/EGL/gallium stack (and, on any host without real GPU-accelerated
  // EGL, llvmpipe -- pulling in libLLVM too, ~15-20MB Pss per process by
  // itself). Measured on fleetwm-dev: fleetwm-bar 131MB->24MB Pss,
  // fleetwm-wallpaper 99MB->22MB Pss, pixel-identical output (grim
  // screenshot diff) -- none of fleetwm's GTK4 clients (bar, wallpaper,
  // settings, launcher, locker) render anything that needs GPU
  // compositing. Inherited by every execlp-spawned helper below.
  env.emplace_back("GSK_RENDERER=cairo");
  // Without this, the session gets no locale env at all (glibc's default
  // is the plain "C" locale, ASCII-only) -- every GTK app and foot then
  // logs its own "not a UTF-8 locale, falling back to C.UTF-8" warning on
  // startup, and non-ASCII text (e.g. this session's own username/prompt
  // glyphs) can render wrong. C.UTF-8 rather than a real language locale
  // (e.g. en_US.UTF-8) since it's guaranteed present on every glibc
  // system without needing locale-gen -- see install.sh, which also sets
  // this as the system default for consistency outside of fleetwm too.
  env.emplace_back("LANG=C.UTF-8");
  env.emplace_back("LC_ALL=C.UTF-8");
  // Every GTK4 client otherwise activates the AT-SPI accessibility
  // D-Bus service on startup (confirmed via the session bus's own
  // activation log while building scripts/pgo-train-session.sh:
  // org.a11y.Bus, then org.a11y.atspi.Registry) even though nothing in
  // fleetwm exposes or consumes it -- explicit user choice to skip
  // that init on every client for the startup-time/memory win, at the
  // cost of assistive tech (screen readers etc.) not working if ever
  // needed.
  env.emplace_back("NO_AT_BRIDGE=1");
  // jemalloc's arena-based allocator actually returns freed memory to
  // the OS (madvise) instead of glibc's single-contiguous-heap model,
  // where memory freed below a still-live allocation near the top of
  // the heap can never be given back no matter how the trim threshold
  // is tuned (see tune_malloc_for_low_rss(), src/common/malloc_tuning.
  // hpp, which every fleetwm binary already calls -- that alone only
  // partially helps). Confirmed live on fleetwm-dev: repeated batches
  // of 30 terminals opened/closed against the real compositor left
  // 9-31MB of un-reclaimed Pss per cycle even with the tuned
  // thresholds; the identical test against a jemalloc-preloaded
  // instance returned to baseline Pss every single time. Checked for
  // existence rather than hardcoded blindly -- libjemalloc2 is an
  // install.sh dependency on a normal fleetwm install, but this floor
  // still needs to degrade cleanly (falls back to plain glibc, tuned
  // as above) for anyone running a manually-built checkout without it.
  if (std::filesystem::exists("/usr/lib/x86_64-linux-gnu/libjemalloc.so.2")) {
    env.emplace_back("LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2");
    // jemalloc's decay-based purge only runs opportunistically, as a
    // side effect of a later malloc/free call -- confirmed live: after
    // opening/closing a batch of terminals against the real desktop
    // compositor, Pss sat unchanged for 30+ idle seconds, then visibly
    // dropped only once a further batch of allocation activity gave it
    // something to piggyback the purge check on. A compositor that's
    // just sitting there with nothing to redraw (the normal case, see
    // output.cpp/output_frame -- wlr_scene's damage tracking means an
    // idle desktop allocates nothing) would otherwise hold onto that
    // freed memory indefinitely. background_thread:true is jemalloc's
    // own documented answer for exactly this: a dedicated thread that
    // purges on a timer regardless of whether the app allocates again.
    // 5s decay (both dirty and muzzy pages) rather than jemalloc's
    // default ~10s+10s, since RAM footprint matters more here than
    // shaving a few background-thread wakeups.
    env.emplace_back(
        "MALLOC_CONF=background_thread:true,dirty_decay_ms:5000,muzzy_decay_ms:5000");
  }
  // Fallback only -- pam_systemd normally supplies XDG_RUNTIME_DIR itself
  // in pam_envlist below, which (appended after this floor) overrides it.
  // But pam_systemd registering the session with logind and logind
  // actually creating/exporting /run/user/<uid> is not guaranteed to have
  // finished by the time PAM hands control back here -- observed in
  // practice after rapid repeated logins, where pam_envlist simply omits
  // XDG_RUNTIME_DIR and the compositor fails to init with "XDG_RUNTIME_DIR
  // is invalid or not set", bouncing the user straight back to the login
  // prompt with no indication why. /run/user/<uid> is the fixed, standard
  // systemd-logind convention (confirmed via `loginctl show-user`), safe
  // to assume even when PAM's own copy hasn't arrived yet.
  env.push_back(std::string("XDG_RUNTIME_DIR=/run/user/") + std::to_string(pw->pw_uid));

  for (const std::string& kv : pam_envlist) {
    env.push_back(kv);
  }
  return env;
}

}  // namespace

[[noreturn]] void exec_as_user(const passwd* pw,
                                const std::vector<std::string>& pam_envlist) {
  if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
    std::perror("fleetwm-greet: initgroups failed");
    std::_Exit(1);
  }
  if (setgid(pw->pw_gid) != 0) {
    std::perror("fleetwm-greet: setgid failed");
    std::_Exit(1);
  }
  if (setuid(pw->pw_uid) != 0) {
    std::perror("fleetwm-greet: setuid failed");
    std::_Exit(1);
  }

  // Defense in depth: confirm the drop actually took before ever exec'ing.
  if (getuid() == 0 || geteuid() == 0 || getgid() == 0 || getegid() == 0) {
    std::fprintf(stderr, "fleetwm-greet: refusing to exec, still privileged after drop\n");
    std::_Exit(1);
  }

  if (chdir(pw->pw_dir) != 0) {
    // Non-fatal: fall back to running from wherever we are (e.g. "/") if
    // the user's home directory is missing or inaccessible.
    std::perror("fleetwm-greet: chdir to home failed, continuing");
  }

  // The compositor's stderr/stdout otherwise go straight to the raw TTY
  // device fleetwm-greet was launched on, where wlroots' startup log gets
  // overwritten the moment the compositor takes DRM master -- redirect to
  // a file instead so it survives and can actually be read afterwards.
  // TEMPORARY: for diagnosing the black-screen rendering issue; remove
  // once root-caused.
  std::string log_path = std::string(pw->pw_dir) + "/fleetwm-session.log";
  int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (log_fd >= 0) {
    dup2(log_fd, STDOUT_FILENO);
    dup2(log_fd, STDERR_FILENO);
    if (log_fd > STDERR_FILENO) {
      close(log_fd);
    }
  }

  std::vector<std::string> env_storage = build_env(pw, pam_envlist);
  std::vector<char*> envp;
  envp.reserve(env_storage.size() + 1);
  for (std::string& e : env_storage) {
    envp.push_back(e.data());
  }
  envp.push_back(nullptr);

  char* const argv[] = {const_cast<char*>(kCompositorPath), nullptr};

  execve(kCompositorPath, argv, envp.data());

  // execve only returns on failure.
  std::perror("fleetwm-greet: execve of compositor failed");
  std::_Exit(1);
}

}  // namespace fleetwm::session
