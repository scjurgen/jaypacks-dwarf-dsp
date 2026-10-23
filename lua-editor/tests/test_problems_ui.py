from __future__ import annotations

from pathlib import Path

import pytest
from PySide6.QtGui import QTextCursor
from pytestqt.qtbot import QtBot

import lua_editor.ui.documentedit as documentedit
from lua_editor.check.problems import Problem
from lua_editor.remote import LocalFS
from lua_editor.ui.documentedit import CHECK_DELAY_MS, DocumentEdit
from lua_editor.ui.mainwindow import MainWindow
from tests.test_ui import FakeDialogs, type_text


def make(text: str, qtbot: QtBot) -> DocumentEdit:
    edit = DocumentEdit("a.lua", text, "\n", (0, 0))
    qtbot.addWidget(edit)
    edit.resize(600, 300)
    edit.show()
    return edit


def marked_texts(edit: DocumentEdit) -> list[str]:
    return [s.cursor.selectedText() for s in edit.extraSelections() if s.cursor.hasSelection()]


# the editor


def test_a_broken_script_is_checked_when_it_is_loaded_and_the_error_is_underlined(qtbot: QtBot) -> None:
    edit = make("local a = 1\nx = = 1\n", qtbot)
    assert edit.problems() == [Problem(2, 5, 1, "unexpected symbol near '='")]
    assert marked_texts(edit) == ["="]
    assert edit._messages_at(2) == "unexpected symbol near '='" and edit._messages_at(1) == ""


def test_a_clean_script_has_no_problems_and_no_marks(qtbot: QtBot) -> None:
    edit = make("x = 1\n", qtbot)
    assert edit.problems() == [] and marked_texts(edit) == []


def test_a_line_without_a_known_column_is_underlined_without_its_indent(qtbot: QtBot) -> None:
    edit = make("do\n    local s = 'open\n", qtbot)
    assert (edit.problems()[0].line, edit.problems()[0].column) == (2, None)
    assert marked_texts(edit) == ["local s = 'open"]


def test_an_error_at_the_end_marks_the_last_line_with_text(qtbot: QtBot) -> None:
    edit = make("do\n    local a = 1\n\n", qtbot)
    assert edit.problems()[0].line == 2
    assert marked_texts(edit) == ["local a = 1"]


def test_the_check_waits_until_typing_pauses(qtbot: QtBot, monkeypatch: pytest.MonkeyPatch) -> None:
    edit = make("x = 1\n", qtbot)
    calls: list[str] = []
    original = documentedit.check_syntax
    monkeypatch.setattr(documentedit, "check_syntax", lambda text: calls.append(text) or original(text))
    qtbot.keyClicks(edit, "y = = 2")
    assert calls == [] and edit.problems() == []
    qtbot.waitUntil(lambda: len(calls) == 1, timeout=CHECK_DELAY_MS * 5)
    assert edit.problems()[0].message == "unexpected symbol near '='"
    qtbot.wait(CHECK_DELAY_MS * 2)
    assert len(calls) == 1


def test_fixing_the_text_clears_the_problem(qtbot: QtBot) -> None:
    edit = make("x = = 1\n", qtbot)
    assert edit.problems()
    type_text(edit, "x = 1\n")
    qtbot.waitUntil(lambda: edit.problems() == [], timeout=CHECK_DELAY_MS * 5)
    assert marked_texts(edit) == []


def test_goto_puts_the_cursor_on_a_line_and_column(qtbot: QtBot) -> None:
    edit = make("one\ntwo three\nfour\n", qtbot)
    edit.goto(2, 5)
    cursor = edit.textCursor()
    assert (cursor.blockNumber(), cursor.positionInBlock()) == (1, 4)
    edit.goto(99)
    assert edit.textCursor().blockNumber() == edit.document().blockCount() - 1
    edit.goto(1, 99)
    assert edit.textCursor().positionInBlock() == 3


