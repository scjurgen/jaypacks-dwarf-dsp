"""Behaviour every RemoteFS backend must show. A backend's test module subclasses RemoteFSContract and provides `fs`."""

from __future__ import annotations

import pytest

from lua_editor.remote import (
    AlreadyExists,
    Conflict,
    InvalidPath,
    NotEmpty,
    NotFound,
    RemoteFS,
    WrongKind,
    normalize_path,
)


class RemoteFSContract:
    @pytest.fixture
    def fs(self) -> RemoteFS:
        raise NotImplementedError

    # paths

    @pytest.mark.parametrize(
        "raw, expected",
        [("", ""), (".", ""), ("a", "a"), ("a/", "a"), ("a//b", "a/b"), ("./a/./b", "a/b")],
    )
    def test_paths_are_normalised(self, raw: str, expected: str) -> None:
        assert normalize_path(raw) == expected

    @pytest.mark.parametrize("raw", ["/a", "..", "a/../b", "a\\b", "a\0b", "x.lua-editor-tmp", "d/x.lua-editor-tmp"])
    def test_bad_paths_are_refused(self, raw: str) -> None:
        with pytest.raises(InvalidPath):
            normalize_path(raw)

    def test_every_operation_checks_the_path(self, fs: RemoteFS) -> None:
        with pytest.raises(InvalidPath):
            fs.read("../x")
        with pytest.raises(InvalidPath):
            fs.write("/x", b"")

    # stat, list, read

    def test_the_root_is_an_empty_folder_at_first(self, fs: RemoteFS) -> None:
        assert fs.stat("").is_dir
        assert fs.list() == []

    def test_a_written_file_is_read_back_byte_for_byte(self, fs: RemoteFS) -> None:
        data = b"-- caf\xc3\xa9\r\nfunction OnStart()\nend\n\x00\xff"
        entry = fs.write("a.lua", data)
        assert fs.read("a.lua") == data
        assert (entry.name, entry.path, entry.is_dir, entry.size) == ("a.lua", "a.lua", False, len(data))
        assert fs.stat("a.lua") == entry

    def test_list_is_sorted_and_has_kinds_and_paths(self, fs: RemoteFS) -> None:
        fs.mkdir("m")
        fs.write("m/z.lua", b"1")
        fs.write("m/a.lua", b"22")
        fs.mkdir("m/sub")
        entries = fs.list("m")
        assert [entry.name for entry in entries] == ["a.lua", "sub", "z.lua"]
        assert [entry.path for entry in entries] == ["m/a.lua", "m/sub", "m/z.lua"]
        assert [entry.is_dir for entry in entries] == [False, True, False]
        assert entries[0].size == 2

    def test_missing_things_raise_not_found(self, fs: RemoteFS) -> None:
        for call in (fs.read, fs.stat, fs.list, fs.delete):
            with pytest.raises(NotFound):
                call("nope")
        assert not fs.exists("nope")

    def test_a_path_through_a_file_is_not_found(self, fs: RemoteFS) -> None:
        fs.write("f", b"")
        with pytest.raises(NotFound):
            fs.stat("f/x")

    def test_kinds_are_not_mixed_up(self, fs: RemoteFS) -> None:
        fs.mkdir("d")
        fs.write("f", b"")
        with pytest.raises(WrongKind):
            fs.read("d")
        with pytest.raises(WrongKind):
            fs.list("f")
        with pytest.raises(WrongKind):
            fs.write("d", b"x")

    # write

    def test_write_replaces_an_existing_file(self, fs: RemoteFS) -> None:
        fs.write("a.lua", b"one")
        fs.write("a.lua", b"second")
        assert fs.read("a.lua") == b"second"

    def test_write_needs_an_existing_folder(self, fs: RemoteFS) -> None:
        with pytest.raises(NotFound):
            fs.write("missing/a.lua", b"x")

    def test_write_leaves_no_temp_file_behind(self, fs: RemoteFS) -> None:
        fs.write("a.lua", b"x")
        fs.write("a.lua", b"y")
        assert [entry.name for entry in fs.list()] == ["a.lua"]

    def test_the_root_cannot_be_written(self, fs: RemoteFS) -> None:
        with pytest.raises(InvalidPath):
            fs.write("", b"x")

    def test_write_with_the_stamp_that_was_read_succeeds(self, fs: RemoteFS) -> None:
        first = fs.write("a.lua", b"one")
        second = fs.write("a.lua", b"two", expected=first.stamp)
        assert fs.read("a.lua") == b"two"
        assert second.size == 3

    def test_write_after_a_change_elsewhere_is_a_conflict_and_changes_nothing(self, fs: RemoteFS) -> None:
        first = fs.write("a.lua", b"one")
        fs.write("a.lua", b"changed elsewhere")
        with pytest.raises(Conflict):
            fs.write("a.lua", b"mine", expected=first.stamp)
        assert fs.read("a.lua") == b"changed elsewhere"

    def test_write_after_the_file_went_away_is_a_conflict(self, fs: RemoteFS) -> None:
        first = fs.write("a.lua", b"one")
        fs.delete("a.lua")
        with pytest.raises(Conflict):
            fs.write("a.lua", b"mine", expected=first.stamp)
        assert not fs.exists("a.lua")

    # mkdir

    def test_mkdir_needs_its_parent_and_refuses_an_existing_name(self, fs: RemoteFS) -> None:
        with pytest.raises(NotFound):
            fs.mkdir("a/b")
        fs.mkdir("a")
        with pytest.raises(AlreadyExists):
            fs.mkdir("a")
        with pytest.raises(InvalidPath):
            fs.mkdir("")

    def test_mkdir_parents_creates_the_chain_and_accepts_what_is_there(self, fs: RemoteFS) -> None:
        fs.mkdir("a/b/c", parents=True)
        fs.mkdir("a/b/c", parents=True)
        assert fs.stat("a/b/c").is_dir

    def test_mkdir_parents_stops_at_a_file(self, fs: RemoteFS) -> None:
        fs.write("a", b"")
        with pytest.raises(AlreadyExists):
            fs.mkdir("a/b", parents=True)

    # rename

    def test_rename_moves_a_file_and_a_folder_with_its_content(self, fs: RemoteFS) -> None:
        fs.write("a.lua", b"x")
        fs.rename("a.lua", "b.lua")
        assert not fs.exists("a.lua") and fs.read("b.lua") == b"x"
        fs.mkdir("d")
        fs.write("d/f", b"y")
        fs.mkdir("e")
        fs.rename("d", "e/d2")
        assert fs.read("e/d2/f") == b"y"

    def test_rename_never_replaces(self, fs: RemoteFS) -> None:
        fs.write("a", b"1")
        fs.write("b", b"2")
        with pytest.raises(AlreadyExists):
            fs.rename("a", "b")
        assert fs.read("a") == b"1" and fs.read("b") == b"2"

    def test_rename_needs_a_source_and_a_target_folder(self, fs: RemoteFS) -> None:
        with pytest.raises(NotFound):
            fs.rename("nope", "x")
        fs.write("a", b"")
        with pytest.raises(NotFound):
            fs.rename("a", "missing/a")
        with pytest.raises(InvalidPath):
            fs.rename("a", "")

    # delete

    def test_delete_removes_a_file_and_an_empty_folder(self, fs: RemoteFS) -> None:
        fs.write("a", b"")
        fs.mkdir("d")
        fs.delete("a")
        fs.delete("d")
        assert fs.list() == []

    def test_delete_keeps_a_folder_that_is_not_empty(self, fs: RemoteFS) -> None:
        fs.mkdir("d")
        fs.write("d/f", b"")
        with pytest.raises(NotEmpty):
            fs.delete("d")
        assert fs.exists("d/f")

    def test_the_root_cannot_be_deleted(self, fs: RemoteFS) -> None:
        with pytest.raises(InvalidPath):
            fs.delete("")
