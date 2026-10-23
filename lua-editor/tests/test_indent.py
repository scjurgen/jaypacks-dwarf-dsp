"""Auto indent: the rules as functions, then the keys in the widget."""

from __future__ import annotations

import pytest
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
from PySide6.QtGui import QTextCursor
from pytestqt.qtbot import QtBot

from pathlib import Path

from lua_editor.api.model import ApiRegistry
from lua_editor.editor import indent
from lua_editor.editor.lualex import LONG_STRING, NORMAL
from lua_editor.editor.widget import CodeEdit

APIS = ApiRegistry.from_folder(Path(__file__).resolve().parent.parent / "apis")

# the rules


@pytest.mark.parametrize(
    "line, expected",
    [
        ("", ""),
        ("x = 1", ""),
        ("    x = 1", "    "),
        ("function OnStep(step)", "    "),
        ("    function OnStep(step)", "        "),
        ("if x then", "    "),
        ("if x then return end", ""),
        ("local f = function() return 1 end", ""),
        ("for i = 1, 3 do", "    "),
        ("while x do", "    "),
        ("repeat", "    "),
        ("    end", "    "),
        ("    end)", "    "),
        ("    until done", "    "),
        ("    else", "        "),
        ("    elseif x then", "        "),
        ("local t = {", "    "),
        ("SetCutoff(60, {", "    "),
        ("    }, {", "        "),
        ("foo(function(x)", "    "),
        ("-- function in a comment", ""),
        ('local s = "function if do {"', ""),
        ("x = 1 -- opens: function", ""),
        ("\tfunction f()", "\t    "),
    ],
)
def test_next_line_indent(line: str, expected: str) -> None:
    assert indent.next_line_indent(line) == expected


def test_the_last_line_of_a_split_call_hands_the_indentation_back_to_the_line_that_opened_it() -> None:
    above = ["local x = 1", "    SetCtrlSlot(0, { source = X,"]
    assert indent.next_line_indent("                  depth = 0.6 })", NORMAL, above) == "    "
    assert indent.next_line_indent("                  depth = 0.6 })") == "                  "  # no context: as before


def test_a_split_call_inside_a_block_returns_to_the_block_level() -> None:
    above = ["function f()", "    local t = foo(a,", "                  b,"]
    assert indent.next_line_indent("                  c)", NORMAL, above) == "    "


def test_a_line_that_closes_one_call_and_leaves_the_block_open_stays_deeper() -> None:
    above = ["    local x = bar(", "        a,"]
    assert indent.next_line_indent("        b) + foo(", NORMAL, above) == "        "


def test_a_line_inside_a_long_comment_or_string_does_not_open_anything() -> None:
    assert indent.next_line_indent("function f()", LONG_STRING) == ""
    assert indent.next_line_indent("]] function f()", LONG_STRING) == "    "  # the string ends, code follows


@pytest.mark.parametrize(
    "line, expected",
    [
        ("    end", "end"),
        ("        end", "    end"),
        ("    else", "else"),
        ("    until", "until"),
        ("    }", "}"),
        ("\tend", "end"),
        ("end", None),
        ("    endpoint", None),
        ("    x = 1", None),
        ("  end", None),
    ],
)
def test_dedented(line: str, expected: str | None) -> None:
    assert indent.dedented(line) == expected


def test_tab_goes_to_the_next_stop() -> None:
    assert [len(indent.tab_insertion(c)) for c in range(6)] == [4, 3, 2, 1, 4, 3]


def test_indent_and_unindent_lines() -> None:
    assert indent.indent_line("x") == "    x" and indent.indent_line("") == ""
    assert indent.unindent_line("        x") == "    x"
    assert indent.unindent_line("  x") == "x"
    assert indent.unindent_line("\tx") == "x"
    assert indent.unindent_line("x") == "x"


# the keys


@pytest.fixture
def edit(qtbot: QtBot) -> CodeEdit:
    editor = CodeEdit()
    qtbot.addWidget(editor)
    editor.resize(600, 400)
    editor.show()
    qtbot.waitExposed(editor)
    return editor


def type_text(editor: CodeEdit, text: str) -> None:
    for char in text:
        if char == "\n":
            QTest.keyClick(editor, Qt.Key.Key_Return)
        else:
            QTest.keyClick(editor, char)


def test_enter_keeps_the_indentation_and_goes_deeper_after_an_opener(edit: CodeEdit) -> None:
    type_text(edit, "function OnStep(step)\nx = 1\n")
    assert edit.toPlainText() == "function OnStep(step)\n    x = 1\n    "


def test_typing_end_steps_back_and_the_next_line_is_level_with_it(edit: CodeEdit) -> None:
    type_text(edit, "function OnStep(step)\nif x then\ny = 1\nend\nend\nz = 2")
    assert edit.toPlainText() == "function OnStep(step)\n    if x then\n        y = 1\n    end\nend\nz = 2"


def test_else_steps_back_and_reopens(edit: CodeEdit) -> None:
    type_text(edit, "if x then\na = 1\nelse\nb = 2\nend")
    assert edit.toPlainText() == "if x then\n    a = 1\nelse\n    b = 2\nend"


def test_elseif_keeps_its_one_step_back(edit: CodeEdit) -> None:
    type_text(edit, "if x then\na = 1\nelseif y then\nb = 2\nend")
    assert edit.toPlainText() == "if x then\n    a = 1\nelseif y then\n    b = 2\nend"


