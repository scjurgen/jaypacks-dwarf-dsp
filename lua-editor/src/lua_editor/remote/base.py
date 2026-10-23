"""The file system lua-editor edits on: one interface, a backend per kind of remote (local folder, ssh shell).

Paths are POSIX, relative to the root of the tree ("" is the root). The public methods check and normalise them and
apply the rules every backend shares; a backend implements only the small underscore primitives.
"""

from __future__ import annotations

import posixpath
from abc import ABC, abstractmethod
from dataclasses import dataclass

# Atomic writes go through a temp file with this suffix; backends hide it and no path may use it.
TEMP_SUFFIX = ".lua-editor-tmp"


class RemoteError(Exception):
    """Base of every error a RemoteFS raises."""


class InvalidPath(RemoteError):
    """The path is absolute, climbs out of the root, or has a character or name that is not allowed."""


class NotFound(RemoteError):
    """The message is the path (relative to the file system that raised it); MountFS puts the mount's name in front."""

    def __init__(self, path: str = "") -> None:
        super().__init__(path)
        self.path = path


class AlreadyExists(RemoteError):
    pass


class WrongKind(RemoteError):
    """A file where a folder is needed, or the other way round."""


class NotEmpty(RemoteError):
    pass


class Conflict(RemoteError):
    """The file changed (or went away) since it was read."""


@dataclass(frozen=True)
class Entry:
    """name and path are POSIX; mtime is whole seconds, the finest unit a busybox stat gives."""

    name: str
    path: str
    is_dir: bool
    size: int
    mtime: int

    @property
    def stamp(self) -> tuple[int, int]:
        """What a later write compares against to notice a change made elsewhere."""
        return (self.size, self.mtime)


def normalize_path(path: str) -> str:
    if "\\" in path or "\0" in path:
        raise InvalidPath(f"not allowed in a path: {path!r}")
    if path.startswith("/"):
        raise InvalidPath(f"path must be relative: {path!r}")
    parts = [part for part in path.split("/") if part not in ("", ".")]
    if ".." in parts:
        raise InvalidPath(f"path leaves the root: {path!r}")
    if any(part.endswith(TEMP_SUFFIX) for part in parts):
        raise InvalidPath(f"name ends in {TEMP_SUFFIX}: {path!r}")
    return "/".join(parts)


class RemoteFS(ABC):
    def list(self, path: str = "") -> list[Entry]:
        """The entries of a folder sorted by name."""
        path = normalize_path(path)
        if not self._stat(path).is_dir:
            raise WrongKind(f"not a folder: {path!r}")
        return sorted(self._list(path), key=lambda entry: entry.name)

    def stat(self, path: str) -> Entry:
        return self._stat(normalize_path(path))

    def exists(self, path: str) -> bool:
        try:
            self._stat(normalize_path(path))
        except NotFound:
            return False
        return True

    def read(self, path: str) -> bytes:
        path = normalize_path(path)
        if self._stat(path).is_dir:
            raise WrongKind(f"is a folder: {path!r}")
        return self._read(path)

    def write(self, path: str, data: bytes, expected: tuple[int, int] | None = None) -> Entry:
        """Atomically replace or create a file and return its new Entry.

        expected is the stamp of the version the caller edited; if the file now differs, or is gone, Conflict is
        raised and nothing is written. None skips the check (a new file, or the user chose to overwrite).
        """
        path = normalize_path(path)
        self._require_name(path)
        try:
            current: Entry | None = self._stat(path)
        except NotFound:
            current = None
        if current is not None and current.is_dir:
            raise WrongKind(f"is a folder: {path!r}")
        if expected is not None and (current is None or current.stamp != expected):
            raise Conflict(f"changed since it was read: {path!r}")
        if not self._stat(posixpath.dirname(path)).is_dir:
            raise WrongKind(f"not a folder: {posixpath.dirname(path)!r}")
        self._write(path, data)
        return self._stat(path)

    def mkdir(self, path: str, parents: bool = False) -> None:
        path = normalize_path(path)
        self._require_name(path)
        if parents:
            built = ""
            for part in path.split("/"):
                built = posixpath.join(built, part)
                try:
                    entry = self._stat(built)
                except NotFound:
                    self._mkdir(built)
                    continue
                if not entry.is_dir:
                    raise AlreadyExists(f"a file is in the way: {built!r}")
            return
        if self.exists(path):
            raise AlreadyExists(path)
        if not self._stat(posixpath.dirname(path)).is_dir:
            raise WrongKind(f"not a folder: {posixpath.dirname(path)!r}")
        self._mkdir(path)

    def rename(self, source: str, target: str) -> None:
        """Move a file or folder; an existing target is never replaced."""
        source = normalize_path(source)
        target = normalize_path(target)
        self._require_name(source)
        self._require_name(target)
        self._stat(source)
        if self.exists(target):
            raise AlreadyExists(target)
        if not self._stat(posixpath.dirname(target)).is_dir:
            raise WrongKind(f"not a folder: {posixpath.dirname(target)!r}")
        self._rename(source, target)

    def delete(self, path: str) -> None:
        """Remove a file or an empty folder."""
        path = normalize_path(path)
        self._require_name(path)
        if self._stat(path).is_dir and self._list(path):
            raise NotEmpty(path)
        self._delete(path)

    @staticmethod
    def _require_name(path: str) -> None:
        if path == "":
            raise InvalidPath("the root cannot be used here")

    @abstractmethod
    def _stat(self, path: str) -> Entry: ...

    @abstractmethod
    def _list(self, path: str) -> list[Entry]:
        """The entries of an existing folder, without the temp files."""

    @abstractmethod
    def _read(self, path: str) -> bytes: ...

    @abstractmethod
    def _write(self, path: str, data: bytes) -> None:
        """Write via a temp file in the same folder and rename it over the target; clean up on failure."""

    @abstractmethod
    def _mkdir(self, path: str) -> None: ...

    @abstractmethod
    def _rename(self, source: str, target: str) -> None: ...

    @abstractmethod
    def _delete(self, path: str) -> None: ...
