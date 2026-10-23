from __future__ import annotations

from pathlib import Path

import pytest

from lua_editor.api.html import anchor_for, main, render_api_html
from lua_editor.api.model import ApiRegistry

APIS = Path(__file__).resolve().parent.parent / "apis"
PLUGINS = ("shipahoy", "morphexsynth", "midiscript")


@pytest.fixture(scope="module")
def registry() -> ApiRegistry:
    return ApiRegistry.from_folder(APIS)


@pytest.mark.parametrize("plugin", PLUGINS)
def test_every_command_hook_enum_and_library_member_is_on_the_page(registry: ApiRegistry, plugin: str) -> None:
    api = registry.for_path(plugin)
    assert api is not None
    page = render_api_html(api)
    for name in api.commands:
        assert f"id='cmd-{name}'" in page
    for name in api.hooks:
        assert f"id='hook-{name}'" in page
    for name, enum in api.enums.items():
        assert f"id='enum-{name}'" in page
        for entry in enum.entries:
            assert f"<code>{entry}</code>" in page
    for library in api.libraries:
        assert f"id='lib-{library.global_name}'" in page
        for member in library.members:
            assert f".{member.name}" in page
    for command in api.commands.values():
        for value in command.args + command.fields:
            assert f"<code>{value.name}</code>" in page


def test_page_is_self_contained_and_ascii(registry: ApiRegistry) -> None:
    page = render_api_html(registry.for_path("shipahoy"))
    page.encode("ascii")
    assert "http://" not in page and "https://" not in page and "<link" not in page


def test_text_is_escaped() -> None:
    api = ApiRegistry.from_folder(APIS).for_path("shipahoy")  # its own copy: the test changes it
    command = next(iter(api.commands.values()))
    command.doc = "a < b & <script>"
    page = render_api_html(api)
    assert "a &lt; b &amp; &lt;script&gt;" in page


def test_ranges_and_defaults_are_shown(registry: ApiRegistry) -> None:
    page = render_api_html(registry.for_path("shipahoy"))
    assert "number 0 to 127, default 69" in page


def test_anchor_for_words(registry: ApiRegistry) -> None:
    api = registry.for_path("shipahoy")
    assert anchor_for(api, "SetCutoff") == "cmd-SetCutoff"
    assert anchor_for(api, "OnNewPitch") == "hook-OnNewPitch"
    assert anchor_for(api, "Ticks") == "enum-Ticks"
    assert anchor_for(api, "Notes") == "lib-Notes"
    assert anchor_for(api, "notes") == "lib-Notes"
    assert anchor_for(api, "local") is None


def test_focus_scrolls_to_the_anchor(registry: ApiRegistry) -> None:
    api = registry.for_path("shipahoy")
    assert "getElementById(\"cmd-SetCutoff\")" in render_api_html(api, "cmd-SetCutoff")
    assert "scrollIntoView" not in render_api_html(api)


def test_command_line_writes_a_file(tmp_path: Path) -> None:
    out = tmp_path / "api.html"
    assert main(["shipahoy", "--apis", str(APIS), "--out", str(out)]) == 0
    assert out.read_text(encoding="utf-8").startswith("<!doctype html>")
    with pytest.raises(SystemExit):
        main(["nothing", "--apis", str(APIS)])
