#!/usr/bin/env python3
"""Finds names that two source files of the same directory define at file scope.

The installer builds with -Dunity=on, which pastes a target's .cpp files into one translation unit.
Two files that each define `constexpr int kPad` (or a helper function, struct or alias) inside an
anonymous namespace compile fine alone and fail once merged. This is the check that would have
caught the debug overlay's `kPad` clashing with the Alt+Tab switcher's, before a release.

  scripts/check-unity-collisions.py [ROOT]     scan ROOT (default: the repository)
  scripts/check-unity-collisions.py --self-test

Heuristic and deliberately a little over-eager: files of one directory are treated as one group, and
only names declared directly inside an anonymous namespace (or `static` at file scope) count. Exit
status 1 when any name is defined in more than one file of a group. scripts/check-unity.sh is the
real thing (an actual unity compile); this one is fast enough to run on every test run.
"""
import os
import re
import sys
from collections import defaultdict

SKIP_DIRS = {"build", "subprojects", "third_party", ".git", "protocols"}
KEYWORDS = {"if", "for", "while", "switch", "return", "else", "do", "catch", "sizeof", "case", "using",
            "namespace", "template", "typedef", "operator", "new", "delete", "throw", "static_assert"}
DECL_START = re.compile(r"^\s*(?:static\s+)?(?:inline\s+)?(?:constexpr|const)\b")
TYPE_DECL = re.compile(r"^\s*(?:struct|class|enum(?:\s+class)?|union)\s+([A-Za-z_]\w*)\b(?!\s*;)")
USING_DECL = re.compile(r"^\s*using\s+([A-Za-z_]\w*)\s*=")
FUNC_ONE = re.compile(r"^\s*(?:static\s+|inline\s+)*[\w:<>,\*&\s]+?[\s\*&]([A-Za-z_]\w*)\s*\([^)]*\)\s*(?:const\s*)?\{")
FUNC_DEF = re.compile(r"^\s*(?:static\s+|inline\s+)*[\w:<>,\*&\s]+?[\s\*&]([A-Za-z_]\w*)\s*\([^;]*$")


def strip(text):
    """Blank out comments and string/char literals, keep line structure."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def names_in(text):
    """Names defined at file scope in anonymous namespaces (plus file-scope `static`) of one file."""
    found = set()
    depth = 0
    anon_stack = []  # brace depths at which an anonymous namespace was opened
    pending = ""
    for raw in strip(text).splitlines():
        line = raw.rstrip()
        stripped = line.strip()
        at_scope = bool(anon_stack) and depth == anon_stack[-1] + 1
        file_scope_static = depth == 0 or (not anon_stack and re.match(r"^\s*static\b", line) and depth <= 1)
        if re.match(r"^\s*namespace\s*\{", line):
            anon_stack.append(depth)
        elif at_scope or (stripped.startswith("static") and depth == 0):
            if pending or DECL_START.match(line) or stripped.startswith("static"):
                pending += " " + stripped
                if ";" in line or "{" in line:
                    stmt = pending
                    pending = ""
                    if "(" in stmt.split("=")[0] and "constexpr" not in stmt.split("(")[0] and re.search(r"\)\s*(const)?\s*(\{|$)", stmt):
                        m = FUNC_DEF.match(stmt)
                        if m and m.group(1) not in KEYWORDS:
                            found.add(m.group(1))
                    else:
                        for m in re.finditer(r"\b([A-Za-z_]\w*)\s*(?==|\{|\[)", stmt):
                            if m.group(1) not in KEYWORDS and not re.match(r"(constexpr|const|static|inline|int|double|float|bool|char|size_t|long|unsigned|auto)$", m.group(1)):
                                found.add(m.group(1))
            else:
                m = TYPE_DECL.match(line) or USING_DECL.match(line)
                if m:
                    found.add(m.group(1))
                elif "(" in line and not stripped.endswith(";") and not stripped.startswith(("#", "}", "return")):
                    m = FUNC_ONE.match(line) or FUNC_DEF.match(line)
                    if m and m.group(1) not in KEYWORDS:
                        found.add(m.group(1))
        depth += line.count("{") - line.count("}")
        while anon_stack and depth <= anon_stack[-1]:
            anon_stack.pop()
    return found


def scan(root):
    groups = defaultdict(lambda: defaultdict(list))  # dir -> name -> [files]
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith("build")]
        for f in sorted(files):
            if f.endswith(".cpp"):
                path = os.path.join(base, f)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    for name in names_in(fh.read()):
                        groups[base][name].append(f)
    clashes = []
    for base, names in sorted(groups.items()):
        for name, files in sorted(names.items()):
            if len(files) > 1:
                clashes.append((os.path.relpath(base, root), name, files))
    return clashes


def self_test():
    a = "namespace fleetwm {\nnamespace {\nconstexpr int kPad = 20, kGap = 14;\nstruct Slot {\n  int x;\n};\nint helper(int v) {\n  return v;\n}\n}  // namespace\n}\n"
    b = "namespace fleetwm {\nnamespace {\nconstexpr int kPad = 8;\nint other() { return 1; }\n}\nvoid pub() { int kPad = 3; }\n}\n"
    c = "namespace {\nconstexpr int kUnique = 1;\n// constexpr int kPad = 99;\nconst char* s = \"constexpr int kPad = 5;\";\n}\n"
    na, nb, nc = names_in(a), names_in(b), names_in(c)
    assert {"kPad", "kGap", "Slot", "helper"} <= na, na
    assert {"kPad", "other"} <= nb and "pub" not in nb, nb
    assert "kPad" not in nc and "kUnique" in nc, nc
    d = "namespace {\nstd::string trim(const std::string& s) {\n  return s;\n}\nusing Clock = int;\nstatic int counter = 0;\n}\n"
    nd = names_in(d)
    assert {"trim", "Clock", "counter"} <= nd, nd
    print("check-unity-collisions: self-test ok")


def main(argv):
    if len(argv) > 1 and argv[1] == "--self-test":
        self_test()
        return 0
    root = argv[1] if len(argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    clashes = scan(root)
    for directory, name, files in clashes:
        print(f"unity clash in {directory}: '{name}' is defined in {', '.join(files)}")
    if clashes:
        print("\nTwo files of one target define the same file-scope name; the installer's unity build "
              "merges them and fails. Rename one (a per-file prefix works), or share it from a header.")
        return 1
    print("check-unity-collisions: no clashes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
