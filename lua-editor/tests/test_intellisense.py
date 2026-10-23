from __future__ import annotations

from pathlib import Path

import pytest

from lua_editor.api.model import ApiRegistry
from lua_editor.editor.context import enclosing_call, in_string
from lua_editor.editor.intellisense import Intellisense
from lua_editor.editor.vocabulary import Vocabulary

APIS = ApiRegistry.from_folder(Path(__file__).resolve().parent.parent / "apis")


@pytest.fixture(scope="module")
def morph() -> Intellisense:
    return Intellisense(Vocabulary(APIS.for_path("morphexsynth/x.lua")))


@pytest.fixture(scope="module")
def midi() -> Intellisense:
    return Intellisense(Vocabulary(APIS.for_path("midiscript/x.lua")))


@pytest.fixture(scope="module")
def ship() -> Intellisense:
    return Intellisense(Vocabulary(APIS.for_path("shipahoy/x.lua")))


def labels(sense: Intellisense, text: str) -> list[str] | None:
    done = sense.complete(text, len(text))
    return None if done is None else [item.label for item in done.items]


# context

def test_the_enclosing_call_knows_the_table_the_key_and_the_argument() -> None:
    text = "SetOscillator(0, { waveform = OscWaveform.Sq"
    call = enclosing_call(text, len(text))
    assert (call.name, call.argument, call.key) == ("SetOscillator", 1, "waveform")
    call = enclosing_call("SetOscillator(0, { detune = 1, le", 99)
    assert (call.key, call.partial_key) == (None, "le")
    assert enclosing_call("local t = { a = ", 16) is None
    assert enclosing_call("SetFilter{ type = ", 18).name == "SetFilter"


def test_brackets_in_strings_and_comments_do_not_count() -> None:
    text = 'SetFilter({ type = "{(", -- )}\n  cutoff = '
    call = enclosing_call(text, len(text))
    assert (call.name, call.key) == ("SetFilter", "cutoff")


def test_in_string_stops_at_a_comment_and_a_closed_string() -> None:
    assert in_string('x = "ab', 7) == 4
    assert in_string('x = "ab" .. y', 13) is None
    assert in_string('x = 1 -- "', 10) is None
    assert in_string('x = "a\\"b', 10) == 4


# completion

def test_commands_complete_from_two_characters(morph: Intellisense) -> None:
    done = morph.complete("SetOs", 5)
    assert done is not None and [i.label for i in done.items] == ["SetOscillator"]
    assert (done.start, done.prefix, done.auto_prefix) == (0, "SetOs", 2)
    assert labels(morph, "SetFilterEnvelope") == ["SetFilterEnvelope"]  # the widget hides a word that is complete
    assert "SetOscillator" in labels(morph, "Set")


def test_table_keys_are_offered_without_the_ones_already_given(morph: Intellisense) -> None:
    assert labels(morph, "SetOscillator(0, { ") == ["waveform", "detune", "level", "pwm", "pitchFactor"]
    assert labels(morph, "SetOscillator(0, { detune = 1, ") == ["waveform", "level", "pwm", "pitchFactor"]
    assert labels(morph, "SetOscillator(0, { detune = 1, pw") == ["pwm"]
    done = morph.complete("SetOscillator(0, { pw", 21)
    assert done.items[0].insert == "pwm = " and done.context == "table-key"


def test_a_key_that_is_edited_does_not_hide_itself_or_double_the_equals(morph: Intellisense) -> None:
    text = "SetOscillator(0, { detune = 1, level = 2 })"
    offset = text.index("level") + 2
    done = morph.complete(text, offset)
    assert [i.label for i in done.items] == ["level"]
    text = "SetOscillator(0, { pw = 1 })"
    done = morph.complete(text, text.index("pw") + 2)
    assert done.items[0].insert == "pwm"


def test_a_value_that_names_an_enum_offers_the_enum_then_its_members(morph: Intellisense) -> None:
    assert labels(morph, "SetOscillator(0, { waveform = ") == ["OscWaveform"]
    text = "SetOscillator(0, { waveform = OscWaveform."
    done = morph.complete(text, len(text))
    assert [i.label for i in done.items] == ["Triangle", "SharkFin", "Saw", "Square", "White"]
    assert [i.detail for i in done.items] == ["= 0", "= 1", "= 2", "= 3", "= 4"]
    assert labels(morph, "SetOscillator(0, { waveform = OscWaveform.Sq") == ["Square"]
    assert labels(morph, "SetOscillator(0, { detune = ") is None


def test_text_values_complete_inside_the_quotes(morph: Intellisense) -> None:
    text = 'SetFilter({ type = "LP'
    done = morph.complete(text, len(text))
    assert [i.label for i in done.items] == ["LP1", "LP2", "LP3", "LP4"]
    assert done.start == 20 and done.context == "string"
    assert labels(morph, 'SetFilter({ type = "4 po') == ["4 pole lp", "4 pole bp"]
    assert labels(morph, 'SetFilter({ cutoff = "') is None


def test_import_completes_module_names(midi: Intellisense, morph: Intellisense) -> None:
    assert labels(midi, 'import "') == ["chords", "rhythm", "scales", "constants"]
    assert labels(midi, 'import "sc') == ["scales"]
    assert labels(morph, 'import "') == ["constants"]


