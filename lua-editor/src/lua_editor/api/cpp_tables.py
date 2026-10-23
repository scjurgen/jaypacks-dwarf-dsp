"""Reads the constexpr tables of scripting/Binding.h (commands, hooks, enums) out of C++ headers.

This is a reader for the layout those tables use, not a C++ parser. It stops with ExtractError on anything it does not
understand, and every count in a table is checked against the entries found, so a changed layout cannot give a
partial API quietly.
"""

from __future__ import annotations

import re
from pathlib import Path

NAMESPACE = re.compile(r"\b(?:script|jp)::")
# inline constexpr std::array<Type, N> name{ ...  |  inline constexpr Type name{ ...
DECLARATION = re.compile(r"inline constexpr\s+(?:std::array<\s*([\w:\s*]+?)\s*,\s*([^>]+)>|([\w:]+))\s+(\w+)\s*\{")
KINDS = {"Number": "number", "Integer": "integer", "Flag": "flag", "Text": "text"}


class ExtractError(Exception):
    pass


def strip_comments(text: str) -> str:
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        if text[i] == '"':
            j = _string_end(text, i)
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
        elif text.startswith("/*", i):
            i = text.index("*/", i) + 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def _string_end(text: str, start: int) -> int:
    j = start + 1
    while text[j] != '"':
        j += 2 if text[j] == "\\" else 1
    return j


def match_close(text: str, start: int) -> int:
    """The index of the bracket that closes the one at start."""
    closers = {"{": "}", "(": ")"}
    stack: list[str] = []
    i = start
    while i < len(text):
        c = text[i]
        if c == '"':
            i = _string_end(text, i)
        elif c in closers:
            stack.append(closers[c])
        elif c in "})":
            if not stack or stack.pop() != c:
                break
            if not stack:
                return i
        i += 1
    raise ExtractError("unbalanced brackets")


