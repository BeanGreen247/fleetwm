#!/usr/bin/env bash
# Fleetwm install script -- Debian 13.6 / Ubuntu & Kubuntu 26.04.
#
# Builds from source and installs to /usr/local. Safe to re-run: ninja
# install is idempotent, and this script doesn't overwrite an existing
# ~/.config/fleetwm/theme.toml.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/install-ui.sh
UI_TOTAL_STEPS=12  # keep equal to the number of ui_step calls below
source "${SCRIPT_DIR}/scripts/install-ui.sh"
# shellcheck source=scripts/install-stats.sh
source "${SCRIPT_DIR}/scripts/install-stats.sh"
# scripts/build-pgo-auto.sh (invoked below, see "Building with PGO")
# always builds into build-pgo/, not build/ -- every later reference to
# BUILD_DIR in this script (recording the update path, checking for the
# greeter binary, etc.) has to point at the same directory the actual
# installed binaries were built from.
BUILD_DIR="${SCRIPT_DIR}/build-pgo"

# The script asks for sudo itself when it needs it. Running the whole thing as root
# leaves root-owned build files behind, which then break every later normal run.
if [[ ${EUID} -eq 0 ]]; then
  echo "error: run ./install.sh as your normal user, not with sudo or as root." >&2
  echo "       It asks for your password itself when it needs it." >&2
  exit 1
fi

ui_step "Checking your account and asking for permission once" \
  "What: confirms you are a normal user and asks for your sudo password a single time." \
  "Why:  the install changes system files (packages, /usr/local, login screen). Asking now, and keeping the" \
  "      permission alive, means no password prompt can pop up in the middle of a progress line later."
ui_sudo_keepalive || { echo "error: sudo permission is required." >&2; exit 1; }

# A build folder left behind by an earlier 'sudo' run is owned by root and makes
# the build fail with a permission error. Give it back to the current user.
for d in build-pgo build-test build; do
  p="${SCRIPT_DIR}/${d}"
  if [[ -e "${p}" ]] && [[ -n "$(find "${p}" ! -user "$(id -u)" -print -quit 2>/dev/null)" ]]; then
    echo "==> ${d}/ has files owned by another user (an earlier sudo run?); fixing ownership"
    sudo chown -R "$(id -u):$(id -g)" "${p}"
  fi
done

ui_step "Enabling Debian's non-free software sources" \
  "What: adds 'contrib', 'non-free' and 'non-free-firmware' to Debian's package sources (the original" \
  "      file is kept next to it as <file>.fleetwm-bak). Nothing changes if they are already enabled." \
  "Why:  the best graphics and video drivers (the Intel media driver with extra codecs, GPU firmware)" \
  "      are only published there. Other distributions skip this step."
