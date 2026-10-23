"""Edit in the repo, push to a user slot of the device, pull back, remove, and the API page: the window on a repo and a
device mount (the device is a local folder here)."""

from __future__ import annotations

from pathlib import Path

import pytest
from PySide6.QtCore import QUrl
from PySide6.QtWidgets import QApplication
from pytestqt.qtbot import QtBot

from lua_editor.api.model import ApiRegistry
from lua_editor.remote import DEVICE, REPO, LocalFS, Mount, MountFS, RepoFS
from lua_editor.ui.mainwindow import MainWindow

APIS = Path(__file__).resolve().parent.parent / "apis"
SCRIPT = "repo/shipahoy/follow.lua"
LIBRARY = "repo/shipahoy/notes.lua"
SLOT = "device/shipahoy/user-3.lua"


class Dialogs:
    """Answers are queued: asking more than was queued fails the test, which is how "does not ask" is checked."""

    def __init__(self) -> None:
        self.confirms: list[bool] = []
        self.choices: list[str | None] = []
        self.names: list[str | None] = []
        self.asked: list[str] = []
        self.details: list[str] = []
        self.offered: list[list[str]] = []
        self.errors: list[str] = []

    def confirm(self, title: str, text: str, accept: str = "Yes", details: str = "") -> bool:
        self.asked.append(text)
        self.details.append(details)
        return self.confirms.pop(0)

    def choose(self, title: str, label: str, items: list[str]) -> str | None:
        self.offered.append(items)
        return self.choices.pop(0)

    def ask_name(self, title: str, label: str, text: str = "") -> str | None:
        return self.names.pop(0)

    def ask_unsaved(self, name: str) -> str:
        return "cancel"

    def ask_conflict(self, name: str) -> str:
        return "cancel"

    def error(self, title: str, text: str, hint: str = "", details: str = "") -> None:
        self.errors.append(f"{title}: {text} {hint}".rstrip())


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    scripts = tmp_path / "plugins" / "shipahoy" / "scripts"
    scripts.mkdir(parents=True)
    (scripts / "follow.lua").write_bytes(b"-- follow\n")
    (scripts / "notes.lua").write_bytes(b"Notes = {}\n")
    return tmp_path / "plugins"


@pytest.fixture
def device(tmp_path: Path) -> Path:
    folder = tmp_path / "device-scripts"
    folder.mkdir()
    return folder


@pytest.fixture
def dialogs() -> Dialogs:
    return Dialogs()


@pytest.fixture
def opened_urls() -> list[QUrl]:
    return []


@pytest.fixture
def window(qtbot: QtBot, repo: Path, device: Path, dialogs: Dialogs, tmp_path: Path, opened_urls: list[QUrl]) -> MainWindow:
    fs = MountFS([Mount(REPO, lambda: RepoFS(repo)), Mount(DEVICE, lambda: LocalFS(device))])
    win = MainWindow(
        fs,
        dialogs,  # type: ignore[arg-type]
        apis=ApiRegistry.from_folder(APIS),
        cache_dir=tmp_path / "cache",
        open_url=lambda url: opened_urls.append(url) or True,
        log_delay_ms=0,
    )
    qtbot.addWidget(win)
    qtbot.waitUntil(lambda: REPO in win.tree.paths())
    return win


def on_device(device: Path, name: str) -> Path:
    return device / "shipahoy" / name


def repo_script(repo: Path, name: str = "follow.lua") -> Path:
    return repo / "shipahoy" / "scripts" / name


def status(window: MainWindow) -> str:
    return window.statusBar().currentMessage()


def push_to_slot(qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, path: str = SCRIPT) -> None:
    dialogs.choices = ["user-3 (free)"]
    window.push(path)
    qtbot.waitUntil(lambda: on_device(device, "user-3.lua").exists())


def test_the_tree_shows_both_mounts(window: MainWindow) -> None:
    assert window.tree.paths() == [DEVICE, REPO]


