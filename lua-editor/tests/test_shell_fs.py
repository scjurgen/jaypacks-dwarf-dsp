"""ShellFS against the real Dwarf; skipped when it cannot be reached (LEDIT_DWARF_HOST picks another address)."""

from __future__ import annotations

import os
import uuid
from collections.abc import Iterator

import pytest

from lua_editor.remote import RemoteError, ShellFS
from lua_editor.remote.shell import DEFAULT_HOST, SshRunner
from tests.contract import RemoteFSContract

TEST_BASE = "/data/jaypacks/lua-editor-test"


@pytest.fixture(scope="module")
def runner() -> Iterator[SshRunner]:
    try:
        connection = SshRunner(os.environ.get("LEDIT_DWARF_HOST", DEFAULT_HOST), timeout=5)
    except RemoteError as error:
        pytest.skip(f"Dwarf not reachable: {error}")
    yield connection
    connection.run(f"rm -rf {TEST_BASE}")
    connection.close()


class TestShellFS(RemoteFSContract):
    @pytest.fixture
    def fs(self, runner: SshRunner) -> Iterator[ShellFS]:
        root = f"{TEST_BASE}/{uuid.uuid4().hex}"
        assert runner.run(f"mkdir -p {root}").status == 0
        yield ShellFS(runner, root)
        runner.run(f"rm -rf {root}")

    def test_names_with_spaces_and_quotes_survive(self, fs: ShellFS) -> None:
        fs.write("it's a name.lua", b"x")
        assert [entry.name for entry in fs.list()] == ["it's a name.lua"]
        assert fs.read("it's a name.lua") == b"x"

    def test_a_larger_file_round_trips(self, fs: ShellFS) -> None:
        data = bytes(range(256)) * 1024
        fs.write("big.bin", data)
        assert fs.read("big.bin") == data