# The best graphics and video drivers are in Debian's non-free parts (the Intel video driver
# with the extra codecs, GPU firmware). Make sure those repositories are enabled; nothing
# is changed when they already are, and the original file is kept as <file>.fleetwm-bak.
. /etc/os-release
if [[ "${ID:-}" == "debian" ]]; then
  for src in /etc/apt/sources.list.d/*.sources; do
    [[ -f "${src}" ]] && grep -q "debian.org" "${src}" && grep -qE "^Components:.* main" "${src}" || continue
    if grep -E "^Components:" "${src}" | grep -vq "non-free-firmware" || grep -E "^Components:" "${src}" | grep -qvE "(^|[[:space:]])non-free([[:space:]]|$)"; then
      echo "==> Enabling contrib, non-free and non-free-firmware in ${src}"
      sudo cp -n "${src}" "${src}.fleetwm-bak"
      sudo sed -i -E '/^Components:/ { s/[[:space:]]+(contrib|non-free-firmware|non-free)\b//g; s/$/ contrib non-free non-free-firmware/ }' "${src}"
    fi
  done
  if [[ -f /etc/apt/sources.list ]] && grep -E "^deb .*debian.*[[:space:]]main" /etc/apt/sources.list | grep -vq "non-free-firmware"; then
    echo "==> Enabling contrib, non-free and non-free-firmware in /etc/apt/sources.list"
    sudo cp -n /etc/apt/sources.list /etc/apt/sources.list.fleetwm-bak
    sudo sed -i -E '/^deb .*debian.*[[:space:]]main/ { s/[[:space:]]+(contrib|non-free-firmware|non-free)\b//g; s/[[:space:]]main\b/ main contrib non-free non-free-firmware/ }' /etc/apt/sources.list
  fi
fi

stats_phase_start deps
ui_step "Installing the compiler and the libraries Fleetwm is built from" \
  "What: refreshes the package lists, then installs the tools that compile the source and the" \
  "      libraries the compositor links against." \
  "Why:  Fleetwm is built from source on your machine, tuned for your exact CPU, which is part of why" \
  "      it can be faster than a generic prebuilt package."
ui_item "build-essential meson ninja git" "compiler and build system (ninja runs the compile in parallel)"
ui_item "libwlroots-0.18-dev" "wlroots: the window-system core the compositor is built on"
ui_item "wayland-protocols libwayland-dev" "the Wayland protocol the programs speak to each other"
ui_item "libinput libxkbcommon libdrm" "keyboard, mouse and touchpad input, key maps, direct screen access"
ui_item "libegl/libgles2 libpixman" "OpenGL ES drawing (GPU) and the software fallback"
ui_item "libpipewire libpam libsystemd" "sound volume, password check for the lock screen, session and power control"
ui_item "libjemalloc2" "a faster memory allocator for the compositor"
ui_live plain "Refreshing the package lists" "$(mktemp)" sudo apt-get update -qq

# NOTE: this is deliberately several separate `apt-get install` calls,
# not one big backslash-continued list -- a `#` comment on its own line
# in the middle of a backslash-continued command silently ends that
# command right there (the comment consumes the rest of its own physical
# line, and since that line has no trailing backslash, nothing continues
# it), so anything listed after such a comment would instead run as its
# own bare, immediate command (e.g. `xwayland`) and fail with "command
# not found" -- fatal here since `set -euo pipefail` is on. Confirmed
# this the hard way: an earlier version of this file had exactly that
# shape and would have failed a truly fresh install.
apt_install \
  build-essential meson ninja-build pkg-config git \
  libwlroots-0.18-dev wayland-protocols libwayland-dev \
  libinput-dev libdrm-dev libxkbcommon-dev libpixman-1-dev \
  libegl1-mesa-dev libgles2-mesa-dev \
  libpipewire-0.3-dev \
  libpam0g-dev \
  libsystemd-dev \
  libjemalloc2

ui_step "Installing drawing, font and cursor support" \
  "What: cairo (draws every window decoration, bar and menu), image decoders for wallpapers and icons," \
  "      the Inter font, a fallback font and a mouse pointer theme." \
  "Why:  Fleetwm's own programs use no big toolkit (no GTK/Qt): they draw directly with cairo, which keeps" \
  "      them small, quick to start and light on memory."
# the GTK-free "fleetkit" clients (src/fleetkit: wallpaper, locker, power menu, bar,
# launcher, audio mixer) draw with cairo and decode images with libpng /
# libjpeg / libwebp (SVG is handled by the vendored nanosvg); fontconfig
# resolves cairo's "Inter" family (it falls back to the system sans when missing), so install Inter plus a default font
apt_install libcairo2-dev libpng-dev libjpeg-dev libwebp-dev fonts-inter fonts-dejavu-core
# A mouse cursor theme. Without one there is no pointer image to draw (the compositor
# has a built-in fallback arrow, but apps load their own); best effort, since the
# package name differs between distributions.
apt_install dmz-cursor-theme || echo "warning: no cursor theme package installed; the built-in pointer will be used" 

ui_step "Installing graphics and video drivers" \
  "What: the Mesa drivers for Intel, AMD and NVIDIA (open Nouveau) GPUs, Vulkan, and video acceleration" \
  "      (VA-API) plus the matching firmware for the GPU found in this machine." \
  "Why:  the compositor draws every frame on the GPU. Without a GPU driver it falls back to software" \
  "      drawing, which is many times slower, and video would play on the CPU."
# Graphics drivers (Mesa) and Vulkan. The compositor renders with GLES2 and falls back to
# software when no GPU driver loads, so the Mesa drivers decide how fast everything feels.
# These are the userspace drivers for Intel, AMD and Nouveau GPUs (mesa-vulkan-drivers holds
# the Vulkan ones: anv for Intel, radv for AMD, nvk for NVIDIA through Nouveau), the
# VA-API video drivers, and vulkaninfo for checking that Vulkan works.
# Best effort: a package missing on some distribution must not stop the install.
apt_install libgl1-mesa-dri libegl-mesa0 libgbm1 libvulkan1 mesa-vulkan-drivers \
  mesa-va-drivers vulkan-tools vainfo ||
  echo "warning: some Mesa/Vulkan packages could not be installed; the compositor falls back to software rendering if no GPU driver loads"
# The video driver that matches the GPU found in sysfs (0x8086 Intel, 0x1002 AMD, 0x10de NVIDIA).
for vendor_file in /sys/class/drm/card*/device/vendor; do
  [[ -r "${vendor_file}" ]] || continue
  case "$(cat "${vendor_file}")" in
    0x8086)
      # The non-free media driver supports more codecs and is the faster one; it replaces the
      # free package if that is installed. i965-va-driver covers GPUs older than Broadwell.
      apt_install intel-media-va-driver-non-free i965-va-driver ||
        apt_install intel-media-va-driver i965-va-driver ||
        echo "warning: no Intel video acceleration driver installed"
      # The i915 GuC/HuC firmware (video and scheduling offload). A separate, optional step: some
      # bases (Armbian's full firmware package) conflict with other firmware packages.
      apt_install firmware-intel-graphics ||
        echo "warning: firmware-intel-graphics not installed (a conflicting firmware package may be in the way)" ;;
    0x1002)
      apt_install firmware-amd-graphics ||
        echo "warning: firmware-amd-graphics not available (needs the non-free-firmware repository)" ;;
    0x10de)
      echo "==> NVIDIA GPU: using the open Nouveau driver (Mesa); install NVIDIA's own driver separately if you need it" ;;
  esac
