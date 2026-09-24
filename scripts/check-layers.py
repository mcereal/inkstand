#!/usr/bin/env python3
"""Fail when one area of inkstand includes a header it is not allowed to see.

Every `.c` in a library compiles into one archive, so the linker has no opinion about
direction: `src/state/store.c` could include "inkstand/nav/router.h" tomorrow and the build
would be delighted. The layering is a rule the compiler cannot see, and this is the check
that does.

An area is the directory under `src/` or `include/inkstand/`, so `src/persist/journal.c` and
`include/inkstand/persist/journal.h` are both `persist`. Three rules, in the order they bite:

1. **An area includes only the areas ALLOWED lists for it**, plus itself.
2. **The headless areas never include inkcell.** state/ and persist/ are what a program with
   nothing to draw links, over inkwell alone; the moment one of them names a toolkit header,
   a daemon needs a framebuffer library to keep a record. CI builds that half with inkcell
   switched off as well, but this is the check that says *which line* did it.
3. **Nothing includes an application.** inkstand is the layer an application stands on, and
   `#include "mesh/..."` is how the first one would quietly become the only one.

inkwell is below every area and is always allowed. Both `"..."` and `<...>` spellings are read,
because both compile.

    check-layers.py              check this tree
    check-layers.py --self-test  check the checker against trees built to break each rule

Run it directly, or through `ctest` / `make test`, which is where it will catch somebody.
"""

import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# What each area may include from, beyond itself and inkwell. The comment on a line is why the
# edge exists; an edge with no reason to be here is one to delete rather than one to document.
ALLOWED = {
    # The records an application keeps on disk: a typed cache, append-only journals, a
    # most-recently-used list. A leaf on purpose - what to write, and when, is the store's
    # business or the application's, and a persistence layer that reads a store is one
    # nobody can use without adopting the store too.
    "persist": set(),
    # The store, the snapshot, actions, effects, selectors. A leaf for the same reason: the
    # application wires a store to its cache; the store does not reach for one.
    "state": set(),
    # Fields, sections, pending edits and the text codec. A form's edits are committed as an
    # action, so it knows what an action is; it draws nothing, but a field's label is a string
    # id, and the catalog that owns string ids is inkcell's.
    "form": {"state"},
    # Where the reader is: the screen stack, overlays, the frame scheduler. It reads state
    # through a snapshot and edits a form through the keyboard.
    "nav": {"state", "form"},
    # The composition root: lifecycle, the control socket, the updater. It is the one area
    # allowed to see every other, because assembling them is what it is for.
    "app": {"state", "persist", "form", "nav"},
}

# The areas a program with nothing to draw links. See rule 2.
HEADLESS = {"state", "persist"}

INCLUDE = re.compile(r'^\s*#\s*include\s+["<]([a-z0-9_]+)/(?:([a-z0-9_]+)/)?')


def area_of(root, path):
    """The area a file belongs to, or None for a file this check has no opinion about."""
    parts = path.relative_to(root).parts
    if parts[0] == "src":
        return parts[1] if len(parts) > 2 else None
    if parts[:2] == ("include", "inkstand"):
        return parts[2] if len(parts) > 3 else None
    return None


def sources(root):
    for top in (root / "src", root / "include" / "inkstand"):
        if not top.exists():
            continue
        for path in sorted(top.rglob("*")):
            if path.suffix in (".c", ".h", ".m", ".def"):
                yield path


def check(root):
    """Every violation under `root`, as a list of lines. Empty means the tree is clean."""
    problems = []
    for path in sources(root):
        area = area_of(root, path)
        if area is None:
            continue
        allowed = ALLOWED.get(area, set()) | {area}
        where = path.relative_to(root)
        for number, line in enumerate(path.read_text().splitlines(), start=1):
            match = INCLUDE.match(line)
            if match is None:
                continue
            library, sub = match.group(1), match.group(2)
            if library == "inkstand" and sub is not None and sub not in allowed:
                problems.append("%s:%d: %s may not include from %s" % (where, number, area, sub))
            elif library == "inkcell" and area in HEADLESS:
                problems.append(
                    "%s:%d: %s is headless and may not include inkcell" % (where, number, area)
                )
            elif library == "mesh":
                problems.append(
                    "%s:%d: inkstand is below every application and may not include %s/"
                    % (where, number, library)
                )

    unknown = {area_of(root, p) for p in sources(root)} - set(ALLOWED) - {None}
    for area in sorted(unknown):
        problems.append("src/%s/: a new area needs an entry in ALLOWED in this script" % area)
    return problems


# (description, file, contents, expected substring of a problem - or None for a clean tree)
SELF_TEST_CASES = [
    ("an area includes itself", "src/state/store.c",
     '#include "inkstand/state/store.h"\n', None),
    ("an area includes inkwell", "src/persist/journal.c",
     '#include "inkwell/base/record_file.h"\n', None),
    ("the composition root sees everything", "src/app/app.c",
     '#include "inkstand/nav/router.h"\n#include "inkstand/persist/cache.h"\n', None),
    ("a drawing area includes inkcell", "src/nav/router.c",
     '#include <inkcell/ui/focus.h>\n', None),
    ("state reaches up into nav", "src/state/store.c",
     '#include "inkstand/nav/router.h"\n', "state may not include from nav"),
    ("persist reaches into state", "include/inkstand/persist/cache.h",
     '#include <inkstand/state/store.h>\n', "persist may not include from state"),
    ("a headless area names the toolkit", "src/state/store.c",
     '#include "inkcell/ui/key.h"\n', "state is headless and may not include inkcell"),
    ("an application header", "src/nav/router.c",
     '#include "mesh/ui/nav.h"\n', "may not include mesh/"),
    ("a new area nobody decided on", "src/widgets/button.c",
     '#include "inkwell/base/log.h"\n', "a new area needs an entry in ALLOWED"),
]


def self_test():
    failures = 0
    for description, name, contents, expected in SELF_TEST_CASES:
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            path = root / name
            path.parent.mkdir(parents=True)
            path.write_text(contents)
            problems = check(root)
        if expected is None and problems:
            print("FAIL %s: expected clean, got %s" % (description, problems), file=sys.stderr)
            failures += 1
        elif expected is not None and not any(expected in p for p in problems):
            print("FAIL %s: expected %r, got %s" % (description, expected, problems),
                  file=sys.stderr)
            failures += 1
    if failures:
        return 1
    print("check-layers self-test: %d cases ok" % len(SELF_TEST_CASES))
    return 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    if argv[1:]:
        print(__doc__, file=sys.stderr)
        return 2
    problems = check(ROOT)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print("\n%d layering violation(s)" % len(problems), file=sys.stderr)
        return 1
    print("layers ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
