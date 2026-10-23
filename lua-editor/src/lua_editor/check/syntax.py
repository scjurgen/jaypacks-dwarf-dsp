"""Syntax check with an embedded Lua 5.4 (lupa): the script is compiled, never run.

Lua stops at the first syntax error, so there is at most one problem. The runtime is created once and must only be used
from the thread that made it (the UI thread).
"""

from __future__ import annotations

import re
from typing import Any

from lua_editor.check.problems import Problem

CHUNK = "script"
MESSAGE = re.compile(rf"^{CHUNK}:(\d+): (.*)$", re.S)
MAX_CANDIDATES = 25
NEAR = re.compile(r"near (?:'(.*)'|<eof>)$", re.S)

_load: Any = None


def available() -> bool:
    try:
        _loader()
    except ImportError:
        return False
    return True


def _loader() -> Any:
    global _load
    if _load is None:
        from lupa.lua54 import LuaRuntime

        runtime = LuaRuntime(encoding="utf-8", register_eval=False, register_builtins=False, unpack_returned_tuples=True)
        _load = runtime.globals().load
    return _load


def check_syntax(text: str) -> list[Problem]:
    """The syntax error of a script, as a list of zero or one problems. Empty too if lupa is not installed."""
    try:
        load = _loader()
    except ImportError:
        return []
    # "t": text chunks only; load compiles and does not call, so no line of the script ever runs
    result = load(text, "=" + CHUNK, "t")
    if not isinstance(result, tuple) or result[0] is not None:
        return []
    return [_problem(load, str(result[1]), text)]


def _problem(load: Any, message: str, text: str) -> Problem:
    found = MESSAGE.match(message)
    if not found:
        return Problem(1, None, 0, message)
    line, reason = int(found.group(1)), found.group(2)
    lines = text.split("\n")
    line = min(line, len(lines))
    # an error at the end of the text is found on the empty line after it; mark the last line that has something
    while line > 1 and not lines[line - 1].strip():
        line -= 1
    line_text = lines[line - 1]
    near = NEAR.search(reason)
    token = near.group(1) if near and near.group(1) else None
    column = _column(load, message, text, lines, line, token) if token else None
    return Problem(line, column, len(token) if column and token else len(line_text), reason)


def _column(load: Any, message: str, text: str, lines: list[str], line: int, token: str) -> int | None:
    """Lua names the token but not where it is. The right occurrence is the first one after which the text, cut there,
    fails with the very same message: the parser meets that token and stops, whatever follows."""
    line_start = sum(len(lines[i]) + 1 for i in range(min(line, len(lines)) - 1))
    line_text = lines[min(line, len(lines)) - 1]
    for found in list(re.finditer(re.escape(token), line_text))[:MAX_CANDIDATES]:
        cut = text[: line_start + found.end()]
        result = load(cut, "=" + CHUNK, "t")
        if isinstance(result, tuple) and result[0] is None and str(result[1]) == message:
            return found.start() + 1
    return None
