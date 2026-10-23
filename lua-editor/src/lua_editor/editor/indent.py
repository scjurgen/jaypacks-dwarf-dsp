"""Indentation rules for Lua, as functions on text so they can be tested without a widget.

A line opens a block with function, if, do (which also ends for and while headers) and repeat, or with ( and {; it
closes one with end, until, ) and }. Strings and comments are blanked first, so a word inside them does not count.
"""

from __future__ import annotations

import re
from collections.abc import Sequence

from lua_editor.editor.lualex import NORMAL, tokenize_line
from lua_editor.editor.vocabulary import Vocabulary

INDENT = "    "
OPENERS = frozenset({"function", "if", "do", "repeat", "{", "("})
CLOSERS = frozenset({"end", "until", "}", ")"})
MIDDLES = frozenset({"else", "elseif"})  # close the block before them and open it again
DEDENT_WORDS = frozenset({"end", "else", "until", "}", ")"})  # alone on a line, typed: the line steps back one level
LOOK_BACK = 60  # lines searched upwards for the line that opened a call
WORDS = re.compile(r"[A-Za-z_]\w*|[{}()]")
_PLAIN = Vocabulary(None)


def leading_whitespace(line: str) -> str:
    return line[: len(line) - len(line.lstrip(" \t"))]


def blank_strings_and_comments(line: str, state: int = NORMAL) -> tuple[str, int]:
    """The line with the text of strings and comments replaced by spaces, and the state at its end."""
    tokens, state = tokenize_line(line, state, _PLAIN)
    chars = list(line)
    for token in tokens:
        if token.kind in ("comment", "string"):
            chars[token.start : token.end] = " " * (token.end - token.start)
    return "".join(chars), state


def _balance(words: list[str]) -> int:
    return sum((word in OPENERS) - (word in CLOSERS) for word in words)


def next_line_indent(line: str, state: int = NORMAL, before: Sequence[str] = ()) -> str:
    """The indentation for the line after this one (this one up to where Enter was pressed).

    The same as the line's own, one level deeper if it opens more than it closes. Closers at the start of the line
    belong to the line itself (its own indentation already allows for them). else and elseif at the start close
    and reopen the block, so the line after them is deeper. At most one level, so foo(function() has one.

    A line that closes more than it opens (the last line of a call that was split over lines) hands the indentation
    back to the line that opened it: before is the text of the lines above (nearest last, all outside long strings).
    """
    code, _ = blank_strings_and_comments(line, state)
    words = WORDS.findall(code)
    start = 0
    while start < len(words) and words[start] in CLOSERS:
        start += 1
    balance = _balance(words[start:])
    if balance < 0:
        opening = _opening_line(-balance, before)
        if opening is not None:
            return leading_whitespace(opening[0]) + (INDENT if opening[1] > 0 else "")
    if start < len(words) and words[start] in MIDDLES:
        balance += 1
    indent = leading_whitespace(line)
    return indent + INDENT if balance > 0 else indent


def _opening_line(owed: int, before: Sequence[str]) -> tuple[str, int] | None:
    """The nearest line above that, with the lines after it, closes `owed` more than it opens, and how much of
    that line stays open (more than 0: the block continues, so the next line is deeper)."""
    for line in reversed(before[-LOOK_BACK:]):
        words = WORDS.findall(blank_strings_and_comments(line)[0])
        total = _balance(words)
        if total >= owed:
            return line, total - owed
        owed -= total
    return None


def dedented(line: str) -> str | None:
    """The line one level shallower if it is only a closing word (just typed) that is indented; else None."""
    if line.strip() not in DEDENT_WORDS:
        return None
    indent = leading_whitespace(line)
    if indent.endswith("\t"):
        return indent[:-1] + line[len(indent) :]
    if indent.endswith(INDENT):
        return indent[: -len(INDENT)] + line[len(indent) :]
    return None


def tab_insertion(column: int) -> str:
    """The spaces a Tab inserts at this column: up to the next multiple of the indent width."""
    return " " * (len(INDENT) - column % len(INDENT))


def indent_line(line: str) -> str:
    return INDENT + line if line.strip() else line


def unindent_line(line: str) -> str:
    """Remove one level: a tab, or up to four spaces."""
    if line.startswith("\t"):
        return line[1:]
    remove = min(len(INDENT), len(line) - len(line.lstrip(" ")))
    return line[remove:]