def test_push_asks_for_a_slot_and_writes_that_user_script(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    assert on_device(device, "user-3.lua").read_bytes() == b"-- follow\n"
    assert not on_device(device, "follow.lua").exists()
    assert dialogs.offered[0][0] == "user-1 (free)" and len(dialogs.offered[0]) == 8
    qtbot.waitUntil(lambda: "1 written (user-3)" in status(window))
    assert "reloads within a second" in status(window)


def test_slots_in_use_are_marked_and_listed_last(qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path) -> None:
    on_device(device, "user-1.lua").parent.mkdir()
    on_device(device, "user-1.lua").write_bytes(b"-- somebody's\n")
    dialogs.choices = [None]
    window.push(SCRIPT)
    qtbot.waitUntil(lambda: bool(dialogs.offered))
    assert dialogs.offered[0][0] == "user-2 (free)" and dialogs.offered[0][-1] == "user-1 (in use)"


def test_cancelling_the_slot_writes_nothing(qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path) -> None:
    dialogs.choices = [None]
    window.push(SCRIPT)
    qtbot.waitUntil(lambda: bool(dialogs.offered))
    qtbot.wait(100)
    assert not (device / "shipahoy").exists()


def test_the_slot_is_remembered_and_a_later_push_does_not_ask(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, repo: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    repo_script(repo).write_bytes(b"-- follow v2\n")
    window.push(SCRIPT)  # a second question or a confirm would fail the test
    qtbot.waitUntil(lambda: on_device(device, "user-3.lua").read_bytes() == b"-- follow v2\n")
    assert dialogs.asked == []


def test_pushing_the_same_file_says_it_is_up_to_date(qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    window.push(SCRIPT)
    qtbot.waitUntil(lambda: "up to date" in status(window))


def test_naming_the_device_side_does_the_same_push(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, repo: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    repo_script(repo).write_bytes(b"-- follow v2\n")
    window.push(SLOT)
    qtbot.waitUntil(lambda: on_device(device, "user-3.lua").read_bytes() == b"-- follow v2\n")


def test_a_library_goes_under_its_own_name_without_asking(
    qtbot: QtBot, window: MainWindow, device: Path
) -> None:
    window.push(LIBRARY)
    qtbot.waitUntil(lambda: on_device(device, "notes.lua").exists())
    qtbot.waitUntil(lambda: "only noticed when a script that imports it reloads" in status(window))


def test_pushing_a_folder_sends_libraries_and_scripts_pushed_before(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, repo: Path
) -> None:
    window.push("repo/shipahoy")
    qtbot.waitUntil(lambda: on_device(device, "notes.lua").exists())
    assert not on_device(device, "user-3.lua").exists()
    push_to_slot(qtbot, window, dialogs, device)
    on_device(device, "notes.lua").unlink()
    on_device(device, "user-3.lua").unlink()
    window.push("repo/shipahoy")
    qtbot.waitUntil(lambda: on_device(device, "notes.lua").exists() and on_device(device, "user-3.lua").exists())


def test_the_examples_folder_is_shown_but_a_folder_push_does_not_take_it_along(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, repo: Path
) -> None:
    examples = repo_script(repo, "examples")
    examples.mkdir()
    (examples / "01-smallest.lua").write_bytes(b"function OnStep(step) SetCutoff(57) end\n")
    assert "repo/shipahoy/examples" in [entry.path for entry in window._fs.list("repo/shipahoy")]
    window.push("repo/shipahoy")
    qtbot.waitUntil(lambda: on_device(device, "notes.lua").exists())
    push_to_slot(qtbot, window, dialogs, device)  # pushes follow.lua to a slot
    assert sorted(p.name for p in (device / "shipahoy").iterdir()) == ["notes.lua", "user-3.lua"]
    # an example itself goes to a slot like any script
    dialogs.choices = ["user-4 (free)"]
    window.push("repo/shipahoy/examples/01-smallest.lua")
    qtbot.waitUntil(lambda: on_device(device, "user-4.lua").exists())


def test_pushing_a_folder_without_anything_to_send_says_so(qtbot: QtBot, window: MainWindow, repo: Path) -> None:
    repo_script(repo, "notes.lua").unlink()
    window.push("repo/shipahoy")
    qtbot.waitUntil(lambda: "nothing to do" in status(window))


def test_a_slot_somebody_else_wrote_is_only_replaced_after_a_yes(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path
) -> None:
    on_device(device, "user-3.lua").parent.mkdir()
    on_device(device, "user-3.lua").write_bytes(b"-- edited on the device\n")
    dialogs.choices = ["user-3 (in use)"]
    dialogs.confirms = [False, True]
    window.push(SCRIPT)
    qtbot.waitUntil(lambda: len(dialogs.asked) == 1)
    qtbot.wait(100)
    assert on_device(device, "user-3.lua").read_bytes() == b"-- edited on the device\n"
    window.push(SCRIPT)
    qtbot.waitUntil(lambda: on_device(device, "user-3.lua").read_bytes() == b"-- follow\n")


def test_pull_brings_the_slot_back_into_its_repo_script(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, repo: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    on_device(device, "user-3.lua").write_bytes(b"-- tuned on the device\n")
    dialogs.confirms = [True]
    window.pull(SLOT)
    qtbot.waitUntil(lambda: repo_script(repo).read_bytes() == b"-- tuned on the device\n")


def test_pull_of_an_unknown_slot_asks_for_the_repo_name(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, repo: Path
) -> None:
    on_device(device, "user-5.lua").parent.mkdir()
    on_device(device, "user-5.lua").write_bytes(b"-- found\n")
    dialogs.names = ["found"]
    window.pull("device/shipahoy/user-5.lua")
    qtbot.waitUntil(lambda: repo_script(repo, "found.lua").exists())
    assert repo_script(repo, "found.lua").read_bytes() == b"-- found\n"


def test_pull_reloads_an_open_unmodified_tab(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    window.open_file(SCRIPT)
    qtbot.waitUntil(lambda: window.editor_for(SCRIPT) is not None)
    on_device(device, "user-3.lua").write_bytes(b"-- tuned\n")
    dialogs.confirms = [True]
    window.pull(SCRIPT)
    qtbot.waitUntil(lambda: window.editor_for(SCRIPT).toPlainText() == "-- tuned\n")


def test_remove_deletes_the_slot_and_its_log_and_forgets_it(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    on_device(device, "user-3.log").write_bytes(b"log")
    dialogs.confirms = [True]
    window.remove_from_device(SCRIPT)  # the repo script names its slot
    qtbot.waitUntil(lambda: not on_device(device, "user-3.lua").exists())
    assert not on_device(device, "user-3.log").exists()
    dialogs.choices = [None]
    window.push(SCRIPT)  # asks again
    qtbot.waitUntil(lambda: len(dialogs.offered) == 2)


def test_remove_declined_keeps_the_file(qtbot: QtBot, window: MainWindow, device: Path, dialogs: Dialogs) -> None:
    on_device(device, "user-3.lua").parent.mkdir()
    on_device(device, "user-3.lua").write_bytes(b"x")
    dialogs.confirms = [False]
    window.remove_from_device(SLOT)
    qtbot.waitUntil(lambda: len(dialogs.asked) == 1)
    qtbot.wait(100)
    assert on_device(device, "user-3.lua").exists()


def test_push_on_save_pushes_after_a_save(qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path) -> None:
    window.open_file(SCRIPT)
    qtbot.waitUntil(lambda: window.editor_for(SCRIPT) is not None)
    editor = window.editor_for(SCRIPT)
    window._push_on_save.setChecked(True)
    dialogs.choices = ["user-3 (free)"]
    editor.selectAll()
    editor.insertPlainText("-- follow edited\n")
    window.save_current()
    qtbot.waitUntil(lambda: on_device(device, "user-3.lua").exists())
    qtbot.waitUntil(lambda: on_device(device, "user-3.lua").read_bytes() == b"-- follow edited\n")
    editor.selectAll()
    editor.insertPlainText("-- second\n")
    window.save_current()  # the slot is known now: no question
    qtbot.waitUntil(lambda: on_device(device, "user-3.lua").read_bytes() == b"-- second\n")


def test_without_the_toggle_a_save_stays_in_the_repo(
    qtbot: QtBot, window: MainWindow, device: Path, repo: Path
) -> None:
    window.open_file(SCRIPT)
    qtbot.waitUntil(lambda: window.editor_for(SCRIPT) is not None)
    editor = window.editor_for(SCRIPT)
    editor.selectAll()
    editor.insertPlainText("-- edited\n")
    window.save_current()
    qtbot.waitUntil(lambda: repo_script(repo).read_bytes() == b"-- edited\n")
    qtbot.wait(100)
    assert not (device / "shipahoy").exists()


def test_a_repo_script_gets_the_plugin_api(qtbot: QtBot, window: MainWindow) -> None:
    window.open_file(SCRIPT)
    qtbot.waitUntil(lambda: window.editor_for(SCRIPT) is not None)
    assert "SetCutoff" in window.editor_for(SCRIPT).vocabulary.commands


def test_the_log_of_a_repo_script_is_not_read_while_the_device_is_not_connected(qtbot: QtBot, window: MainWindow) -> None:
    window.open_file(SCRIPT)
    qtbot.waitUntil(lambda: window.editor_for(SCRIPT) is not None)
    window.refresh_log()
    assert not window._fs.is_open(DEVICE)


def test_the_log_of_a_pushed_repo_script_is_the_log_of_its_slot(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    on_device(device, "user-3.log").write_bytes(b"state: ok\n")
    window.open_file(SCRIPT)
    qtbot.waitUntil(lambda: window.editor_for(SCRIPT) is not None)
    window.refresh_log()
    qtbot.waitUntil(lambda: "state: ok" in window.log_panel.toPlainText())
    assert window.log_panel.toPlainText().startswith("user-3.log")


def test_api_reference_opens_the_page_of_the_plugin_at_the_word_under_the_cursor(
    qtbot: QtBot, window: MainWindow, opened_urls: list[QUrl], tmp_path: Path
) -> None:
    window.open_file(SCRIPT)
    qtbot.waitUntil(lambda: window.editor_for(SCRIPT) is not None)
    editor = window.editor_for(SCRIPT)
    editor.selectAll()
    editor.insertPlainText("SetCutoff(60)\n")
    cursor = editor.textCursor()
    cursor.setPosition(3)
    editor.setTextCursor(cursor)
    window.show_api_reference()
    page = tmp_path / "cache" / "api-shipahoy.html"
    assert page.is_file() and "cmd-SetCutoff" in page.read_text(encoding="utf-8")
    assert opened_urls[-1].toLocalFile() == str(page)
    assert opened_urls[-1].fragment() == "cmd-SetCutoff"
    assert 'getElementById("cmd-SetCutoff")' in page.read_text(encoding="utf-8")


def test_api_reference_without_a_tab_asks_which_plugin(
    window: MainWindow, opened_urls: list[QUrl], dialogs: Dialogs, tmp_path: Path
) -> None:
    dialogs.choices = ["midiscript", None]
    window.show_api_reference()
    assert (tmp_path / "cache" / "api-midiscript.html").is_file()
    assert opened_urls[-1].fragment() == ""
    opened_urls.clear()
    window.show_api_reference()
    assert opened_urls == []


def test_the_help_menu_copies_the_brief_for_an_llm(window: MainWindow, dialogs: Dialogs) -> None:
    help_menu = next(a.menu() for a in window.menuBar().actions() if a.text() == "&Help")
    assert "&Copy API brief for an LLM" in [a.text() for a in help_menu.actions()]
    dialogs.choices = ["shipahoy", None]
    window.copy_api_brief()
    text = QApplication.clipboard().text()
    assert text.startswith("# Ship Ahoy scripting brief") and "# Part 3: examples" in text
    QApplication.clipboard().clear()
    window.copy_api_brief()  # the choice was cancelled
    assert QApplication.clipboard().text() == ""


def test_push_needs_both_mounts(qtbot: QtBot, tmp_path: Path, dialogs: Dialogs) -> None:
    (tmp_path / "shipahoy").mkdir()
    (tmp_path / "shipahoy" / "a.lua").write_bytes(b"")
    win = MainWindow(LocalFS(tmp_path), dialogs)  # type: ignore[arg-type]
    qtbot.addWidget(win)
    win.push("shipahoy/a.lua")
    assert "needs the repo and the device" in status(win)


# what the user is told

def activity(window: MainWindow) -> str:
    return window.activity_panel.toPlainText()


def test_pulling_a_folder_whose_libraries_are_not_on_the_device_is_not_an_error(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, repo: Path
) -> None:
    """The first version aborted with an alert that said only 'shipahoy/notes.lua'."""
    (device / "shipahoy").mkdir()
    window.pull("repo/shipahoy")
    qtbot.waitUntil(lambda: "not on the device" in activity(window))
    assert dialogs.errors == []
    assert "Pull to the repo: 1 not on the device - never pushed or removed (notes)" in activity(window)
    assert repo_script(repo, "notes.lua").read_bytes() == b"Notes = {}\n"


def test_pulling_what_exists_and_skipping_what_does_not(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path, repo: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    on_device(device, "user-3.lua").write_bytes(b"-- tuned\n")
    dialogs.confirms = [True]
    window.pull("repo/shipahoy")  # the library is missing on the device, the pushed script is there
    qtbot.waitUntil(lambda: repo_script(repo).read_bytes() == b"-- tuned\n")
    qtbot.waitUntil(lambda: "1 written (user-3)" in activity(window) and "1 not on the device" in activity(window))
    assert dialogs.errors == []


def test_pulling_one_missing_library_is_reported_not_raised(qtbot: QtBot, window: MainWindow, dialogs: Dialogs) -> None:
    window.pull(LIBRARY)
    qtbot.waitUntil(lambda: "not on the device" in status(window))
    assert dialogs.errors == []


def test_the_activity_list_keeps_what_the_status_line_forgets(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path
) -> None:
    push_to_slot(qtbot, window, dialogs, device)
    window.push(SCRIPT)
    qtbot.waitUntil(lambda: "already up to date" in activity(window))
    lines = [line for line in activity(window).splitlines() if line]
    assert any("1 written (user-3)" in line for line in lines)
    assert all(line[2] == ":" and line[:8].replace(":", "").isdigit() for line in lines)  # HH:MM:SS first


def test_a_warning_brings_the_activity_tab_to_the_front(qtbot: QtBot, window: MainWindow) -> None:
    window.show()
    window.pull(LIBRARY)
    qtbot.waitUntil(lambda: "not on the device" in status(window))
    assert window._activity_dock.isVisible()


def test_an_error_dialog_explains_and_the_activity_list_records_it(
    qtbot: QtBot, tmp_path: Path, dialogs: Dialogs
) -> None:
    def unreachable() -> LocalFS:
        from lua_editor.remote import RemoteError

        raise RemoteError("cannot connect to 192.0.2.1: timed out")

    fs = MountFS([Mount(REPO, lambda: RepoFS(tmp_path)), Mount(DEVICE, unreachable)])
    win = MainWindow(fs, dialogs)  # type: ignore[arg-type]
    qtbot.addWidget(win)
    qtbot.waitUntil(lambda: DEVICE in win.tree.paths())
    dialogs.names = ["found"]
    win.pull("device/shipahoy/user-1.lua")
    qtbot.waitUntil(lambda: bool(dialogs.errors))
    assert "cannot be reached" in dialogs.errors[0] and "--no-device" in dialogs.errors[0]
    assert "cannot be reached" in activity(win)


def test_overwrite_question_lists_the_files_with_sizes(
    qtbot: QtBot, window: MainWindow, dialogs: Dialogs, device: Path
) -> None:
    on_device(device, "user-3.lua").parent.mkdir()
    on_device(device, "user-3.lua").write_bytes(b"-- edited on the device\n")
    dialogs.choices = ["user-3 (in use)"]
    dialogs.confirms = [False]
    window.push(SCRIPT)
    qtbot.waitUntil(lambda: bool(dialogs.details))
    assert "device/shipahoy/user-3.lua: 24 bytes now, replaced by 10 bytes from repo/shipahoy/follow.lua" in dialogs.details[0]
    qtbot.waitUntil(lambda: "cancelled" in activity(window))
    assert on_device(device, "user-3.lua").read_bytes() == b"-- edited on the device\n"
