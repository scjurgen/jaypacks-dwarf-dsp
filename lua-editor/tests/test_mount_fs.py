from __future__ import annotations

from pathlib import Path

import pytest

from lua_editor.api.model import ApiRegistry
from lua_editor.remote import (
    DEVICE,
    REPO,
    InvalidPath,
    LocalFS,
    Mount,
    MountFS,
    NotEmpty,
    NotFound,
    RemoteError,
    RepoFS,
    swap_mount,
)
from lua_editor.remote.transfer import State, plan_copy, remove_scripts, run_copy, script_files


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    plugins = tmp_path / "plugins"
    scripts = plugins / "shipahoy" / "scripts"
    scripts.mkdir(parents=True)
    (scripts / "follow.lua").write_bytes(b"-- follow\n")
    (scripts / "notes.lua").write_bytes(b"-- notes\n")
    (scripts / "readme.txt").write_bytes(b"not a script\n")
    (plugins / "shipahoy" / "ShipAhoyPedal.h").write_bytes(b"// not a script folder entry\n")
    (plugins / "nonscripted").mkdir()
    return plugins


@pytest.fixture
def device(tmp_path: Path) -> Path:
    folder = tmp_path / "device-scripts"
    folder.mkdir()
    return folder


@pytest.fixture
def mounts(repo: Path, device: Path) -> MountFS:
    return MountFS([Mount(REPO, lambda: RepoFS(repo)), Mount(DEVICE, lambda: LocalFS(device))])


def test_repo_fs_shows_scripts_folders_as_plugins(repo: Path) -> None:
    fs = RepoFS(repo)
    assert [e.name for e in fs.list("")] == ["shipahoy"]
    assert [e.name for e in fs.list("shipahoy")] == ["follow.lua", "notes.lua", "readme.txt"]
    assert fs.read("shipahoy/follow.lua") == b"-- follow\n"


def test_repo_fs_writes_into_the_scripts_folder(repo: Path) -> None:
    RepoFS(repo).write("shipahoy/new.lua", b"-- new\n")
    assert (repo / "shipahoy" / "scripts" / "new.lua").read_bytes() == b"-- new\n"


def test_repo_fs_refuses_a_symlink_out_of_the_scripts_folder(repo: Path, tmp_path: Path) -> None:
    outside = tmp_path / "outside"
    outside.mkdir()
    (outside / "secret.lua").write_bytes(b"-- secret\n")
    (repo / "shipahoy" / "scripts" / "out").symlink_to(outside)
    with pytest.raises(InvalidPath):
        RepoFS(repo).read("shipahoy/out/secret.lua")


def test_root_lists_the_mounts(mounts: MountFS) -> None:
    entries = mounts.list("")
    assert [(e.name, e.path, e.is_dir) for e in entries] == [(DEVICE, DEVICE, True), (REPO, REPO, True)]


def test_paths_are_routed_and_lifted(mounts: MountFS) -> None:
    entries = mounts.list("repo/shipahoy")
    assert [e.path for e in entries] == [
        "repo/shipahoy/follow.lua",
        "repo/shipahoy/notes.lua",
        "repo/shipahoy/readme.txt",
    ]
    assert mounts.read("repo/shipahoy/notes.lua") == b"-- notes\n"
    assert mounts.stat("repo/shipahoy").is_dir


def test_unknown_mount_is_not_found(mounts: MountFS) -> None:
    with pytest.raises(NotFound):
        mounts.read("elsewhere/x.lua")


def test_a_mount_itself_cannot_be_changed(mounts: MountFS) -> None:
    with pytest.raises((InvalidPath, NotEmpty)):  # a mount with files is NotEmpty before it gets that far
        mounts.delete("repo")
    with pytest.raises(InvalidPath):
        mounts.rename("repo", "other")


def test_rename_across_mounts_is_refused(mounts: MountFS) -> None:
    with pytest.raises(InvalidPath):
        mounts.rename("repo/shipahoy/follow.lua", "device/follow.lua")


def test_mount_is_opened_on_first_use_and_a_failed_open_is_retried() -> None:
    attempts = []

    def opener() -> LocalFS:
        attempts.append(1)
        if len(attempts) == 1:
            raise RemoteError("cannot connect")
        return LocalFS(Path("."))

    fs = MountFS([Mount(DEVICE, opener)])
    assert fs.stat("device").is_dir and attempts == []  # the mount's own node never connects
    with pytest.raises(RemoteError):
        fs.list("device")
    assert not fs.is_open(DEVICE)
    fs.list("device")
    assert fs.is_open(DEVICE) and len(attempts) == 2


def test_swap_mount() -> None:
    assert swap_mount("repo/shipahoy/a.lua", REPO, DEVICE) == "device/shipahoy/a.lua"
    assert swap_mount("repo", REPO, DEVICE) == "device"
    assert swap_mount("device/a.lua", REPO, DEVICE) is None
    assert swap_mount("repository/a.lua", REPO, DEVICE) is None


def test_script_files_of_a_file_and_a_folder(mounts: MountFS) -> None:
    assert script_files(mounts, "repo/shipahoy/follow.lua") == ["repo/shipahoy/follow.lua"]
    assert script_files(mounts, "repo/shipahoy/readme.txt") == []
    assert script_files(mounts, "repo/shipahoy") == ["repo/shipahoy/follow.lua", "repo/shipahoy/notes.lua"]


