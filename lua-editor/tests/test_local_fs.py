from __future__ import annotations

import os
from pathlib import Path

import pytest

from lua_editor.remote import InvalidPath, LocalFS, NotFound, RemoteError
from tests.contract import RemoteFSContract


class TestLocalFS(RemoteFSContract):
    @pytest.fixture
    def fs(self, tmp_path: Path) -> LocalFS:
        return LocalFS(tmp_path)

    def test_the_root_must_exist(self, tmp_path: Path) -> None:
        with pytest.raises(NotFound):
            LocalFS(tmp_path / "missing")

    def test_a_failed_write_keeps_the_old_file_and_removes_the_temp(
        self, fs: LocalFS, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        fs.write("a.lua", b"old")

        def broken(*_: object) -> None:
            raise OSError("disk full")

        monkeypatch.setattr(os, "replace", broken)
        with pytest.raises(RemoteError):
            fs.write("a.lua", b"new")
        monkeypatch.undo()
        assert fs.read("a.lua") == b"old"
        assert [path.name for path in fs.root.iterdir()] == ["a.lua"]

    def test_a_stray_temp_file_is_not_listed(self, fs: LocalFS) -> None:
        (fs.root / ".a.lua.lua-editor-tmp").write_bytes(b"half")
        assert fs.list() == []

    def test_a_symlink_cannot_lead_out_of_the_root(self, fs: LocalFS, tmp_path_factory: pytest.TempPathFactory) -> None:
        outside = tmp_path_factory.mktemp("outside")
        (outside / "secret").write_bytes(b"x")
        try:
            os.symlink(outside, fs.root / "link", target_is_directory=True)
        except (OSError, NotImplementedError):
            pytest.skip("symlinks not available")
        with pytest.raises(InvalidPath):
            fs.read("link/secret")
