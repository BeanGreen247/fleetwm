#!/usr/bin/env bash
# Re-installs the login screen's PAM service so the login keyring is unlocked at login (see docs/GREETER.md, "Keyring"). A machine whose
# /etc/pam.d/fleetwm-greeter predates the keyring lines keeps asking for the keyring password when git, gh or a browser touches the Secret
# Service. Run it once with sudo available, then log out and back in. Run from anywhere: ./scripts/fix-keyring-pam.sh
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/packaging/fleetwm-greeter-pam.conf"
DST=/etc/pam.d/fleetwm-greeter
if cmp -s "${SRC}" "${DST}"; then echo "${DST} is already current"; else sudo install -m 644 "${SRC}" "${DST}" && echo "installed ${DST}"; fi
[[ -e /usr/lib/x86_64-linux-gnu/security/pam_gnome_keyring.so || -e /usr/lib64/security/pam_gnome_keyring.so ]] ||
  echo "warning: pam_gnome_keyring.so is missing: install libpam-gnome-keyring (Debian) or gnome-keyring-pam (Fedora)" >&2
echo "Log out and back in. If it still asks, the Login keyring's password differs from your login password: change it in Seahorse (Passwords and Keys, right-click Login, Change Password)."
