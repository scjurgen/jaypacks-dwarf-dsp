"""Finds the global tables a Lua file defines (the libraries scripts load with import "name") and their members.

A heuristic for the layout the factory libraries use: definitions start in column 0, and the members are documented in
the header comment as `--   Name.member(args)    what it does`. Anything else is still found, only without a doc.
"""

from __future__ import annotations

import re
from dataclasses import asdict, dataclass, field

from lua_editor.api.model import Library
from lua_editor.api.model import Member as LibraryMember

MAX_VALUE_TEXT = 80
LONG_BRACKET = re.compile(r"(--)?\[(=*)\[")


@dataclass
class Member:
    name: str
    kind: str  # "function", "table" or "value"
    params: list[str] | None = None  # functions only; a method (Name:member) has "self" implied
    method: bool = False
    value: str = ""  # tables and values: the source text, shortened
    doc: str = ""


@dataclass
class Module:
    import_name: str  # the name for import "name": the file name without .lua
    global_name: str  # the global table the file defines
    file: str
    summary: str = ""
    members: list[Member] = field(default_factory=list)

    def to_dict(self) -> dict:
        return asdict(self)

    def to_library(self) -> Library:
        members = [LibraryMember(m.name, m.kind, m.params, m.method, m.value, m.doc) for m in self.members]
        return Library(self.import_name, self.global_name, self.file, self.summary, members)


def mask(text: str) -> str:
    """The text with comments and the inside of strings blanked out (same length, same line breaks)."""
    out: list[str] = []
    i, n = 0, len(text)

    def blank(piece: str) -> str:
        return re.sub(r"[^\n]", " ", piece)

    while i < n:
        long_open = LONG_BRACKET.match(text, i)
        if long_open:
            close = "]" + long_open.group(2) + "]"
            end = text.find(close, long_open.end())
            end = n if end < 0 else end + len(close)
            piece = text[i:end]
            out.append(blank(piece) if long_open.group(1) else piece[0] + blank(piece[1:-1]) + piece[-1])
            i = end
        elif text.startswith("--", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(blank(text[i:end]))
            i = end
        elif text[i] in "\"'":
            quote, j = text[i], i + 1
            while j < n and text[j] != quote and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            closed = j < n and text[j] == quote
            out.append(quote + blank(text[i + 1 : j]) + (quote if closed else ""))
            i = j + 1 if closed else j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def _close(code: str, start: int) -> int:
    pairs = {"{": "}", "(": ")", "[": "]"}
    stack: list[str] = []
    for i in range(start, len(code)):
        c = code[i]
        if c in pairs:
            stack.append(pairs[c])
        elif c in "})]":
            if not stack or stack.pop() != c:
                break
            if not stack:
                return i
    return len(code)


def _split(body: str) -> list[tuple[int, str]]:
    """The entries of a table constructor body as (offset, text), split at top-level commas and semicolons."""
    entries: list[tuple[int, str]] = []
    depth = begin = 0
    for i, c in enumerate(body):
        if c in "{([":
            depth += 1
        elif c in "})]":
            depth -= 1
        elif c in ",;" and depth == 0:
            entries.append((begin, body[begin:i]))
            begin = i + 1
    entries.append((begin, body[begin:]))
    return entries


def _shorten(text: str) -> str:
    text = re.sub(r"\s+", " ", text).strip()
    return text if len(text) <= MAX_VALUE_TEXT else text[: MAX_VALUE_TEXT - 3] + "..."


def _params(code: str) -> list[str]:
    return [p.strip() for p in code.split(",") if p.strip()]


def _doc_comment(text: str) -> tuple[str, dict[str, str], str]:
    """(summary, docs by member name, the doc of an entry with '...' that covers the other values)."""
    lines: list[str] = []
    for line in text.splitlines():
        if not line.startswith("--"):
            break
        lines.append(line[2:])
    summary = ""
    if lines:
        first = lines[0].strip()
        summary = first.split(":", 1)[1].strip() if ":" in first else first
    docs: dict[str, list[str]] = {}
    describes_rest: list[str] = []
    current: list[str] | None = None
    column = 0
    for line in lines[1:]:
        entry = re.match(r"^(\s{2,})(\S.*?\S)\s{2,}(\S.*)$", line)
        if entry and re.match(r"[A-Za-z_]\w*[.:]\w+", entry.group(2)):
            names = re.findall(r"[A-Za-z_]\w*[.:](\w+)", entry.group(2))
            current = [entry.group(3).strip()]
            column = entry.start(3)
            for name in names:
                docs[name] = current
            if "..." in entry.group(2):
                describes_rest = current
        elif current is not None and line.strip() and len(line) - len(line.lstrip()) >= column:
            current.append(line.strip())
        else:
            current = None
    return summary, {name: " ".join(parts) for name, parts in docs.items()}, " ".join(describes_rest)


def scan_modules(file_name: str, text: str) -> list[Module]:
    """The global tables defined in column 0 of a Lua file, each with its members."""
    code = mask(text)
    import_name = file_name.removesuffix(".lua")
    summary, docs, spread_doc = _doc_comment(text)
    modules: dict[str, Module] = {}

    for m in re.finditer(r"^([A-Za-z_]\w*)[ \t]*=[ \t]*\{", code, re.M):
        module = modules.setdefault(m.group(1), Module(import_name, m.group(1), file_name, summary))
        body_start = m.end()
        body = code[body_start : _close(code, m.end() - 1)]
        for offset, entry in _split(body):
            item = re.match(r"\s*([A-Za-z_]\w*)\s*=\s*(.*)$", entry, re.S)
            if not item:
                continue
            raw = text[body_start + offset : body_start + offset + len(entry)]
            raw_value = raw.split("=", 1)[1]
            value = item.group(2).strip()
            if value.startswith("function"):
                params = re.match(r"function\s*\(([^)]*)\)", value)
                module.members.append(Member(item.group(1), "function", _params(params.group(1)) if params else []))
            else:
                kind = "table" if value.startswith("{") else "value"
                module.members.append(Member(item.group(1), kind, value=_shorten(raw_value)))

    for m in re.finditer(r"^function[ \t]+([A-Za-z_]\w*)([.:])(\w+)[ \t]*\(([^)]*)\)", code, re.M):
        module = modules.setdefault(m.group(1), Module(import_name, m.group(1), file_name, summary))
        module.members.append(Member(m.group(3), "function", _params(m.group(4)), method=m.group(2) == ":"))

    for m in re.finditer(r"^([A-Za-z_]\w*)\.(\w+)[ \t]*=[ \t]*([^\n]*)", code, re.M):
        if m.group(1) in modules:
            value = m.group(3).strip()
            if value.startswith("function"):
                params = re.match(r"function\s*\(([^)]*)\)", value)
                modules[m.group(1)].members.append(
                    Member(m.group(2), "function", _params(params.group(1)) if params else [])
                )
            else:
                modules[m.group(1)].members.append(Member(m.group(2), "value", value=_shorten(value)))

    for module in modules.values():
        for member in module.members:
            if member.name in docs:
                member.doc = docs[member.name]
            elif spread_doc and member.kind != "function":
                member.doc = spread_doc
    return list(modules.values())
