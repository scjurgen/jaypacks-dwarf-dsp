"""Copying scripts between two places of one RemoteFS (the repo mount and the device mount).

A copy is planned first (which files differ, which would overwrite something the editor did not put there) so the
window can ask once, then carried out. Both steps run in the worker thread.
"""

from __future__ import annotations

import posixpath
from collections.abc import Callable
from dataclasses import dataclass
from enum import Enum

from lua_editor.naming import SCRIPT_SUFFIX
from lua_editor.remote.base import Entry, NotFound, RemoteFS


class State(Enum):
    NEW = "new"  # nothing at the target yet
    SAME = "same"  # the target already has these bytes
    OURS = "ours"  # the target differs, but it is the version this editor wrote there before
    CHANGED = "changed"  # the target differs and somebody else wrote it: needs the user's go-ahead
    MISSING = "missing"  # the source is not there: nothing to copy


@dataclass(frozen=True)
class Item:
    source: str
    target: str
    data: bytes
    state: State
    target_size: int | None = None  # the size of what a copy would replace


def script_files(fs: RemoteFS, path: str) -> list[str]:
    """The script a path names, or the scripts below a folder (subfolders included), sorted."""
    entry = fs.stat(path)
    if not entry.is_dir:
        return [path] if path.endswith(SCRIPT_SUFFIX) else []
    found: list[str] = []
    for child in fs.list(path):
        if child.is_dir:
            found.extend(script_files(fs, child.path))
        elif child.name.endswith(SCRIPT_SUFFIX):
            found.append(child.path)
    return sorted(found)


def plan_copy(
    fs: RemoteFS,
    pairs: list[tuple[str, str]],
    known: dict[str, tuple[int, int]],
) -> list[Item]:
    """Read every source and compare it with its target. known holds the stamps of targets this editor wrote."""
    items = []
    for source, target in pairs:
        try:
            data = fs.read(source)
        except NotFound:
            items.append(Item(source, target, b"", State.MISSING))
            continue
        try:
            current: Entry | None = fs.stat(target)
        except NotFound:
            current = None
        if current is None:
            state = State.NEW
        elif fs.read(target) == data:
            state = State.SAME
        elif known.get(target) == current.stamp:
            state = State.OURS
        else:
            state = State.CHANGED
        items.append(Item(source, target, data, state, None if current is None else current.size))
    return items


def run_copy(fs: RemoteFS, items: list[Item], progress: Callable[[str], None] | None = None) -> dict[str, Entry]:
    """Write the items whose target differs (folders are made as needed); returns the new entry of each target."""
    written: dict[str, Entry] = {}
    for item in items:
        if item.state in (State.SAME, State.MISSING):
            continue
        fs.mkdir(posixpath.dirname(item.target), parents=True)
        written[item.target] = fs.write(item.target, item.data)
        if progress is not None:
            progress(item.target)
    return written


def remove_scripts(fs: RemoteFS, files: list[str]) -> list[str]:
    """Delete the scripts and their logs; a file that is already gone is not an error. Returns what was removed."""
    removed = []
    for path in files:
        for candidate in (path, path.removesuffix(SCRIPT_SUFFIX) + ".log"):
            try:
                fs.delete(candidate)
            except NotFound:
                continue
            removed.append(candidate)
    return removed
