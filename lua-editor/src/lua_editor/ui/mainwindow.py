"""The main window: file tree on the left, one tab per open script on the right."""

from __future__ import annotations

import posixpath
from collections.abc import Callable
from pathlib import Path

from PySide6.QtCore import QStandardPaths, Qt, QTimer, QUrl
from PySide6.QtGui import QAction, QCloseEvent, QDesktopServices, QKeySequence, QTextCursor
from PySide6.QtWidgets import QApplication, QDockWidget, QLabel, QMainWindow, QSplitter, QTabWidget

from lua_editor.api.html import anchor_for, render_api_html
from lua_editor.api.lua_modules import scan_modules
from lua_editor.api.markdown import render_api_markdown
from lua_editor.api.model import ApiRegistry, Library
from lua_editor.naming import SCRIPT_SUFFIX, USER_SLOTS, InvalidName, plain_name, script_filename
from lua_editor.remote import DEVICE, REPO, AlreadyExists, Conflict, Entry, MountFS, NotFound, RemoteFS, swap_mount
from lua_editor.remote.transfer import Item, State, plan_copy, remove_scripts, run_copy, script_files
from lua_editor.textcodec import NotText, decode_text
from lua_editor.ui.activity import ActivityPanel
from lua_editor.ui.dialogs import Dialogs
from lua_editor.ui.documentedit import DocumentEdit
from lua_editor.ui.explain import explain, side_of
from lua_editor.ui.filetree import FileTree
from lua_editor.ui.logpanel import LogPanel
from lua_editor.ui.problems import ProblemsPanel
from lua_editor.ui.worker import RemoteWorker

MESSAGE_MS = 5000
MAX_LIBRARY_BYTES = 256 * 1024  # the size limit of a script of the plugins
LOG_SUFFIX = ".log"
LOG_DELAY_MS = 1500  # the plugin polls the file's mtime twice a second, then loads and writes the log


