from __future__ import annotations

from pathlib import Path

import pytest
from pytestqt.qtbot import QtBot

from lua_editor.remote import LocalFS
from lua_editor.ui.mainwindow import MainWindow
from lua_editor.ui.worker import RemoteWorker


class FakeDialogs:
    """Answers the window's questions from a script and records what it was told."""

    def __init__(self) -> None:
        self.names: list[str | None] = []
        self.confirms: list[bool] = []
        self.unsaved = "cancel"
        self.conflict = "cancel"
        self.errors: list[str] = []

    def ask_name(self, title: str, label: str, text: str = "") -> str | None:
        return self.names.pop(0)

    def confirm(self, title: str, text: str, accept: str = "Yes", details: str = "") -> bool:
        return self.confirms.pop(0)

    def ask_unsaved(self, name: str) -> str:
        return self.unsaved

    def ask_conflict(self, name: str) -> str:
        return self.conflict

    def error(self, title: str, text: str, hint: str = "", details: str = "") -> None:
        self.errors.append(f"{title}: {text}")


@pytest.fixture
def fs(tmp_path: Path) -> LocalFS:
    (tmp_path / "synth").mkdir()
    (tmp_path / "synth" / "bass.lua").write_bytes(b"-- bass\r\nfunction OnStart()\r\nend\r\n")
    (tmp_path / "synth" / "lead.lua").write_bytes(b"-- lead\n")
    (tmp_path / "synth" / "sub").mkdir()
    (tmp_path / "top.lua").write_bytes(b"-- top\n")
    return LocalFS(tmp_path)


@pytest.fixture
def dialogs() -> FakeDialogs:
    return FakeDialogs()


@pytest.fixture
def window(qtbot: QtBot, fs: LocalFS, dialogs: FakeDialogs) -> MainWindow:
    win = MainWindow(fs, dialogs)  # type: ignore[arg-type]
    qtbot.addWidget(win)
    qtbot.waitUntil(lambda: "synth" in win.tree.paths())
    return win


def type_text(editor, text: str) -> None:
    """Replace the content the way typing does; setPlainText would reset the modified flag."""
    editor.selectAll()
    editor.insertPlainText(text)


def open_and_wait(qtbot: QtBot, window: MainWindow, path: str):
    window.open_file(path)
    qtbot.waitUntil(lambda: window.editor_for(path) is not None)
    return window.editor_for(path)


# worker


def test_the_worker_delivers_results_and_errors_in_the_ui_thread(qtbot: QtBot) -> None:
    worker = RemoteWorker()
    results: list[object] = []

    def boom() -> None:
        raise ValueError("no")

    worker.submit(lambda: 42, results.append, results.append)
    worker.submit(boom, results.append, results.append)
    qtbot.waitUntil(lambda: len(results) == 2)
    assert results[0] == 42
    assert isinstance(results[1], ValueError)
    assert worker.call(lambda: "after the queue") == "after the queue"
    worker.shutdown()


# tree


def test_the_tree_lists_folders_when_they_are_expanded(qtbot: QtBot, window: MainWindow, fs: LocalFS) -> None:
    assert window.tree.paths() == ["synth", "top.lua"]
    index = window.tree.model().index(0, 0)
    window.tree.expand(index)
    qtbot.waitUntil(lambda: "synth/bass.lua" in window.tree.paths())
    assert window.tree.paths() == ["synth", "synth/bass.lua", "synth/lead.lua", "synth/sub", "top.lua"]


def test_refresh_shows_new_and_removed_files_and_keeps_the_rest(qtbot: QtBot, window: MainWindow, fs: LocalFS) -> None:
    window.tree.expand(window.tree.model().index(0, 0))
    qtbot.waitUntil(lambda: "synth/bass.lua" in window.tree.paths())
    fs.write("synth/new.lua", b"")
    fs.delete("synth/lead.lua")
    window.tree.refresh_folder("synth")
    qtbot.waitUntil(lambda: "synth/new.lua" in window.tree.paths())
    assert window.tree.paths() == ["synth", "synth/bass.lua", "synth/new.lua", "synth/sub", "top.lua"]


def test_the_tree_shows_folders_first(qtbot: QtBot, window: MainWindow) -> None:
    model = window.tree.model()
    assert [model.index(row, 0).data() for row in range(model.rowCount())] == ["synth", "top.lua"]
    window.tree.expand(model.index(0, 0))
    qtbot.waitUntil(lambda: "synth/sub" in window.tree.paths())
    folder = model.index(0, 0)
    assert [model.index(row, 0, folder).data() for row in range(model.rowCount(folder))] == ["sub", "bass.lua", "lead.lua"]


# opening and saving


