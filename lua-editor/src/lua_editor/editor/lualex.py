"""A Lua tokenizer for highlighting: one line at a time, with the state that carries long strings and comments over."""

from __future__ import annotations

import re
from dataclasses import dataclass

from lua_editor.editor import luastd
from lua_editor.editor.vocabulary import Vocabulary

NORMAL = 0
LONG_COMMENT = 1  # state = LONG_COMMENT + level
LONG_STRING = 100  # state = LONG_STRING + level

NUMBER = re.compile(r"0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*(?:[pP][+-]?\d+)?|\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?")
NAME = re.compile(r"[A-Za-z_]\w*")
LONG_OPEN = re.compile(r"\[(=*)\[")
ASSIGN = re.compile(r"\s*=(?!=)")


@dataclass(frozen=True)
class Token:
    start: int
    end: int
    kind: str  # comment, string, number, keyword, command, hook, enum, enum_member, runtime, library, std, field, member


def _long_end(line: str, start: int, level: int) -> int | None:
    close = "]" + "=" * level + "]"
    at = line.find(close, start)
    return None if at < 0 else at + len(close)


def tokenize_line(line: str, state: int, vocab: Vocabulary) -> tuple[list[Token], int]:
    tokens: list[Token] = []
    i, n = 0, len(line)

    if state != NORMAL:
        kind, level = ("comment", state - LONG_COMMENT) if state < LONG_STRING else ("string", state - LONG_STRING)
        end = _long_end(line, 0, level)
        if end is None:
            return ([Token(0, n, kind)] if n else []), state
        tokens.append(Token(0, end, kind))
        i = end

    previous: list[tuple[str, str]] = []  # the last significant tokens of this line: (kind, text)
    while i < n:
        c = line[i]
        if c.isspace():
            i += 1
        elif line.startswith("--", i):
            opened = LONG_OPEN.match(line, i + 2)
            if opened:
                level = len(opened.group(1))
                end = _long_end(line, opened.end(), level)
                if end is None:
                    tokens.append(Token(i, n, "comment"))
                    return tokens, LONG_COMMENT + level
                tokens.append(Token(i, end, "comment"))
                i = end
            else:
                tokens.append(Token(i, n, "comment"))
                return tokens, NORMAL
        elif c in "\"'":
            j = i + 1
            while j < n and line[j] != c:
                j += 2 if line[j] == "\\" else 1
            end = min(j + 1, n)
            tokens.append(Token(i, end, "string"))
            previous.append(("string", line[i:end]))
            i = end
        elif c == "[" and LONG_OPEN.match(line, i):
            level = len(LONG_OPEN.match(line, i).group(1))  # type: ignore[union-attr]
            end = _long_end(line, i + level + 2, level)
            if end is None:
                tokens.append(Token(i, n, "string"))
                return tokens, LONG_STRING + level
            tokens.append(Token(i, end, "string"))
            i = end
        elif c.isdigit() or (c == "." and i + 1 < n and line[i + 1].isdigit()):
            m = NUMBER.match(line, i)
            end = m.end() if m else i + 1
            tokens.append(Token(i, end, "number"))
            previous.append(("number", line[i:end]))
            i = end
        elif NAME.match(line, i):
            m = NAME.match(line, i)
            assert m is not None
            word = m.group(0)
            kind = _classify(word, line, m.end(), previous, vocab)
            if kind:
                tokens.append(Token(i, m.end(), kind))
            previous.append(("name", word))
            i = m.end()
        else:
            previous.append(("op", c))
            i += 1
        del previous[:-3]
    return tokens, NORMAL


def _classify(word: str, line: str, end: int, previous: list[tuple[str, str]], vocab: Vocabulary) -> str:
    if len(previous) >= 2 and previous[-1][0] == "op" and previous[-1][1] in ".:" and previous[-2][0] == "name":
        base = previous[-2][1]
        if base in vocab.enums:
            return "enum_member" if word in vocab.enums[base].entries else ""
        if base in vocab.libraries:
            return "member" if vocab.libraries[base].member(word) else ""
        if base in vocab.std_tables:
            return "std" if word in vocab.std_tables[base] else ""
        return ""
    if previous and previous[-1] == ("op", ".") or previous and previous[-1] == ("op", ":"):
        return ""
    if word in luastd.KEYWORDS:
        return "keyword"
    if word in vocab.commands:
        return "command"
    if word in vocab.hooks:
        return "hook"
    if word in vocab.enums:
        return "enum"
    if word in vocab.runtime_globals:
        return "runtime"
    if word in vocab.libraries:
        return "library"
    if word in vocab.std_tables or word in luastd.BASE:
        return "std"
    if word in vocab.field_names and ASSIGN.match(line, end):
        return "field"
    return ""