def test_hooks_after_function(morph: Intellisense, midi: Intellisense) -> None:
    done = morph.complete("function On", 11)
    assert [i.label for i in done.items][:3] == ["OnStart", "OnNoteOn", "OnNoteOff"]
    assert done.items[1].insert == "OnNoteOn(channel, note, velocity)"
    assert "OnTick" in labels(midi, "function ")
    assert "OnTick" not in labels(morph, "function ")
    assert "OnStart" not in (labels(morph, "local function On") or [])


def test_library_members_only_after_the_import(midi: Intellisense) -> None:
    assert labels(midi, "x = Scales.") is None
    text = 'import "scales"\nx = Scales.qu'
    assert labels(midi, text) == ["quantise"]
    assert "Major" in labels(midi, 'import "scales"\nx = Scales.')
    assert labels(midi, 'import "chords"\nx = Scales.') is None


def test_shipahoy_pitch_hooks_and_the_notes_library_complete(ship: Intellisense) -> None:
    done = ship.complete("function OnNew", 14)
    assert [i.label for i in done.items] == ["OnNewPitch"]
    assert done.items[0].insert == "OnNewPitch(note, hz, pitch, periodicity, level)"
    assert "OnPitchEnd" in labels(ship, "function ")
    assert "notes" in labels(ship, 'import "')
    assert labels(ship, "x = Notes.") is None
    assert labels(ship, 'import "notes"\nx = Notes.int') == ["intervals"]
    assert {"window", "remember", "fallback"} <= set(labels(ship, 'import "notes"\nx = Notes.'))


def test_the_standard_library_and_the_sandbox(morph: Intellisense) -> None:
    assert labels(morph, "x = math.fl") == ["floor"]
    assert "dump" not in labels(morph, "x = string.")
    assert "insert" in labels(morph, "table.")
    assert labels(morph, "x = lo") == ["Log", "local"]  # the case of the first letters does not matter


def test_general_completion_includes_the_words_of_the_script(morph: Intellisense) -> None:
    text = "local held = {}\nlocal helper = 1\nhe"
    assert labels(morph, text) == ["held", "helper"]


def test_commands_come_before_keywords_and_words(morph: Intellisense) -> None:
    order = labels(morph, "Se")
    assert order[0].startswith("Set") and "select" in order
    assert order.index("SetVolume") < order.index("select")


def test_nothing_is_offered_in_comments_or_after_a_number(morph: Intellisense) -> None:
    assert labels(morph, "-- Set") is None


# description

def test_description_of_the_word_under_the_cursor(morph: Intellisense) -> None:
    text = "SetOscillator(0, { detune = 1 })"
    assert morph.describe(text, 4).startswith("SetOscillator(index, { fields }): Sets one of the three oscillators")
    assert morph.describe(text, text.index("detune") + 2) == (
        "SetOscillator.detune: number -48 to 48, default 0. tuning against the note in semitones"
    )
    assert morph.describe("x = OscWaveform.Square", 18) == "OscWaveform.Square = 3"
    assert morph.describe("x = OscWaveform", 8).startswith("OscWaveform: Oscillator waveforms")
    assert morph.describe("function OnNoteOn(c, n, v) end", 12).startswith("OnNoteOn(channel, note, velocity): A note began")
    assert morph.describe("Log(1)", 1).startswith("Log(...)")
    assert morph.describe("local x = 1", 7) == ""


def test_description_of_an_argument_and_of_a_library_member(morph: Intellisense, midi: Intellisense) -> None:
    assert morph.describe("SetOscillator(0", 15).endswith("index: whole number 0 to 2, default 0. which oscillator, 0 to 2")
    assert morph.describe("SetOscillator(0, { ", 19).startswith("SetOscillator(index, { fields })")
    text = 'import "scales"\nx = Scales.quantise(1)'
    assert midi.describe(text, text.index("quantise") + 2) == (
        "Scales.quantise(note, root, shape): the note of the scale nearest to `note` (a tie goes down)"
    )
    assert midi.describe("x = Scales.Major", 15).startswith("Scales.Major = {0, 2, 4, 5, 7, 9, 11}: the shapes")


def test_description_of_a_text_field_mentions_the_names(morph: Intellisense) -> None:
    text = 'SetFilter({ type = "LP4" })'
    shown = morph.describe(text, text.index("type") + 1)
    assert shown.startswith("SetFilter.type: string. filter type, default LP4") and "50 names" in shown


def test_without_an_api_there_is_nothing_to_complete_but_lua() -> None:
    bare = Intellisense(Vocabulary(None))
    assert labels(bare, "SetV") is None
    assert "function" in labels(bare, "fu")
    assert bare.describe("SetVolume(1)", 3) == ""


def test_nothing_is_offered_inside_comments_and_long_strings(morph: Intellisense) -> None:
    assert labels(morph, "x = 1 -- SetOs") is None
    assert labels(morph, "--[[ a\nSetOs") is None
    assert labels(morph, "s = [[ a\nSetOs") is None
    assert labels(morph, "--[[ a ]] SetOs") == ["SetOscillator"]
