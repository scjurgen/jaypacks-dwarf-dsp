"""Several file systems under one root: the first folder of a path names the mount ("repo/shipahoy/follow.lua").

A mount is opened when it is first needed, so the editor starts without the device and a mount that cannot be reached
only fails the operations that touch it. The mounts' own paths are the rest of the path after the mount's name.
"""

from __future__ import annotations

import posixpath
from collections.abc import Callable
from typing import TypeVar

from lua_editor.remote.base import Entry, InvalidPath, NotFound, RemoteFS

REPO = "repo"
DEVICE = "device"
T = TypeVar("T")


class Mount:
    def __init__(self, name: str, opener: Callable[[], RemoteFS]) -> None:
        self.name = name
        self._opener = opener
        self._fs: RemoteFS | None = None

    @property
    def is_open(self) -> bool:
        return self._fs is not None

    def fs(self) -> RemoteFS:
        """The mount's file system; opened on the first call. A failed open raises and is tried again next time."""
        if self._fs is None:
            self._fs = self._opener()
        return self._fs

    def close(self) -> None:
        close = getattr(self._fs, "close", None)
        if close is not None:
            close()
        self._fs = None


def swap_mount(path: str, source: str, target: str) -> str | None:
    """The same file below another mount ("repo/a/b.lua" -> "device/a/b.lua"); None if path is not below source."""
    head, _, rest = path.partition("/")
    if head != source:
        return None
    return posixpath.join(target, rest) if rest else target


class MountFS(RemoteFS):
    def __init__(self, mounts: list[Mount]) -> None:
        self._mounts = {mount.name: mount for mount in mounts}

    def names(self) -> list[str]:
        return list(self._mounts)

    def mount(self, name: str) -> Mount | None:
        return self._mounts.get(name)

    def is_open(self, name: str) -> bool:
        mount = self._mounts.get(name)
        return mount is not None and mount.is_open

    def close(self) -> None:
        for mount in self._mounts.values():
            mount.close()

    def _route(self, path: str) -> tuple[Mount, str]:
        name, _, rest = path.partition("/")
        mount = self._mounts.get(name)
        if mount is None:
            raise NotFound(path)
        return mount, rest

    @staticmethod
    def _in(mount: Mount, call: Callable[[RemoteFS], T]) -> T:
        """Run a call on the mount's file system; a NotFound names the file with the mount in front."""
        try:
            return call(mount.fs())
        except NotFound as error:
            raise NotFound(posixpath.join(mount.name, error.path) if error.path else mount.name) from None

    @staticmethod
    def _lift(mount: Mount, entry: Entry) -> Entry:
        path = posixpath.join(mount.name, entry.path) if entry.path else mount.name
        return Entry(entry.name or mount.name, path, entry.is_dir, entry.size, entry.mtime)

    @staticmethod
    def _folder(name: str) -> Entry:
        return Entry(name, name, True, 0, 0)

    @staticmethod
    def _require_inside(rest: str, path: str) -> None:
        if rest == "":
            raise InvalidPath(f"a mount cannot be changed: {path!r}")

    def _stat(self, path: str) -> Entry:
        if path == "":
            return self._folder("")
        mount, rest = self._route(path)
        if rest == "":
            return self._folder(mount.name)  # does not connect: a mount is always there
        return self._lift(mount, self._in(mount, lambda fs: fs._stat(rest)))

    def _list(self, path: str) -> list[Entry]:
        if path == "":
            return [self._folder(name) for name in self._mounts]
        mount, rest = self._route(path)
        return [self._lift(mount, entry) for entry in self._in(mount, lambda fs: fs._list(rest))]

    def _read(self, path: str) -> bytes:
        mount, rest = self._route(path)
        return self._in(mount, lambda fs: fs._read(rest))

    def _write(self, path: str, data: bytes) -> None:
        mount, rest = self._route(path)
        self._require_inside(rest, path)
        self._in(mount, lambda fs: fs._write(rest, data))

    def _mkdir(self, path: str) -> None:
        mount, rest = self._route(path)
        self._require_inside(rest, path)
        self._in(mount, lambda fs: fs._mkdir(rest))

    def _rename(self, source: str, target: str) -> None:
        source_mount, source_rest = self._route(source)
        self._require_inside(source_rest, source)
        target_mount, target_rest = self._route(target)
        if source_mount is not target_mount:
            raise InvalidPath("a file cannot be renamed into another mount (push or pull it)")
        self._require_inside(target_rest, target)
        self._in(source_mount, lambda fs: fs._rename(source_rest, target_rest))

    def _delete(self, path: str) -> None:
        mount, rest = self._route(path)
        self._require_inside(rest, path)
        self._in(mount, lambda fs: fs._delete(rest))