def split_top(body: str) -> list[str]:
    """Split at the commas that are not inside brackets or strings."""
    parts: list[str] = []
    depth = start = i = 0
    while i < len(body):
        c = body[i]
        if c == '"':
            i = _string_end(body, i)
        elif c in "{(":
            depth += 1
        elif c in "})":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(body[start:i].strip())
            start = i + 1
        i += 1
    tail = body[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def inner(group: str) -> str:
    return group.strip()[1:-1]


def clean(number: float) -> int | float:
    return int(number) if number == int(number) else number


class Tables:
    """The declarations and enums of a set of headers."""

    def __init__(self, files: list[Path]) -> None:
        # name -> (element type, body, count expression; None for a single value)
        self.declarations: dict[str, tuple[str, str, str | None]] = {}
        self.enums: dict[str, dict[str, int]] = {}
        for path in files:
            self._read(strip_comments(path.read_text(encoding="utf-8")))

    def _read(self, text: str) -> None:
        for m in DECLARATION.finditer(text):
            body = text[m.end() : match_close(text, m.end() - 1)]
            if m.group(1):
                self.declarations[m.group(4)] = (m.group(1).split("::")[-1], body, m.group(2).strip())
            else:
                self.declarations[m.group(4)] = (m.group(3).split("::")[-1], body, None)
        for m in re.finditer(r"enum class (\w+)[^{;]*\{", text):
            members: dict[str, int] = {}
            value = 0
            for item in split_top(text[m.end() : match_close(text, m.end() - 1)]):
                name = item
                if "=" in item:
                    name, expression = (part.strip() for part in item.split("=", 1))
                    value = int(expression, 0)
                members[name] = value
                value += 1
            self.enums.setdefault(m.group(1), members)

    # values

    def number(self, expression: str) -> float:
        expression = NAMESPACE.sub("", re.sub(r"static_cast<\w+>", "", expression))

        def constant(m: re.Match[str]) -> str:
            known = self.declarations.get(m.group(0))
            if known is not None and known[2] is None:
                return f"({self.number(known[1])})"
            raise ExtractError(f"unknown name in a number: {m.group(0)}")

        expression = re.sub(r"[A-Za-z_]\w*", constant, expression.replace("'", ""))
        expression = re.sub(r"(?<=[\d.])[fFuUlL]\b", "", expression)
        if not re.fullmatch(r"[\d.eE+\-*/() ]+", expression):
            raise ExtractError(f"not plain arithmetic: {expression!r}")
        return float(eval(expression, {"__builtins__": {}}))  # only digits and operators got through

    @staticmethod
    def string(expression: str) -> str:
        literals = re.findall(r'"((?:[^"\\]|\\.)*)"', expression)
        if not literals:
            raise ExtractError(f"not a string: {expression!r}")
        return "".join(literals).replace('\\"', '"')

    # tables

    def elements(self, reference: str) -> list[str]:
        """The initialisers of the elements of a table, whatever braces the std::array initialisation used."""
        name = NAMESPACE.sub("", reference)
        index = None
        indexed = re.fullmatch(r"(\w+)\[(\d+)\]", name)
        if indexed:
            name, index = indexed.group(1), int(indexed.group(2))
        if name not in self.declarations:
            raise ExtractError(f"no table named {name}")
        _type, body, count = self.declarations[name]
        if count is None:
            return [body]
        items = split_top(body)
        wanted = int(self.number(count))
        while len(items) == 1 and items[0].startswith("{"):
            parts = split_top(inner(items[0]))
            # one more brace level (std::array's own) if it holds braced entries or exactly the declared count
            if not (all(part.startswith("{") for part in parts) or (wanted > 1 and len(parts) == wanted)):
                break
            items = parts
        if len(items) != wanted:
            raise ExtractError(f"{name} declares {wanted} elements, found {len(items)}")
        return items if index is None else [items[index]]

    def _value_spec(self, fields: list[str]) -> dict:
        if len(fields) not in (6, 7):
            raise ExtractError(f"a value has 6 or 7 parts, found {len(fields)}: {fields}")
        kind = KINDS[fields[1].split("::")[-1]]
        spec: dict = {"name": self.string(fields[0]), "kind": kind}
        if kind in ("number", "integer"):
            spec["min"] = clean(self.number(fields[2]))
            spec["max"] = clean(self.number(fields[3]))
        if kind == "flag":
            spec["default"] = bool(self.number(fields[4]))
        elif kind != "text":
            spec["default"] = clean(self.number(fields[4]))
        spec["doc"] = self.string(fields[5])
        if len(fields) == 7:
            spec["enum"] = self.string(fields[6])
        return spec

    def _values(self, reference: str, count: int) -> list[dict]:
        if reference == "nullptr":
            found: list[dict] = []
        else:
            found = []
            for item in self.elements(reference.removesuffix(".data()")):
                item = item.strip()
                body = inner(item) if item.startswith("{") else self.declarations[item][1]
                found.append(self._value_spec(split_top(body)))
        if len(found) != count:
            raise ExtractError(f"{reference}: {count} values declared, found {len(found)}")
        return found

    def commands(self, table: str) -> list[dict]:
        found: list[dict] = []
        for item in self.elements(table):
            if not item.startswith("{"):
                found.extend(self.commands(item))
                continue
            f = split_top(inner(item))
            found.append(
                {
                    "name": self.string(f[0]),
                    "doc": self.string(f[1]),
                    "args": self._values(f[2], int(f[3])),
                    "fields": self._values(f[4], int(f[5])),
                }
            )
        return found

    def getters(self, table: str) -> list[dict]:
        """The functions a script calls to read a number (GetterSpec), as commands without arguments. [] if the plugin
        has no such table."""
        if NAMESPACE.sub("", table) not in self.declarations:
            return []
        found: list[dict] = []
        for item in self.elements(table):
            f = split_top(inner(item))
            low, high = clean(self.number(f[2])), clean(self.number(f[3]))
            found.append(
                {
                    "name": self.string(f[0]),
                    "doc": f"{self.string(f[1])} Returns a number from {low} to {high}.",
                    "args": [],
                    "fields": [],
                }
            )
        return found

    def hooks(self, table: str) -> list[dict]:
        found: list[dict] = []
        for item in self.elements(table):
            if not item.startswith("{"):
                found.extend(self.hooks(item))
                continue
            f = split_top(inner(item))
            names = [] if f[2] == "nullptr" else [self.string(a) for a in self.elements(f[2].removesuffix(".data()"))]
            if len(names) != int(f[3]):
                raise ExtractError(f"hook {f[0]}: {f[3]} arguments declared, found {len(names)}")
            found.append({"name": self.string(f[0]), "doc": self.string(f[1]), "args": names})
        return found

    def enum_tables(self, table: str) -> list[dict]:
        found: list[dict] = []
        for item in self.elements(table):
            f = split_top(inner(item))
            entries: list[dict] = []
            for entry in self.elements(f[2].removesuffix(".data()")):
                macro = re.fullmatch(r"\{?\s*\w+\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*\}?", entry.strip())
                if macro:
                    kind, member = macro.groups()
                    if kind not in self.enums or member not in self.enums[kind]:
                        raise ExtractError(f"enum {kind}::{member} not found in the headers read")
                    entries.append({"name": member, "value": self.enums[kind][member]})
                else:
                    parts = split_top(inner(entry))
                    entries.append({"name": self.string(parts[0]), "value": int(self.number(parts[1]))})
            if len(entries) != int(f[3]):
                raise ExtractError(f"enum {f[0]}: {f[3]} entries declared, found {len(entries)}")
            found.append({"name": self.string(f[0]), "doc": self.string(f[1]), "entries": entries})
        return found
