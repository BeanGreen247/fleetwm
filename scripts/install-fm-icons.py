#!/usr/bin/env python3
"""Meson install script: writes the file manager's application icon (drawn in code by `fleetwm-fm --write-icon`) into the hicolor
icon theme at the install prefix, one PNG per size. Usage: install-fm-icons.py PATH/TO/fleetwm-fm SIZE [SIZE ...]"""
import os
import subprocess
import sys

exe = sys.argv[1]
sizes = sys.argv[2:]
prefix = os.environ.get("MESON_INSTALL_DESTDIR_PREFIX", "")
for size in sizes:
    out_dir = os.path.join(prefix, "share", "icons", "hicolor", f"{size}x{size}", "apps")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "fleetwm-fm.png")
    subprocess.run([exe, "--write-icon", out, size], check=True)
    print(f"Installing {out}")
