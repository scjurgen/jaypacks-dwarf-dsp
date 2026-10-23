#!/usr/bin/env python3
"""What X and Y do in each factory script of a plugin, taken from the scripts' header comments.

Ship Ahoy has two knobs without a meaning of their own, X and Y; the running script decides what they do and says so
in its header comment, in two lines:

  -- X: what X does
  -- Y: what Y does          (or "-- X, Y: not used")

The table in docs/plugins/shipahoy.md between <!-- xy:begin --> and <!-- xy:end --> is generated from
plugins/shipahoy/scripts/*.lua in the order of the Script port (plugins/shipahoy/shipahoy.json); tools/test_script_headers.py
fails when a script lacks the lines or the table differs, so they cannot go stale.

As a script:
  script_headers.py update     rewrite the table from the scripts
  script_headers.py check      exit 1 if a script lacks the lines or the table is out of date

Standard library only.
"""

import json
import re
import sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent
BEGIN = "<!-- xy:begin -->"
END = "<!-- xy:end -->"
PAGE = REPO_DIR / "docs" / "plugins" / "shipahoy.md"
SCRIPTS = REPO_DIR / "plugins" / "shipahoy" / "scripts"
BLUEPRINT = REPO_DIR / "plugins" / "shipahoy" / "shipahoy.json"
LINE = re.compile(r"^--\s*(X|Y|X, Y):\s*(.+?)\s*$", re.M)


def factory_scripts(blueprint=BLUEPRINT):
    """[(menu name, file stem)] of the plugin's own scripts, in the order of the Script port (the entries before User 1)."""
    port = next(p for p in json.loads(blueprint.read_text())["ports-control"] if p["symbol"] == "script")
    names = []
    for item in port["listitems"]:
        if item.startswith("User "):
            break
        names.append((item, item.lower().replace(" ", "-")))
    return names


def header_lines(text):
    """{"X": text, "Y": text} from a script's header comment; "X, Y: not used" gives both the same text."""
    found = {}
    for key, value in LINE.findall(text):
        for axis in key.split(", "):
            found.setdefault(axis, value)
    return found


def missing(scripts=SCRIPTS, blueprint=BLUEPRINT):
    """The scripts of the Script port that lack the X or the Y line, or whose file does not exist."""
    problems = []
    for menu, stem in factory_scripts(blueprint):
        path = scripts / f"{stem}.lua"
        if not path.exists():
            problems.append(f"{menu}: {path.name} does not exist")
            continue
        lines = header_lines(path.read_text(encoding="ascii"))
        for axis in ("X", "Y"):
            if axis not in lines:
                problems.append(f"{menu}: no '-- {axis}:' line in {path.name}")
    return problems


def table(scripts=SCRIPTS, blueprint=BLUEPRINT):
    rows = ["| Script | X | Y |", "|---|---|---|"]
    for menu, stem in factory_scripts(blueprint):
        lines = header_lines((scripts / f"{stem}.lua").read_text(encoding="ascii"))
        rows.append(f"| {menu} (`{stem}.lua`) | {lines['X']} | {lines['Y']} |")
    return "\n".join(rows)


def updated(page_text, new_table):
    begin = page_text.index(BEGIN) + len(BEGIN)
    end = page_text.index(END)
    return page_text[:begin] + "\n" + new_table + "\n" + page_text[end:]


def main(argv):
    if len(argv) != 2 or argv[1] not in ("update", "check"):
        print(__doc__)
        return 2
    problems = missing()
    if problems:
        print("\n".join(problems))
        return 1
    text = PAGE.read_text(encoding="ascii")
    fresh = updated(text, table())
    if argv[1] == "check":
        if fresh != text:
            print(f"{PAGE.relative_to(REPO_DIR)}: the X and Y table is out of date (script_headers.py update)")
            return 1
        return 0
    PAGE.write_text(fresh, encoding="ascii")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