def test_opening_a_file_shows_its_text_with_lf(qtbot: QtBot, window: MainWindow) -> None:
    editor = open_and_wait(qtbot, window, "synth/bass.lua")
    assert editor.toPlainText() == "-- bass\nfunction OnStart()\nend\n"
    assert window.tabs.tabText(0) == "bass.lua"
    assert not editor.is_dirty()


def test_opening_twice_gives_one_tab(qtbot: QtBot, window: MainWindow) -> None:
    open_and_wait(qtbot, window, "top.lua")
    window.open_file("top.lua")
    assert window.tabs.count() == 1


def test_a_file_that_is_not_utf8_is_refused(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    fs.write("bin.lua", b"caf\xe9")
    window.open_file("bin.lua")
    qtbot.waitUntil(lambda: bool(dialogs.errors))
    assert window.tabs.count() == 0


def test_editing_marks_the_tab_and_save_writes_with_the_files_line_ending(
    qtbot: QtBot, window: MainWindow, fs: LocalFS
) -> None:
    editor = open_and_wait(qtbot, window, "synth/bass.lua")
    type_text(editor, "-- bass\nfunction OnStart()\n    Log(1)\nend\n")
    assert editor.is_dirty()
    assert window.tabs.tabText(0) == "bass.lua *"
    window.save_current()
    qtbot.waitUntil(lambda: not editor.is_dirty())
    assert fs.read("synth/bass.lua") == b"-- bass\r\nfunction OnStart()\r\n    Log(1)\r\nend\r\n"
    assert window.tabs.tabText(0) == "bass.lua"


def test_a_second_save_after_the_first_is_not_a_conflict(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    editor = open_and_wait(qtbot, window, "top.lua")
    type_text(editor, "one\n")
    window.save_current()
    qtbot.waitUntil(lambda: not editor.is_dirty())
    type_text(editor, "two\n")
    window.save_current()
    qtbot.waitUntil(lambda: not editor.is_dirty())
    assert fs.read("top.lua") == b"two\n"
    assert dialogs.errors == []


def test_a_change_elsewhere_gives_a_conflict_that_can_be_cancelled(
    qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs
) -> None:
    editor = open_and_wait(qtbot, window, "top.lua")
    fs.write("top.lua", b"-- changed by someone else\n")
    type_text(editor, "mine\n")
    dialogs.conflict = "cancel"
    window.save_current()
    qtbot.wait(200)
    assert editor.is_dirty()
    assert fs.read("top.lua") == b"-- changed by someone else\n"


def test_a_conflict_can_be_overwritten(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    editor = open_and_wait(qtbot, window, "top.lua")
    fs.write("top.lua", b"-- changed by someone else\n")
    type_text(editor, "mine\n")
    dialogs.conflict = "overwrite"
    window.save_current()
    qtbot.waitUntil(lambda: not editor.is_dirty())
    assert fs.read("top.lua") == b"mine\n"


def test_a_conflict_can_be_resolved_by_reloading(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    editor = open_and_wait(qtbot, window, "top.lua")
    fs.write("top.lua", b"-- changed by someone else\n")
    type_text(editor, "mine\n")
    dialogs.conflict = "reload"
    window.save_current()
    qtbot.waitUntil(lambda: editor.toPlainText() == "-- changed by someone else\n")
    assert not editor.is_dirty()


def test_reload_asks_before_discarding_changes(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    editor = open_and_wait(qtbot, window, "top.lua")
    type_text(editor, "mine\n")
    dialogs.confirms = [False]
    window.reload_current()
    qtbot.wait(100)
    assert editor.toPlainText() == "mine\n"
    dialogs.confirms = [True]
    window.reload_current()
    qtbot.waitUntil(lambda: editor.toPlainText() == "-- top\n")


# closing


def test_closing_a_dirty_tab_follows_the_answer(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    editor = open_and_wait(qtbot, window, "top.lua")
    type_text(editor, "mine\n")
    dialogs.unsaved = "cancel"
    assert not window.close_editor(editor)
    assert window.tabs.count() == 1
    dialogs.unsaved = "save"
    assert window.close_editor(editor)
    assert window.tabs.count() == 0
    assert fs.read("top.lua") == b"mine\n"


def test_discard_closes_without_writing(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    editor = open_and_wait(qtbot, window, "top.lua")
    type_text(editor, "mine\n")
    dialogs.unsaved = "discard"
    assert window.close_editor(editor)
    assert fs.read("top.lua") == b"-- top\n"


# tree operations


def test_new_script_adds_the_suffix_creates_the_file_and_opens_it(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    window.tree.expand(window.tree.model().index(0, 0))
    qtbot.waitUntil(lambda: "synth/bass.lua" in window.tree.paths())
    dialogs.names = ["fresh"]
    window.new_script("synth")
    qtbot.waitUntil(lambda: window.editor_for("synth/fresh.lua") is not None)
    assert fs.read("synth/fresh.lua") == b""
    qtbot.waitUntil(lambda: "synth/fresh.lua" in window.tree.paths())


def test_new_script_refuses_a_bad_name_and_an_existing_file(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    dialogs.names = ["bad name"]
    window.new_script("synth")
    assert len(dialogs.errors) == 1
    dialogs.names = ["bass"]
    window.new_script("synth")
    qtbot.waitUntil(lambda: len(dialogs.errors) == 2)
    assert fs.read("synth/bass.lua").startswith(b"-- bass")


def test_new_folder(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    dialogs.names = ["extra"]
    window.new_folder("")
    qtbot.waitUntil(lambda: "extra" in window.tree.paths())
    assert fs.stat("extra").is_dir


def test_rename_moves_the_file_and_keeps_the_open_tab(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    editor = open_and_wait(qtbot, window, "top.lua")
    dialogs.names = ["renamed"]
    window.rename("top.lua", False)
    qtbot.waitUntil(lambda: fs.exists("renamed.lua"))
    qtbot.waitUntil(lambda: window.editor_for("renamed.lua") is editor)
    assert window.editor_for("top.lua") is None
    assert window.tabs.tabText(0) == "renamed.lua"
    type_text(editor, "saved after rename\n")
    window.save_current()
    qtbot.waitUntil(lambda: not editor.is_dirty())
    assert fs.read("renamed.lua") == b"saved after rename\n"


def test_renaming_a_folder_retargets_the_tabs_inside(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    editor = open_and_wait(qtbot, window, "synth/lead.lua")
    dialogs.names = ["keys"]
    window.rename("synth", True)
    qtbot.waitUntil(lambda: window.editor_for("keys/lead.lua") is editor)
    assert fs.exists("keys/lead.lua")


def test_delete_asks_first_and_closes_the_tab(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    open_and_wait(qtbot, window, "top.lua")
    dialogs.confirms = [False]
    window.delete("top.lua", False)
    qtbot.wait(100)
    assert fs.exists("top.lua")
    dialogs.confirms = [True]
    window.delete("top.lua", False)
    qtbot.waitUntil(lambda: not fs.exists("top.lua"))
    qtbot.waitUntil(lambda: window.tabs.count() == 0)
    qtbot.waitUntil(lambda: "top.lua" not in window.tree.paths())


def test_deleting_a_folder_that_is_not_empty_reports_the_error(qtbot: QtBot, window: MainWindow, fs: LocalFS, dialogs: FakeDialogs) -> None:
    dialogs.confirms = [True]
    window.delete("synth", True)
    qtbot.waitUntil(lambda: bool(dialogs.errors))
    assert fs.exists("synth/bass.lua")


def test_the_plugin_log_is_shown_after_a_save_and_on_a_tab_change(
    qtbot: QtBot, fs: LocalFS, dialogs: FakeDialogs, tmp_path: Path
) -> None:
    (tmp_path / "synth" / "bass.log").write_bytes(b"state: running\ngeneration: 3\n")
    win = MainWindow(fs, dialogs, log_delay_ms=10)  # type: ignore[arg-type]
    qtbot.addWidget(win)
    qtbot.waitUntil(lambda: "synth" in win.tree.paths())
    win.open_file("synth/bass.lua")
    qtbot.waitUntil(lambda: "generation: 3" in win.log_panel.toPlainText())
    assert win.log_panel.toPlainText().startswith("bass.log")
    (tmp_path / "synth" / "bass.log").write_bytes(b"state: running\ngeneration: 4\n")
    editor = win.editor_for("synth/bass.lua")
    editor.insertPlainText("-- x\n")
    win.save_current()
    qtbot.waitUntil(lambda: "generation: 4" in win.log_panel.toPlainText())
    win.open_file("synth/lead.lua")
    qtbot.waitUntil(lambda: "no log file yet" in win.log_panel.toPlainText())
    assert win.log_panel.toPlainText().startswith("lead.log")


def test_only_scripts_and_folders_are_listed_and_other_files_do_not_open(
    qtbot: QtBot, fs: LocalFS, dialogs: FakeDialogs, tmp_path: Path
) -> None:
    (tmp_path / "synth" / "bass.log").write_bytes(b"state: running\n")
    win = MainWindow(fs, dialogs)  # type: ignore[arg-type]
    qtbot.addWidget(win)
    qtbot.waitUntil(lambda: "synth" in win.tree.paths())
    win.tree.refresh_folder("synth", expand=True)
    qtbot.waitUntil(lambda: "synth/bass.lua" in win.tree.paths())
    assert "synth/bass.log" not in win.tree.paths()
    win.open_file("synth/bass.log")
    assert win.editor_for("synth/bass.log") is None
    assert dialogs.errors == []
