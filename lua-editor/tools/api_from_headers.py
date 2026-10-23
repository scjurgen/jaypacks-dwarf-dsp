"""Write apis/<plugin>/api.json from the DSP repo's binding headers (the repo is only read).

usage: python tools/api_from_headers.py [--dsp PATH] [--check] [plugin ...]

--check writes nothing and exits 1 if a file differs from what the sources give now.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from lua_editor.api.cpp_tables import ExtractError
from lua_editor.api.extract import PLUGINS, extract_plugin, render

HERE = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("plugins", nargs="*", choices=[*PLUGINS, []], default=[], metavar="plugin")
    parser.add_argument("--dsp", type=Path, default=HERE.parent)
    parser.add_argument("--out", type=Path, default=HERE / "apis")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not (args.dsp / "plugins").is_dir():
        parser.error(f"not the DSP repo: {args.dsp}")
    stale = False
    for name in args.plugins or PLUGINS:
        try:
            text = render(extract_plugin(args.dsp, name))
        except ExtractError as error:
            print(f"{name}: {error}", file=sys.stderr)
            return 1
        target = args.out / name / "api.json"
        if args.check:
            current = target.read_text(encoding="utf-8") if target.is_file() else None
            if current != text:
                print(f"{name}: {target} is out of date")
                stale = True
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")
        print(f"{name}: {target}")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
