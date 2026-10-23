from __future__ import annotations

import os
from pathlib import Path

import pytest

from lua_editor.api.model import ApiRegistry
from lua_editor.check.api_check import check_api, check_size, declared_names
from lua_editor.check.luatokens import tokenize
from lua_editor.editor.vocabulary import Vocabulary

ROOT = Path(__file__).resolve().parent.parent
APIS = ApiRegistry.from_folder(ROOT / "apis")
SCRIPTS = Path(os.environ.get("LEDIT_DSP_REPO", ROOT.parent)) / "plugins"


@pytest.fixture(scope="module")
def morph() -> Vocabulary:
    return Vocabulary(APIS.for_path("morphexsynth/x.lua"))


@pytest.fixture(scope="module")
def midi() -> Vocabulary:
    return Vocabulary(APIS.for_path("midiscript/x.lua"))


def found(text: str, vocab: Vocabulary, names: set[str] | None = None):
    return [(p.severity, p.line, p.column, p.message) for p in check_api(text, vocab, names)]


def messages(text: str, vocab: Vocabulary, names: set[str] | None = None) -> list[str]:
    return [f"{severity}: {message}" for severity, _line, _column, message in found(text, vocab, names)]


# the tokenizer

def test_tokens_carry_positions_and_values() -> None:
    tokens = tokenize('x = 0x10 .. "a\\nb" -- c\n  y = [[long]]')
    assert [(t.kind, t.text) for t in tokens] == [
        ("name", "x"), ("op", "="), ("number", "0x10"), ("op", ".."), ("string", '"a\\nb"'),
        ("name", "y"), ("op", "="), ("string", "[[long]]"),
    ]
    assert tokens[2].value == 16 and tokens[4].value == "a\nb" and tokens[7].value == "long"
    assert (tokens[5].line, tokens[5].column) == (2, 3)


def test_declared_names_cover_locals_functions_parameters_loops_and_assignments() -> None:
    text = (
        "local a, b = 1, 2\nlocal function f(p, q) end\nfunction g(r) end\nfunction T:m() end\n"
        "for i, v in pairs(t) do end\nfor k = 1, 2 do end\nx, y = 1, 2\nlocal z <const> = 3\nt = {key = 1}\n"
    )
    assert declared_names(tokenize(text)) == {
        "a", "b", "f", "p", "q", "g", "r", "self", "i", "v", "k", "x", "y", "z", "t", "key",
    }


# functions and the sandbox

def test_a_clean_script_has_no_problems(morph: Vocabulary) -> None:
    text = (
        "local function helper(x) return x * 2 end\nfunction OnStart()\n  SetVolume(helper(3))\n  Log(math.floor(1.5))\n"
        "  local t = {}\n  table.insert(t, 1)\n  for i, v in ipairs(t) do print(v) end\nend\n"
    )
    assert found(text, morph) == []


def test_an_unknown_function_is_an_error_with_a_suggestion(morph: Vocabulary) -> None:
    assert messages("SetOscilator(0)", morph) == ["error: unknown function SetOscilator; did you mean SetOscillator?"]
    assert found("x = foo(1)", morph)[0][:3] == ("error", 1, 5)
    assert messages("x = unpack(t)", morph) == ["error: unknown function unpack; use table.unpack"]


def test_a_function_the_script_defines_or_receives_is_known(morph: Vocabulary) -> None:
    assert found("function foo() end\nfoo()", morph) == []
    assert found("local function foo() end\nfoo()", morph) == []
    assert found("local function run(cb) cb(1) end", morph) == []
    assert found("local t = {}\nt.method(1)\nt:other(2)", morph) == []
    assert found("handler = function() end\nhandler()", morph) == []


def test_what_the_sandbox_removed_is_an_error(morph: Vocabulary) -> None:
    assert messages("x = os.time()", morph) == [
        "error: os is not available in the Morphexsynth sandbox (it has base, math, table, string)"
    ]
    assert len(found("io.write('x')\nload('y')\nrequire('z')\ncollectgarbage()", morph)) == 4
    assert messages("x = string.dump(f)", morph) == ["error: string.dump is removed from the Morphexsynth sandbox"]
    assert found("local os = {}\nx = os.time", morph) == []  # the script's own variable


def test_the_standard_library_is_checked(morph: Vocabulary) -> None:
    assert found("x = math.floor(1) + string.format('%d', 1) + table.concat(t)", morph) == []
    assert messages("x = math.flor(1)", morph) == ["error: math.flor does not exist; did you mean floor?"]
    assert messages("x = math.pow(2, 3)", morph) == ["error: math.pow does not exist; use the ^ operator"]


# members

