"""The scripts of the repository laid out like the device's scripts folder.

The repository keeps them in plugins/<plugin>/scripts, the device in <scripts folder>/<plugin>; this file system shows
the first as the second: "<plugin>/<rest>" is plugins/<plugin>/scripts/<rest>.
"""

from __future__ import annotations

import os
from pathlib import Path

from lua_editor.remote.base import Entry, InvalidPath
from lua_editor.remote.local import LocalFS

SCRIPTS_FOLDER = "scripts"


class RepoFS(LocalFS):
    """root is the plugins folder of the repository; only plugins that have a scripts folder show up."""

    def _full(self, path: str) -> Path:
        if not path:
            return self._root
        plugin, _, rest = path.partition("/")
        base = self._root / plugin / SCRIPTS_FOLDER
        full = base.joinpath(*rest.split("/")) if rest else base
        if not full.resolve().is_relative_to(base.resolve()):
            raise InvalidPath(f"path leaves the plugin's scripts folder: {path!r}")
        return full

    def _list(self, path: str) -> list[Entry]:
        entries = super()._list(path)
        if path:
            return entries
        return [entry for entry in entries if entry.is_dir and os.path.isdir(self._root / entry.name / SCRIPTS_FOLDER)]
