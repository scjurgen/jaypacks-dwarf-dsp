"""The API of a plugin as one Markdown text for an LLM: the guide (AGENTS.md), the API, then every example.

python -m lua_editor.api.markdown <plugin> [--apis FOLDER] [--out FILE]   (without --out the text goes to stdout)

It is made from api.json only, so it is as current as the generated file.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from lua_editor.api.html import _member_signature
from lua_editor.api.model import Api, ApiRegistry, Example, Value


def _demote(text: str) -> str:
    """The headings of a document one level deeper, outside fenced code."""
    out, fenced = [], False
    for line in text.splitlines():
        if line.startswith("```"):
            fenced = not fenced
        out.append("#" + line if not fenced and line.startswith("#") else line)
    return "\n".join(out)


def _fence(code: str) -> str:
    longest = max((len(run) for run in re.findall(r"`+", code)), default=0)
    return "`" * max(3, longest + 1)


def _values(values: list[Value]) -> list[str]:
    lines = []
    for value in values:
        note = f" {value.doc}" if value.doc else ""
        lines.append(f"- `{value.name}`: {value.describe()}.{note}")
        lines += [f"  - `{name}`: {doc}" if doc else f"  - `{name}`" for name, doc in value.values]
    return lines


def _example(example: Example) -> list[str]:
    fence = _fence(example.code)
    return [
        f"### {example.title}",
        "",
        f"File `examples/{example.file.split('/')[-1]}`. {example.summary}",
        "",
        f"{fence}lua",
        example.code.rstrip("\n"),
        fence,
        "",
    ]


def render_api_markdown(api: Api) -> str:
    out = [
        f"# {api.title} scripting brief",
        "",
        f"For writing Lua scripts for the plugin `{api.plugin}` (Lua {api.lua}). Part 1 is the guide, part 2 the API "
        "(the guide calls it API.md and constants.lua: the constants are in the API part), part 3 the examples.",
        "",
    ]
    if api.guide:
        out += ["# Part 1: guide", "", _demote(api.guide).strip(), ""]
    out += ["# Part 2: API", "", "## Commands", ""]
    for command in api.commands.values():
        out += [f"### `{command.signature()}`", "", command.doc, ""]
        if command.args:
            out += ["Arguments:", "", *_values(command.args), ""]
        if command.fields:
            out += ["Fields of the table argument, all optional:", "", *_values(command.fields), ""]
    out += ["## Hooks", ""]
    for hook in api.hooks.values():
        out += [f"### `function {hook.signature()}`", "", hook.doc, ""]
    if api.enums:
        out += ["## Constants", ""]
        for enum in api.enums.values():
            entries = ", ".join(f"{name} = {value}" for name, value in enum.entries.items())
            out += [f"### `{enum.name}`", "", enum.doc, "", entries, ""]
    if api.libraries:
        out += ["## Libraries", ""]
        for library in api.libraries:
            out += [
                f"### `{library.global_name}`",
                "",
                f"`import \"{library.import_name}\"` (file `{library.file}`): {library.summary}",
                "",
            ]
            for member in library.members:
                text = f"- `{_member_signature(library, member)}`"
                if member.value:
                    text += f" = `{member.value}`"
                if member.doc:
                    text += f": {member.doc}"
                out.append(text)
            out.append("")
    out += ["## Limits and sandbox", ""]
    out += [f"- `{key}`: {value}" for key, value in api.limits.items()]
    sandbox = api.sandbox
    removed = ", ".join(f"{lib}.{name}" for lib, names in sandbox.get("removedFields", {}).items() for name in names)
    for label, text in (
        ("Lua libraries", ", ".join(sandbox.get("libraries", []))),
        ("Removed globals", ", ".join(sandbox.get("removedGlobals", []))),
        ("Removed fields", removed),
        ("Added globals", ", ".join(sandbox.get("runtimeGlobals", []))),
        ("Accepted without effect", ", ".join(f'import "{name}"' for name in api.noop_imports)),
    ):
        if text:
            out.append(f"- {label}: {text}")
    out.append("")
    if api.examples:
        out += ["# Part 3: examples", ""]
        for example in api.examples:
            out += _example(example)
    return "\n".join(out).rstrip("\n") + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="lua_editor.api.markdown", description=__doc__.splitlines()[0])
    parser.add_argument("plugin")
    parser.add_argument("--apis", type=Path, default=Path("apis"), help="folder with <plugin>/api.json files")
    parser.add_argument("--out", type=Path, help="file to write (default: standard output)")
    args = parser.parse_args(argv)
    api = ApiRegistry.from_folder(args.apis).for_path(args.plugin)
    if api is None:
        parser.error(f"no api.json for {args.plugin!r} in {args.apis}")
    text = render_api_markdown(api)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
