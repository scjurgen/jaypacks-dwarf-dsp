"""The api.json of a plugin as objects, and the registry that picks the right one for a file."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Value:
    """One positional argument or one field of a command's table."""

    name: str
    kind: str  # number, integer, flag, text
    doc: str = ""
    min: float | None = None
    max: float | None = None
    default: float | bool | None = None
    enum: str | None = None
    values: list[tuple[str, str]] = field(default_factory=list)  # accepted names of a text value, with a note

    def describe(self) -> str:
        """Kind, range and default in a few words."""
        if self.kind == "integer":
            text = f"whole number {_num(self.min)} to {_num(self.max)}"
        elif self.kind == "number":
            text = f"number {_num(self.min)} to {_num(self.max)}"
        elif self.kind == "flag":
            text = "true or false"
        else:
            text = "string"
        if self.enum:
            text += f", a {self.enum} constant"
        if self.default is not None and self.kind != "text":
            text += f", default {self.default if self.kind != 'flag' else str(self.default).lower()}"
        return text


def _num(value: float | None) -> str:
    return "?" if value is None else f"{value:g}"


@dataclass
class Command:
    name: str
    doc: str
    args: list[Value]
    fields: list[Value]

    def signature(self) -> str:
        parts = [a.name for a in self.args]
        if self.fields:
            parts.append("{ fields }")
        return f"{self.name}({', '.join(parts)})"

    def field(self, name: str) -> Value | None:
        return next((f for f in self.fields if f.name == name), None)


@dataclass
class Hook:
    name: str
    doc: str
    args: list[str]

    def signature(self) -> str:
        return f"{self.name}({', '.join(self.args)})"


@dataclass
class Enum:
    name: str
    doc: str
    entries: dict[str, int]


@dataclass
class Member:
    name: str
    kind: str  # function, table, value
    params: list[str] | None = None
    method: bool = False
    value: str = ""
    doc: str = ""


@dataclass
class Library:
    import_name: str
    global_name: str
    file: str
    summary: str
    members: list[Member]

    def member(self, name: str) -> Member | None:
        return next((m for m in self.members if m.name == name), None)


@dataclass
class Example:
    """A small script that ships with the plugin (an example, or a complete factory script)."""

    id: str
    file: str  # relative to the plugin's scripts folder
    title: str
    summary: str
    code: str
    uses: list[str]  # commands, hooks, enums and library globals it names, in order of appearance


@dataclass
class Api:
    plugin: str
    title: str
    lua: str
    limits: dict
    sandbox: dict
    noop_imports: list[str]
    commands: dict[str, Command]
    hooks: dict[str, Hook]
    enums: dict[str, Enum]
    libraries: list[Library]
    examples: list[Example] = field(default_factory=list)
    scripts: list[Example] = field(default_factory=list)  # the complete factory scripts
    guide: str = ""  # AGENTS.md of the plugin, for an AI assistant

    def used_in(self, name: str) -> list[Example]:
        """The examples that name a command, hook, enum or library global."""
        return [e for e in self.examples if name in e.uses]

    def scripts_using(self, name: str) -> list[Example]:
        return [e for e in self.scripts if name in e.uses]


def _value(raw: dict) -> Value:
    return Value(
        raw["name"],
        raw["kind"],
        raw.get("doc", ""),
        raw.get("min"),
        raw.get("max"),
        raw.get("default"),
        raw.get("enum"),
        [(v["name"], v.get("doc", "")) for v in raw.get("values", [])],
    )


def _examples(raw: list[dict]) -> list[Example]:
    return [Example(e["id"], e["file"], e["title"], e["summary"], e["code"], e["uses"]) for e in raw]


def parse_api(raw: dict) -> Api:
    commands = {
        c["name"]: Command(c["name"], c["doc"], [_value(a) for a in c["args"]], [_value(f) for f in c["fields"]])
        for c in raw["commands"]
    }
    hooks = {h["name"]: Hook(h["name"], h["doc"], h["args"]) for h in raw["hooks"]}
    enums = {e["name"]: Enum(e["name"], e["doc"], {x["name"]: x["value"] for x in e["entries"]}) for e in raw["enums"]}
    libraries = [
        Library(
            lib["import_name"],
            lib["global_name"],
            lib["file"],
            lib["summary"],
            [
                Member(m["name"], m["kind"], m.get("params"), m.get("method", False), m.get("value", ""), m.get("doc", ""))
                for m in lib["members"]
            ],
        )
        for lib in raw["libraries"]
    ]
    return Api(
        raw["plugin"], raw["title"], raw["lua"], raw["limits"], raw["sandbox"], raw["noopImports"],
        commands, hooks, enums, libraries,
        _examples(raw.get("examples", [])), _examples(raw.get("scripts", [])), raw.get("guide", ""),
    )


def load_api(path: Path) -> Api:
    return parse_api(json.loads(path.read_text(encoding="utf-8")))


class ApiRegistry:
    """The APIs of all plugins, found by the plugin's name in the first folders of a file's path (a mount may come first)."""

    def __init__(self, apis: dict[str, Api] | None = None) -> None:
        self._apis = apis or {}

    @classmethod
    def from_folder(cls, folder: Path) -> ApiRegistry:
        apis = {}
        for path in sorted(folder.glob("*/api.json")):
            apis[path.parent.name] = load_api(path)
        return cls(apis)

    def for_path(self, path: str) -> Api | None:
        for part in path.split("/")[:2]:
            if part in self._apis:
                return self._apis[part]
        return None

    def plugins(self) -> list[str]:
        return sorted(self._apis)
