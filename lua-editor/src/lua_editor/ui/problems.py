"""The list of problems of the script in the current tab."""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QListWidget, QListWidgetItem

from lua_editor.check.problems import Problem

LINE_ROLE = Qt.ItemDataRole.UserRole + 1
COLUMN_ROLE = Qt.ItemDataRole.UserRole + 2


class ProblemsPanel(QListWidget):
    problemActivated = Signal(int, int)  # line and column (0 when only the line is known)

    def __init__(self) -> None:
        super().__init__()
        self.itemActivated.connect(self._activated)

    def show_problems(self, problems: list[Problem]) -> None:
        self.clear()
        for problem in problems:
            where = f"{problem.line}:{problem.column}" if problem.column else f"{problem.line}"
            item = QListWidgetItem(f"{problem.severity}  {where}  {problem.message}")
            item.setData(LINE_ROLE, problem.line)
            item.setData(COLUMN_ROLE, problem.column or 0)
            self.addItem(item)

    def texts(self) -> list[str]:
        return [self.item(row).text() for row in range(self.count())]

    def _activated(self, item: QListWidgetItem) -> None:
        self.problemActivated.emit(item.data(LINE_ROLE), item.data(COLUMN_ROLE))
