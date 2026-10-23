"""Syntax highlighting for Qt: the token kinds of lualex in the colours of the current theme."""

from __future__ import annotations

from PySide6.QtGui import QColor, QFont, QPalette, QSyntaxHighlighter, QTextCharFormat, QTextDocument
from PySide6.QtWidgets import QApplication

from lua_editor.editor.lualex import NORMAL, tokenize_line
from lua_editor.editor.palette import DARK, LIGHT
from lua_editor.editor.vocabulary import Vocabulary


def is_dark() -> bool:
    return QApplication.palette().color(QPalette.ColorRole.Base).lightness() < 128


def make_formats(dark: bool) -> dict[str, QTextCharFormat]:
    formats: dict[str, QTextCharFormat] = {}
    for kind, (colour, bold, italic) in (DARK if dark else LIGHT).items():
        fmt = QTextCharFormat()
        fmt.setForeground(QColor(colour))
        if bold:
            fmt.setFontWeight(QFont.Weight.Bold)
        fmt.setFontItalic(italic)
        formats[kind] = fmt
    return formats


class LuaHighlighter(QSyntaxHighlighter):
    def __init__(self, document: QTextDocument, vocab: Vocabulary, dark: bool | None = None) -> None:
        super().__init__(document)
        self._vocab = vocab
        self._formats = make_formats(is_dark() if dark is None else dark)

    def set_vocabulary(self, vocab: Vocabulary) -> None:
        self._vocab = vocab
        self.rehighlight()

    def highlightBlock(self, text: str) -> None:
        previous = self.previousBlockState()
        tokens, state = tokenize_line(text, NORMAL if previous < 0 else previous, self._vocab)
        for token in tokens:
            self.setFormat(token.start, token.end - token.start, self._formats[token.kind])
        self.setCurrentBlockState(state)
