"""The questions the window asks the user, in one place so tests can answer them."""

from __future__ import annotations

from PySide6.QtWidgets import QInputDialog, QMessageBox, QWidget


class Dialogs:
    def __init__(self, parent: QWidget) -> None:
        self._parent = parent

    def ask_name(self, title: str, label: str, text: str = "") -> str | None:
        answer, accepted = QInputDialog.getText(self._parent, title, label, text=text)
        return answer if accepted else None

    def choose(self, title: str, label: str, items: list[str]) -> str | None:
        answer, accepted = QInputDialog.getItem(self._parent, title, label, items, 0, False)
        return answer if accepted else None

    def confirm(self, title: str, text: str, accept: str = "Yes", details: str = "") -> bool:
        """A question with a button named for the action (default: Cancel); details is a list shown on request."""
        box = QMessageBox(QMessageBox.Icon.Question, title, text, parent=self._parent)
        yes = box.addButton(accept, QMessageBox.ButtonRole.AcceptRole)
        cancel = box.addButton("Cancel" if accept != "Yes" else "No", QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(cancel)
        if details:
            box.setDetailedText(details)
        box.exec()
        return box.clickedButton() is yes

    def ask_unsaved(self, name: str) -> str:
        """'save', 'discard' or 'cancel'."""
        box = QMessageBox(self._parent)
        box.setWindowTitle("Unsaved changes")
        box.setText(f"{name} has unsaved changes.")
        save = box.addButton("Save", QMessageBox.ButtonRole.AcceptRole)
        discard = box.addButton("Discard", QMessageBox.ButtonRole.DestructiveRole)
        box.addButton("Cancel", QMessageBox.ButtonRole.RejectRole)
        box.exec()
        return {save: "save", discard: "discard"}.get(box.clickedButton(), "cancel")

    def ask_conflict(self, name: str) -> str:
        """'overwrite', 'reload' or 'cancel'."""
        box = QMessageBox(self._parent)
        box.setWindowTitle("File changed on the device")
        box.setText(f"{name} was changed since you opened it.")
        overwrite = box.addButton("Overwrite", QMessageBox.ButtonRole.DestructiveRole)
        reload = box.addButton("Reload (lose my changes)", QMessageBox.ButtonRole.DestructiveRole)
        box.addButton("Cancel", QMessageBox.ButtonRole.RejectRole)
        box.exec()
        return {overwrite: "overwrite", reload: "reload"}.get(box.clickedButton(), "cancel")

    def error(self, title: str, text: str, hint: str = "", details: str = "") -> None:
        """text says what went wrong, hint what to try; details (exception, traceback) are behind Show Details."""
        box = QMessageBox(QMessageBox.Icon.Warning, title, text, QMessageBox.StandardButton.Ok, self._parent)
        if hint:
            box.setInformativeText(hint)
        if details:
            box.setDetailedText(details)
        box.exec()
