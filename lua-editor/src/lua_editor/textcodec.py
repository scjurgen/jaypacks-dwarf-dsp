"""File bytes to editor text and back. The editor works with '\\n'; the file keeps its own line ending."""

from __future__ import annotations


class NotText(ValueError):
    """The bytes are not UTF-8."""


def decode_text(data: bytes) -> tuple[str, str]:
    """Return (text with '\\n' line ends, the file's line ending '\\n' or '\\r\\n').

    A file that mixes both is written back with the one it uses more.
    """
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise NotText(str(error)) from error
    crlf = text.count("\r\n")
    bare = text.count("\n") - crlf
    eol = "\r\n" if crlf > bare else "\n"
    return text.replace("\r\n", "\n"), eol


def encode_text(text: str, eol: str) -> bytes:
    return text.replace("\n", eol).encode("utf-8")