done

ui_step "Installing dark/light theme support for other programs" \
  "What: the desktop portal, Adwaita dark theme, and the Qt theme plugins that follow GTK." \
  "Why:  when you switch the Fleetwm theme between light and dark, Chromium, Thunar, Qt and GTK programs" \
  "      read that setting from these pieces and switch with it."
# Dark/light mode for other toolkits: the portal (settings backend that Chromium and
# libadwaita read), an Adwaita dark theme for GTK 3, the Qt platform themes that follow GTK,
# and the D-Bus pieces that start the portal. Best effort.
apt_install xdg-desktop-portal xdg-desktop-portal-gtk gnome-themes-extra \
  qt5-gtk-platformtheme qt6-gtk-platformtheme dbus-user-session dbus-bin libglib2.0-bin \
  gsettings-desktop-schemas dconf-gsettings-backend ||
  echo "warning: some theme packages could not be installed; GTK, Chromium and Qt apps may not follow dark/light mode"

ui_step "Installing sound and power-control permissions" \
  "What: PipeWire and WirePlumber (sound), polkit and the time-zone permission rule." \
  "Why:  the bar's volume readout and the audio mixer talk to PipeWire; the power menu (suspend, reboot," \
  "      shut down) and Date & Time settings are refused by the system without polkit."
# runtime audio stack the bar's volume readout and fleetwm-audiomixer talk
# to (PipeWire + the WirePlumber session manager; pipewire-bin ships
# pw-cli/pw-cat, handy for testing without sound hardware)
apt_install pipewire pipewire-bin wireplumber

# runtime dependency for the bar's power menu (fleetwm-powermenu):
# systemd-logind refuses Sleep/Reboot/Shut down for a non-root caller
# without a running polkit to authorize the request, regardless of
# session state -- fails with "Access denied" even from an active
# session. Log out doesn't need this (ending your own session needs no
# authorization), and sudo bypasses it too (root needs no polkit
# check), which is why those two paths could look like they worked
# while this one silently didn't on a minimal install that never
# pulled polkit in as a transitive dependency of anything else.
# Package name is "polkitd" (Debian 13/trixie) -- the older
# "policykit-1" transitional package no longer exists there; both work
# on Ubuntu 26.04.
apt_install polkitd pkexec

