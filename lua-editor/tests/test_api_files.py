"""The generated apis/<plugin>/api.json, checked against the factory scripts of the repository (plugins/<plugin>/scripts)
and, when the DSP repo is the parent of this folder, against what its sources give now."""

from __future__ import annotations

import json
import os
import re
import shutil
from pathlib import Path

import pytest

from lua_editor.api.cpp_tables import match_close, split_top
from lua_editor.api.extract import PLUGINS, extract_plugin, render

ROOT = Path(__file__).resolve().parent.parent
DSP = Path(os.environ.get("LEDIT_DSP_REPO", ROOT.parent))


def load(plugin: str) -> dict:
    return json.loads((ROOT / "apis" / plugin / "api.json").read_text(encoding="utf-8"))


def scripts(plugin: str) -> list[Path]:
    folder = DSP / "plugins" / plugin / "scripts"
    found = sorted(folder.glob("*.lua"))
    if not found:
        pytest.skip(f"no scripts in {folder} (LEDIT_DSP_REPO picks the repository)")
    return found


# the shape of the files

COUNTS = {
    "morphexsynth": {"commands": 14, "fields": 45, "hooks": 6, "enums": 6, "members": 44, "libraries": 0},
    "midiscript": {"commands": 10, "fields": 19, "hooks": 11, "enums": 1, "members": 8, "libraries": 3},
    "shipahoy": {"commands": 9, "fields": 11, "hooks": 9, "enums": 2, "members": 24, "libraries": 1},
}


@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_tables_are_complete(plugin: str) -> None:
    api = load(plugin)
    want = COUNTS[plugin]
    assert len(api["commands"]) == want["commands"]
    assert sum(len(c["fields"]) for c in api["commands"]) == want["fields"]
    assert len(api["hooks"]) == want["hooks"]
    assert len(api["enums"]) == want["enums"]
    assert sum(len(e["entries"]) for e in api["enums"]) == want["members"]
    assert len(api["libraries"]) == want["libraries"]
    assert api["lua"] == "5.4.8"
    assert api["limits"]["textMaxLength"] == 31
    assert api["limits"]["scriptMaxBytes"] == 256 * 1024


@pytest.mark.parametrize("plugin", PLUGINS)
def test_every_enum_a_field_names_exists(plugin: str) -> None:
    api = load(plugin)
    enums = {e["name"] for e in api["enums"]}
    named = {f["enum"] for c in api["commands"] for f in c["args"] + c["fields"] if "enum" in f}
    assert named <= enums


@pytest.mark.parametrize("plugin", PLUGINS)
def test_every_source_has_a_hash_and_the_list_is_sorted(plugin: str) -> None:
    sources = load(plugin)["sources"]
    assert [s["path"] for s in sources] == sorted(s["path"] for s in sources)
    assert all(re.fullmatch(r"[0-9a-f]{64}", s["sha256"]) for s in sources)


def test_morphexsynth_enum_numbers_and_filter_names() -> None:
    api = load("morphexsynth")
    enums = {e["name"]: {x["name"]: x["value"] for x in e["entries"]} for e in api["enums"]}
    assert enums["CtrlTarget"]["Distortion"] == 16
    assert enums["OscWaveform"] == {"Triangle": 0, "SharkFin": 1, "Saw": 2, "Square": 3, "White": 4}
    assert set(enums) == {"CtrlSource", "CtrlCurve", "CtrlTarget", "CtrlValueType", "OscWaveform", "LfoWaveform"}
    filter_type = next(f for c in api["commands"] if c["name"] == "SetFilter" for f in c["fields"] if f["name"] == "type")
    names = [v["name"] for v in filter_type["values"]]
    assert len(names) == 15 + 35
    assert "LP4" in names and "4 pole lp" in names and "Bell" in names
    assert max(map(len, names)) <= api["limits"]["textMaxLength"]


def test_midiscript_timers_and_libraries_come_through() -> None:
    api = load("midiscript")
    commands = {c["name"]: c for c in api["commands"]}
    assert {"SetTimer", "StopTimer", "Pass", "Panic"} <= set(commands)
    assert commands["SetTimer"]["args"][0]["max"] == 7
    assert [f["name"] for f in commands["SetTimer"]["fields"]] == ["ms", "ticks", "repeating"]
    assert {"OnTick", "OnTransportStart", "OnTransportStop", "OnTimer", "OnOther"} <= {h["name"] for h in api["hooks"]}
    libraries = {lib["global_name"]: lib for lib in api["libraries"]}
    assert set(libraries) == {"Scales", "Chords", "Rhythm"}
    assert libraries["Scales"]["import_name"] == "scales"
    assert {m["name"] for m in libraries["Scales"]["members"]} >= {"Major", "Blues", "quantise", "contains"}


