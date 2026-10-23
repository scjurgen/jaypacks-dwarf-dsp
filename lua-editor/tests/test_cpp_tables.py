from __future__ import annotations

from pathlib import Path

import pytest

from lua_editor.api.cpp_tables import ExtractError, Tables, split_top, strip_comments

HEADER = r"""
#define JP_ENTRY(Type, Name) {#Name, static_cast<int>(Type::Name)}
inline constexpr double c_max{100.0};
inline constexpr int c_count{4};

inline constexpr std::array<ValueSpec, 1> c_one{
    {{"a", ValueKind::Number, 0.0, c_max, 1.5, "doc of a, with {braces} " "and more", "Mode"}}};
inline constexpr std::array<ValueSpec, 2> c_two{{
    {"x", ValueKind::Integer, -1.0, static_cast<double>(c_count - 1), 0.0, "x"},
    // {"ghost", ValueKind::Number, 0, 1, 0, "commented out"},
    {"t", ValueKind::Text, 0.0, 0.0, 0.0, "text"},
}};
inline constexpr ValueSpec c_named{"flag", ValueKind::Flag, 0.0, 1.0, 1.0, "a named value"};
inline constexpr std::array<ValueSpec, 2> c_refs{{c_named, c_named}};

inline constexpr std::array<CommandSpec, 3> c_commands{{
    {"Cmd", "first", c_one.data(), 1, c_two.data(), 2},
    {"Other", "second", nullptr, 0, c_refs.data(), 2},
    c_extra[0],
}};
inline constexpr std::array<CommandSpec, 1> c_extra{{{"Extra", "third", nullptr, 0, nullptr, 0}}};

inline constexpr std::array<const char*, 2> c_noteArgs{"channel", "note"};
inline constexpr std::array<HookSpec, 2> c_hooks{{
    {"OnStart", "once", nullptr, 0},
    {"OnNote", "a note", c_noteArgs.data(), 2},
}};

enum class Mode { Off, Soft, Hard = 5, Loud };
inline constexpr std::array<EnumEntry, 3> c_entries{{JP_ENTRY(Mode, Soft), JP_ENTRY(Mode, Hard), JP_ENTRY(Mode, Loud)}};
inline constexpr std::array<EnumEntry, 2> c_plain{{{"Whole", 96}, {"Half", 48}}};
inline constexpr std::array<EnumSpec, 2> c_enums{{
    {"Mode", "modes", c_entries.data(), 3},
    {"Ticks", "lengths", c_plain.data(), 2},
}};
"""


@pytest.fixture
def tables(tmp_path: Path) -> Tables:
    # c_extra is declared after its use above on purpose: the reader must not depend on the order
    path = tmp_path / "Script.h"
    path.write_text(HEADER, encoding="utf-8")
    return Tables([path])


def test_commands_have_their_values_with_kind_range_default_and_doc(tables: Tables) -> None:
    commands = tables.commands("c_commands")
    assert [c["name"] for c in commands] == ["Cmd", "Other", "Extra"]
    cmd = commands[0]
    assert cmd["args"] == [
        {"name": "a", "kind": "number", "min": 0, "max": 100, "default": 1.5, "doc": "doc of a, with {braces} and more", "enum": "Mode"}
    ]
    assert cmd["fields"][0] == {"name": "x", "kind": "integer", "min": -1, "max": 3, "default": 0, "doc": "x"}
    assert cmd["fields"][1] == {"name": "t", "kind": "text", "doc": "text"}


def test_named_values_and_entries_by_reference_are_resolved(tables: Tables) -> None:
    other = tables.commands("c_commands")[1]
    assert other["args"] == []
    assert [f["name"] for f in other["fields"]] == ["flag", "flag"]
    assert other["fields"][0]["default"] is True
    assert tables.commands("c_commands")[2]["doc"] == "third"


def test_hooks_have_their_argument_names(tables: Tables) -> None:
    assert tables.hooks("c_hooks") == [
        {"name": "OnStart", "doc": "once", "args": []},
        {"name": "OnNote", "doc": "a note", "args": ["channel", "note"]},
    ]


def test_enum_values_come_from_the_declaration_or_the_table(tables: Tables) -> None:
    enums = tables.enum_tables("c_enums")
    assert enums[0]["entries"] == [
        {"name": "Soft", "value": 1},
        {"name": "Hard", "value": 5},
        {"name": "Loud", "value": 6},
    ]
    assert enums[1]["entries"] == [{"name": "Whole", "value": 96}, {"name": "Half", "value": 48}]


def test_a_count_that_does_not_match_stops_the_extraction(tmp_path: Path) -> None:
    header = tmp_path / "bad.h"
    header.write_text(
        'inline constexpr std::array<ValueSpec, 1> c_v{{{"a", ValueKind::Flag, 0.0, 1.0, 0.0, "d"}}};\n'
        'inline constexpr std::array<CommandSpec, 1> c_commands{{{"C", "d", c_v.data(), 2, nullptr, 0}}};\n',
        encoding="utf-8",
    )
    with pytest.raises(ExtractError, match="2 values declared, found 1"):
        Tables([header]).commands("c_commands")


def test_a_table_with_fewer_elements_than_declared_stops_the_extraction(tmp_path: Path) -> None:
    header = tmp_path / "bad.h"
    header.write_text('inline constexpr std::array<HookSpec, 3> c_hooks{{{"OnStart", "d", nullptr, 0}}};\n', encoding="utf-8")
    with pytest.raises(ExtractError, match="declares 3 elements, found 1"):
        Tables([header]).hooks("c_hooks")


def test_an_enum_that_is_not_in_the_headers_stops_the_extraction(tmp_path: Path) -> None:
    header = tmp_path / "bad.h"
    header.write_text(
        "inline constexpr std::array<EnumEntry, 1> c_e{{JP_ENTRY(Missing, X)}};\n"
        'inline constexpr std::array<EnumSpec, 1> c_enums{{{"E", "d", c_e.data(), 1}}};\n',
        encoding="utf-8",
    )
    with pytest.raises(ExtractError, match="Missing::X"):
        Tables([header]).enum_tables("c_enums")


def test_a_name_that_is_not_a_constant_stops_the_number(tables: Tables) -> None:
    with pytest.raises(ExtractError):
        tables.number("c_unknown + 1")
    assert tables.number("c_max * 2 + 1") == 201


def test_comments_are_removed_but_not_inside_strings() -> None:
    assert strip_comments('a // b\n"x // y" /* c */ d') == 'a \n"x // y"  d'


def test_split_top_ignores_commas_in_brackets_and_strings() -> None:
    assert split_top('a, {b, c}, f(d, e), "g, h"') == ["a", "{b, c}", "f(d, e)", '"g, h"']
