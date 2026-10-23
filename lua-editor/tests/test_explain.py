from __future__ import annotations

import pytest

from lua_editor.remote import (
    AlreadyExists,
    Conflict,
    InvalidPath,
    NotEmpty,
    NotFound,
    RemoteError,
    WrongKind,
)
from lua_editor.textcodec import NotText
from lua_editor.ui.explain import explain, side_of


def test_not_found_names_the_file_and_the_side() -> None:
    info = explain("Pull", NotFound("device/midiscript/chords.lua"))
    assert info.summary == "device/midiscript/chords.lua was not found on the device."
    assert "Push it first" in info.hint


def test_not_found_in_the_repo() -> None:
    info = explain("Open", NotFound("repo/shipahoy/gone.lua"))
    assert "in the repo" in info.summary and "F5" in info.hint


def test_cannot_connect_says_what_to_check_and_how_to_work_without() -> None:
    info = explain("List device", RemoteError("cannot connect to 192.168.51.1: timed out"))
    assert "cannot be reached" in info.summary and "192.168.51.1: timed out" in info.summary
    assert "ssh root@" in info.hint and "--no-device" in info.hint


def test_host_key_problem_points_to_known_hosts() -> None:
    info = explain("List device", RemoteError("host key of 192.168.51.1 differs from known_hosts: x"))
    assert "known_hosts" in info.hint and "--accept-new-host" in info.hint


def test_connection_lost_during_an_operation() -> None:
    info = explain("Push", RemoteError("ssh command failed: Socket is closed"))
    assert "broke" in info.summary and "old file is still there" in info.hint


@pytest.mark.parametrize(
    "error, word",
    [
        (Conflict("changed since it was read: 'a.lua'"), "changed"),
        (AlreadyExists("b.lua"), "already exists"),
        (NotEmpty("sub"), "files in it"),
        (WrongKind("not a folder: 'a.lua'"), "Not a folder"),
        (InvalidPath("path leaves the root: '../x'"), "leaves the root"),
        (NotText("x"), "not UTF-8"),
        (PermissionError("denied"), "Not allowed"),
        (OSError("disk full"), "disk full"),
    ],
)
def test_expected_cases_have_a_sentence_and_a_hint(error: Exception, word: str) -> None:
    info = explain("Op", error)
    assert word in info.summary and info.hint


def test_an_unexpected_exception_is_still_shown_with_its_type() -> None:
    info = explain("Op", KeyError("boom"))
    assert "KeyError" in info.summary


def test_details_have_operation_exception_and_traceback() -> None:
    try:
        raise NotFound("device/x.lua")
    except NotFound as error:
        info = explain("Pull", error)
    assert info.details.startswith("Operation: Pull\nError: NotFound: device/x.lua")
    assert "Traceback" in info.details


def test_side_of() -> None:
    assert side_of("device/a") == "on the device" and side_of("repo/a") == "in the repo" and side_of("a") == "there"
