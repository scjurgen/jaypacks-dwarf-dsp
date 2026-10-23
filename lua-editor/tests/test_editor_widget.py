from __future__ import annotations

from pathlib import Path

import pytest
from PySide6.QtGui import QTextCursor
from pytestqt.qtbot import QtBot

from lua_editor.api.model import ApiRegistry
from lua_editor.editor.highlighter import is_dark, make_formats
from lua_editor.editor.widget import CodeEdit
from lua_editor.remote import LocalFS
from lua_editor.ui.mainwindow import MainWindow
from tests.test_ui import FakeDialogs

APIS = ApiRegistry.from_folder(Path(__file__).resolve().parent.parent / "apis")


@pytest.fixture
def edit(qtbot: QtBot) -> CodeEdit:
    editor = CodeEdit()
    editor.set_api(APIS.for_path("morphexsynth/x.lua"))
    qtbot.addWidget(editor)
    editor.resize(600, 400)
    editor.show()
    qtbot.waitExposed(editor)
    return editor


def move_to(editor: CodeEdit, position: int) -> None:
    cursor = editor.textCursor()
    cursor.setPosition(position, QTextCursor.MoveMode.MoveAnchor)
    editor.setTextCursor(cursor)


# highlighting


def test_the_highlighter_colours_bound_names_by_kind(edit: CodeEdit) -> None:
    text = "SetOscillator(0, { waveform = OscWaveform.Square }) -- note"
    edit.setPlainText(text)
    edit._highlighter.rehighlight()
    formats = make_formats(is_dark())
    seen = {
        (r.start, r.length): r.format.foreground().color().name()
        for r in edit.document().firstBlock().layout().formats()
    }

    def colour(kind: str) -> str:
        return formats[kind].foreground().color().name()

    assert seen[(0, 13)] == colour("command")
    assert seen[(text.index("waveform"), 8)] == colour("field")
    assert seen[(text.index("OscWaveform"), 11)] == colour("enum")
    assert seen[(text.index("Square"), 6)] == colour("enum_member")
    assert seen[(text.index("--"), 7)] == colour("comment")


def test_a_long_comment_is_coloured_on_every_line_it_covers(edit: CodeEdit) -> None:
    edit.setPlainText("--[[ one\ntwo SetVolume\nthree ]] SetVolume(1)")
    edit._highlighter.rehighlight()
    comment = make_formats(is_dark())["comment"].foreground().color().name()
    second = edit.document().findBlockByNumber(1).layout().formats()
    assert [(r.start, r.length, r.format.foreground().color().name()) for r in second] == [(0, 13, comment)]
    third = edit.document().findBlockByNumber(2).layout().formats()
    assert third[0].format.foreground().color().name() == comment and third[1].start == 9


def test_switching_the_api_recolours(edit: CodeEdit) -> None:
    edit.setPlainText("SetVolume(1)")
    edit._highlighter.rehighlight()
    assert edit.document().firstBlock().layout().formats()
    edit.set_api(None)
    assert [r.start for r in edit.document().firstBlock().layout().formats()] == [10]  # only the number


# line numbers


def test_the_line_number_column_grows_with_the_digits(edit: CodeEdit) -> None:
    narrow = edit.line_number_width()
    edit.setPlainText("\n" * 1500)
    assert edit.line_number_width() > narrow
    assert edit.viewportMargins().left() == edit.line_number_width()


# completion


def test_typing_two_letters_opens_the_list_and_accepting_replaces_the_word(qtbot: QtBot, edit: CodeEdit) -> None:
    qtbot.keyClicks(edit, "SetOs")
    shown = edit.completion()
    assert shown is not None and [i.label for i in shown.items] == ["SetOscillator"]
    edit.accept_completion("SetOscillator")
    assert edit.toPlainText() == "SetOscillator"
    assert edit.completion() is None


def test_one_letter_does_not_open_the_list_but_ctrl_space_does(qtbot: QtBot, edit: CodeEdit) -> None:
    qtbot.keyClicks(edit, "S")
    assert edit.completion() is None
    edit.complete_now(True)
    assert edit.completion() is not None


def test_a_complete_word_closes_the_list(qtbot: QtBot, edit: CodeEdit) -> None:
    qtbot.keyClicks(edit, "SetFilterEnvelope")
    assert edit.completion() is None


def test_table_keys_complete_after_one_letter_and_insert_the_equals_sign(qtbot: QtBot, edit: CodeEdit) -> None:
    qtbot.keyClicks(edit, "SetOscillator(0, { pw")
    assert [i.label for i in edit.completion().items] == ["pwm"]
    edit.accept_completion("pwm")
    assert edit.toPlainText() == "SetOscillator(0, { pwm = "


def test_a_dot_after_an_enum_lists_its_members(qtbot: QtBot, edit: CodeEdit) -> None:
    qtbot.keyClicks(edit, "x = OscWaveform.")
    assert [i.label for i in edit.completion().items] == ["Triangle", "SharkFin", "Saw", "Square", "White"]
    qtbot.keyClicks(edit, "Sq")
    edit.accept_completion("Square")
    assert edit.toPlainText() == "x = OscWaveform.Square"


def test_text_values_complete_inside_a_string(qtbot: QtBot, edit: CodeEdit) -> None:
    qtbot.keyClicks(edit, 'SetFilter({ type = "4 po')
    edit.accept_completion("4 pole bp")
    assert edit.toPlainText() == 'SetFilter({ type = "4 pole bp'


