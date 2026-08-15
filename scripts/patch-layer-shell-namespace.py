#!/usr/bin/env python3
"""Rewrites the `namespace` identifier in wayland-scanner's generated
wlr-layer-shell-unstable-v1 server header to `namespace_`.

`namespace` is a valid C identifier but a reserved C++ keyword; the
generated header declares `const char *namespace` (parameter) and
`char *namespace;` (struct field), both hard parse errors under a C++
compiler. See the comment in protocols/meson.build for the full
rationale -- this script is invoked as a custom_target command chained
on wayland-scanner's raw output, matching the same C-vs-C++ workaround
scripts/patch-wlroots-cpp-headers.py already uses for wlroots' own
headers.

Usage: patch-layer-shell-namespace.py <input_header> <output_header>
"""
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1

    with open(sys.argv[1]) as f:
        text = f.read()
    patched = text.replace("*namespace)", "*namespace_)").replace(
        "*namespace;", "*namespace_;"
    )
    with open(sys.argv[2], "w") as f:
        f.write(patched)
    return 0


if __name__ == "__main__":
    sys.exit(main())
