"""The log the plugin writes next to a script (`<name>.log`): state, error line, the script's own Log lines."""

from __future__ import annotations

from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import QPlainTextEdit


class LogPanel(QPlainTextEdit):
    def __init__(self) -> None:
        super().__init__()
        self.setReadOnly(True)
        self.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
        self.setPlaceholderText("The plugin's log of the script in the current tab appears here after a save.")

    def show_log(self, name: str, text: str | None) -> None:
        """text None: there is no log file (yet)."""
        self.setPlainText(f"{name}\n\n(no log file yet)" if text is None else f"{name}\n\n{text}")