def test_enum_members_are_checked(morph: Vocabulary) -> None:
    assert found("x = OscWaveform.Square", morph) == []
    assert messages("x = OscWaveform.Squar", morph) == ["error: OscWaveform has no member Squar; did you mean Square?"]
    assert found("local OscWaveform = {Foo = 1}\nx = OscWaveform.Foo", morph) == []


def test_libraries_need_their_import_and_their_members(midi: Vocabulary) -> None:
    assert messages("x = Scales.Major", midi) == ['error: Scales comes from import "scales", which this script does not do']
    assert found('import "scales"\nx = Scales.Major\ny = Scales.quantise(1, 0, x)', midi, {"scales"}) == []
    assert messages('import "scales"\nx = Scales.Majr', midi, {"scales"}) == [
        "error: Scales has no member Majr; did you mean Major?"
    ]
    assert found('import "scales"\nScales.Mine = {0, 7}\nx = Scales.Mine', midi, {"scales"}) == []
    assert found("Scales = {}\nScales.x = 1", midi) == []  # a library file defines its own table


def test_a_library_of_another_plugin_is_unknown(morph: Vocabulary) -> None:
    assert messages('import "scales"\nx = Scales.Major', morph, set()) == [
        'error: import "scales": there is no scales.lua here'
    ]


# import

def test_import_names_are_checked_against_the_folder(midi: Vocabulary) -> None:
    assert found('import "constants"\nimport "mine"', midi, {"mine"}) == []
    assert messages('import "mien"', midi, {"mine", "other"}) == ['error: import "mien": there is no mien.lua here; did you mean mine?']
    assert messages('import "bad name"', midi, set()) == ["error: an import name has 1 to 40 letters, digits, - or _"]
    assert found('import "mine"', midi, None) == []  # the folder is not known yet
    assert found('import("scales")', midi, set()) == []  # a factory library is always there


# hooks

def test_hooks_are_checked_against_the_plugin(morph: Vocabulary, midi: Vocabulary) -> None:
    assert found("function OnNoteOn(c, n, v) end", morph) == []
    assert messages("function OnNoteOnn(c) end", morph) == ["warning: OnNoteOnn is not a hook of Morphexsynth; did you mean OnNoteOn?"]
    assert messages("function OnTick(t) end", morph)[0].startswith("warning: OnTick is not a hook of Morphexsynth")
    assert found("function OnTick(t) end", midi) == []
    assert messages("local function OnNoteOn() end", morph) == ["warning: OnNoteOn is local, so the plugin never calls it; remove local"]
    assert found("function OnHelper() end", morph)[0][0] == "warning"
    assert found("function helper() end", morph) == []


# commands

def test_the_number_of_arguments_is_checked(morph: Vocabulary) -> None:
    assert found("SetVolume(1)\nSetOscillator(0, {})\nSetOscillator(0)\nSetAmpEnvelope()\nSetAmpEnvelope({})", morph) == []
    assert messages("SetVolume()", morph) == ["error: SetVolume expects 1 argument(s) and gets 0"]
    assert messages("SetVolume(1, 2)", morph) == ["error: SetVolume takes at most 1 argument(s) and gets 2"]
    assert messages("SetOscillator(0, {}, 3)", morph) == ["error: SetOscillator takes at most 2 argument(s) and gets 3"]
    assert messages("SetOscillator({})", morph) == ["error: SetOscillator: argument index must be a number, not a table"]
    assert messages("SetFilter{ type = 'LP4' }", morph) == []


def test_the_last_argument_must_be_a_table(morph: Vocabulary) -> None:
    assert messages("SetOscillator(0, 5)", morph) == ["error: SetOscillator: the last argument must be a table of fields"]
    assert found("SetOscillator(0, nil)\nSetOscillator(0, fields)", morph) == []


def test_unknown_fields_and_nameless_entries_are_warnings(morph: Vocabulary) -> None:
    assert messages("SetOscillator(0, { levl = 1 })", morph) == [
        "warning: SetOscillator has no field levl; the plugin ignores it; did you mean level?"
    ]
    assert found("SetOscillator(0, { level = 1 })", morph) == []
    result = found("SetOscillator(0, { levl = 1 })", morph)
    assert (result[0][1], result[0][2]) == (1, 20)
    assert messages("SetOscillator(0, { 5 })", morph) == ["warning: SetOscillator: an entry without a name is ignored"]


