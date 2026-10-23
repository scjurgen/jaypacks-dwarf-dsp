from __future__ import annotations

import os
from pathlib import Path

from lua_editor.api.lua_modules import mask, scan_modules

LIBRARY = '''-- Tools: small helpers.
-- import "tools"
--
--   Tools.One, Tools.Two, ...        the numbers
--   Tools.add(a, b)                  the sum of `a` and
--                                    `b`
--   Tools:scale(factor)              a method

local hidden = {Not = 1}
Tools = {
    One = 1,
    Two = {2, 2},
    twice = function(x) return 2 * x end,
    Text = "{ not a brace } -- not a comment",
}

function Tools.add(a, b)
    Other = {Inner = 1}
    return a + b
end

function Tools:scale(factor) end
Tools.Late = 3
'''


def members(text: str, name: str = "tools.lua"):
    modules = scan_modules(name, text)
    assert len(modules) == 1
    return modules[0], {m.name: m for m in modules[0].members}


def test_a_global_table_is_a_module_named_after_the_file() -> None:
    module, _ = members(LIBRARY)
    assert (module.import_name, module.global_name, module.file) == ("tools", "Tools", "tools.lua")
    assert module.summary == "small helpers."


def test_members_of_the_constructor_and_of_function_definitions_are_found() -> None:
    _, by_name = members(LIBRARY)
    assert list(by_name) == ["One", "Two", "twice", "Text", "add", "scale", "Late"]
    assert (by_name["One"].kind, by_name["One"].value) == ("value", "1")
    assert (by_name["Two"].kind, by_name["Two"].value) == ("table", "{2, 2}")
    assert (by_name["twice"].kind, by_name["twice"].params) == ("function", ["x"])
    assert (by_name["add"].kind, by_name["add"].params, by_name["add"].method) == ("function", ["a", "b"], False)
    assert (by_name["scale"].params, by_name["scale"].method) == (["factor"], True)
    assert by_name["Late"].kind == "value"


def test_text_in_strings_and_comments_and_locals_inside_functions_are_not_members() -> None:
    modules = scan_modules("tools.lua", LIBRARY)
    assert [m.global_name for m in modules] == ["Tools"]
    assert "Not" not in {m.name for m in modules[0].members}
    assert "Inner" not in {m.name for m in modules[0].members}


def test_the_header_comment_documents_members_also_over_several_lines() -> None:
    _, by_name = members(LIBRARY)
    assert by_name["add"].doc == "the sum of `a` and `b`"
    assert by_name["scale"].doc == "a method"
    assert by_name["One"].doc == "the numbers"
    assert by_name["Two"].doc == "the numbers"
    assert by_name["Text"].doc == "the numbers"
    assert by_name["twice"].doc == ""


def test_a_script_without_a_global_table_is_no_module() -> None:
    script = "local held = {}\nfunction OnNoteOn(channel, note, velocity)\n    held[note] = true\nend\n"
    assert scan_modules("arp.lua", script) == []


def test_an_empty_global_table_with_functions_is_a_module() -> None:
    module = scan_modules("r.lua", "Rhythm = {}\nfunction Rhythm.swing(step) end\n")[0]
    assert [m.name for m in module.members] == ["swing"]


def test_mask_keeps_the_length_and_the_lines() -> None:
    text = 'a = "x{y"  -- c {\nb = [[ {\n]] --[[ {\n]] c'
    masked = mask(text)
    assert len(masked) == len(text)
    assert masked.count("\n") == text.count("\n")
    assert "{" not in masked.replace("= [[ {", "")


def test_an_unterminated_string_does_not_change_the_length() -> None:
    text = 'a = "open\nb = 1\n'
    assert len(mask(text)) == len(text)


SANDBOX = (
    Path(os.environ.get("LEDIT_DSP_REPO", Path(__file__).resolve().parent.parent.parent))
    / "plugins" / "midiscript" / "scripts"
)


def test_the_factory_libraries_give_their_documented_members() -> None:
    scales = scan_modules("scales.lua", (SANDBOX / "scales.lua").read_text(encoding="utf-8"))[0]
    by_name = {m.name: m for m in scales.members}
    assert scales.global_name == "Scales"
    assert len([m for m in scales.members if m.kind == "table"]) == 12
    assert by_name["Major"].value == "{0, 2, 4, 5, 7, 9, 11}"
    assert by_name["quantise"].params == ["note", "root", "shape"]
    assert by_name["quantise"].doc == "the note of the scale nearest to `note` (a tie goes down)"
    rhythm = scan_modules("rhythm.lua", (SANDBOX / "rhythm.lua").read_text(encoding="utf-8"))[0]
    assert "pushed back by amount" in {m.name: m for m in rhythm.members}["swing"].doc
    for script in SANDBOX.glob("*.lua"):
        if script.name.startswith("user-"):
            continue
        found = scan_modules(script.name, script.read_text(encoding="utf-8"))
        assert bool(found) == (script.stem in ("scales", "chords", "rhythm")), script.name
