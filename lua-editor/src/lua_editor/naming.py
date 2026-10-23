"""Names the plugins accept (ScriptHost.h: letters, digits, '-' and '_', at most 47, file <name>.lua)."""

from __future__ import annotations

import re

SCRIPT_SUFFIX = ".lua"
USER_SLOTS = tuple(f"user-{number}" for number in range(1, 9))  # the scripts the plugins load from the user folder
_SCRIPT_STEM = re.compile(r"[A-Za-z0-9_-]{1,47}")
_PLAIN_NAME = re.compile(r"[A-Za-z0-9_-][A-Za-z0-9_.-]{0,63}")


class InvalidName(ValueError):
    """The message is meant for the user."""


def script_filename(name: str) -> str:
    """The file name for a script the user typed; '.lua' is added when missing."""
    name = name.strip()
    stem = name[: -len(SCRIPT_SUFFIX)] if name.endswith(SCRIPT_SUFFIX) else name
    if not _SCRIPT_STEM.fullmatch(stem):
        raise InvalidName("A script name has 1 to 47 letters, digits, '-' or '_' (then .lua).")
    return stem + SCRIPT_SUFFIX


def plain_name(name: str) -> str:
    """A folder name, or the name of a file that is not a script."""
    name = name.strip()
    if not _PLAIN_NAME.fullmatch(name):
        raise InvalidName("A name has up to 64 letters, digits, '-', '_' or '.', and does not start with a dot.")
    return name