def test_shipahoy_pitch_hooks_and_notes_library_come_through() -> None:
    api = load("shipahoy")
    hooks = {h["name"]: h for h in api["hooks"]}
    assert hooks["OnNewPitch"]["args"] == ["note", "hz", "pitch", "periodicity", "level"]
    assert hooks["OnPitchEnd"]["args"] == ["note"]
    assert hooks["OnControl"]["args"] == ["x", "y"]
    assert "without the lead" in hooks["OnNewPitch"]["doc"]
    commands = {c["name"]: c for c in api["commands"]}
    assert commands["GetX"]["args"] == [] and "0 to 100" in commands["GetX"]["doc"]  # a getter, as a command without arguments
    (notes,) = api["libraries"]
    assert (notes["import_name"], notes["global_name"]) == ("notes", "Notes")
    members = {m["name"]: m for m in notes["members"]}
    assert set(members) == {
        "size", "fallback", "list", "remember", "played", "last", "clear", "window", "pitchClasses", "has",
        "intervals", "sequence",
    }
    assert members["intervals"]["params"] == ["root"]
    assert all(members[name]["doc"] for name in members if members[name]["kind"] == "function")


# the example scripts use the API as the tables describe it


def strip_lua_comments(text: str) -> str:
    return re.sub(r"--[^\n]*", "", text)


def calls(text: str):
    for m in re.finditer(r"(?<![\w.:])([A-Z]\w*)\s*\(", text):
        try:
            end = match_close(text, m.end() - 1)
        except Exception:
            continue
        yield m.group(1), text[m.end() : end]


def table_keys(arguments: str) -> list[tuple[str, str]]:
    start = arguments.find("{")
    if start < 0:
        return []
    end = match_close(arguments, start)
    keys = []
    for part in split_top(arguments[start + 1 : end]):
        m = re.fullmatch(r"(\w+)\s*=\s*(.+)", part, re.S)
        if m:
            keys.append((m.group(1), m.group(2).strip()))
    return keys


@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_example_scripts_fit_the_tables(plugin: str) -> None:
    api = load(plugin)
    commands = {c["name"]: c for c in api["commands"]}
    hooks = {h["name"] for h in api["hooks"]}
    enums = {e["name"]: {x["name"] for x in e["entries"]} for e in api["enums"]}
    libraries = {lib["global_name"] for lib in api["libraries"]}
    problems: list[str] = []
    for path in scripts(plugin):
        text = strip_lua_comments(path.read_text(encoding="utf-8"))
        for enum, member in re.findall(r"\b([A-Z]\w*)\.([A-Z]\w*)\b", text):
            if enum in enums and member not in enums[enum]:
                problems.append(f"{path.name}: {enum}.{member}")
        for name, arguments in calls(text):
            if name in commands:
                command = commands[name]
                fields = {f["name"]: f for f in command["fields"]}
                for key, raw in table_keys(arguments):
                    if key not in fields:
                        problems.append(f"{path.name}: {name} has no field {key}")
                    elif re.fullmatch(r"-?\d+(\.\d+)?", raw) and "min" in fields[key]:
                        if not fields[key]["min"] <= float(raw) <= fields[key]["max"]:
                            problems.append(f"{path.name}: {name}.{key} = {raw} is out of range")
                positional = [a for a in split_top(arguments.split("{")[0]) if a]
                if len(positional) != len(command["args"]):
                    problems.append(f"{path.name}: {name} takes {len(command['args'])} values")
            elif name not in hooks | libraries | {"Log"} and not re.search(rf"function\s+{name}\b", text):
                problems.append(f"{path.name}: unknown call {name}")
        for hook in re.findall(r"^function\s+(On\w+)", text, re.M):
            if hook not in hooks:
                problems.append(f"{path.name}: unknown hook {hook}")
        for module, member in re.findall(r"\b(Scales|Chords|Rhythm|Notes)\.(\w+)", text):
            names = {m["name"] for lib in api["libraries"] if lib["global_name"] == module for m in lib["members"]}
            if member not in names:
                problems.append(f"{path.name}: {module}.{member} is not in the library")
    assert problems == []


# against the sources

needs_dsp = pytest.mark.skipif(not (DSP / "plugins").is_dir(), reason="the DSP repo is not the parent of lua-editor")


@needs_dsp
@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_committed_file_is_what_the_sources_give_now(plugin: str) -> None:
    current = (ROOT / "apis" / plugin / "api.json").read_text(encoding="utf-8")
    assert render(extract_plugin(DSP, plugin)) == current, "run: python tools/api_from_headers.py"


def test_a_draft_in_the_scripts_folder_is_not_an_api_source(tmp_path: Path) -> None:
    # a tree of links to the repository, except plugins/shipahoy/scripts which is a copy with a draft added
    for entry in DSP.iterdir():
        if entry.name not in (".git", "plugins", "build-tests", "lua-editor"):
            (tmp_path / entry.name).symlink_to(entry)
    plugins = tmp_path / "plugins"
    plugins.mkdir()
    for entry in (DSP / "plugins").iterdir():
        if entry.name != "shipahoy":
            (plugins / entry.name).symlink_to(entry)
    shutil.copytree(DSP / "plugins" / "shipahoy", plugins / "shipahoy", symlinks=True)
    draft = plugins / "shipahoy" / "scripts" / "user-1.lua"
    draft.write_text('-- Draft: x.\n-- import "draft"\nDraft = {}\n', encoding="utf-8")
    assert extract_plugin(tmp_path, "shipahoy") == extract_plugin(DSP, "shipahoy")
