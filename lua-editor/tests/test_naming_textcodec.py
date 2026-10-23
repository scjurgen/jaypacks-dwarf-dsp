from __future__ import annotations

import pytest

from lua_editor.naming import InvalidName, plain_name, script_filename
from lua_editor.textcodec import NotText, decode_text, encode_text


@pytest.mark.parametrize(
    "typed, expected",
    [("warm-bass", "warm-bass.lua"), ("warm-bass.lua", "warm-bass.lua"), (" a_1 ", "a_1.lua"), ("x" * 47, "x" * 47 + ".lua")],
)
def test_script_names_get_the_suffix(typed: str, expected: str) -> None:
    assert script_filename(typed) == expected


@pytest.mark.parametrize("typed", ["", ".lua", "a b", "a.b", "a/b", "ä", "x" * 48, "a.txt", "a.lua.lua"])
def test_bad_script_names_are_refused(typed: str) -> None:
    with pytest.raises(InvalidName):
        script_filename(typed)


@pytest.mark.parametrize("typed", ["sub", "my-dir_2", "a.b", "x" * 64])
def test_plain_names(typed: str) -> None:
    assert plain_name(typed) == typed


@pytest.mark.parametrize("typed", ["", ".hidden", "a b", "a/b", "x" * 65, ".."])
def test_bad_plain_names_are_refused(typed: str) -> None:
    with pytest.raises(InvalidName):
        plain_name(typed)


def test_lf_and_crlf_files_keep_their_ending() -> None:
    assert decode_text(b"a\nb\n") == ("a\nb\n", "\n")
    assert decode_text(b"a\r\nb\r\n") == ("a\nb\n", "\r\n")
    assert encode_text("a\nb\n", "\r\n") == b"a\r\nb\r\n"


def test_a_mixed_file_takes_the_ending_it_uses_more() -> None:
    assert decode_text(b"a\r\nb\r\nc\n")[1] == "\r\n"
    assert decode_text(b"a\nb\nc\r\n")[1] == "\n"


def test_an_empty_file_and_a_bom_survive() -> None:
    assert decode_text(b"") == ("", "\n")
    text, eol = decode_text(b"\xef\xbb\xbfx\n")
    assert encode_text(text, eol) == b"\xef\xbb\xbfx\n"


def test_bytes_that_are_not_utf8_are_refused() -> None:
    with pytest.raises(NotText):
        decode_text(b"caf\xe9")