def test_changed_problems_are_announced_once(qtbot: QtBot) -> None:
    edit = make("x = = 1\n", qtbot)
    announced: list[int] = []
    edit.problemsChanged.connect(lambda: announced.append(1))
    edit.check_now()
    assert announced == []  # the same result again changes nothing
    type_text(edit, "x = 1\n")
    edit.check_now()
    assert announced == [1]


# the window


@pytest.fixture
def fs(tmp_path: Path) -> LocalFS:
    # bytes, so that the files have LF endings on every platform
    (tmp_path / "bad.lua").write_bytes(b"local a = 1\nx = = 2\n")
    (tmp_path / "good.lua").write_bytes(b"x = 1\n")
    return LocalFS(tmp_path)


@pytest.fixture
def window(qtbot: QtBot, fs: LocalFS) -> MainWindow:
    win = MainWindow(fs, FakeDialogs())  # type: ignore[arg-type]
    qtbot.addWidget(win)
    win.show()
    qtbot.waitUntil(lambda: "bad.lua" in win.tree.paths())
    return win


def open_and_wait(qtbot: QtBot, window: MainWindow, path: str) -> DocumentEdit:
    window.open_file(path)
    qtbot.waitUntil(lambda: window.editor_for(path) is not None)
    return window.editor_for(path)


def test_a_tab_with_an_error_is_marked_and_the_panel_lists_it(qtbot: QtBot, window: MainWindow) -> None:
    open_and_wait(qtbot, window, "bad.lua")
    assert window.tabs.tabText(0) == "bad.lua !"
    assert window._problems.texts() == ["error  2:5  unexpected symbol near '='"]
    assert window._problems_dock.windowTitle() == "Problems (1)"
    assert "1 error" in window._position.text()


def test_the_panel_follows_the_current_tab(qtbot: QtBot, window: MainWindow) -> None:
    open_and_wait(qtbot, window, "bad.lua")
    open_and_wait(qtbot, window, "good.lua")
    assert window._problems.texts() == [] and window._problems_dock.windowTitle() == "Problems"
    assert window.tabs.tabText(1) == "good.lua"
    window.tabs.setCurrentIndex(0)
    assert len(window._problems.texts()) == 1
    assert "1 error" in window._position.text()
    window.tabs.setCurrentIndex(1)
    assert "error" not in window._position.text()


def test_activating_a_problem_jumps_to_it(qtbot: QtBot, window: MainWindow) -> None:
    editor = open_and_wait(qtbot, window, "bad.lua")
    window._problems.itemActivated.emit(window._problems.item(0))
    cursor = editor.textCursor()
    assert (cursor.blockNumber() + 1, cursor.positionInBlock() + 1) == (2, 5)


def test_fixing_the_script_removes_the_mark_from_the_tab(qtbot: QtBot, window: MainWindow) -> None:
    editor = open_and_wait(qtbot, window, "bad.lua")
    type_text(editor, "x = 2\n")
    qtbot.waitUntil(lambda: window.tabs.tabText(0) == "bad.lua *", timeout=CHECK_DELAY_MS * 5)
    assert window._problems.texts() == []
    assert "error" not in window._position.text()


def test_a_script_can_still_be_saved_with_an_error(qtbot: QtBot, window: MainWindow, fs: LocalFS) -> None:
    editor = open_and_wait(qtbot, window, "bad.lua")
    type_text(editor, "y = = 3\n")
    window.save_current()
    qtbot.waitUntil(lambda: not editor.is_dirty())
    assert fs.read("bad.lua") == b"y = = 3\n"


# the checks of the plugin's API in the editor and the window

from lua_editor.api.model import ApiRegistry  # noqa: E402

APIS = ApiRegistry.from_folder(Path(__file__).resolve().parent.parent / "apis")


