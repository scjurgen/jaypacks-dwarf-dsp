"""The text widget scripts are edited in: line numbers, Lua highlighting, completion and a hint for the cursor."""

from __future__ import annotations

from PySide6.QtCore import QEvent, QPoint, QRect, QSize, QStringListModel, Qt, Signal
from PySide6.QtGui import (
    QColor, QFontDatabase, QFontMetricsF, QHelpEvent, QKeyEvent, QPainter, QPalette, QResizeEvent, QTextCharFormat,
    QTextCursor, QTextFormat,
)
from PySide6.QtWidgets import QCompleter, QPlainTextEdit, QTextEdit, QToolTip, QWidget

from lua_editor.api.model import Api, Library
from lua_editor.check.problems import Problem
from lua_editor.editor.highlighter import is_dark
from lua_editor.editor.highlighter import LuaHighlighter
from lua_editor.editor import indent
from lua_editor.editor.intellisense import Completion, Intellisense, Item
from lua_editor.editor.lualex import NORMAL
from lua_editor.editor.vocabulary import Vocabulary

TAB_WIDTH_SPACES = len(indent.INDENT)
KEYWORDS_TYPED_ON_THE_WAY = ("end", "else", "elseif", "until")  # a line that is a start of one of these keeps its dedent
POPUP_ROWS = 10
# (light theme, dark theme); each against its own editor background
SEVERITY_COLOURS = {"error": ("#d32f2f", "#f48771"), "warning": ("#a85d00", "#e5a50a")}


def severity_colour(severity: str) -> QColor:
    light, dark = SEVERITY_COLOURS.get(severity, SEVERITY_COLOURS["error"])
    return QColor(dark if is_dark() else light)


class LineNumberArea(QWidget):
    def __init__(self, editor: CodeEdit) -> None:
        super().__init__(editor)
        self._editor = editor

    def sizeHint(self) -> QSize:
        return QSize(self._editor.line_number_width(), 0)

    def paintEvent(self, event) -> None:
        self._editor.paint_line_numbers(event)

    def event(self, event: QEvent) -> bool:
        if event.type() == QEvent.Type.ToolTip and isinstance(event, QHelpEvent):
            self._editor.show_problem_tip(QPoint(0, event.pos().y()), event.globalPos())
            return True
        return super().event(event)


