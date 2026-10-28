#!/usr/bin/env bash
# Fleetwm installer for Fedora, Nobara and other dnf systems (Fedora 41 or newer: wlroots 0.18 to 0.20, glibc, systemd). Safe to re-run.
# Builds from source with release flags and installs; it does NOT replace your display manager or install the Fleetwm login screen (that is install.sh on Debian):
# Fleetwm appears as a session ("Fleetwm") in SDDM, GDM or any other login manager that lists /usr/local/share/wayland-sessions, or start it from a console with `dbus-run-session fleetwm`.
#
#   FLEETWM_PREFIX=/usr/local   where to install (default /usr/local; a path in your home needs no sudo)
#   FLEETWM_SKIP_DEPS=1         do not run dnf (the build dependencies are already there)
#   FLEETWM_RUNTIME=0           skip the optional runtime programs (terminal, screenshots, fonts, audio, polkit)
#   FLEETWM_TESTS=1             also build and run the unit tests (scripts/run-tests.sh) before installing
#   FLEETWM_GREETER=1           also install the Fleetwm login screen (PAM service with keyring unlock, systemd unit, seatd) but leave it disabled; prints how to switch to it
#   FLEETWM_GREETER=enable      the same, then enable it on the console (FLEETWM_GREETER_TTY, default tty2) and disable the display manager for the next boot (never stops it)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${FLEETWM_PREFIX:-/usr/local}"
BUILD_DIR="${FLEETWM_BUILD_DIR:-${SCRIPT_DIR}/build}"

. /etc/os-release
case "${ID:-} ${ID_LIKE:-}" in *fedora*|*rhel*|*nobara*) ;; *) echo "error: this script is for Fedora and its relatives (found ${PRETTY_NAME:-unknown}); use install.sh on Debian and Ubuntu" >&2; exit 1 ;; esac
[[ ${EUID} -eq 0 ]] && echo "note: running as root; the build and the files in your home will belong to root" >&2

# a prefix inside the user's own home needs no sudo for the install step
SUDO=sudo
if [[ ${EUID} -eq 0 ]] || { mkdir -p "${PREFIX}" 2>/dev/null && [[ -w ${PREFIX} ]]; }; then SUDO=""; fi

if [[ ${FLEETWM_SKIP_DEPS:-0} != 1 ]]; then
  echo "==> Installing the compiler and the libraries (dnf)"
  bash "${SCRIPT_DIR}/scripts/install-deps-fedora.sh"
  if [[ ${FLEETWM_RUNTIME:-1} != 0 ]]; then
    echo "==> Installing the programs Fleetwm uses at run time (dnf)"
    # best effort, one at a time: a package that does not exist on this release must not stop the rest
    for pkg in xorg-x11-server-Xwayland foot grim slurp wl-clipboard wtype rsms-inter-fonts dejavu-sans-fonts \
               pipewire pipewire-pulseaudio wireplumber alsa-utils power-profiles-daemon polkit udisks2 gvfs xdg-utils xdg-desktop-portal-gtk \
               gnome-keyring NetworkManager bluez mesa-dri-drivers mesa-vulkan-drivers gamemode; do
      sudo dnf install -y --setopt=install_weak_deps=False "${pkg}" >/dev/null 2>&1 || echo "  (not installed: ${pkg})"
    done
  fi
fi

# A build folder left with root-owned files (an earlier `sudo ninja install` rewrites .ninja_deps) stops ninja with "Error writing to deps log:
# Permission denied": hand the folders back to the user before building.
for dir in "${BUILD_DIR}" "${BUILD_DIR}-pgo"; do
  if [[ -d ${dir} ]] && [[ -n $(find "${dir}" -user root -print -quit 2>/dev/null) ]]; then
    echo "==> Giving ${dir} back to $(id -un) (root-owned files from an earlier install)"
    sudo chown -R "$(id -u):$(id -g)" "${dir}"
  fi
done

# Always the profile-guided build (instrumented build, a training run on a virtual screen, optimized build): there is no plain-build path, so an
# installed Fleetwm is never the slower one.
echo "==> Profile-guided build (scripts/build-pgo-auto.sh): instrumented build, training run on a virtual screen, optimized build"
for tool in wtype wlrctl foot grim wpctl pw-cli; do command -v "${tool}" >/dev/null 2>&1 || sudo dnf install -y --setopt=install_weak_deps=False "$([[ ${tool} == wpctl ]] && echo wireplumber || { [[ ${tool} == pw-cli ]] && echo pipewire-utils || echo ${tool}; })" >/dev/null 2>&1 || true; done
BUILD_DIR="${BUILD_DIR}-pgo"
FLEETWM_PGO_DIR="${BUILD_DIR}" FLEETWM_PREFIX="${PREFIX}" bash "${SCRIPT_DIR}/scripts/build-pgo-auto.sh"

