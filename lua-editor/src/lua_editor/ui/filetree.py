"""The folder tree of the remote side. A folder is listed when it is first expanded and again on refresh."""

from __future__ import annotations

import posixpath

from PySide6.QtCore import QModelIndex, QPersistentModelIndex, QPoint, Qt, Signal
from PySide6.QtGui import QAction, QKeySequence, QStandardItem, QStandardItemModel
from PySide6.QtWidgets import QAbstractItemView, QApplication, QMenu, QStyle, QTreeView

from lua_editor.naming import SCRIPT_SUFFIX
from lua_editor.remote import Entry, NotFound, RemoteFS
from lua_editor.ui.worker import RemoteWorker

PATH_ROLE = Qt.ItemDataRole.UserRole + 1
DIR_ROLE = Qt.ItemDataRole.UserRole + 2
SORT_ROLE = Qt.ItemDataRole.UserRole + 3


class FileTree(QTreeView):
    openRequested = Signal(str)
    newScriptRequested = Signal(str)
    newFolderRequested = Signal(str)
    renameRequested = Signal(str, bool)
    deleteRequested = Signal(str, bool)
    pushRequested = Signal(str, bool)
    pullRequested = Signal(str, bool)
    removeFromDeviceRequested = Signal(str, bool)
    failed = Signal(str)

    def __init__(self, fs: RemoteFS, worker: RemoteWorker) -> None:
        super().__init__()
        self._fs = fs
        self._worker = worker
        self._model = QStandardItemModel(self)
        self._model.setSortRole(SORT_ROLE)
        self.setModel(self._model)
        self.setHeaderHidden(True)
        self.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        # Persistent indexes, never QStandardItem wrappers: the model deletes items behind Python's back, and a stale
        # wrapper of a deleted item gets handed out again for a new object at the same address (seen with PySide6 6.12).
        self._known: dict[str, QPersistentModelIndex] = {}
        self._loaded: set[str] = set()
        self._loading: set[str] = set()
        self.expanded.connect(self._on_expanded)
        self.activated.connect(self._on_activated)
        self.customContextMenuRequested.connect(self._show_menu)
        self._build_actions()

    # public

    def load_root(self) -> None:
        self._load("")

    def refresh_folder(self, path: str, expand: bool = False) -> None:
        """List a folder again if it was listed before; with expand, open it (which lists it if it was not)."""
        if self._item(path) is None:
            return
        if path in self._loaded:
            self._load(path)
        if expand and path:
            self.expand(self._index(path))

    def selected(self) -> tuple[str, bool] | None:
        index = self.currentIndex()
        if not index.isValid():
            return None
        return index.data(PATH_ROLE), bool(index.data(DIR_ROLE))

    def selected_folder(self) -> str:
        """The folder a new item goes into: the selected folder, the folder of the selected file, or the root."""
        selection = self.selected()
        if selection is None:
            return ""
        path, is_dir = selection
        return path if is_dir else posixpath.dirname(path)

    def paths(self) -> list[str]:
        """Every path the tree knows, for tests and diagnostics."""
        return sorted(self._known)

    def _index(self, path: str) -> QModelIndex:
        known = self._known.get(path)
        if known is None or not known.isValid():
            return QModelIndex()
        return self._model.index(known.row(), 0, known.parent())

    def _item(self, path: str) -> QStandardItem | None:
        """The item of a folder, or None if it is gone; the root is the invisible item."""
        if path == "":
            return self._model.invisibleRootItem()
        index = self._index(path)
        return self._model.itemFromIndex(index) if index.isValid() else None

    # actions

    def _build_actions(self) -> None:
        self._new_script = self._action("New script...", None, lambda: self.newScriptRequested.emit(self.selected_folder()))
        self._new_folder = self._action("New folder...", None, lambda: self.newFolderRequested.emit(self.selected_folder()))
        self._rename = self._action("Rename...", "F2", self._request_rename)
        self._delete = self._action("Delete...", "Delete", self._request_delete)
        self._refresh = self._action("Refresh", "F5", lambda: self.refresh_folder(self.selected_folder()))
        self._push = self._action("Push to device", None, lambda: self._emit_selected(self.pushRequested))
        self._pull = self._action("Pull from device", None, lambda: self._emit_selected(self.pullRequested))
        self._remove = self._action(
            "Remove from device...", None, lambda: self._emit_selected(self.removeFromDeviceRequested)
        )

    def _action(self, text: str, shortcut: str | None, handler) -> QAction:
        action = QAction(text, self)
        if shortcut:
            action.setShortcut(QKeySequence(shortcut))
            action.setShortcutContext(Qt.ShortcutContext.WidgetShortcut)
        action.triggered.connect(lambda _checked=False: handler())
        self.addAction(action)
        return action

    def _emit_selected(self, signal) -> None:
        selection = self.selected()
        if selection is not None:
            signal.emit(*selection)

    def _request_rename(self) -> None:
        selection = self.selected()
        if selection is not None:
            self.renameRequested.emit(*selection)

    def _request_delete(self) -> None:
        selection = self.selected()
        if selection is not None:
            self.deleteRequested.emit(*selection)

    def _show_menu(self, position: QPoint) -> None:
        index = self.indexAt(position)
        self.setCurrentIndex(index)
        if not index.isValid():
            self.clearSelection()
        self._rename.setEnabled(index.isValid())
        self._delete.setEnabled(index.isValid())
        menu = QMenu(self)
        menu.addActions([self._new_script, self._new_folder])
        menu.addSeparator()
        menu.addActions([self._rename, self._delete])
        menu.addSeparator()
        for action in (self._push, self._pull, self._remove):
            action.setEnabled(index.isValid())
        menu.addActions([self._push, self._pull, self._remove])
        menu.addSeparator()
        menu.addAction(self._refresh)
        menu.exec(self.viewport().mapToGlobal(position))

    # loading

    def _on_expanded(self, index: QModelIndex) -> None:
        path = index.data(PATH_ROLE)
        if path not in self._loaded:
            self._load(path)

    def _on_activated(self, index: QModelIndex) -> None:
        if not index.data(DIR_ROLE):
            self.openRequested.emit(index.data(PATH_ROLE))

    def _load(self, path: str) -> None:
        if path in self._loading:
            return
        self._loading.add(path)
        self._worker.submit(
            lambda: self._fs.list(path),
            lambda entries: self._listed(path, entries),
            lambda error: self._load_failed(path, error),
        )

    def _load_failed(self, path: str, error: Exception) -> None:
        self._loading.discard(path)
        self.failed.emit(f"Cannot list {path or '/'}: {error}")
        if path and isinstance(error, NotFound):
            self.refresh_folder(posixpath.dirname(path))

    def _listed(self, path: str, entries: list[Entry]) -> None:
        self._loading.discard(path)
        parent = self._item(path)
        if parent is None:
            return
        self._loaded.add(path)
        # only scripts and folders: the plugin's <name>.log files are not for editing (the Log panel shows them)
        entries = [entry for entry in entries if entry.is_dir or entry.name.endswith(SCRIPT_SUFFIX)]
        kept = self._prune(parent, {entry.path: entry for entry in entries})
        for entry in entries:
            if entry.path not in kept:
                parent.appendRow(self._make_item(entry))
                self._known[entry.path] = QPersistentModelIndex(parent.child(parent.rowCount() - 1).index())
        parent.sortChildren(0)

    def _prune(self, parent: QStandardItem, wanted: dict[str, Entry]) -> set[str]:
        """Remove the rows that are placeholders or no longer listed; return the paths that stay."""
        kept: set[str] = set()
        for row in reversed(range(parent.rowCount())):
            child_path = parent.child(row).data(PATH_ROLE)
            entry = wanted.get(child_path)
            if child_path is None or entry is None or bool(parent.child(row).data(DIR_ROLE)) != entry.is_dir:
                parent.removeRow(row)
                if child_path is not None:
                    self._forget(child_path)
            else:
                kept.add(child_path)
        return kept

    def _make_item(self, entry: Entry) -> QStandardItem:
        style = QApplication.style()
        icon = style.standardIcon(QStyle.StandardPixmap.SP_DirIcon if entry.is_dir else QStyle.StandardPixmap.SP_FileIcon)
        item = QStandardItem(icon, entry.name)
        item.setEditable(False)
        item.setData(entry.path, PATH_ROLE)
        item.setData(entry.is_dir, DIR_ROLE)
        item.setData(("0" if entry.is_dir else "1") + entry.name.casefold(), SORT_ROLE)
        if entry.is_dir:
            # a child that is not listed yet, so the folder can be expanded
            item.appendRow(QStandardItem())
        return item

    def _forget(self, path: str) -> None:
        for known in [known for known in self._known if known == path or known.startswith(path + "/")]:
            del self._known[known]
            self._loaded.discard(known)
