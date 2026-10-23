"""The examples and the guide of the plugins (plugins/<plugin>/scripts/examples and AGENTS.md), as api.json, the page and
the brief carry them. A wrong example teaches the wrong thing, so everything is checked against the real API."""

from __future__ import annotations

import html
import os
import re
from pathlib import Path

import pytest

from lua_editor.api.extract import _uses
from lua_editor.api.html import anchor_for, render_api_html
from lua_editor.api.markdown import render_api_markdown
from lua_editor.api.model import Api, ApiRegistry
from lua_editor.check.api_check import check_api
from lua_editor.check.syntax import check_syntax
from lua_editor.editor.vocabulary import Vocabulary

ROOT = Path(__file__).resolve().parent.parent
DSP = Path(os.environ.get("LEDIT_DSP_REPO", ROOT.parent))
APIS = ApiRegistry.from_folder(ROOT / "apis")
# the plugins with examples and a guide: all three scripted ones
PLUGINS = ["shipahoy", "midiscript", "morphexsynth"]
# misspelt on purpose, in the table of mistakes of the guides
MISSPELT = {"SetCutof"}


def api_of(plugin: str) -> Api:
    api = APIS.for_path(plugin)
    assert api is not None
    return api


def folder(plugin: str) -> Path:
    return DSP / "plugins" / plugin / "scripts"


@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_examples_are_collected_from_the_folder(plugin: str) -> None:
    files = sorted(p.name for p in (folder(plugin) / "examples").glob("*.lua"))
    api = api_of(plugin)
    assert files
    assert [Path(e.file).name for e in api.examples] == files
    assert all(e.file.startswith("examples/") for e in api.examples)
    assert len({e.id for e in api.examples}) == len(api.examples)
    assert api.guide == (folder(plugin) / "AGENTS.md").read_text(encoding="utf-8")


@pytest.mark.parametrize("plugin", PLUGINS)
def test_every_example_compiles_and_gives_no_api_problems(plugin: str) -> None:
    api = api_of(plugin)
    # opened from the examples folder: the neighbours for import are the factory libraries only
    vocab = Vocabulary(APIS.for_path(f"{plugin}/examples/x.lua"))
    libraries = {lib.import_name for lib in api.libraries}
    for example in api.examples:
        assert check_syntax(example.code) == [], example.file
        assert check_api(example.code, vocab, libraries, example.id) == [], example.file
        for imported in re.findall(r'import\s*\(?\s*"([^"]+)"', example.code):
            assert imported in libraries, f"{example.file} imports {imported}, which is not a factory library"


@pytest.mark.parametrize("plugin", PLUGINS)
def test_every_example_has_a_title_a_summary_and_uses(plugin: str) -> None:
    api = api_of(plugin)
    known = set(api.commands) | set(api.hooks) | set(api.enums) | {lib.global_name for lib in api.libraries}
    for example in api.examples:
        assert example.code.startswith("-- "), example.file
        assert example.title and not example.title.endswith("."), example.file
        assert len(example.summary) > 40, f"{example.file}: say what it shows and what to change"
        assert example.uses and set(example.uses) <= known, example.file
        assert 5 <= len(example.code.splitlines()) <= 40, f"{example.file}: one idea, a few lines"
        example.code.encode("ascii")


def test_uses_ignore_comments_strings_and_fields() -> None:
    api = ApiRegistry.from_folder(ROOT / "apis").for_path("shipahoy")
    commands = [{"name": n} for n in api.commands]
    hooks = [{"name": n} for n in api.hooks]
    enums = [{"name": n} for n in api.enums]
    libraries = [{"global_name": "Notes", "import_name": "notes"}]
    code = (
        '-- SetDrift in a comment\nimport "notes"\nlocal s = "SetTimer"\nlocal t = {}\nt.SetResonance = 1\n'
        "function OnStep(step) SetCutoff(60, { glide = FilterType.Lp12 }) end\n"
    )
    assert _uses(code, commands, hooks, enums, libraries) == ["Notes", "OnStep", "SetCutoff", "FilterType"]


@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_index_of_the_guide_lists_exactly_the_examples(plugin: str) -> None:
    api = api_of(plugin)
    listed = re.findall(r"`(examples/[^`]+\.lua)`", api.guide)
    assert sorted(listed) == sorted(e.file for e in api.examples)
    assert len(set(listed)) == len(listed)


@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_guide_names_only_what_exists(plugin: str) -> None:
    api = api_of(plugin)
    for word in set(re.findall(r"\b(?:Set|Get|On)[A-Z]\w*|\bStopTimer\b|\bSpeedRatio\b", api.guide)) - MISSPELT:
        assert word in api.commands or word in api.hooks, f"AGENTS.md names {word}, which is not in the API"
    for library in api.libraries:
        for member in set(re.findall(rf"\b{library.global_name}\.(\w+)", api.guide)):
            assert library.member(member) is not None, f"AGENTS.md names {library.global_name}.{member}"
    for enum in api.enums.values():
        for entry in set(re.findall(rf"\b{enum.name}\.(\w+)", api.guide)):
            assert entry in enum.entries, f"AGENTS.md names {enum.name}.{entry}"
    for word in set(re.findall(r'import "([^"]+)"', api.guide)) - {"name"}:  # "name" is the placeholder
        assert word in {lib.import_name for lib in api.libraries}, word


@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_guide_has_no_api_facts_of_its_own(plugin: str) -> None:
    # limits and constants are read from API.md; the guide only points to them
    guide = api_of(plugin).guide
    for key in ("handlerInstructions", "loadInstructions", "memoryBytes"):
        assert key not in guide
    assert "API.md" in guide and "constants.lua" in guide


@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_page_has_every_example_verbatim_and_uses_link_back(plugin: str) -> None:
    api = api_of(plugin)
    page = render_api_html(api)
    text = html.unescape(re.sub(r"<[^>]+>", "", page))
    for example in api.examples:
        assert example.code in text, example.file
        assert f"id='ex-{example.id}'" in page
        for name in example.uses:
            assert f"href='#ex-{example.id}'" in page
    for example in api.scripts:
        assert example.code in text, example.file
        assert f"id='script-{example.id}'" in page
    # under whatever the first example uses: a link back to it
    first = api.examples[0]
    anchor = anchor_for(api, first.uses[0])
    section = page[page.index(f"id='{anchor}'") :]
    section = section[: section.index("</section>")]
    assert "Used in:" in section and f"#ex-{first.id}" in section


@pytest.mark.parametrize("plugin", PLUGINS)
def test_the_brief_has_the_guide_the_api_and_every_example(plugin: str) -> None:
    api = api_of(plugin)
    brief = render_api_markdown(api)
    brief.encode("ascii")
    assert api.guide.splitlines()[0].lstrip("# ") in brief
    assert "Writing a" in brief and "| Log says | Cause and fix |" in brief
    for name in api.commands:
        assert f"{name}(" in brief
    for name in api.hooks:
        assert f"function {name}(" in brief
    for example in api.examples:
        assert example.code.rstrip("\n") in brief, example.file
    assert brief.index("Part 1") < brief.index("Part 2") < brief.index("Part 3")


def test_the_brief_fence_is_longer_than_any_backticks_in_the_code() -> None:
    api = ApiRegistry.from_folder(ROOT / "apis").for_path("shipahoy")  # its own copy: the test changes it
    api.examples[0].code += "\n-- ```` four\n"
    brief = render_api_markdown(api)
    assert "`````lua" in brief