def test_literals_are_checked_against_kind_and_range(morph: Vocabulary) -> None:
    assert messages("SetVolume('loud')", morph) == ["error: SetVolume: argument db must be a number, not a string"]
    assert messages("SetOscillator(0, { level = true })", morph) == ["error: SetOscillator.level must be a number, not a boolean"]
    assert messages("SetOscillator(3, {})", morph) == ["warning: SetOscillator: argument index = 3 is outside 0 to 2; the plugin ignores the call"]
    assert messages("SetOscillator(1.5, {})", morph) == ["warning: SetOscillator: argument index = 1.5 is not a whole number; the plugin ignores the call"]
    assert messages("SetOscillator(0, { detune = 60 })", morph) == ["warning: SetOscillator.detune = 60 is outside -48 to 48; it is limited to that range"]
    assert messages("SetOscillator(0, { detune = -60 })", morph) == ["warning: SetOscillator.detune = -60 is outside -48 to 48; it is limited to that range"]
    assert found("SetOscillator(0, { detune = -48, level = 1, pitchFactor = 0.01 })", morph) == []
    assert found("SetOscillator(0, { detune = offset, level = 2 * x })", morph) == []  # not literals: not checked


def test_flags_and_text_values(midi: Vocabulary, morph: Vocabulary) -> None:
    assert found("SetTimer(0, { ms = 100, repeating = true })", midi) == []
    assert messages("SetTimer(0, { repeating = 1 })", midi) == ["error: SetTimer.repeating must be true or false"]
    assert messages("SetFilter({ type = 3 })", morph) == ["error: SetFilter.type must be a string"]
    assert messages("SetFilter({ type = 'LP9' })", morph) == [
        "warning: SetFilter.type: 'LP9' is not a known name; the plugin ignores the call; did you mean LP4?"
    ]
    assert found("SetFilter({ type = 'LP4' })\nSetFilter({ type = '4 pole lp' })", morph) == []
    long = "x" * 32
    assert messages(f"SetFilter({{ type = '{long}' }})", morph) == [
        "warning: SetFilter.type is longer than 31 characters; the plugin ignores the call"
    ]


def test_enum_values_are_checked_against_the_field(morph: Vocabulary) -> None:
    assert found("SetOscillator(0, { waveform = OscWaveform.Saw })", morph) == []
    assert messages("SetOscillator(0, { waveform = CtrlCurve.Linear })", morph) == [
        "warning: SetOscillator.waveform wants a OscWaveform constant, this is a CtrlCurve one"
    ]
    assert messages("SetOscillator(0, { waveform = OscWaveform.Bogus })", morph) == ["error: OscWaveform has no member Bogus"]
    assert found("SetOscillator(0, { waveform = 4 })", morph) == []
    assert messages("SetOscillator(0, { waveform = 9 })", morph) == ["warning: SetOscillator.waveform = 9 is outside 0 to 4; the plugin ignores the call"]


def test_midi_commands_check_their_ranges(midi: Vocabulary) -> None:
    assert found("Note(1, 60, 100, { lengthTicks = Ticks.Quarter })", midi) == []
    assert messages("Note(17, 60, 100)", midi) == ["warning: Note: argument channel = 17 is outside 1 to 16; the plugin ignores the call"]
    assert messages("NoteOn(1, 60)", midi) == ["error: NoteOn expects 3 argument(s) and gets 2"]
    assert messages("Note(1, 60, 100, { lenghtMs = 5 })", midi)[0].startswith("warning: Note has no field lenghtMs")


def test_a_call_that_spans_lines_marks_the_right_place(morph: Vocabulary) -> None:
    text = "SetOscillator(0, {\n    waveform = OscWaveform.Saw,\n    levl = 1,\n})"
    result = found(text, morph)
    assert [(r[1], r[2]) for r in result] == [(3, 5)]


# the size

def test_the_size_limit(morph: Vocabulary) -> None:
    assert check_size(1000, morph) == []
    problem = check_size(300 * 1024, morph, "first line")[0]
    assert problem.severity == "error" and problem.line == 1
    assert problem.message == "the script is 300 KiB; the plugin loads at most 256 KiB"
    assert check_size(10**9, Vocabulary(None)) == []


def test_without_an_api_nothing_is_checked() -> None:
    assert check_api("os.time(foo())", Vocabulary(None)) == []


# the factory scripts are the corpus that must stay clean

@pytest.mark.parametrize("plugin", ["morphexsynth", "midiscript", "shipahoy"])
def test_the_factory_scripts_give_no_problems(plugin: str) -> None:
    vocab = Vocabulary(APIS.for_path(f"{plugin}/x.lua"))
    folder = SCRIPTS / plugin / "scripts"
    scripts = sorted(p for p in folder.glob("*.lua") if not p.name.startswith("user-"))
    names = {p.stem for p in scripts}
    assert scripts
    for path in scripts:
        assert check_api(path.read_text(encoding="utf-8"), vocab, names, path.stem) == [], path.name
