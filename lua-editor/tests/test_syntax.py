from __future__ import annotations

import os
from pathlib import Path

import pytest

from lua_editor.check.problems import Problem
from lua_editor.check.syntax import available, check_syntax

PLUGINS = Path(os.environ.get("LEDIT_DSP_REPO", Path(__file__).resolve().parent.parent.parent)) / "plugins"


def test_lupa_is_there() -> None:
    assert available()


@pytest.mark.parametrize("text", ["", "x = 1", "local function f(a) return a // 2 end", "goto done ::done::", "x = 0x10 + 1e3"])
def test_good_scripts_have_no_problems(text: str) -> None:
    assert check_syntax(text) == []


def test_a_bad_symbol_is_marked_where_it_is() -> None:
    assert check_syntax("x = = 1") == [Problem(1, 5, 1, "unexpected symbol near '='")]
    assert check_syntax("a = 1\nb = 3x") == [Problem(2, 5, 2, "malformed number near '3x'")]


def test_a_block_that_is_not_closed_is_reported_where_lua_found_out() -> None:
    problem = check_syntax("function f()\n  return 1\n")[0]
    assert problem.line == 2 and problem.column is None  # the last line with text, not the empty one after it
    assert "'end' expected (to close 'function' at line 1) near <eof>" == problem.message


def test_the_whole_line_is_marked_when_the_token_is_not_known() -> None:
    problem = check_syntax("local a = 1\ns = 'abc")[0]
    assert (problem.line, problem.column, problem.length) == (2, None, len("s = 'abc"))
    assert problem.message == "unfinished string near <eof>"


def test_a_message_without_a_line_is_put_on_the_first_line() -> None:
    problem = check_syntax("\x1bLua")[0]
    assert (problem.line, problem.column) == (1, None)
    assert "binary chunk" in problem.message


def test_the_script_is_not_run() -> None:
    assert check_syntax("while true do end") == []
    assert check_syntax("error('boom')") == []
    assert check_syntax("os.execute('exit 1') io.write('x')") == []


def test_unicode_in_a_line_does_not_confuse_the_column() -> None:
    text = "s = 'äöü' x = = 1"
    assert check_syntax(text)[0].column == text.rindex("=") + 1


def test_a_long_script_is_checked_quickly() -> None:
    import time

    text = "local t = {}\n" + "t[#t + 1] = {a = 1, b = 'two', c = {3, 4, 5}}\n" * 5000
    started = time.perf_counter()
    assert check_syntax(text) == []
    assert time.perf_counter() - started < 1.0


def test_all_the_factory_scripts_are_clean() -> None:
    scripts = sorted(p for p in PLUGINS.glob("*/scripts/*.lua") if not p.name.startswith("user-"))
    assert scripts
    assert {s.name: check_syntax(s.read_text(encoding="utf-8")) for s in scripts} == {s.name: [] for s in scripts}


def test_the_right_one_of_several_equal_tokens_is_marked() -> None:
    text = "local t = {a = 1, b = 2} x = = 3"
    assert check_syntax(text)[0].column == text.rindex("=") + 1
    text = "f(1, 2 3)"
    assert check_syntax(text)[0].column == text.index("3") + 1
