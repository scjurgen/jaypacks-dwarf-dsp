"""Where the cursor is in a script: inside a command's table argument, inside a string, after a dot.

Everything works on the text before the cursor with comments and strings blanked out (lua_modules.mask), so a brace in
a string or a comment never counts.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

from lua_editor.api.lua_modules import mask
from lua_editor.editor.lualex import NORMAL, tokenize_line
from lua_editor.editor.vocabulary import Vocabulary

_PLAIN = Vocabulary(None)
IDENT_BEFORE = re.compile(r"([A-Za-z_]\w*)\s*$")
ASSIGNED = re.compile(r"\s*([A-Za-z_]\w*)\s*=(?!=)")


@dataclass(frozen=True)
class CallContext:
    """The innermost call whose arguments hold the cursor."""

    name: str
    argument: int  # index of the argument the cursor is in (the table counts as the last one)
    table_start: int | None  # offset of the '{' if the cursor is inside the table argument
    key: str | None  # in a table: the field being given a value (cursor after its '='), else None
    partial_key: str  # in a table before an '=': what has been typed of the key


def open_brackets(code: str) -> list[tuple[str, int]]:
    """The brackets still open at the end of code, outermost first, as (bracket, offset)."""
    stack: list[tuple[str, int]] = []
    pairs = {")": "(", "}": "{", "]": "["}
    for i, c in enumerate(code):
        if c in "({[":
            stack.append((c, i))
        elif c in pairs and stack and stack[-1][0] == pairs[c]:
            stack.pop()
    return stack


def _top_level_segment(code: str, start: int, stop: int) -> str:
    """The text of the table entry that ends at stop: after the last ',' or ';' that is not nested."""
    depth = 0
    begin = start
    for i in range(start, stop):
        c = code[i]
        if c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
        elif c in ",;" and depth == 0:
            begin = i + 1
    return code[begin:stop]


def _arguments_before(code: str, paren: int, stop: int) -> int:
    depth = commas = 0
    for i in range(paren + 1, stop):
        c = code[i]
        if c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
        elif c == "," and depth == 0:
            commas += 1
    return commas


def enclosing_call(text: str, offset: int) -> CallContext | None:
    offset = min(offset, len(text))
    code = mask(text[:offset])
    stack = open_brackets(code)
    table_start: int | None = None
    key: str | None = None
    partial = ""
    if stack and stack[-1][0] == "{":
        table_start = stack[-1][1]
        stack = stack[:-1]
        segment = _top_level_segment(code, table_start + 1, offset)
        assigned = ASSIGNED.match(segment)
        if assigned:
            key = assigned.group(1)
        elif re.fullmatch(r"\s*\w*\s*", segment):
            partial = segment.strip()
    if stack and stack[-1][0] == "(":
        paren = stack[-1][1]
        name = IDENT_BEFORE.search(code[:paren])
        if name:
            return CallContext(name.group(1), _arguments_before(code, paren, offset), table_start, key, partial)
    if table_start is not None:
        # a table written straight after the name, Name{...}, is a call as well
        direct = IDENT_BEFORE.search(code[:table_start])
        if direct:
            return CallContext(direct.group(1), 0, table_start, key, partial)
    return None


def in_string(text: str, offset: int) -> int | None:
    """The offset of the opening quote if the cursor is inside a one-line string, else None."""
    line_start = text.rfind("\n", 0, offset) + 1
    line = text[line_start:offset]
    quote: str | None = None
    start = 0
    i = 0
    while i < len(line):
        c = line[i]
        if quote:
            if c == "\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote, start = c, line_start + i
        elif line.startswith("--", i):
            return None
        i += 1
    return start if quote else None


def in_comment(text: str, offset: int) -> bool:
    """True if the cursor is inside a comment, or inside a long string, where nothing is completed."""
    state = NORMAL
    lines = text[:offset].split("\n")
    for line in lines[:-1]:
        _, state = tokenize_line(line, state, _PLAIN)
    last = lines[-1]
    tokens, state = tokenize_line(last, state, _PLAIN)
    if state != NORMAL:
        return True
    return any(t.kind == "comment" and t.end == len(last) for t in tokens)