# fleetwm-settings' Date & Time tab changes the system time zone and the
# automatic-time (NTP) switch through timedatectl. Without a polkit
# authentication agent (fleetwm has none) that is refused with "Interactive
# authentication required", so allow it for administrators in an active
# local session. Delete the file to go back to prompting.
sudo install -m 644 "${SCRIPT_DIR}/packaging/50-fleetwm-time.rules" /etc/polkit-1/rules.d/50-fleetwm-time.rules

ui_step "Installing everyday programs and helpers" \
  "What: XWayland (runs older X11 programs inside Fleetwm), the foot terminal, grim/slurp/wl-clipboard" \
  "      (Alt+Shift+S screenshots), wlrctl/wtype/gdb/imagemagick (testing over SSH) and the tools the" \
  "      training run in the build needs (dbus-daemon, python3)." \
  "Why:  without XWayland X11 programs will not start; without foot there is no terminal; without the" \
  "      screenshot tools the screenshot shortcut does nothing."
apt_install xwayland foot

# end-user runtime: Alt+Shift+S's screenshot keybind
# (compositor/input.cpp's kScreenshotCommand) -- grim captures, slurp
# picks the region, wl-copy puts it on the clipboard, notify-send
# confirms it
apt_install grim slurp wl-clipboard libnotify-bin

# dev-box testing: pixel inspection (imagemagick's `convert ...
# txt:-`) + synthetic pointer/keyboard input (wlrctl/wtype) over SSH,
# since fleetwm advertises wlr-screencopy-v1, wlr-virtual-pointer-v1,
# and wlr-virtual-keyboard-v1 for exactly this; gdb for live-attaching
# to the compositor to catch crashes
apt_install wlrctl wtype gdb imagemagick

# build-time only: scripts/build-pgo-auto.sh's synthetic PGO training
# pass (now run unconditionally below, see "Building with PGO") needs
# `dbus-run-session` (an isolated session bus, so the training run's
# programs don't talk to a real desktop session's bus instead of
# doing any work) and python3 (already present on every
# supported distro here, listed for completeness).
apt_install dbus-daemon python3

stats_phase_end deps
ui_step "Setting the system language" \
  "What: sets the default locale to C.UTF-8." \
  "Why:  it exists on every system and handles all characters, so text in the terminal and in" \
  "      Fleetwm's programs never breaks because a language pack was never generated."
# fleetwm-greet's session env (src/greeter/session.cpp) also hardcodes
# this as a floor for every fleetwm session regardless of the system
# default, but setting it here too keeps outside-of-fleetwm logins (a
# plain TTY, SSH) consistent instead of inheriting whatever partial/
# unavailable locale (e.g. a language locale that was never actually
# generated on this machine) came from the base install. C.UTF-8 rather
# than a real language locale since it's guaranteed present on every
# glibc system with no locale-gen step required.
sudo update-locale LANG=C.UTF-8 LC_ALL=C.UTF-8 LANGUAGE=

ui_step "Building Fleetwm (optimized, in three stages)" \
  "What: stage 1 compiles an instrumented copy, stage 2 runs it for a short training session that records" \
  "      which code is used most, stage 3 recompiles using that record. The unit tests run after each" \
  "      compile and must pass before anything is installed." \
  "Why:  this profile-guided build makes the hot paths (drawing, window moves, input) faster. It is the" \
  "      slowest part of the install: the compile counter and the timer keep moving even during the" \
  "      long link step at the end of each stage."