def test_a_name_that_starts_like_a_closing_word_keeps_its_indentation(edit: CodeEdit) -> None:
    type_text(edit, "function f()\nendpoint = 1")
    assert edit.toPlainText() == "function f()\n    endpoint = 1"


def test_enter_after_a_split_call_returns_to_the_statement(edit: CodeEdit) -> None:
    type_text(edit, "function f()\nSetCtrlSlot(0, { a = 1,\nb = 2 })\nx = 1")
    assert edit.toPlainText().endswith("    SetCtrlSlot(0, { a = 1,\n        b = 2 })\n    x = 1")  # x: back at the call


def test_a_table_opened_on_the_line_indents_and_its_brace_steps_back(edit: CodeEdit) -> None:
    type_text(edit, "local t = {\na = 1,\n}")
    assert edit.toPlainText() == "local t = {\n    a = 1,\n}"


def test_enter_between_braces_puts_the_closer_on_its_own_line(edit: CodeEdit) -> None:
    edit.setPlainText("local t = {}")
    cursor = edit.textCursor()
    cursor.setPosition(len("local t = {"))
    edit.setTextCursor(cursor)
    QTest.keyClick(edit, Qt.Key.Key_Return)
    assert edit.toPlainText() == "local t = {\n    \n}"
    assert edit.textCursor().position() == len("local t = {\n    ")


def test_enter_inside_a_long_string_adds_no_indentation(edit: CodeEdit) -> None:
    edit.setPlainText("local s = [[\n    text")
    cursor = edit.textCursor()
    cursor.movePosition(QTextCursor.MoveOperation.End)
    edit.setTextCursor(cursor)
    QTest.keyClick(edit, Qt.Key.Key_Return)
    assert edit.toPlainText() == "local s = [[\n    text\n"


def test_enter_replaces_a_selection(edit: CodeEdit) -> None:
    edit.setPlainText("if x then abc")
    cursor = edit.textCursor()
    cursor.setPosition(len("if x then "))
    cursor.setPosition(len("if x then abc"), QTextCursor.MoveMode.KeepAnchor)
    edit.setTextCursor(cursor)
    QTest.keyClick(edit, Qt.Key.Key_Return)
    assert edit.toPlainText() == "if x then \n    "


def test_one_enter_with_its_indentation_is_one_undo_step(edit: CodeEdit) -> None:
    type_text(edit, "function f()")
    QTest.keyClick(edit, Qt.Key.Key_Return)
    edit.undo()
    assert edit.toPlainText() == "function f()"


def test_typing_end_and_its_dedent_undo_together(edit: CodeEdit) -> None:
    type_text(edit, "function f()\nx = 1\n")
    before = edit.toPlainText()
    type_text(edit, "end")
    assert edit.toPlainText().endswith("\nend")
    for _ in range(3):
        edit.undo()
        if edit.toPlainText() == before:
            break
    assert edit.toPlainText() == before  # the word and its dedent never leave a half done state behind


def test_tab_inserts_spaces_to_the_next_stop(edit: CodeEdit) -> None:
    type_text(edit, "ab")
    QTest.keyClick(edit, Qt.Key.Key_Tab)
    assert edit.toPlainText() == "ab  "
    QTest.keyClick(edit, Qt.Key.Key_Tab)
    assert edit.toPlainText() == "ab      "


def test_tab_and_shift_tab_move_every_selected_line(edit: CodeEdit) -> None:
    edit.setPlainText("a\nb\n\nc")
    edit.selectAll()
    QTest.keyClick(edit, Qt.Key.Key_Tab)
    assert edit.toPlainText() == "    a\n    b\n\n    c"  # a blank line stays blank
    QTest.keyClick(edit, Qt.Key.Key_Backtab)
    assert edit.toPlainText() == "a\nb\n\nc"


def test_a_selection_ending_at_the_start_of_a_line_leaves_that_line_alone(edit: CodeEdit) -> None:
    edit.setPlainText("a\nb\nc")
    cursor = edit.textCursor()
    cursor.setPosition(0)
    cursor.setPosition(len("a\nb\n"), QTextCursor.MoveMode.KeepAnchor)
    edit.setTextCursor(cursor)
    QTest.keyClick(edit, Qt.Key.Key_Tab)
    assert edit.toPlainText() == "    a\n    b\nc"


def test_shift_tab_without_a_selection_unindents_the_line(edit: CodeEdit) -> None:
    edit.setPlainText("        x")
    cursor = edit.textCursor()
    cursor.movePosition(QTextCursor.MoveOperation.End)
    edit.setTextCursor(cursor)
    QTest.keyClick(edit, Qt.Key.Key_Backtab)
    assert edit.toPlainText() == "    x"
    assert edit.textCursor().positionInBlock() == len("    x")


def test_the_completion_popup_takes_enter_for_a_partial_word_but_not_for_a_finished_one(edit: CodeEdit) -> None:
    edit.set_api(APIS.for_path("morphexsynth/x.lua"))
    type_text(edit, "SetOsc")
    popup = edit._completer.popup()
    assert popup.isVisible()
    QTest.keyClick(edit, Qt.Key.Key_Return)  # the list takes it and completes the word
    assert "\n" not in edit.toPlainText() and edit.toPlainText().startswith("SetOsc")
    edit.setPlainText("")
    type_text(edit, "function f()\nx = 1\nend")
    QTest.keyClick(edit, Qt.Key.Key_Return)  # "end" is complete: a new line, not an acceptance of itself
    assert edit.toPlainText() == "function f()\n    x = 1\nend\n"


def test_the_state_constants_are_what_the_rules_use() -> None:
    assert NORMAL == 0
