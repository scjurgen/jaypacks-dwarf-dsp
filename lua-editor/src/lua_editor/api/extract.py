"""Builds api.json for a plugin from the DSP repo's sources (read only).

The tables come from the plugin's binding header, the sandbox and limits from the scripting core, the libraries from the
Lua files in the plugin's scripts folder. docs/api-source-analysis.md explains the choices.
"""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

from lua_editor.api.cpp_tables import ExtractError, Tables, clean, split_top, strip_comments, inner, match_close
from lua_editor.api.lua_modules import scan_modules
from lua_editor.check.luatokens import tokenize

SCHEMA = 1
LUA_HEADER = "third_party/lua/lua.h"
TABLE_SOURCES = ["scripting/Binding.h", "scripting/Timers.h"]
SANDBOX_SOURCE = "scripting/LuaRuntime.cpp"
LIMIT_SOURCES = ["scripting/LuaRuntime.h", "scripting/ScriptHost.h"]
DOC_SOURCE = "scripting/ApiDescription.cpp"
# the standard Lua global that LuaRuntime wraps and sets again; it is not one of the plugin's own globals
STANDARD_GLOBALS = {"setmetatable"}
GUIDE_FILE = "AGENTS.md"
EXAMPLES_FOLDER = "examples"


@dataclass(frozen=True)
class Plugin:
    header: str
    enum_headers: tuple[str, ...] = ()
    filter_types: str | None = None  # header with the filter mix table, for plugins whose SetFilter takes a name


PLUGINS: dict[str, Plugin] = {
    "morphexsynth": Plugin(
        "plugins/morphexsynth/MorphexScript.h",
        (
            "dsp/Synthesizer/MpeRouting.h",
            "dsp/Synthesizer/MpeCurve.h",
            "dsp/Generators/SynthLfo.h",
            "plugins/morphexsynth/MorphPatch.h",
        ),
        filter_types="dsp/Filters/PoleMixingFilter.h",
    ),
    "midiscript": Plugin("plugins/midiscript/MidiScript.h"),
    "shipahoy": Plugin("plugins/shipahoy/ShipAhoyScript.h"),
}


@dataclass
class Reader:
    root: Path
    read: dict[str, bytes] = field(default_factory=dict)

    def path(self, relative: str) -> Path:
        self.read.setdefault(relative, (self.root / relative).read_bytes())
        return self.root / relative

    def text(self, relative: str) -> str:
        self.path(relative)
        return strip_comments(self.read[relative].decode("utf-8"))

    def sources(self) -> list[dict]:
        return [
            {"path": name, "sha256": hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()}
            for name, data in sorted(self.read.items())
        ]


def extract_plugin(dsp: Path, name: str) -> dict:
    plugin = PLUGINS[name]
    reader = Reader(dsp)
    tables = Tables([reader.path(p) for p in (*TABLE_SOURCES, *plugin.enum_headers, plugin.header)])
    header = reader.text(plugin.header)

    commands = tables.commands("c_commands")
    commands += tables.getters("c_getters")
    hooks = tables.hooks("c_hooks")
    enums = tables.enum_tables("c_enums")
    for kind, items in (("command", commands), ("hook", hooks), ("enum", enums)):
        names = [item["name"] for item in items]
        if len(names) != len(set(names)):
            raise ExtractError(f"{name}: a {kind} name is used twice")
    title = re.search(r'BindingSet\s+\w+\s*\{\s*"([^"]+)"', header)
    if not title:
        raise ExtractError(f"{plugin.header}: no BindingSet with a title")

    if plugin.filter_types:
        _add_filter_types(commands, tables, reader, plugin.filter_types)

    scripts = dsp / "plugins" / name / "scripts"
    libraries: list[dict] = []
    # user-N.lua are drafts (a pull from the device can leave them here), not part of the API
    for script in sorted(s for s in scripts.glob("*.lua") if not s.name.startswith("user-")):
        relative = script.relative_to(dsp).as_posix()
        reader.path(relative)
        for module in scan_modules(script.name, script.read_bytes().decode("utf-8")):
            libraries.append(module.to_dict())

    examples = _collect_scripts(reader, scripts, scripts / EXAMPLES_FOLDER, commands, hooks, enums, libraries)
    complete = _collect_scripts(
        reader, scripts, scripts, commands, hooks, enums, libraries, skip={m["file"] for m in libraries}
    )
    guide = scripts / GUIDE_FILE
    guide_text = reader.path(f"plugins/{name}/scripts/{GUIDE_FILE}").read_bytes().decode("utf-8") if guide.is_file() else None

    api = {
        "schema": SCHEMA,
        "plugin": name,
        "title": title.group(1),
        "lua": _lua_version(reader),
        "limits": _limits(tables, reader),
        "sandbox": _sandbox(reader),
        "noopImports": _noop_imports(reader),
        "commands": commands,
        "hooks": hooks,
        "enums": enums,
        "libraries": libraries,
    }
    # optional keys: absent when the plugin has no such files
    if examples:
        api["examples"] = examples
    if complete:
        api["scripts"] = complete
    if guide_text is not None:
        api["guide"] = guide_text
    # last, so that it lists every file that was read
    api["sources"] = reader.sources()
    return api


