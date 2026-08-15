#!/usr/bin/env python3
"""Generate C++-safe copies of the small set of wlroots headers that use
C99-only syntax (`const T x[static N]` array parameters, and a `class`
struct field name) which is a hard parse error under a C++ compiler.

Both constructs are declaration-only differences with no ABI impact --
`[static N]` is purely an optimizer hint that the pointer is non-null with
at least N elements, and C has no parameter-name mangling -- so rewriting
them is safe. This is the standard workaround used by other C++ wlroots
consumers (see docs/adr for the wlroots-vs-Smithay C++ interop rationale);
patching in-place isn't an option since these are system package headers.

Usage: patch-wlroots-cpp-headers.py <wlroots_include_dir> <output_dir>

Mirrors the wlroots header tree at <output_dir>/wlr/... : untouched headers
are copied as-is, the two known-broken ones are rewritten. Pointing the
compositor's include path at <output_dir> ahead of the real wlroots
include dir (via meson's include_directories(..., is_system: false) placed
first) makes '#include <wlr/...>' resolve to these patched copies.
"""
import re
import shutil
import sys
from pathlib import Path

# Relative to the wlroots include root.
PATCHED_FILES = {
    "wlr/types/wlr_scene.h": [
        (re.compile(r"const float color\[static 4\]"), "const float color[4]"),
    ],
    "wlr/xwayland/xwayland.h": [
        (re.compile(r"\bchar \*class;"), "char *class_;"),
    ],
}


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1

    src_root = Path(sys.argv[1])
    out_root = Path(sys.argv[2]) / "wlr"
    out_root.mkdir(parents=True, exist_ok=True)

    for rel_path, patches in PATCHED_FILES.items():
        src = src_root / rel_path
        dst = Path(sys.argv[2]) / rel_path
        dst.parent.mkdir(parents=True, exist_ok=True)

        text = src.read_text()
        for pattern, replacement in patches:
            text, count = pattern.subn(replacement, text)
            if count == 0:
                print(
                    f"warning: pattern {pattern.pattern!r} not found in {src} "
                    "-- wlroots header layout may have changed upstream; "
                    "the C++-incompatible construct this patches may have "
                    "moved or been fixed",
                    file=sys.stderr,
                )
        dst.write_text(text)

    return 0


if __name__ == "__main__":
    sys.exit(main())
