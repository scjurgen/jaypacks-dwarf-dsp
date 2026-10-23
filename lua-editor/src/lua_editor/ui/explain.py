"""An exception as something a person can act on: what happened, what to try, and the details for a bug report."""

from __future__ import annotations

import traceback
from dataclasses import dataclass

from lua_editor.remote import (
    DEVICE,
    REPO,
    AlreadyExists,
    Conflict,
    InvalidPath,
    NotEmpty,
    NotFound,
    RemoteError,
    WrongKind,
)
from lua_editor.textcodec import NotText


@dataclass(frozen=True)
class Explanation:
    summary: str  # one sentence: what went wrong
    hint: str  # what to try; may be empty
    details: str  # the operation, the exception and its traceback


def side_of(path: str) -> str:
    """Where a mount path lives, in words."""
    head = path.split("/", 1)[0]
    if head == DEVICE:
        return "on the device"
    if head == REPO:
        return "in the repo"
    return "there"


def explain(operation: str, error: BaseException) -> Explanation:
    summary, hint = _summary_and_hint(error)
    trace = "".join(traceback.format_exception(type(error), error, error.__traceback__)).rstrip()
    return Explanation(summary, hint, f"Operation: {operation}\nError: {type(error).__name__}: {error}\n\n{trace}")


def _summary_and_hint(error: BaseException) -> tuple[str, str]:
    text = str(error)
    if isinstance(error, NotFound):
        where = side_of(error.path)
        hint = "Refresh the tree (F5) to see what is there."
        if error.path.split("/", 1)[0] == DEVICE:
            hint = "It is not on the Dwarf (never pushed, or removed). Push it first, or refresh the tree (F5)."
        return f"{error.path or 'The file'} was not found {where}.", hint
    if isinstance(error, Conflict):
        return (
            "The file was changed by somebody else after it was read.",
            "Reload it (Ctrl+R) to see the other version, or save again and choose Overwrite.",
        )
    if isinstance(error, AlreadyExists):
        return f"{text} already exists.", "Choose another name."
    if isinstance(error, NotEmpty):
        return f"{text} is a folder with files in it.", "Delete or move its files first."
    if isinstance(error, WrongKind):
        return text[0].upper() + text[1:] + ".", "A file was used as a folder or the other way round."
    if isinstance(error, InvalidPath):
        return text[0].upper() + text[1:] + ".", "Names are letters, digits, '-', '_' and '.', and stay inside their folder."
    if isinstance(error, NotText):
        return "The file is not UTF-8 text.", "The editor only opens text files; check the file in another tool."
    if isinstance(error, RemoteError):
        lowered = text.lower()
        if "host key" in lowered:
            return (
                f"The Dwarf's ssh host key is not trusted: {text}",
                "Connect once with ssh to add it to known_hosts, or start the editor with --accept-new-host.",
            )
        if "cannot connect" in lowered:
            return (
                f"The Dwarf cannot be reached: {text.removeprefix('cannot connect to ')}",
                "Is it on, and cable or network up? 'ssh root@192.168.51.1' with your key must work. "
                f"The editor also works without it: start with --no-device, or use {REPO}/ only.",
            )
        if "ssh command failed" in lowered:
            return (
                f"The connection to the Dwarf broke: {text}",
                "Try again. A write goes through a temporary file, so the old file is still there.",
            )
        return text or "The operation failed.", "The details show where it happened."
    if isinstance(error, PermissionError):
        return f"Not allowed: {text}", "Check the permissions of the file and its folder."
    if isinstance(error, OSError):
        return f"The file system reported: {text}", "Check the disk space and the permissions."
    return f"{type(error).__name__}: {text}", "This is not an expected case; the details help to report it."