class CodeEdit(QPlainTextEdit):
    hintChanged = Signal(str)
    problemsChanged = Signal()
    vocabularyChanged = Signal()  # the API, the libraries or the script names changed: checks are out of date

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._problems: list[Problem] = []
        self._dedented: tuple[int, str, str] | None = None  # (line, indentation it had, word) after an automatic dedent
        self._script_names: set[str] | None = None
        font = QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)
        self.setFont(font)
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.setTabStopDistance(QFontMetricsF(font).horizontalAdvance(" ") * TAB_WIDTH_SPACES)

        self._numbers = LineNumberArea(self)
        self.blockCountChanged.connect(self._update_margin)
        self.updateRequest.connect(self._scroll_numbers)
        self.cursorPositionChanged.connect(self._cursor_moved)
        self._update_margin()

        self._api: Api | None = None
        self._libraries: list[Library] = []
        self._vocab = Vocabulary(None)
        self._sense = Intellisense(self._vocab)
        self._highlighter = LuaHighlighter(self.document(), self._vocab)
        self._completion: Completion | None = None
        self._items: dict[str, Item] = {}
        self._model = QStringListModel(self)
        self._completer = QCompleter(self._model, self)
        self._completer.setWidget(self)
        self._completer.setCompletionMode(QCompleter.CompletionMode.PopupCompletion)
        self._completer.setCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive)
        self._completer.activated[str].connect(self._accept)
        self._completer.highlighted[str].connect(self._item_hint)

    # the plugin's API

    def set_api(self, api: Api | None) -> None:
        self._api = api
        self._rebuild_vocabulary()

    def set_libraries(self, libraries: list[Library]) -> None:
        """The libraries found in the script's own folder, on top of the factory ones of the API."""
        self._libraries = libraries
        self._rebuild_vocabulary()

    def set_script_names(self, names: set[str] | None) -> None:
        """The scripts of the script's folder, which import can load (None: not known yet)."""
        self._script_names = names
        self.vocabularyChanged.emit()

    @property
    def vocabulary(self) -> Vocabulary:
        return self._vocab

    @property
    def script_names(self) -> set[str] | None:
        return self._script_names

    def _rebuild_vocabulary(self) -> None:
        self._vocab = Vocabulary(self._api, self._libraries)
        self._sense = Intellisense(self._vocab)
        self._highlighter.set_vocabulary(self._vocab)
        self._cursor_moved()
        self.vocabularyChanged.emit()

    # completion

    def completion(self) -> Completion | None:
        """What the list offers now, None if it is closed."""
        return self._completion if self._completer.popup().isVisible() else None

    def complete_now(self, explicit: bool = True) -> None:
        text = self.toPlainText()
        offset = self.textCursor().position()
        result = self._sense.complete(text, offset)
        popup = self._completer.popup()
        if result is None or (not explicit and len(result.prefix) < result.auto_prefix):
            popup.hide()
            return
        if len(result.items) == 1 and result.items[0].label == result.prefix:
            popup.hide()  # the word is typed in full already
            return
        self._completion = result
        self._items = {item.label: item for item in result.items}
        self._model.setStringList([item.label for item in result.items])
        self._completer.setCompletionPrefix(result.prefix)
        popup.setCurrentIndex(self._completer.completionModel().index(0, 0))
        rows = min(POPUP_ROWS, self._completer.completionModel().rowCount())
        rect = self.cursorRect()
        rect.setWidth(popup.sizeHintForColumn(0) + popup.verticalScrollBar().sizeHint().width() + 8)
        rect.setHeight(rect.height() * max(rows, 1))
        self._completer.complete(rect)

    def accept_completion(self, label: str) -> None:
        self._accept(label)

    def _accept(self, label: str) -> None:
        item = self._items.get(label)
        if item is None or self._completion is None:
            return
        cursor = self.textCursor()
        end = cursor.position()
        cursor.setPosition(self._completion.start)
        cursor.setPosition(end, QTextCursor.MoveMode.KeepAnchor)
        cursor.insertText(item.insert)
        self.setTextCursor(cursor)
        self._completer.popup().hide()

    def _item_hint(self, label: str) -> None:
        item = self._items.get(label)
        if item is not None:
            self.hintChanged.emit(f"{item.label}  {item.detail}" if item.detail else item.label)

    def keyPressEvent(self, event: QKeyEvent) -> None:
        popup = self._completer.popup()
        if popup.isVisible() and event.key() in (Qt.Key.Key_Enter, Qt.Key.Key_Return):
            if self._completer.currentCompletion() == self._completer.completionPrefix():
                popup.hide()  # the word is typed in full: Enter is a new line, not an acceptance of itself
        if popup.isVisible() and event.key() in (
            Qt.Key.Key_Enter, Qt.Key.Key_Return, Qt.Key.Key_Escape, Qt.Key.Key_Tab, Qt.Key.Key_Backtab,
        ):
            event.ignore()  # the list takes these
            return
        dedented, self._dedented = self._dedented, None
        if self._indenting_key(event):
            return
        explicit = event.key() == Qt.Key.Key_Space and bool(event.modifiers() & Qt.KeyboardModifier.ControlModifier)
        if not explicit:
            super().keyPressEvent(event)
            self._after_typing(event, dedented)
        if explicit or (event.text() and not event.modifiers() & Qt.KeyboardModifier.ControlModifier):
            self.complete_now(explicit)
        elif popup.isVisible() and event.key() in (Qt.Key.Key_Left, Qt.Key.Key_Right, Qt.Key.Key_Home, Qt.Key.Key_End):
            popup.hide()

    # indentation

    def _indenting_key(self, event: QKeyEvent) -> bool:
        """Enter, Tab and Shift+Tab; True if handled. The completion list has already taken them when it is open."""
        held = event.modifiers() & (
            Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.AltModifier | Qt.KeyboardModifier.MetaModifier
        )
        if held:
            return False
        key = event.key()
        if key in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            return self._new_line()
        if key in (Qt.Key.Key_Tab, Qt.Key.Key_Backtab):
            self._shift_lines(unindent=key == Qt.Key.Key_Backtab)
            return True
        return False

    @staticmethod
    def _state_before(block) -> int:
        previous = block.previous()
        state = previous.userState() if previous.isValid() else NORMAL
        return NORMAL if state < 0 else state

    @staticmethod
    def _lines_above(block) -> list[str]:
        """The text of the lines above, nearest last, up to the last one that is inside a long string or comment."""
        lines: list[str] = []
        previous = block.previous()
        while previous.isValid() and len(lines) < indent.LOOK_BACK:
            if previous.userState() > NORMAL:
                break
            lines.append(previous.text())
            previous = previous.previous()
        return lines[::-1]

    def _new_line(self) -> bool:
        """Enter: the new line gets the indentation of the line, one level deeper after a block opener. A closer
        right behind the cursor (`{|}`) moves to a line of its own. Inside a long string or comment: not handled."""
        cursor = self.textCursor()
        state = self._state_before(cursor.block())
        if state != NORMAL:
            return False
        cursor.beginEditBlock()
        cursor.removeSelectedText()
        block = cursor.block()
        column = cursor.positionInBlock()
        before, after = block.text()[:column], block.text()[column:]
        deeper = indent.next_line_indent(before, state, self._lines_above(block))
        closing = after[:1] in ("}", ")") or after.startswith(("end", "until"))
        if closing and deeper != indent.leading_whitespace(before):
            cursor.insertText("\n" + deeper + "\n" + indent.leading_whitespace(before))
            cursor.movePosition(QTextCursor.MoveOperation.Up)
            cursor.movePosition(QTextCursor.MoveOperation.EndOfBlock)
        else:
            cursor.insertText("\n" + deeper)
        cursor.endEditBlock()
        self.setTextCursor(cursor)
        self.ensureCursorVisible()
        return True

    def _shift_lines(self, unindent: bool) -> None:
        """Tab: spaces up to the next tab stop, or one level more for the selected lines. Shift+Tab: one level less."""
        cursor = self.textCursor()
        if not cursor.hasSelection() and not unindent:
            cursor.insertText(indent.tab_insertion(cursor.positionInBlock()))
            return
        document = self.document()
        first = document.findBlock(cursor.selectionStart())
        last = document.findBlock(cursor.selectionEnd())
        if last != first and cursor.selectionEnd() == last.position():
            last = last.previous()  # a selection that ends at the start of a line does not include it
        selected = cursor.hasSelection()
        first_number, last_number = first.blockNumber(), last.blockNumber()
        column = cursor.positionInBlock()
        cursor.beginEditBlock()
        for number in range(first_number, last_number + 1):
            block = document.findBlockByNumber(number)
            edit = QTextCursor(block)
            if unindent:
                removed = len(block.text()) - len(indent.unindent_line(block.text()))
                edit.movePosition(QTextCursor.MoveOperation.Right, QTextCursor.MoveMode.KeepAnchor, removed)
                edit.removeSelectedText()
            elif block.text().strip():
                edit.insertText(indent.INDENT)
        cursor.endEditBlock()
        cursor = QTextCursor(document.findBlockByNumber(first_number))
        if selected:
            end = document.findBlockByNumber(last_number)
            cursor.setPosition(end.position() + end.length() - 1, QTextCursor.MoveMode.KeepAnchor)
        else:
            cursor.setPosition(cursor.block().position() + min(column, len(cursor.block().text())))
        self.setTextCursor(cursor)

    def _after_typing(self, event: QKeyEvent, dedented: tuple[int, str, str] | None) -> None:
        """A closing word alone on its line steps back one level when it is typed; if more letters follow (endpoint),
        the step is taken back."""
        text = event.text()
        if len(text) != 1 or not text.isprintable():
            return
        cursor = self.textCursor()
        block = cursor.block()
        line = block.text()
        if dedented is not None and dedented[0] == block.blockNumber():
            _, original, word = dedented
            current = line.strip()
            follows = current[len(word) : len(word) + 1]
            if current.startswith(word) and (follows.isalnum() or follows == "_"):
                if not any(keyword.startswith(current) for keyword in KEYWORDS_TYPED_ON_THE_WAY):
                    self._replace_line(cursor, original + current)
                    return
        if cursor.positionInBlock() != len(line) or self._state_before(block) != NORMAL:
            return
        shallower = indent.dedented(line)
        if shallower is not None:
            self._dedented = (block.blockNumber(), indent.leading_whitespace(line), line.strip())
            self._replace_line(cursor, shallower)

    def _replace_line(self, cursor: QTextCursor, text: str) -> None:
        """Replace the text of the cursor's line, as part of the edit that has just been made (one undo step)."""
        edit = QTextCursor(cursor.block())
        edit.movePosition(QTextCursor.MoveOperation.EndOfBlock, QTextCursor.MoveMode.KeepAnchor)
        edit.joinPreviousEditBlock()
        edit.insertText(text)
        edit.endEditBlock()
        end = self.textCursor()
        end.movePosition(QTextCursor.MoveOperation.EndOfBlock)
        self.setTextCursor(end)

    # the hint for the word under the cursor

    def hint(self) -> str:
        return self._sense.describe(self.toPlainText(), self.textCursor().position())

    def _cursor_moved(self) -> None:
        self._refresh_selections()
        if not self._completer.popup().isVisible():
            self.hintChanged.emit(self.hint())

    # problems: a wavy underline in the text, a dot in the margin, the message as a tooltip

    def problems(self) -> list[Problem]:
        return list(self._problems)

    def set_problems(self, problems: list[Problem]) -> None:
        if problems == self._problems:
            return
        self._problems = list(problems)
        self._refresh_selections()
        self._numbers.update()
        self.problemsChanged.emit()

    def goto(self, line: int, column: int | None = None) -> None:
        """Put the cursor on a 1-based line and column and bring it into view."""
        block = self.document().findBlockByNumber(max(line, 1) - 1)
        if not block.isValid():
            block = self.document().lastBlock()
        cursor = self.textCursor()
        cursor.setPosition(block.position() + min((column or 1) - 1, max(block.length() - 1, 0)))
        self.setTextCursor(cursor)
        self.centerCursor()
        self.setFocus()

    def _problem_selection(self, problem: Problem) -> QTextEdit.ExtraSelection | None:
        block = self.document().findBlockByNumber(problem.line - 1)
        if not block.isValid():
            block = self.document().lastBlock()
        text = block.text()
        if problem.column:
            start, length = problem.column - 1, max(problem.length, 1)
        else:
            start = len(text) - len(text.lstrip())
            length = max(len(text.rstrip()) - start, 1)
        start = min(start, max(len(text) - 1, 0))
        length = min(length, max(len(text) - start, 1)) if text else 0
        selection = QTextEdit.ExtraSelection()
        selection.format.setUnderlineStyle(QTextCharFormat.UnderlineStyle.WaveUnderline)
        selection.format.setUnderlineColor(severity_colour(problem.severity))
        selection.cursor = QTextCursor(block)
        selection.cursor.setPosition(block.position() + start)
        selection.cursor.setPosition(block.position() + start + length, QTextCursor.MoveMode.KeepAnchor)
        return selection

    def _refresh_selections(self) -> None:
        line = QTextEdit.ExtraSelection()
        colour = QColor(self.palette().color(QPalette.ColorRole.Highlight))
        colour.setAlpha(36)
        line.format.setBackground(colour)
        line.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
        line.cursor = self.textCursor()
        line.cursor.clearSelection()
        marks = [self._problem_selection(problem) for problem in self._problems]
        self.setExtraSelections([line, *[mark for mark in marks if mark is not None]])

    def _messages_at(self, line: int) -> str:
        return "\n".join(p.message for p in self._problems if p.line == line)

    def show_problem_tip(self, position: QPoint, global_position: QPoint) -> None:
        """The message of the problem on the line at a viewport position, as a tooltip."""
        message = self._messages_at(self.cursorForPosition(position).blockNumber() + 1)
        if message:
            QToolTip.showText(global_position, message, self)
        else:
            QToolTip.hideText()

    def event(self, event: QEvent) -> bool:
        if event.type() == QEvent.Type.ToolTip and isinstance(event, QHelpEvent):
            self.show_problem_tip(event.pos(), event.globalPos())
            return True
        return super().event(event)

    # line numbers

    def line_number_width(self) -> int:
        digits = max(3, len(str(self.blockCount())))
        return 14 + self.fontMetrics().horizontalAdvance("9") * digits

    def _update_margin(self, _count: int = 0) -> None:
        self.setViewportMargins(self.line_number_width(), 0, 0, 0)

    def _scroll_numbers(self, rect: QRect, dy: int) -> None:
        if dy:
            self._numbers.scroll(0, dy)
        else:
            self._numbers.update(0, rect.y(), self._numbers.width(), rect.height())
        if rect.contains(self.viewport().rect()):
            self._update_margin()

    def resizeEvent(self, event: QResizeEvent) -> None:
        super().resizeEvent(event)
        area = self.contentsRect()
        self._numbers.setGeometry(QRect(area.left(), area.top(), self.line_number_width(), area.height()))

    def paint_line_numbers(self, event) -> None:
        painter = QPainter(self._numbers)
        palette = self.palette()
        painter.fillRect(event.rect(), palette.color(QPalette.ColorRole.AlternateBase))
        painter.setPen(palette.color(QPalette.ColorRole.PlaceholderText))
        block = self.firstVisibleBlock()
        number = block.blockNumber()
        top = round(self.blockBoundingGeometry(block).translated(self.contentOffset()).top())
        bottom = top + round(self.blockBoundingRect(block).height())
        marked: dict[int, str] = {}
        for problem in self._problems:
            if marked.get(problem.line) != "error":
                marked[problem.line] = problem.severity
        dot = max(self.fontMetrics().height() // 2, 6)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        while block.isValid() and top <= event.rect().bottom():
            if block.isVisible() and bottom >= event.rect().top():
                if number + 1 in marked:
                    painter.setBrush(severity_colour(marked[number + 1]))
                    painter.setPen(Qt.PenStyle.NoPen)
                    painter.drawEllipse(3, top + (self.fontMetrics().height() - dot) // 2, dot, dot)
                    painter.setPen(palette.color(QPalette.ColorRole.PlaceholderText))
                painter.drawText(
                    0, top, self._numbers.width() - 6, self.fontMetrics().height(),
                    Qt.AlignmentFlag.AlignRight, str(number + 1),
                )
            block = block.next()
            top = bottom
            bottom = top + round(self.blockBoundingRect(block).height())
            number += 1
