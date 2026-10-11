#!/usr/bin/env python3
"""Keeps the language catalogs and the interface strings in step.

Every string the interface shows is written in English at the place it is
drawn, wrapped in Tr("..."). A catalog is a JSON document under data/lang whose
"strings" object maps that English source to the translation.

    python3 tools/lang.py list [file]     print every source string
    python3 tools/lang.py check [file]    report what a catalog is missing and
                                          what it carries that no longer exists

`check` exits non zero when a catalog is out of step, so it can be run in CI;
`list` prints the keys in the order they are found.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = sorted((ROOT / "src").rglob("*.cpp")) + sorted((ROOT / "src").rglob("*.h"))
DEFAULT_CATALOG = ROOT / "data" / "lang" / "zh-Hans.json"

# Tr("..."), which may be a run of adjacent literals
CALL = re.compile(r'Tr\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')
LITERAL = re.compile(r'"(?:[^"\\]|\\.)*"')


def unescape(text):
    return text.encode("utf-8").decode("unicode_escape").encode("latin-1", "backslashreplace").decode("utf-8")


def decode(run):
    """The value of a C++ string literal run, without its quotes."""
    return run.replace("\\n", "\n").replace("\\t", "\t").replace('\\"', '"').replace("\\\\", "\\")


def sources():
    found = []
    seen = set()

    def add(value):
        if value and value not in seen:
            seen.add(value)
            found.append(value)

    for path in SOURCES:
        text = path.read_text(encoding="utf-8")
        for match in CALL.finditer(text):
            run = "".join(literal.group(0)[1:-1] for literal in LITERAL.finditer(match.group(1)))
            add(decode(run))
        # A table of strings that is drawn through Tr(variable): a table at
        # namespace scope cannot call Tr() while it is built, because the
        # catalogs are read after that. The marker says its literals are
        # interface text all the same.
        lines = text.split("\n")
        for index, line in enumerate(lines):
            if "i18n:table" not in line:
                continue
            for row in lines[index + 1:]:
                if row.strip() == "};" or row.strip().endswith("};"):
                    break
                for literal in LITERAL.finditer(row):
                    add(decode(literal.group(0)[1:-1]))
    return found


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("list", "check"):
        print(__doc__)
        return 2
    keys = sources()
    if sys.argv[1] == "list":
        for key in keys:
            print(key)
        print(f"\n{len(keys)} strings", file=sys.stderr)
        return 0

    catalog = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_CATALOG
    document = json.loads(catalog.read_text(encoding="utf-8"))
    strings = document.get("strings", {})
    missing = [key for key in keys if key not in strings]
    extra = [key for key in strings if key not in keys]
    for key in missing:
        print(f"missing: {key}")
    for key in extra:
        print(f"unused:  {key}")
    translated = len(keys) - len(missing)
    print(f"\n{translated} of {len(keys)} strings translated in {catalog.name}, "
        f"{len(extra)} entries have no source any more")
    return 1 if missing or extra else 0


sys.exit(main())