# Every install now goes through the full instrumented-build ->
# synthetic-training -> profile-optimized-rebuild pipeline
# (scripts/build-pgo-auto.sh), not just a single release build --
# mandatory, not opt-in, per explicit user request. This takes longer
# than a plain build (compiles twice, plus a training pass -- budget an
# extra minute or two on top of a normal build), but every installed
# binary ends up profile-guided rather than only the ones someone
# happened to PGO-train by hand. See build-pgo-auto.sh/build-pgo.sh/
# pgo-train-session.sh for the full flag rationale (LTO, -march=native,
# -DG_DISABLE_ASSERT, full RELRO, etc. -- all still applied, PGO is
# layered on top of the same release-build flags this script used
# before) and exactly what the synthetic training pass exercises.
#
# Both the instrumented and final builds run the unit test suite
# themselves (build-pgo.sh runs the fleetwm-unit-tests binary directly,
# so you see every one of the 240+ individual test cases run, not just
# meson's single-line wrapper; `set -euo pipefail` propagates any
# failure) -- a failing test aborts here, before any installed binary is
# touched, same "tests gate the install" contract as before.
STATS_BUILD_CPU0=$(stats_cpu_seconds)
bash "${SCRIPT_DIR}/scripts/build-pgo-auto.sh"
STATS_BUILD_CPU=$(awk -v a="${STATS_BUILD_CPU0}" -v b="$(stats_cpu_seconds)" 'BEGIN { printf "%.0f", b - a }')

stats_phase_start install
ui_step "Installing Fleetwm and setting up your account" \
  "What: copies the programs to /usr/local, adds the Fleetwm entry to the login session list, lets the" \
  "      compositor run at higher priority, and adds you to the input/video/render/audio groups." \
  "Why:  the groups give the compositor access to your keyboard, mouse, screen and sound; the priority" \
  "      keeps the desktop responsive when the machine is busy. Log out and back in afterwards."
sudo ninja -C "${BUILD_DIR}" install
# 'sudo ninja' can leave root-owned files in the build folder; hand them back so the
# next run (or fleetwm-update) can rebuild without a permission error.
sudo chown -R "$(id -u):$(id -g)" "${BUILD_DIR}"

# The install prefix is /usr/local, but display managers (GDM, SDDM, LightDM) list the
# sessions in /usr/share/wayland-sessions, so put the session entry there too.
if [[ -f /usr/local/share/wayland-sessions/fleetwm.desktop ]]; then
  echo "==> Adding the Fleetwm session to /usr/share/wayland-sessions"
  sudo install -D -m 644 /usr/local/share/wayland-sessions/fleetwm.desktop \
    /usr/share/wayland-sessions/fleetwm.desktop
fi

echo "==> Letting the compositor run at higher priority (smoother, lower input delay)"
# Members of the video group may raise their priority to nice -10; the compositor asks for it.
printf '# Fleetwm: lets the compositor run at higher priority\n@video - nice -10\n' |
  sudo tee /etc/security/limits.d/fleetwm.conf >/dev/null

echo "==> Recording source checkout path for 'fleetwm update'"
echo "${SCRIPT_DIR}" | sudo tee /etc/fleetwm-source-path >/dev/null

echo "==> Adding $(whoami) to device-access groups (input/video/render/audio)"
# The compositor needs libinput device access (input), DRM/GPU access
# (video, and render on distros that split it out), and PipeWire/audio
# device access (audio) to function -- without these, keyboard/mouse
# events or rendering can silently fail with no visible error, since
# open() on a permission-denied /dev node just makes libinput/wlroots see
# no device rather than raising an obvious error. plugdev covers some
# USB peripherals (e.g. certain webcams/removable media) on Debian-family
# systems that gate them separately from video. Only add groups that
# actually exist on this system -- not all of these exist on every distro
# or hardware configuration.
for group in input video render audio plugdev; do
  if getent group "${group}" >/dev/null 2>&1; then
    sudo usermod -aG "${group}" "$(whoami)"
  fi
done
echo "(If any of these groups were newly added, log out and back in --"
echo "or reboot -- for group membership to take effect.)"

