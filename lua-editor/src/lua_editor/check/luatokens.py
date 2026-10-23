"""A Lua tokenizer for the checks: names, numbers, strings and punctuation with their positions; comments are dropped.

It assumes the text compiled (the syntax check runs first), so it does not report errors; an unterminated string just
ends at the end of the line.
"""

from __future__ import annotations

import bisect
import re
from dataclasses import dataclass

NUMBER = re.compile(r"0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*(?:[pP][+-]?\d+)?|\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?")
NAME = re.compile(r"[A-Za-z_]\w*")
LONG_OPEN = re.compile(r"\[(=*)\[")
OPERATORS = ("...", "==", "~=", "<=", ">=", "..", "//", "::", "<<", ">>")
ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}


@dataclass(frozen=True)
class Tok:
    kind: str  # name, number, string, op
    text: str  # the source text of the token
    start: int  # offsets into the text
    end: int
    line: int  # 1-based
    column: int  # 1-based
    value: float | str | None = None  # a number's value, a string's content


def _number(text: str) -> float | None:
    try:
        if text[:2] in ("0x", "0X") and not re.search(r"[.pP]", text):
            return float(int(text, 16))
        if text[:2] in ("0x", "0X"):
            return float.fromhex(text)
        return float(text)
    except ValueError:
        return None


def _unescape(body: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            nxt = body[i + 1]
            if nxt in ESCAPES:
                out.append(ESCAPES[nxt])
                i += 2
                continue
            digits = re.match(r"\d{1,3}", body[i + 1 :])
            if digits:
                out.append(chr(min(int(digits.group(0)), 255)))
                i += 1 + len(digits.group(0))
                continue
        out.append(c)
        i += 1
    return "".join(out)


def tokenize(text: str) -> list[Tok]:
    starts = [0] + [m.end() for m in re.finditer("\n", text)]

    def place(offset: int) -> tuple[int, int]:
        line = bisect.bisect_right(starts, offset)
        return line, offset - starts[line - 1] + 1

    tokens: list[Tok] = []
    i, n = 0, len(text)

    def add(kind: str, end: int, value: float | str | None = None) -> None:
        line, column = place(i)
        tokens.append(Tok(kind, text[i:end], i, end, line, column, value))

    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif text.startswith("--", i):
            opened = LONG_OPEN.match(text, i + 2)
            if opened:
                close = "]" + opened.group(1) + "]"
                end = text.find(close, opened.end())
                i = n if end < 0 else end + len(close)
            else:
                end = text.find("\n", i)
                i = n if end < 0 else end
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            end = min(j + 1, n)
            add("string", end, _unescape(text[i + 1 : j]))
            i = end
        elif c == "[" and LONG_OPEN.match(text, i):
            opened = LONG_OPEN.match(text, i)
            assert opened is not None
            close = "]" + opened.group(1) + "]"
            end = text.find(close, opened.end())
            end = n if end < 0 else end + len(close)
            body = text[opened.end() : end - len(close)]
            add("string", end, body[1:] if body.startswith("\n") else body)
            i = end
        elif c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            m = NUMBER.match(text, i)
            end = m.end() if m else i + 1
            add("number", end, _number(text[i:end]))
            i = end
        elif NAME.match(text, i):
            m = NAME.match(text, i)
            assert m is not None
            add("name", m.end())
            i = m.end()
        else:
            op = next((o for o in OPERATORS if text.startswith(o, i)), c)
            add("op", i + len(op))
            i += len(op)
    return tokens