def _header(text: str) -> tuple[str, str]:
    """Title and summary of a script: the first comment line without its final period, and the rest of the leading
    comment block as one line."""
    lines: list[str] = []
    for line in text.splitlines():
        if not line.startswith("--"):
            break
        lines.append(line[2:].strip())
    lines = [line for line in lines if line] or [""]
    return lines[0].rstrip("."), " ".join(lines[1:])


def _uses(code: str, commands: list[dict], hooks: list[dict], enums: list[dict], libraries: list[dict]) -> list[str]:
    """The commands, hooks, enums and library globals a script names, in order of appearance (comments and strings
    excluded; a name after a dot or colon is a field). import "name" counts as the library's global."""
    known = {item["name"] for item in (*commands, *hooks, *enums)} | {lib["global_name"] for lib in libraries}
    by_import = {lib["import_name"]: lib["global_name"] for lib in libraries}
    found: list[str] = []
    tokens = tokenize(code)
    for i, token in enumerate(tokens):
        word: str | None = None
        if token.kind == "name" and token.text in known:
            if i == 0 or tokens[i - 1].text not in (".", ":"):
                word = token.text
        elif token.kind == "string" and i >= 1 and tokens[i - 1].text == "import" and token.value in by_import:
            word = by_import[str(token.value)]
        if word and word not in found:
            found.append(word)
    return found


def _collect_scripts(
    reader: Reader,
    scripts: Path,
    folder: Path,
    commands: list[dict],
    hooks: list[dict],
    enums: list[dict],
    libraries: list[dict],
    skip: frozenset[str] | set[str] = frozenset(),
) -> list[dict]:
    """The *.lua files of a folder as entries of api.json (not the drafts user-N.lua and not the libraries in skip)."""
    entries = []
    if not folder.is_dir():
        return entries
    for script in sorted(folder.glob("*.lua")):
        if script.name.startswith("user-") or script.name in skip:
            continue
        relative = script.relative_to(reader.root).as_posix()
        reader.path(relative)
        code = reader.read[relative].decode("utf-8")
        title, summary = _header(code)
        entries.append(
            {
                "id": script.stem,
                "file": script.relative_to(scripts).as_posix(),
                "title": title,
                "summary": summary,
                "code": code,
                "uses": _uses(code, commands, hooks, enums, libraries),
            }
        )
    return entries


def _lua_version(reader: Reader) -> str:
    text = reader.text(LUA_HEADER)
    parts = [re.search(rf'#define\s+LUA_VERSION_{part}\s+"(\d+)"', text) for part in ("MAJOR", "MINOR", "RELEASE")]
    if not all(parts):
        raise ExtractError(f"{LUA_HEADER}: no Lua version")
    return ".".join(p.group(1) for p in parts if p)