@pytest.fixture
def plugin_window(qtbot: QtBot, tmp_path: Path) -> MainWindow:
    folder = tmp_path / "morphexsynth"
    folder.mkdir()
    (folder / "typo.lua").write_bytes(b"SetOscilator(0)\n")
    (folder / "soft.lua").write_bytes(b"SetOscillator(0, { levl = 1 })\n")
    (folder / "clean.lua").write_bytes(b"SetVolume(0)\n")
    (folder / "broken.lua").write_bytes(b"SetOscilator(0\nx = = 1\n")
    (folder / "user.lua").write_bytes(b'import "helpers"\n')
    win = MainWindow(LocalFS(tmp_path), FakeDialogs(), APIS)  # type: ignore[arg-type]
    qtbot.addWidget(win)
    win.show()
    qtbot.waitUntil(lambda: "morphexsynth" in win.tree.paths())
    return win


def test_an_unknown_command_is_an_error_on_the_tab_and_in_the_panel(qtbot: QtBot, plugin_window: MainWindow) -> None:
    open_and_wait(qtbot, plugin_window, "morphexsynth/typo.lua")
    assert plugin_window.tabs.tabText(0) == "typo.lua !"
    assert plugin_window._problems.texts() == ["error  1:1  unknown function SetOscilator; did you mean SetOscillator?"]


def test_a_warning_marks_the_tab_with_a_question_mark_and_is_counted(qtbot: QtBot, plugin_window: MainWindow) -> None:
    open_and_wait(qtbot, plugin_window, "morphexsynth/soft.lua")
    assert plugin_window.tabs.tabText(0) == "soft.lua ?"
    assert plugin_window._problems.texts()[0].startswith("warning  1:20  SetOscillator has no field levl")
    assert "1 warning" in plugin_window._position.text() and "error" not in plugin_window._position.text()


def test_a_clean_script_has_no_mark(qtbot: QtBot, plugin_window: MainWindow) -> None:
    open_and_wait(qtbot, plugin_window, "morphexsynth/clean.lua")
    assert plugin_window.tabs.tabText(0) == "clean.lua"


def test_a_syntax_error_hides_the_api_problems(qtbot: QtBot, plugin_window: MainWindow) -> None:
    open_and_wait(qtbot, plugin_window, "morphexsynth/broken.lua")
    texts = plugin_window._problems.texts()
    assert len(texts) == 1 and "unknown function" not in texts[0]


def test_errors_and_warnings_have_their_own_colours(qtbot: QtBot, plugin_window: MainWindow) -> None:
    error = open_and_wait(qtbot, plugin_window, "morphexsynth/typo.lua")
    warning = open_and_wait(qtbot, plugin_window, "morphexsynth/soft.lua")

    def underline(edit: DocumentEdit) -> str:
        marks = [s for s in edit.extraSelections() if s.cursor.hasSelection()]
        return marks[0].format.underlineColor().name()

    assert underline(error) != underline(warning)


def test_an_import_of_a_script_that_is_not_there_is_an_error_until_it_exists(
    qtbot: QtBot, plugin_window: MainWindow
) -> None:
    editor = open_and_wait(qtbot, plugin_window, "morphexsynth/user.lua")
    qtbot.waitUntil(lambda: editor.script_names is not None)
    assert len(editor.problems()) == 1
    assert editor.problems()[0].message.startswith('import "helpers": there is no helpers.lua here')
    plugin_window._dialogs.names = ["helpers"]  # type: ignore[attr-defined]
    plugin_window.new_script("morphexsynth")
    qtbot.waitUntil(lambda: plugin_window.editor_for("morphexsynth/helpers.lua") is not None)
    qtbot.waitUntil(lambda: editor.problems() == [])


def test_a_script_over_the_size_limit_is_an_error(qtbot: QtBot) -> None:
    edit = DocumentEdit("morphexsynth/big.lua", "x = 1\n" * 50000, "\n", (0, 0))
    qtbot.addWidget(edit)
    edit.set_api(APIS.for_path("morphexsynth/big.lua"))
    messages = [p.message for p in edit.problems()]
    assert messages == ["the script is 293 KiB; the plugin loads at most 256 KiB"]
    edit.eol = "\r\n"
    edit.check_now()
    assert edit.problems()[0].message.startswith("the script is 342 KiB")