def test_a_hook_is_inserted_with_its_arguments(qtbot: QtBot, edit: CodeEdit) -> None:
    qtbot.keyClicks(edit, "function OnNoteOn")
    edit.complete_now(True)
    edit.accept_completion("OnNoteOn")
    assert edit.toPlainText() == "function OnNoteOn(channel, note, velocity)"


def test_without_an_api_nothing_bound_is_offered(qtbot: QtBot, edit: CodeEdit) -> None:
    edit.set_api(None)
    qtbot.keyClicks(edit, "SetOs")
    assert edit.completion() is None


# the hint


def test_the_hint_follows_the_cursor(edit: CodeEdit) -> None:
    seen: list[str] = []
    edit.hintChanged.connect(seen.append)
    edit.setPlainText("SetOscillator(0, { detune = 1 })")
    move_to(edit, 3)
    assert seen[-1].startswith("SetOscillator(index, { fields }): Sets one of the three oscillators")
    move_to(edit, 22)
    assert seen[-1].startswith("SetOscillator.detune: number -48 to 48, default 0")
    move_to(edit, 0)
    edit.setPlainText("local x = 1")
    move_to(edit, 7)
    assert seen[-1] == ""


# in the window


def test_the_window_gives_each_file_the_api_of_its_plugin(qtbot: QtBot, tmp_path: Path) -> None:
    (tmp_path / "morphexsynth").mkdir()
    (tmp_path / "midiscript").mkdir()
    (tmp_path / "morphexsynth" / "a.lua").write_text("SetVolume(0)\n", encoding="utf-8")
    (tmp_path / "midiscript" / "b.lua").write_text("SetVolume(0)\n", encoding="utf-8")
    window = MainWindow(LocalFS(tmp_path), FakeDialogs(), APIS)  # type: ignore[arg-type]
    qtbot.addWidget(window)
    qtbot.waitUntil(lambda: "midiscript" in window.tree.paths())
    for path in ("morphexsynth/a.lua", "midiscript/b.lua"):
        window.open_file(path)
        qtbot.waitUntil(lambda p=path: window.editor_for(p) is not None)
    morph, midi = window.editor_for("morphexsynth/a.lua"), window.editor_for("midiscript/b.lua")
    move_to(morph, 3)
    move_to(midi, 3)
    assert morph.hint().startswith("SetVolume(db)")
    assert midi.hint() == ""
    shown = []
    window._hint.setText("")
    morph.hintChanged.connect(shown.append)
    move_to(morph, 4)
    assert window._hint.text().startswith("SetVolume(db)")


def open_in(qtbot: QtBot, window: MainWindow, *paths: str) -> None:
    for path in paths:
        window.open_file(path)
        qtbot.waitUntil(lambda p=path: window.editor_for(p) is not None)


def test_libraries_next_to_a_script_are_offered_after_import(qtbot: QtBot, tmp_path: Path) -> None:
    folder = tmp_path / "midiscript"
    folder.mkdir()
    (folder / "mine.lua").write_text(
        "-- Mine: my helpers.\n--\n--   Util.twice(x)   doubles x\n\nUtil = {Base = 12}\nfunction Util.twice(x) return 2 * x end\n",
        encoding="utf-8",
    )
    (folder / "main.lua").write_text('import "mine"\n', encoding="utf-8")
    window = MainWindow(LocalFS(tmp_path), FakeDialogs(), APIS)  # type: ignore[arg-type]
    qtbot.addWidget(window)
    window.show()
    qtbot.waitUntil(lambda: "midiscript" in window.tree.paths())
    open_in(qtbot, window, "midiscript/main.lua")
    editor = window.editor_for("midiscript/main.lua")
    qtbot.waitUntil(lambda: "Util" in editor._vocab.libraries)
    qtbot.keyClicks(editor, "x = Util.")
    assert [i.label for i in editor.completion().items] == ["Base", "twice"]
    assert editor.completion().items[1].detail == "(x)  doubles x"
    editor.setPlainText('import "')
    editor.moveCursor(QTextCursor.MoveOperation.End)
    editor.complete_now(True)
    assert [i.label for i in editor.completion().items] == ["chords", "mine", "rhythm", "scales", "constants"]


def test_a_library_saved_in_the_folder_reaches_the_other_open_scripts(qtbot: QtBot, tmp_path: Path) -> None:
    folder = tmp_path / "midiscript"
    folder.mkdir()
    (folder / "lib.lua").write_text("Util = {}\n", encoding="utf-8")
    (folder / "main.lua").write_text('import "lib"\n', encoding="utf-8")
    window = MainWindow(LocalFS(tmp_path), FakeDialogs(), APIS)  # type: ignore[arg-type]
    qtbot.addWidget(window)
    qtbot.waitUntil(lambda: "midiscript" in window.tree.paths())
    open_in(qtbot, window, "midiscript/lib.lua", "midiscript/main.lua")
    main, lib = window.editor_for("midiscript/main.lua"), window.editor_for("midiscript/lib.lua")
    qtbot.waitUntil(lambda: "Util" in main._vocab.libraries)
    lib.selectAll()
    lib.insertPlainText("Util = {}\nfunction Util.added() end\n")
    window.save(lib)
    qtbot.waitUntil(lambda: any(m.name == "added" for m in main._vocab.libraries["Util"].members))