if [[ ${FLEETWM_TESTS:-0} == 1 ]]; then
  echo "==> Unit tests"
  bash "${SCRIPT_DIR}/scripts/run-tests.sh"
fi

echo "==> Installing to ${PREFIX}"
# meson install --no-rebuild: root must not run ninja inside the build folder (it rewrites .ninja_deps as root and the next build fails)
${SUDO} meson install -C "${BUILD_DIR}" --no-rebuild >/dev/null
if [[ ${SUDO} == sudo ]]; then sudo chown -R "$(id -u):$(id -g)" "${BUILD_DIR}"; fi

echo "==> Pointer: GTK programs use Fleetwm's own cursor theme, so the cursor is the same in every window"
if [[ ${EUID} -eq 0 && -n ${SUDO_USER:-} ]]; then sudo -u "${SUDO_USER}" -H bash "${SCRIPT_DIR}/scripts/set-cursor-theme.sh" || true
else bash "${SCRIPT_DIR}/scripts/set-cursor-theme.sh" || true; fi

echo "==> Login session entry"
SESSION_DIR="${PREFIX}/share/wayland-sessions"
if [[ -f ${SESSION_DIR}/fleetwm.desktop ]]; then echo "installed: ${SESSION_DIR}/fleetwm.desktop"; fi
if [[ ${PREFIX} == /usr/local && ! -e /usr/share/wayland-sessions/fleetwm.desktop ]]; then
  # SDDM and GDM on Fedora read /usr/share/wayland-sessions; some versions skip /usr/local/share. Link it when the directory exists.
  if [[ -d /usr/share/wayland-sessions ]] && ${SUDO:-sudo} ln -sf "${SESSION_DIR}/fleetwm.desktop" /usr/share/wayland-sessions/fleetwm.desktop 2>/dev/null; then
    echo "linked into /usr/share/wayland-sessions"
  fi
fi

echo "==> Lock screen password check (PAM service fleetwm-locker)"
sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-locker-pam.conf" /etc/pam.d/fleetwm-locker

if [[ -n ${FLEETWM_GREETER:-} && ${FLEETWM_GREETER} != 0 ]]; then
  echo "==> Fleetwm login screen (experimental on Fedora: written from the Debian one, not yet tried at a real boot)"
  sudo dnf install -y --setopt=install_weak_deps=False seatd gnome-keyring gnome-keyring-pam jemalloc >/dev/null 2>&1 || echo "  (some greeter packages could not be installed)"
  TTY_NAME="${FLEETWM_GREETER_TTY:-tty2}"
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-greeter-pam.conf" /etc/pam.d/fleetwm-greeter
  # the unit starts /usr/local/bin/fleetwm-greet; point it at the prefix that was really used
  sed "s#/usr/local/bin/fleetwm-greet#${PREFIX}/bin/fleetwm-greet#" "${SCRIPT_DIR}/packaging/fleetwm-greeter@.service" | sudo tee /usr/lib/systemd/system/fleetwm-greeter@.service >/dev/null
  sudo systemctl daemon-reload
  sudo systemctl enable --now seatd.service
  if [[ ${FLEETWM_GREETER} == enable ]]; then
    for dm in sddm gdm lightdm greetd; do
      if systemctl is-enabled --quiet "${dm}.service" 2>/dev/null; then sudo systemctl disable "${dm}.service" && echo "disabled ${dm} for the next boot (it keeps running now)"; fi
    done
    sudo systemctl disable "getty@${TTY_NAME}.service" 2>/dev/null || true
    sudo systemctl enable "fleetwm-greeter@${TTY_NAME}.service"
    echo "The Fleetwm login screen takes over ${TTY_NAME} at the next boot. To go back: sudo systemctl disable fleetwm-greeter@${TTY_NAME} && sudo systemctl enable sddm (or your display manager) getty@${TTY_NAME}"
  else
    echo "Installed, not enabled. To use it:  sudo systemctl disable sddm;  sudo systemctl disable getty@${TTY_NAME};  sudo systemctl enable fleetwm-greeter@${TTY_NAME}  (or rerun with FLEETWM_GREETER=enable)"
  fi
  echo "Keyring: if the keyring still asks for a password after login, run scripts/fix-keyring-pam.sh and make the Login keyring's password the same as your login password."
fi

echo "==> Group memberships for input and GPU"
for group in input video render; do
  if getent group "${group}" >/dev/null && ! id -nG | tr ' ' '\n' | grep -qx "${group}"; then sudo usermod -aG "${group}" "$(id -un)" 2>/dev/null && echo "added you to ${group}"; fi
done

cat <<EOF

Fleetwm is installed in ${PREFIX}.
  * Pick the "Fleetwm" session at the login screen, or from a console:  dbus-run-session ${PREFIX}/bin/fleetwm
  * Games: scripts/gaming-tune.sh (optional), Settings -> Gaming; docs/GAMING.md
  * Profiling: scripts/setup-hotspot.sh; docs/HOTSPOT_PROFILING.md
EOF
