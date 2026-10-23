"""A folder on this machine standing in for the device; the sandbox folder can be copied between machines."""

from __future__ import annotations

import os
import posixpath
import stat as stat_module
from pathlib import Path

from lua_editor.remote.base import TEMP_SUFFIX, Entry, InvalidPath, NotFound, RemoteError, RemoteFS


class LocalFS(RemoteFS):
    def __init__(self, root: str | os.PathLike[str]) -> None:
        self._root = Path(root).resolve()
        if not self._root.is_dir():
            raise NotFound(f"not a folder: {self._root}")

    @property
    def root(self) -> Path:
        return self._root

    def _full(self, path: str) -> Path:
        full = self._root.joinpath(*path.split("/")) if path else self._root
        # a symlink inside the tree must not lead out of it
        if not full.resolve().is_relative_to(self._root):
            raise InvalidPath(f"path leaves the root: {path!r}")
        return full

    @staticmethod
    def _entry(path: str, info: os.stat_result) -> Entry:
        is_dir = stat_module.S_ISDIR(info.st_mode)
        return Entry(posixpath.basename(path), path, is_dir, 0 if is_dir else info.st_size, int(info.st_mtime))

    def _stat(self, path: str) -> Entry:
        try:
            return self._entry(path, self._full(path).stat())
        except (FileNotFoundError, NotADirectoryError):
            raise NotFound(path) from None
        except OSError as error:
            raise RemoteError(f"{path!r}: {error}") from error

    def _list(self, path: str) -> list[Entry]:
        entries = []
        try:
            with os.scandir(self._full(path)) as scan:
                for item in scan:
                    if not item.name.endswith(TEMP_SUFFIX):
                        entries.append(self._entry(posixpath.join(path, item.name), item.stat()))
        except OSError as error:
            raise RemoteError(f"{path!r}: {error}") from error
        return entries

    def _read(self, path: str) -> bytes:
        try:
            return self._full(path).read_bytes()
        except FileNotFoundError:
            raise NotFound(path) from None
        except OSError as error:
            raise RemoteError(f"{path!r}: {error}") from error

    def _write(self, path: str, data: bytes) -> None:
        target = self._full(path)
        temp = target.with_name("." + target.name + TEMP_SUFFIX)
        try:
            with open(temp, "wb") as handle:
                handle.write(data)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temp, target)
        except OSError as error:
            raise RemoteError(f"{path!r}: {error}") from error
        finally:
            temp.unlink(missing_ok=True)

    def _mkdir(self, path: str) -> None:
        try:
            self._full(path).mkdir()
        except OSError as error:
            raise RemoteError(f"{path!r}: {error}") from error

    def _rename(self, source: str, target: str) -> None:
        try:
            os.rename(self._full(source), self._full(target))
        except OSError as error:
            raise RemoteError(f"{source!r} to {target!r}: {error}") from error

    def _delete(self, path: str) -> None:
        full = self._full(path)
        try:
            full.rmdir() if full.is_dir() else full.unlink()
        except OSError as error:
            raise RemoteError(f"{path!r}: {error}") from error