class MainWindow(QMainWindow):
    def __init__(
        self,
        fs: RemoteFS,
        dialogs: Dialogs | None = None,
        apis: ApiRegistry | None = None,
        log_delay_ms: int = LOG_DELAY_MS,
        cache_dir: Path | None = None,
        open_url: Callable[[QUrl], bool] | None = None,
    ) -> None:
        super().__init__()
        self._log_delay_ms = log_delay_ms
        self._cache_dir = cache_dir
        self._open_url = open_url or QDesktopServices.openUrl
        self._device_stamps: dict[str, tuple[int, int]] = {}  # device files this editor wrote, by path
        self._slots: dict[str, str] = {}  # repo script -> the user slot file on the device it was pushed to
        self._log_serial = 0
        self._fs = fs
        self._apis = apis or ApiRegistry()
        self._worker = RemoteWorker(self)
        self._dialogs = dialogs or Dialogs(self)
        self._editors: dict[str, DocumentEdit] = {}
        self._opening: set[str] = set()
        self._saving: set[str] = set()

        self._tree = FileTree(fs, self._worker)
        self._tabs = QTabWidget()
        self._tabs.setTabsClosable(True)
        self._tabs.setMovable(True)
        self._tabs.setDocumentMode(True)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(self._tree)
        splitter.addWidget(self._tabs)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([260, 840])
        self.setCentralWidget(splitter)
        self._problems = ProblemsPanel()
        self._problems_dock = QDockWidget("Problems", self)
        self._problems_dock.setWidget(self._problems)
        self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, self._problems_dock)
        self._log = LogPanel()
        self._log_dock = QDockWidget("Log", self)
        self._log_dock.setWidget(self._log)
        self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, self._log_dock)
        self._activity = ActivityPanel()
        self._activity_dock = QDockWidget("Activity", self)
        self._activity_dock.setWidget(self._activity)
        self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, self._activity_dock)
        self.tabifyDockWidget(self._problems_dock, self._log_dock)
        self.tabifyDockWidget(self._log_dock, self._activity_dock)
        self._problems_dock.raise_()
        self._position = QLabel()
        self._hint = QLabel()
        self._hint.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.statusBar().addWidget(self._hint, 1)
        self.statusBar().addPermanentWidget(self._position)

        self._build_menu()
        self._tree.openRequested.connect(self.open_file)
        self._tree.newScriptRequested.connect(self.new_script)
        self._tree.newFolderRequested.connect(self.new_folder)
        self._tree.renameRequested.connect(self.rename)
        self._tree.deleteRequested.connect(self.delete)
        self._tree.pushRequested.connect(self.push)
        self._tree.pullRequested.connect(self.pull)
        self._tree.removeFromDeviceRequested.connect(self.remove_from_device)
        self._tree.failed.connect(self._message)
        self._tabs.tabCloseRequested.connect(lambda index: self.close_editor(self._tabs.widget(index)))
        self._tabs.currentChanged.connect(lambda _index: self._current_changed())
        self._problems.problemActivated.connect(self._show_problem)
        self._tree.load_root()

    # access for tests and for later steps

    @property
    def tree(self) -> FileTree:
        return self._tree

    @property
    def tabs(self) -> QTabWidget:
        return self._tabs

    def editor_for(self, path: str) -> DocumentEdit | None:
        return self._editors.get(path)

    def current_editor(self) -> DocumentEdit | None:
        widget = self._tabs.currentWidget()
        return widget if isinstance(widget, DocumentEdit) else None

    # menu

    def _build_menu(self) -> None:
        menu = self.menuBar().addMenu("&File")
        self._add_action(menu, "&New script...", "Ctrl+N", lambda: self.new_script(self._tree.selected_folder()))
        self._add_action(menu, "New &folder...", "Ctrl+Shift+N", lambda: self.new_folder(self._tree.selected_folder()))
        menu.addSeparator()
        self._add_action(menu, "&Save", "Ctrl+S", self.save_current)
        self._add_action(menu, "&Reload file", "Ctrl+R", self.reload_current)
        self._add_action(menu, "Refresh &log", "Ctrl+L", self.refresh_log)
        self._add_action(menu, "&Close tab", "Ctrl+W", self._close_current)
        menu.addSeparator()
        self._add_action(menu, "&Quit", "Ctrl+Q", self.close)
        device = self.menuBar().addMenu("&Device")
        self._add_action(device, "&Push to device", "Ctrl+U", lambda: self._with_target(self.push))
        self._add_action(device, "P&ull from device", "Ctrl+Shift+U", lambda: self._with_target(self.pull))
        self._add_action(device, "&Remove from device...", "", lambda: self._with_target(self.remove_from_device))
        device.addSeparator()
        self._push_on_save = QAction("Push on &save", self)
        self._push_on_save.setCheckable(True)
        device.addAction(self._push_on_save)
        help_menu = self.menuBar().addMenu("&Help")
        self._add_action(help_menu, "&API reference", "F1", self.show_api_reference)
        self._add_action(help_menu, "&Copy API brief for an LLM", "", self.copy_api_brief)

    def _add_action(self, menu, text: str, shortcut: str, handler) -> QAction:
        # built here, not taken from menu.addAction(text): that returned a stale wrapper of a freed QStandardItem
        action = QAction(text, self)
        menu.addAction(action)
        if shortcut:
            action.setShortcut(QKeySequence(shortcut))
        action.triggered.connect(lambda _checked=False: handler())
        return action

    # opening

    def open_file(self, path: str) -> None:
        if not path.endswith(SCRIPT_SUFFIX):
            self._message(f"{path} is not a script")
            return
        editor = self._editors.get(path)
        if editor is not None:
            self._tabs.setCurrentWidget(editor)
            return
        if path in self._opening:
            return
        self._opening.add(path)
        self._worker.submit(
            lambda: self._read(path),
            lambda result: self._opened(path, *result),
            lambda error: self._failed(f"Open {path}", error, opening=path),
        )

    def _read(self, path: str) -> tuple[Entry, bytes]:
        # stat first: a change between the two calls then shows up as a conflict, never goes unnoticed
        return self._fs.stat(path), self._fs.read(path)

    def _opened(self, path: str, entry: Entry, data: bytes) -> None:
        self._opening.discard(path)
        try:
            text, eol = decode_text(data)
        except NotText:
            self._dialogs.error("Open", f"{path} is not a UTF-8 text file.")
            return
        editor = DocumentEdit(path, text, eol, entry.stamp)
        editor.set_api(self._apis.for_path(path))
        editor.hintChanged.connect(self._hint.setText)
        editor.titleChanged.connect(lambda: self._retitle(editor))
        editor.problemsChanged.connect(lambda: self._problems_changed(editor))
        editor.cursorPositionChanged.connect(self._update_position)
        self._editors[path] = editor
        self._tabs.addTab(editor, editor.name)
        self._retitle(editor)
        self._tabs.setCurrentWidget(editor)
        editor.setFocus()
        self._scan_libraries(editor)

    # libraries in the folder of a script

    def _scan_libraries(self, editor: DocumentEdit) -> None:
        """Look at the other scripts of the folder for global tables, which scripts can import."""
        folder, own, fs = posixpath.dirname(editor.path), editor.path, self._fs

        def scan() -> tuple[list[Library], set[str]]:
            found: list[Library] = []
            names: set[str] = set()
            for entry in fs.list(folder):
                if entry.is_dir or not entry.name.endswith(SCRIPT_SUFFIX):
                    continue
                names.add(entry.name.removesuffix(SCRIPT_SUFFIX))
                if entry.path == own or entry.size > MAX_LIBRARY_BYTES:
                    continue
                text = fs.read(entry.path).decode("utf-8", errors="replace")
                found.extend(module.to_library() for module in scan_modules(entry.name, text))
            return found, names

        self._worker.submit(
            scan,
            lambda result: self._libraries_scanned(editor, own, *result),
            lambda error: self._message(f"Libraries of {folder or '/'}: {error}"),
        )

    def _libraries_scanned(self, editor: DocumentEdit, path: str, libraries: list[Library], names: set[str]) -> None:
        if self._editors.get(path) is editor:
            editor.set_libraries(libraries)
            editor.set_script_names(names)

    def _rescan(self, *folders: str) -> None:
        """The scripts of these folders changed (created, renamed, deleted, saved): look again for the open ones."""
        for editor in list(self._editors.values()):
            if posixpath.dirname(editor.path) in folders:
                self._scan_libraries(editor)

    # reloading

    def reload_current(self) -> None:
        editor = self.current_editor()
        if editor is None:
            return
        if editor.is_dirty() and not self._dialogs.confirm("Reload", f"Discard the changes to {editor.name}?"):
            return
        self._reload(editor)

    def _reload(self, editor: DocumentEdit) -> None:
        path = editor.path
        self._worker.submit(
            lambda: self._read(path),
            lambda result: self._reloaded(editor, path, *result),
            lambda error: self._failed(f"Reload {path}", error),
        )

    def _reloaded(self, editor: DocumentEdit, path: str, entry: Entry, data: bytes) -> None:
        if self._editors.get(path) is not editor:
            return
        try:
            text, eol = decode_text(data)
        except NotText:
            self._dialogs.error("Reload", f"{path} is not a UTF-8 text file.")
            return
        editor.load(text, eol, entry.stamp)
        self._message(f"Reloaded {path}")

    # the plugin's log

    def refresh_log(self) -> None:
        """Fetch `<name>.log` of the script in the current tab and show it in the Log panel."""
        editor = self.current_editor()
        if editor is None or not editor.path.endswith(SCRIPT_SUFFIX):
            self._log.clear()
            return
        script = editor.path
        if isinstance(self._fs, MountFS):
            # the plugin writes the log next to the script on the device; a repo script has its twin there once pushed
            if swap_mount(script, REPO, REPO) is not None:
                twin = self._slots.get(script)
                if twin is None or not self._fs.is_open(DEVICE):  # never connect just for a log
                    self._log.clear()
                    return
                script = twin
        log_path = script.removesuffix(SCRIPT_SUFFIX) + LOG_SUFFIX
        self._log_serial += 1
        serial = self._log_serial

        def read() -> str | None:
            try:
                return self._fs.read(log_path).decode("utf-8", errors="replace")
            except NotFound:
                return None

        def show(text: str | None) -> None:
            if serial == self._log_serial:  # a newer request supersedes this one
                self._log.show_log(posixpath.basename(log_path), text)

        self._worker.submit(read, show, lambda error: self._message(f"Log of {script}: {error}"))

    @property
    def log_panel(self) -> LogPanel:
        return self._log

    # saving

    def save_current(self) -> None:
        editor = self.current_editor()
        if editor is not None:
            self.save(editor)

    def save(self, editor: DocumentEdit, overwrite: bool = False) -> None:
        path = editor.path
        if path in self._saving:
            return
        self._saving.add(path)
        data, revision = editor.payload()
        expected = None if overwrite else editor.stamp
        self._progress(f"Saving {path}...")
        self._worker.submit(
            lambda: self._fs.write(path, data, expected),
            lambda entry: self._saved(editor, path, revision, entry),
            lambda error: self._save_failed(editor, path, error),
        )

    def _saved(self, editor: DocumentEdit, path: str, revision: int, entry: Entry) -> None:
        self._saving.discard(path)
        if self._editors.get(path) is editor:
            editor.mark_saved(entry.stamp, revision)
        self._message(f"Saved {path}")
        self._rescan(posixpath.dirname(path))
        QTimer.singleShot(self._log_delay_ms, self.refresh_log)
        if self._push_on_save.isChecked() and swap_mount(path, REPO, REPO) is not None:
            self.push(path)

    def _save_failed(self, editor: DocumentEdit, path: str, error: Exception) -> None:
        self._saving.discard(path)
        if self._editors.get(path) is not editor:
            return
        if not isinstance(error, Conflict):
            self._failed(f"Save {path}", error)
            return
        choice = self._dialogs.ask_conflict(editor.name)
        if choice == "overwrite":
            self.save(editor, overwrite=True)
        elif choice == "reload":
            self._reload(editor)

    def _save_and_wait(self, editor: DocumentEdit) -> bool:
        """Write before closing; blocks the window for the length of one write."""
        path = editor.path
        data, revision = editor.payload()
        expected: tuple[int, int] | None = editor.stamp
        while True:
            try:
                entry = self._worker.call(lambda: self._fs.write(path, data, expected))
            except Conflict:
                choice = self._dialogs.ask_conflict(editor.name)
                if choice == "overwrite":
                    expected = None
                    continue
                if choice == "reload":
                    self._reload(editor)
                return False
            except Exception as error:
                self._dialogs.error(f"Save {path}", str(error))
                return False
            editor.mark_saved(entry.stamp, revision)
            return True

    # between the repo and the device

    def _with_target(self, handler: Callable[[str, bool], None]) -> None:
        """Run a push, pull or remove on the file of the current tab, else on the selection of the tree."""
        editor = self.current_editor()
        target = (editor.path, False) if editor is not None else self._tree.selected()
        if target is None:
            self._message("Select a script or a folder first")
            return
        handler(*target)

    def _has_mounts(self, verb: str) -> bool:
        if isinstance(self._fs, MountFS) and self._fs.mount(REPO) and self._fs.mount(DEVICE):
            return True
        self._message(f"{verb} needs the repo and the device (start without --root)")
        return False

    def push(self, path: str, is_dir: bool = False) -> None:
        """Copy a repo script to the device. A script goes to a user slot (user-1 to user-8, asked once and remembered),
        a library to a file of its own name. A folder: its libraries and the scripts that were pushed before."""
        self._transfer("Push", path)

    def pull(self, path: str, is_dir: bool = False) -> None:
        """Copy a script back from the device into the repo; the same pairs as push, either side can be named."""
        self._transfer("Pull", path)

    def _transfer(self, verb: str, path: str) -> None:
        if not self._has_mounts(verb):
            return
        repo = self._repo_side(verb, path)
        if repo is None:
            return
        dirty = [e.name for p, e in self._editors.items() if self._is_under(p, repo) and e.is_dirty()]
        if verb == "Push" and dirty:
            if not self._dialogs.confirm(verb, f"{', '.join(dirty)} has unsaved changes. Push the saved version?"):
                return
        fs, slots = self._fs, dict(self._slots)
        self._progress(f"{verb}: looking at {repo}...")
        self._worker.submit(
            lambda: self._facts(fs, repo, slots),
            lambda facts: self._facts_ready(verb, repo, facts),
            lambda error: self._failed(verb, error),
        )

    def _repo_side(self, verb: str, path: str) -> str | None:
        """The repo path a push or pull is about, whichever side was named; asks for a name for a slot nobody knows."""
        if swap_mount(path, REPO, REPO) is not None:
            return path
        device = swap_mount(path, DEVICE, DEVICE)
        if device is None:
            self._message(f"{verb}: {path} is not below {REPO}/ or {DEVICE}/")
            return None
        owner = next((repo for repo, twin in self._slots.items() if twin == device), None)
        if owner is not None:
            return owner
        stem = posixpath.basename(device).removesuffix(SCRIPT_SUFFIX)
        if stem not in USER_SLOTS:
            return swap_mount(device, DEVICE, REPO)
        answer = self._dialogs.ask_name(verb, f"{stem} is not from a repo script this session. Save it in the repo as:")
        if answer is None:
            return None
        try:
            name = script_filename(answer)
        except InvalidName as error:
            self._dialogs.error(verb, str(error))
            return None
        repo = posixpath.join(posixpath.dirname(swap_mount(device, DEVICE, REPO) or device), name)
        self._slots[repo] = device
        return repo

    @staticmethod
    def _facts(fs: RemoteFS, repo: str, slots: dict[str, str]) -> dict:
        """What the worker finds out about a repo path: for a folder the pairs to copy, for a file whether it is a
        library and which slots of the plugin the device has in use."""
        plugin = repo.split("/")[1] if repo.count("/") >= 1 else ""
        try:
            entry: Entry | None = fs.stat(repo)
        except NotFound:
            entry = None
        if entry is not None and entry.is_dir:
            pairs = []
            for file in script_files(fs, repo):
                text = fs.read(file).decode("utf-8", errors="replace")
                if scan_modules(posixpath.basename(file), text):
                    pairs.append((file, swap_mount(file, REPO, DEVICE)))
                elif file in slots:
                    pairs.append((file, slots[file]))
            return {"pairs": pairs}
        library = False
        if entry is not None:
            library = bool(scan_modules(posixpath.basename(repo), fs.read(repo).decode("utf-8", errors="replace")))
        try:
            used = {e.name.removesuffix(SCRIPT_SUFFIX) for e in fs.list(posixpath.join(DEVICE, plugin))}
        except NotFound:
            used = set()
        return {"library": library, "used": used}

    def _facts_ready(self, verb: str, repo: str, facts: dict) -> None:
        if "pairs" in facts:
            pairs = facts["pairs"]
            if not pairs:
                self._warn(f"{verb}: nothing to do in {repo} (push a script once to choose its slot)")
                return
        else:
            device = self._device_file(verb, repo, facts)
            if device is None:
                return
            pairs = [(repo, device)]
        if verb == "Pull":
            pairs = [(twin, source) for source, twin in pairs]
        self._plan_copy(verb, pairs, DEVICE if verb == "Push" else REPO)

    def _device_file(self, verb: str, repo: str, facts: dict) -> str | None:
        if facts["library"]:
            return swap_mount(repo, REPO, DEVICE)
        known = self._slots.get(repo)
        if known is not None:
            return known
        used = facts["used"]
        slots = sorted(USER_SLOTS, key=lambda slot: slot in used)  # free ones first
        items = [f"{slot} ({'in use' if slot in used else 'free'})" for slot in slots]
        name = posixpath.basename(repo)
        label = f"Slot of {name} on the device (the Script setting selects user-1 to user-8):"
        chosen = self._dialogs.choose(verb, label, items)
        if chosen is None:
            return None
        device = posixpath.join(DEVICE, repo.split("/")[1], chosen.split(" ")[0] + SCRIPT_SUFFIX)
        self._slots[repo] = device
        return device

    def _plan_copy(self, verb: str, pairs: list[tuple[str, str]], target: str) -> None:
        fs, known = self._fs, dict(self._device_stamps)
        origin = REPO if target == DEVICE else DEVICE
        self._progress(f"{verb}: comparing {len(pairs)} file{'s' if len(pairs) != 1 else ''}...")
        self._worker.submit(
            lambda: plan_copy(fs, pairs, known),
            lambda items: self._planned(verb, origin, target, items),
            lambda error: self._failed(verb, error),
        )

    def _planned(self, verb: str, origin: str, target: str, items: list[Item]) -> None:
        changed = [item for item in items if item.state is State.CHANGED]
        if changed:
            lines = [
                f"{item.target}: {item.target_size} bytes now, replaced by {len(item.data)} bytes from {item.source}"
                for item in changed
            ]
            names = ", ".join(posixpath.basename(item.target) for item in changed)
            question = f"{names} at the {target} differs and was not written by this editor. Replace it?"
            if not self._dialogs.confirm(verb, question, accept="Overwrite", details="\n".join(lines)):
                self._message(f"{verb}: cancelled, {names} at the {target} was left as it is")
                return
        if not any(item.state not in (State.SAME, State.MISSING) for item in items):
            self._report(verb, origin, target, items, {})
            return
        fs = self._fs
        self._worker.submit(
            lambda: run_copy(fs, items),
            lambda written: self._copied(verb, origin, target, items, written),
            lambda error: self._failed(verb, error),
        )

    def _report(self, verb: str, origin: str, target: str, items: list[Item], written: dict[str, Entry]) -> None:
        """One line for the Activity list: what was written, what was already there, what the source did not have."""
        def names(state_items: list[Item]) -> str:
            return ", ".join(posixpath.basename(item.target).removesuffix(SCRIPT_SUFFIX) for item in state_items)

        done = [item for item in items if item.target in written]
        same = [item for item in items if item.state is State.SAME]
        missing = [item for item in items if item.state is State.MISSING]
        parts = []
        if done:
            parts.append(f"{len(done)} written ({names(done)})")
        if same:
            parts.append(f"{len(same)} already up to date ({names(same)})")
        if missing:
            where = "never pushed or removed" if origin == DEVICE else "gone"
            parts.append(f"{len(missing)} not {side_of(origin)} - {where} ({names(missing)})")
        text = f"{verb} to the {target}: " + ", ".join(parts)
        if target == DEVICE and done:
            text += ". A user script reloads within a second while the Script setting selects it"
            if any(posixpath.basename(item.target).removesuffix(SCRIPT_SUFFIX) not in USER_SLOTS for item in done):
                text += "; a library is only noticed when a script that imports it reloads"
        (self._warn if missing else self._message)(text)

    def _copied(self, verb: str, origin: str, target: str, items: list[Item], written: dict[str, Entry]) -> None:
        if target == DEVICE:
            self._device_stamps.update({path: entry.stamp for path, entry in written.items()})
        unsaved = []
        for path in written:
            folder = posixpath.dirname(path)
            self._tree.refresh_folder(posixpath.dirname(folder))
            self._tree.refresh_folder(folder)
            editor = self._editors.get(path)
            if editor is None:
                continue
            if editor.is_dirty():
                unsaved.append(editor.name)
            else:
                self._reload(editor)
        self._rescan(*{posixpath.dirname(path) for path in written})
        self._report(verb, origin, target, items, written)
        if unsaved:
            self._warn(f"{', '.join(unsaved)} has unsaved changes in its tab; Ctrl+R loads the file from disk")

    def remove_from_device(self, path: str, is_dir: bool = False) -> None:
        """Delete a script (or the scripts of a folder) and its log from the device; naming the repo side works too."""
        if not self._has_mounts("Remove"):
            return
        device_path = swap_mount(path, DEVICE, DEVICE) or self._slots.get(path) or swap_mount(path, REPO, DEVICE)
        if device_path is None:
            self._message(f"Remove: {path} is not below {REPO}/ or {DEVICE}/")
            return
        fs = self._fs
        self._worker.submit(
            lambda: script_files(fs, device_path),
            lambda files: self._remove_confirmed(files),
            lambda error: self._failed("Remove from device", error),
        )

    def _remove_confirmed(self, files: list[str]) -> None:
        if not files:
            self._message("Remove: no scripts there")
            return
        affected = [editor for path, editor in self._editors.items() if path in files]
        text = f"Remove {len(files)} script{'s' if len(files) != 1 else ''} and their logs from the device?"
        if any(editor.is_dirty() for editor in affected):
            text += " Unsaved changes in open tabs are lost."
        if not self._dialogs.confirm("Remove from device", text, accept="Remove", details="\n".join(files)):
            return
        fs = self._fs

        def removed(paths: list[str]) -> None:
            for editor in affected:
                self._remove_editor(editor)
            for path in files:
                self._device_stamps.pop(path, None)
            self._slots = {repo: twin for repo, twin in self._slots.items() if twin not in files}
            for folder in {posixpath.dirname(path) for path in files}:
                self._tree.refresh_folder(folder)
            self._message(f"Removed from the device: {', '.join(paths)}")

        self._worker.submit(lambda: remove_scripts(fs, files), removed, lambda error: self._failed("Remove", error))

    # the API as a page for the browser

    def _api_to_show(self, title: str):
        """The API of the plugin of the current tab, or one chosen; None if there is none or the choice was cancelled."""
        editor = self.current_editor()
        api = self._apis.for_path(editor.path) if editor is not None else None
        if api is None:
            plugins = self._apis.plugins()
            if not plugins:
                self._message("No API files loaded (see --apis)")
                return None
            chosen = plugins[0] if len(plugins) == 1 else self._dialogs.choose(title, "Plugin:", plugins)
            if chosen is None:
                return None
            api = self._apis.for_path(chosen)
        return api

    def copy_api_brief(self) -> None:
        """Put the brief for an LLM (guide, API, examples) of the plugin of the current tab on the clipboard."""
        api = self._api_to_show("API brief for an LLM")
        if api is None:
            return
        QApplication.clipboard().setText(render_api_markdown(api))
        self._message(f"Copied the {api.title} brief for an LLM ({len(api.examples)} examples)")

    def show_api_reference(self) -> None:
        """Open the API of the plugin of the current tab (or a chosen one) in the browser, at the word under the cursor."""
        editor = self.current_editor()
        api = self._api_to_show("API reference")
        if api is None:
            return
        anchor = None
        if editor is not None and api is not None:
            cursor = editor.textCursor()
            cursor.select(QTextCursor.SelectionType.WordUnderCursor)
            anchor = anchor_for(api, cursor.selectedText())
        folder = self._cache_dir or Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.CacheLocation))
        page = folder / f"api-{api.plugin}.html"
        try:
            page.parent.mkdir(parents=True, exist_ok=True)
            page.write_text(render_api_html(api, anchor), encoding="utf-8")
        except OSError as error:
            self._dialogs.error("API reference", f"Cannot write {page}: {error}")
            return
        url = QUrl.fromLocalFile(str(page))
        if anchor:
            url.setFragment(anchor)
        if not self._open_url(url):
            self._message(f"Could not open the browser; the page is {page}")

    # closing

    def _close_current(self) -> None:
        editor = self.current_editor()
        if editor is not None:
            self.close_editor(editor)

    def close_editor(self, editor: DocumentEdit) -> bool:
        """Close a tab; False if the user cancelled or the save failed."""
        if editor.is_dirty():
            choice = self._dialogs.ask_unsaved(editor.name)
            if choice == "cancel" or (choice == "save" and not self._save_and_wait(editor)):
                return False
        self._remove_editor(editor)
        return True

    def _remove_editor(self, editor: DocumentEdit) -> None:
        self._tabs.removeTab(self._tabs.indexOf(editor))
        self._editors.pop(editor.path, None)
        editor.deleteLater()

    def closeEvent(self, event: QCloseEvent) -> None:
        for editor in list(self._editors.values()):
            if not self.close_editor(editor):
                event.ignore()
                return
        self._worker.shutdown()
        event.accept()

    # tree operations

    def new_script(self, folder: str) -> None:
        answer = self._dialogs.ask_name("New script", "Name:")
        if answer is None:
            return
        try:
            name = script_filename(answer)
        except InvalidName as error:
            self._dialogs.error("New script", str(error))
            return
        path = posixpath.join(folder, name)

        def create() -> None:
            if self._fs.exists(path):
                raise AlreadyExists(path)
            self._fs.write(path, b"")

        def created(_result: None) -> None:
            self._tree.refresh_folder(folder, expand=True)
            self.open_file(path)
            self._rescan(folder)

        self._worker.submit(create, created, lambda error: self._failed("New script", error))

    def new_folder(self, folder: str) -> None:
        answer = self._dialogs.ask_name("New folder", "Name:")
        if answer is None:
            return
        try:
            name = plain_name(answer)
        except InvalidName as error:
            self._dialogs.error("New folder", str(error))
            return
        path = posixpath.join(folder, name)
        self._worker.submit(
            lambda: self._fs.mkdir(path),
            lambda _result: self._tree.refresh_folder(folder, expand=True),
            lambda error: self._failed("New folder", error),
        )

    def rename(self, path: str, is_dir: bool) -> None:
        old_name = posixpath.basename(path)
        answer = self._dialogs.ask_name("Rename", "New name:", old_name)
        if answer is None:
            return
        try:
            is_script = not is_dir and path.endswith(SCRIPT_SUFFIX)
            name = script_filename(answer) if is_script else plain_name(answer)
        except InvalidName as error:
            self._dialogs.error("Rename", str(error))
            return
        if name == old_name:
            return
        parent = posixpath.dirname(path)
        target = posixpath.join(parent, name)

        def renamed(_result: None) -> None:
            self._retarget_editors(path, target)
            self._tree.refresh_folder(parent)
            self._rescan(parent, target)

        self._worker.submit(
            lambda: self._fs.rename(path, target), renamed, lambda error: self._failed("Rename", error)
        )

    def delete(self, path: str, is_dir: bool) -> None:
        affected = [editor for editor_path, editor in self._editors.items() if self._is_under(editor_path, path)]
        text = f"Delete {path}?"
        if any(editor.is_dirty() for editor in affected):
            text += " Unsaved changes in open tabs are lost."
        if not self._dialogs.confirm("Delete", text, accept="Delete"):
            return
        parent = posixpath.dirname(path)

        def deleted(_result: None) -> None:
            for editor in affected:
                self._remove_editor(editor)
            self._tree.refresh_folder(parent)
            self._rescan(parent)

        self._worker.submit(lambda: self._fs.delete(path), deleted, lambda error: self._failed("Delete", error))

    @staticmethod
    def _is_under(path: str, folder: str) -> bool:
        return path == folder or path.startswith(folder + "/")

    def _retarget_editors(self, old: str, new: str) -> None:
        for path, editor in list(self._editors.items()):
            if self._is_under(path, old):
                del self._editors[path]
                editor.path = new + path[len(old):]
                self._editors[editor.path] = editor
                editor.set_api(self._apis.for_path(editor.path))
                self._retitle(editor)

    # feedback

    def _retitle(self, editor: DocumentEdit) -> None:
        index = self._tabs.indexOf(editor)
        if index >= 0:
            severities = {p.severity for p in editor.problems()}
            marks = (" *" if editor.is_dirty() else "") + (" !" if "error" in severities else " ?" if severities else "")
            self._tabs.setTabText(index, editor.name + marks)
            self._tabs.setTabToolTip(index, editor.path)

    def _current_changed(self) -> None:
        self._update_position()
        self.refresh_log()
        self._show_problems()

    def _problems_changed(self, editor: DocumentEdit) -> None:
        self._retitle(editor)
        if editor is self.current_editor():
            self._show_problems()
            self._update_position()

    def _show_problems(self) -> None:
        editor = self.current_editor()
        problems = editor.problems() if editor else []
        self._problems.show_problems(problems)
        self._problems_dock.setWindowTitle(f"Problems ({len(problems)})" if problems else "Problems")

    def _show_problem(self, line: int, column: int) -> None:
        editor = self.current_editor()
        if editor is not None:
            editor.goto(line, column or None)

    def _update_position(self) -> None:
        editor = self.current_editor()
        if editor is None:
            self._position.setText("")
            return
        cursor = editor.textCursor()
        eol = "CRLF" if editor.eol == "\r\n" else "LF"
        errors = sum(p.severity == "error" for p in editor.problems())
        warnings = sum(p.severity == "warning" for p in editor.problems())
        counts = [f"{n} {word}{'s' if n != 1 else ''}" for n, word in ((errors, "error"), (warnings, "warning")) if n]
        status = "   " + ", ".join(counts) if counts else ""
        self._position.setText(
            f"{editor.path}   Ln {cursor.blockNumber() + 1}, Col {cursor.positionInBlock() + 1}   {eol}{status}"
        )

    def _message(self, text: str) -> None:
        """Tell the user in the status line and keep it in the Activity list."""
        self.statusBar().showMessage(text, MESSAGE_MS)
        self._activity.add(text)

    def _progress(self, text: str) -> None:
        """The status line only: something that is under way and will be followed by a result."""
        self.statusBar().showMessage(text, MESSAGE_MS)

    def _warn(self, text: str, level: str = "warning") -> None:
        """Like _message, and brings the Activity list to the front: something was skipped or failed."""
        self.statusBar().showMessage(text, MESSAGE_MS)
        self._activity.add(text, level)
        self._activity_dock.raise_()

    @property
    def activity_panel(self) -> ActivityPanel:
        return self._activity

    def _failed(self, title: str, error: Exception, opening: str | None = None) -> None:
        if opening is not None:
            self._opening.discard(opening)
        info = explain(title, error)
        self._warn(f"{title}: {info.summary} {info.hint}".rstrip(), "error")
        self._dialogs.error(title, info.summary, info.hint, info.details)
