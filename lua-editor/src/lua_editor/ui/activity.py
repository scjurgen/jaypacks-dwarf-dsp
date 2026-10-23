"""What the editor did, one line per operation and result, with the full paths. Kept in memory for the session."""

from __future__ import annotations

from datetime import datetime

from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import QPlainTextEdit

LEVELS = ("info", "warning", "error")
MARKS = {"info": "  ", "warning": "! ", "error": "!!"}


class ActivityPanel(QPlainTextEdit):
    def __init__(self) -> None:
        super().__init__()
        self.setReadOnly(True)
        self.setMaximumBlockCount(1000)
        self.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
        self.setPlaceholderText("Pushes, pulls, saves and errors are listed here.")

    def add(self, text: str, level: str = "info") -> None:
        stamp = datetime.now().strftime("%H:%M:%S")
        self.appendPlainText(f"{stamp} {MARKS[level]} {text}")
