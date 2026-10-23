"""One open file: the editor widget plus what is needed to write it back."""

from __future__ import annotations

import posixpath

from PySide6.QtCore import QTimer, Signal

from lua_editor.check.api_check import check_api, check_size
from lua_editor.check.syntax import check_syntax
from lua_editor.editor.widget import CodeEdit
from lua_editor.textcodec import encode_text

CHECK_DELAY_MS = 400  # the check runs this long after the last change


class DocumentEdit(CodeEdit):
    titleChanged = Signal()

    def __init__(self, path: str, text: str, eol: str, stamp: tuple[int, int]) -> None:
        super().__init__()
        self.path = path
        self.eol = eol
        self.stamp = stamp
        self._check_timer = QTimer(self)
        self._check_timer.setSingleShot(True)
        self._check_timer.setInterval(CHECK_DELAY_MS)
        self._check_timer.timeout.connect(self.check_now)
        self.load(text, eol, stamp)
        self.document().modificationChanged.connect(lambda _modified: self.titleChanged.emit())
        self.document().contentsChanged.connect(self._check_timer.start)
        self.vocabularyChanged.connect(self.check_now)

    @property
    def stem(self) -> str:
        """The name import knows this script by."""
        return self.name.removesuffix(".lua")

    def check_now(self) -> None:
        """Check what is in the editor and show the result: the syntax first, and only if that is right, the use of
        the plugin's API; the size always."""
        self._check_timer.stop()
        text = self.toPlainText()
        problems = check_syntax(text) or check_api(text, self.vocabulary, self.script_names, self.stem)
        size = len(encode_text(text, self.eol))
        self.set_problems(problems + check_size(size, self.vocabulary, text.split("\n", 1)[0]))

    @property
    def name(self) -> str:
        return posixpath.basename(self.path)

    def is_dirty(self) -> bool:
        return self.document().isModified()

    def load(self, text: str, eol: str, stamp: tuple[int, int]) -> None:
        """Replace the content with what is on the device; the undo history starts over."""
        self.eol = eol
        self.stamp = stamp
        self.setPlainText(text)
        self.document().setModified(False)
        self.check_now()

    def payload(self) -> tuple[bytes, int]:
        """The bytes to write and the document revision they belong to."""
        return encode_text(self.toPlainText(), self.eol), self.document().revision()

    def mark_saved(self, stamp: tuple[int, int], revision: int) -> None:
        """Keep the new stamp; the text counts as saved only if nothing was typed while the write ran."""
        self.stamp = stamp
        if self.document().revision() == revision:
            self.document().setModified(False)
