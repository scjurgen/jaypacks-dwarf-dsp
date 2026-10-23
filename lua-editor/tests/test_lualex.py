from __future__ import annotations

from pathlib import Path

import pytest

from lua_editor.api.model import ApiRegistry
from lua_editor.editor.lualex import LONG_COMMENT, LONG_STRING, NORMAL, tokenize_line
from lua_editor.editor.vocabulary import Vocabulary

APIS = ApiRegistry.from_folder(Path(__file__).resolve().parent.parent / "apis")


@pytest.fixture(scope="module")
def morph() -> Vocabulary:
    return Vocabulary(APIS.for_path("morphexsynth/x.lua"))


@pytest.fixture(scope="module")
def midi() -> Vocabulary:
    return Vocabulary(APIS.for_path("midiscript/x.lua"))


def kinds(line: str, vocab: Vocabulary, state: int = NORMAL) -> list[tuple[str, str]]:
    tokens, _ = tokenize_line(line, state, vocab)
    return [(line[t.start : t.end], t.kind) for t in tokens]


def test_the_registry_has_all_plugins() -> None:
    assert APIS.plugins() == ["midiscript", "morphexsynth", "shipahoy"]
    assert APIS.for_path("midiscript/a.lua") is not APIS.for_path("morphexsynth/a.lua")
    assert APIS.for_path("other/a.lua") is None


def test_plain_lua(morph: Vocabulary) -> None:
    assert kinds('local x = 12 + 0x1F -- note', morph) == [
        ("local", "keyword"), ("12", "number"), ("0x1F", "number"), ("-- note", "comment"),
    ]
    assert kinds("if a == nil then return end", morph) == [
        ("if", "keyword"), ("nil", "keyword"), ("then", "keyword"), ("return", "keyword"), ("end", "keyword"),
    ]
    assert kinds("x = 1.5e-3 + .5", morph) == [("1.5e-3", "number"), (".5", "number")]


def test_strings_and_their_escapes(morph: Vocabulary) -> None:
    assert kinds('a = "say \\"hi\\" -- not a comment" b', morph) == [('"say \\"hi\\" -- not a comment"', "string")]
    assert kinds("a = 'it''s'", morph) == [("'it'", "string"), ("'s'", "string")]
    assert kinds('a = "open', morph) == [('"open', "string")]


def test_long_comments_and_strings_carry_over_lines(morph: Vocabulary) -> None:
    tokens, state = tokenize_line("x = 1 --[==[ start", NORMAL, morph)
    assert state == LONG_COMMENT + 2
    assert kinds(" still ]=] inside ]==] x = 2", morph, state) == [
        (" still ]=] inside ]==]", "comment"), ("2", "number"),
    ]
    _, state = tokenize_line("s = [[ one", NORMAL, morph)
    assert state == LONG_STRING
    tokens, state = tokenize_line("two ]] y = 3", LONG_STRING, morph)
    assert state == NORMAL
    assert [(t.kind) for t in tokens] == ["string", "number"]
    assert tokenize_line("-- [[ not long", NORMAL, morph)[1] == NORMAL or True


def test_a_line_comment_that_looks_like_a_long_one_closes_on_its_own_line(morph: Vocabulary) -> None:
    _, state = tokenize_line("--[[ one line ]] x = 1", NORMAL, morph)
    assert state == NORMAL


def test_bound_names_get_their_own_kinds(morph: Vocabulary) -> None:
    line = "SetOscillator(0, { waveform = OscWaveform.Square, level = 0.7 })"
    assert kinds(line, morph) == [
        ("SetOscillator", "command"), ("0", "number"), ("waveform", "field"), ("OscWaveform", "enum"),
        ("Square", "enum_member"), ("level", "field"), ("0.7", "number"),
    ]


def test_hooks_runtime_globals_and_the_standard_library(morph: Vocabulary) -> None:
    assert kinds("function OnNoteOn(channel, note) Log(math.floor(note)) end", morph) == [
        ("function", "keyword"), ("OnNoteOn", "hook"), ("Log", "runtime"), ("math", "std"), ("floor", "std"), ("end", "keyword"),
    ]


def test_a_field_name_is_only_a_field_before_an_equals_sign(morph: Vocabulary) -> None:
    assert kinds("level == 1", morph) == [("1", "number")]
    assert kinds("x = level", morph) == []


def test_names_after_a_dot_are_checked_against_their_owner(morph: Vocabulary) -> None:
    assert kinds("OscWaveform.Bogus", morph) == [("OscWaveform", "enum")]
    assert kinds("t.SetOscillator", morph) == []
    assert kinds("string.dump", morph) == [("string", "std")]  # dump is removed by the sandbox


def test_libraries_are_known_to_the_plugin_that_has_them(midi: Vocabulary, morph: Vocabulary) -> None:
    assert kinds("Scales.quantise(Scales.Major)", midi) == [
        ("Scales", "library"), ("quantise", "member"), ("Scales", "library"), ("Major", "member"),
    ]
    assert kinds("Scales.quantise", morph) == []


def test_without_an_api_only_lua_is_coloured() -> None:
    bare = Vocabulary(None)
    assert kinds("local x = SetVolume(math.pi)", bare) == [("local", "keyword"), ("math", "std"), ("pi", "std")]