if [[ -x "${BUILD_DIR}/src/greeter/fleetwm-greet" ]]; then
  # fleetwm-greet now runs its own wlroots compositor for the login UI
  # (src/greeter/compositor.{hpp,cpp}), which needs a seat backend to get
  # DRM access before anyone's logged in -- seatd, not logind (see
  # packaging/fleetwm-greeter@.service's own comment for why logind
  # specifically doesn't work here: it segfaults on a real login).
  ui_step "Setting up the login screen" \
    "What: installs seatd, the login screen's PAM files and service, and enables it on tty1 (unless a" \
    "      display manager is already in charge or FLEETWM_NO_GREETER=1 is set)." \
    "Why:  the login screen runs its own small compositor, which needs seatd to open the screen before" \
    "      anyone is logged in. Other consoles keep a normal text login in case you need it."
  apt_install seatd
  sudo systemctl enable --now seatd.service

  echo "==> Installing greeter PAM config and systemd unit"
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-greeter-pam.conf" /etc/pam.d/fleetwm-greeter
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-greeter@.service" /usr/lib/systemd/system/fleetwm-greeter@.service
  sudo systemctl daemon-reload

  # The Fleetwm login screen is the default way in: enable it on tty1. It takes
  # over at the next boot (or when you start it by hand, below); starting it right
  # now would replace the console this installer is running on. Skipped when a
  # display manager is already enabled (they would fight over the screen), or when
  # you set FLEETWM_NO_GREETER=1.
  GREETER_TTY="${FLEETWM_GREETER_TTY:-tty1}"
  GREETER_ENABLED=0
  if [[ "${FLEETWM_NO_GREETER:-0}" == "1" ]]; then
    echo "==> FLEETWM_NO_GREETER=1: leaving the login screen disabled"
  elif systemctl is-enabled display-manager.service >/dev/null 2>&1; then
    echo "==> A display manager is enabled; leaving the Fleetwm login screen disabled."
    echo "    Pick 'Fleetwm' from its session list instead."
  else
    echo "==> Enabling the Fleetwm login screen on ${GREETER_TTY}"
    sudo systemctl disable "getty@${GREETER_TTY}.service" 2>/dev/null || true
    sudo systemctl enable "fleetwm-greeter@${GREETER_TTY}.service"
    # The unit is wanted by graphical.target, so make sure that is what boots.
    if [[ "$(systemctl get-default)" != "graphical.target" ]]; then
      echo "==> Setting the default boot target to graphical.target"
      sudo systemctl set-default graphical.target
    fi
    GREETER_ENABLED=1
  fi

  # fleetwm-locker (the Lock power-menu action) re-verifies the running
  # user's password via its own PAM service -- separate from
  # fleetwm-greeter's above since it never opens a session (pam_unix +
  # pam_systemd's session lines make no sense for re-auth of an
  # already-running session).
  echo "==> Installing locker PAM config"
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-locker-pam.conf" /etc/pam.d/fleetwm-locker
else
  ui_step "Setting up the login screen" \
    "Skipped: this build was made without the greeter (-Dgreeter=false)."
fi

stats_phase_end install
stats_summary "${SCRIPT_DIR}" "${BUILD_DIR}"

echo "Fleetwm installed. Log out and select 'Fleetwm' from your display"
echo "manager's session list to start using it."
echo "Run 'fleetwm update' at any time to pull and rebuild the latest version."

if [[ "${GREETER_ENABLED:-0}" == "1" ]]; then
  echo
  echo "The Fleetwm login screen is enabled on ${GREETER_TTY}. Reboot to see it, or start it now"
  echo "(this replaces the login on that console):"
  echo "  sudo systemctl start fleetwm-greeter@${GREETER_TTY}.service"
  echo "Other consoles (Ctrl+Alt+F2 and up) keep a normal text login in case you need it."
  echo "To go back to the normal login:"
  echo "  sudo systemctl disable --now fleetwm-greeter@${GREETER_TTY}.service"
  echo "  sudo systemctl enable --now getty@${GREETER_TTY}.service"
elif [[ -x "${BUILD_DIR}/src/greeter/fleetwm-greet" ]]; then
  echo
  echo "The Fleetwm login screen (fleetwm-greet) is installed but not enabled. To use it on tty1:"
  echo "  sudo systemctl disable --now getty@tty1.service"
  echo "  sudo systemctl enable --now fleetwm-greeter@tty1.service"
fi