def _limits(tables: Tables, reader: Reader) -> dict:
    limits: dict = {"textMaxLength": clean(tables.number("c_maxTextLength"))}
    script_size = re.search(r"c_maxScriptBytes\s*\{([^}]+)\}", reader.text(LIMIT_SOURCES[1]))
    if not script_size:
        raise ExtractError("no c_maxScriptBytes in ScriptHost.h")
    limits["scriptMaxBytes"] = clean(tables.number(script_size.group(1)))
    runtime_text = reader.text(LIMIT_SOURCES[0])
    runtime = re.search(r"struct Limits\s*\{", runtime_text)
    if not runtime:
        raise ExtractError("no struct Limits in LuaRuntime.h")
    body = runtime_text[runtime.end() : match_close(runtime_text, runtime.end() - 1)]
    for _kind, name, value in re.findall(r"(\w+)\s+(\w+)\s*\{([^}]*)\}\s*;", body):
        limits[name] = clean(tables.number(value))
    if len(limits) < 7:
        raise ExtractError("LuaRuntime.h: fewer limits than expected")
    return limits


def _sandbox(reader: Reader) -> dict:
    text = reader.text(SANDBOX_SOURCE)
    libraries = re.findall(r"luaL_requiref\(\s*state\s*,\s*[^,]+,\s*luaopen_(\w+)\s*,", text)
    removed = re.search(
        r"for \(const char\* name : \{([^}]*)\}\)\s*\{\s*lua_pushnil\(state\);\s*lua_setglobal\(state, name\);", text
    )
    dump = re.search(
        r'lua_getglobal\(state, "string"\);\s*lua_pushnil\(state\);\s*lua_setfield\(state, -2, "(\w+)"\)', text
    )
    own = [n for n in re.findall(r'lua_setglobal\(state, "(\w+)"\)', text) if n not in STANDARD_GLOBALS]
    if not (libraries and removed and dump and own):
        raise ExtractError(f"{SANDBOX_SOURCE}: the sandbox setup is not laid out as expected")
    return {
        "libraries": libraries,
        "removedGlobals": re.findall(r'"(\w+)"', removed.group(1)),
        "removedFields": {"string": [dump.group(1)]},
        "runtimeGlobals": list(dict.fromkeys(own)),
    }


def _noop_imports(reader: Reader) -> list[str]:
    if 'import \\"constants\\"' not in reader.text(DOC_SOURCE):
        raise ExtractError(f"{DOC_SOURCE}: no longer says that import \"constants\" does nothing")
    return ["constants"]


def _add_filter_types(commands: list[dict], tables: Tables, reader: Reader, mix_header: str) -> None:
    text = reader.text(mix_header)
    start = re.search(r"c_poleMixingPresets\[\]\s*=\s*\{", text)
    if not start:
        raise ExtractError(f"{mix_header}: no c_poleMixingPresets table")
    rows = split_top(text[start.end() : match_close(text, start.end() - 1)])
    # the name of a mix is the last string of its row
    presets = [re.findall(r'"((?:[^"\\]|\\.)*)"', row)[-1] for row in rows]
    aliases = [split_top(inner(item)) for item in tables.elements("c_filterAliases")]
    values = []
    for alias, preset in aliases:
        alias, preset = Tables.string(alias), Tables.string(preset)
        if preset not in presets:
            raise ExtractError(f"filter alias {alias} names {preset!r}, which is not a filter mix")
        values.append({"name": alias, "doc": f"the mix {preset!r}"})
    values += [{"name": preset} for preset in presets]
    for command in commands:
        if command["name"] == "SetFilter":
            for value in command["fields"]:
                if value["name"] == "type":
                    value["values"] = values
                    return
    raise ExtractError("SetFilter has no field type to attach the filter names to")


def render(api: dict) -> str:
    return json.dumps(api, indent=2, ensure_ascii=False) + "\n"