def test_plan_and_run_copy(mounts: MountFS, device: Path) -> None:
    pairs = [("repo/shipahoy/follow.lua", "device/shipahoy/follow.lua")]
    items = plan_copy(mounts, pairs, {})
    assert [i.state for i in items] == [State.NEW]
    written = run_copy(mounts, items)
    assert (device / "shipahoy" / "follow.lua").read_bytes() == b"-- follow\n"
    stamp = written["device/shipahoy/follow.lua"].stamp

    assert [i.state for i in plan_copy(mounts, pairs, {})] == [State.SAME]
    mounts.write("repo/shipahoy/follow.lua", b"-- follow v2\n")
    assert [i.state for i in plan_copy(mounts, pairs, {"device/shipahoy/follow.lua": stamp})] == [State.OURS]
    assert [i.state for i in plan_copy(mounts, pairs, {})] == [State.CHANGED]


def test_run_copy_skips_files_that_are_the_same(mounts: MountFS) -> None:
    pairs = [("repo/shipahoy/follow.lua", "device/shipahoy/follow.lua")]
    run_copy(mounts, plan_copy(mounts, pairs, {}))
    assert run_copy(mounts, plan_copy(mounts, pairs, {})) == {}


def test_remove_scripts_takes_the_log_along(mounts: MountFS, device: Path) -> None:
    (device / "shipahoy").mkdir()
    (device / "shipahoy" / "follow.lua").write_bytes(b"x")
    (device / "shipahoy" / "follow.log").write_bytes(b"log")
    removed = remove_scripts(mounts, ["device/shipahoy/follow.lua", "device/shipahoy/gone.lua"])
    assert removed == ["device/shipahoy/follow.lua", "device/shipahoy/follow.log"]
    assert list((device / "shipahoy").iterdir()) == []


def test_api_registry_finds_the_plugin_below_a_mount() -> None:
    sentinel = object()
    registry = ApiRegistry({"shipahoy": sentinel})  # type: ignore[dict-item]
    assert registry.for_path("shipahoy/a.lua") is sentinel
    assert registry.for_path("repo/shipahoy/a.lua") is sentinel
    assert registry.for_path("repo/other/shipahoy/a.lua") is None


def _parse(*argv: str):
    import argparse

    from lua_editor.app import build_filesystem

    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--device", nargs="?", const="host")
    parser.add_argument("--no-device", action="store_true")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--device-root", default="/scripts")
    parser.add_argument("--user")
    parser.add_argument("--key")
    parser.add_argument("--accept-new-host", action="store_true")
    return build_filesystem(parser.parse_args(argv), parser)


def test_command_line_builds_repo_and_device_mounts_without_connecting(repo: Path) -> None:
    fs, title = _parse("--repo", str(repo.parent))
    assert isinstance(fs, MountFS) and fs.names() == [REPO, DEVICE]
    assert not fs.is_open(DEVICE) and "device" in title


def test_command_line_without_device_has_only_the_repo(repo: Path) -> None:
    fs, _title = _parse("--repo", str(repo.parent), "--no-device")
    assert fs.names() == [REPO]


def test_command_line_root_edits_one_folder(tmp_path: Path) -> None:
    fs, _title = _parse("--root", str(tmp_path))
    assert isinstance(fs, LocalFS)


def test_command_line_refuses_a_folder_that_is_not_the_repo(tmp_path: Path) -> None:
    with pytest.raises(SystemExit):
        _parse("--repo", str(tmp_path))


def test_a_missing_file_is_named_with_its_mount(mounts: MountFS) -> None:
    for call in (
        lambda: mounts.read("device/shipahoy/gone.lua"),
        lambda: mounts.stat("repo/shipahoy/gone.lua"),
    ):
        with pytest.raises(NotFound) as caught:
            call()
        assert str(caught.value).split("/", 1)[0] in (DEVICE, REPO) and str(caught.value).endswith("gone.lua")
    with pytest.raises(NotFound) as caught:
        mounts.list("device/missing")
    assert caught.value.path == "device/missing"


def test_plan_copy_marks_a_missing_source_and_run_copy_skips_it(mounts: MountFS, device: Path) -> None:
    pairs = [("device/shipahoy/gone.lua", "repo/shipahoy/gone.lua"), ("repo/shipahoy/follow.lua", "device/shipahoy/f.lua")]
    items = plan_copy(mounts, pairs, {})
    assert [i.state for i in items] == [State.MISSING, State.NEW]
    assert list(run_copy(mounts, items)) == ["device/shipahoy/f.lua"]
    assert not (device / "shipahoy" / "gone.lua").exists()


def test_a_changed_target_reports_its_size(mounts: MountFS, device: Path) -> None:
    (device / "shipahoy").mkdir()
    (device / "shipahoy" / "f.lua").write_bytes(b"12345")
    item = plan_copy(mounts, [("repo/shipahoy/follow.lua", "device/shipahoy/f.lua")], {})[0]
    assert item.state is State.CHANGED and item.target_size == 5
