"""Completion and the one-line description of what is under the cursor, from the plugin's vocabulary.

Pure text in, text out: the Qt widget only shows what comes back.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

from lua_editor.api.lua_modules import mask
from lua_editor.api.model import Library, Member, Value
from lua_editor.editor import luastd
from lua_editor.editor.context import CallContext, enclosing_call, in_comment, in_string
from lua_editor.editor.vocabulary import RUNTIME_DOCS, Vocabulary

WORD_END = re.compile(r"[A-Za-z_]\w*$")
BASE_BEFORE = re.compile(r"([A-Za-z_]\w*)\s*[.:]\s*(\w*)$")
IMPORT_BEFORE = re.compile(r"\bimport\s*\(?\s*$")
KIND_RANK = {"command": 0, "enum": 1, "library": 2, "global": 3, "std": 4, "keyword": 5, "word": 6}


@dataclass(frozen=True)
class Item:
    label: str
    insert: str
    detail: str
    kind: str  # command, field, enum, enum_member, member, library, global, std, keyword, hook, module, value, word


@dataclass
class Completion:
    start: int  # the text from here to the cursor is replaced by the chosen item
    prefix: str  # what is there already; the items all start with it
    items: list[Item]
    auto_prefix: int  # how many characters have to be typed before the list opens by itself
    context: str  # member, string, table-key, table-value, hook or general


def _member_detail(member: Member) -> str:
    if member.kind == "function":
        return f"({', '.join(member.params or [])})" + (f"  {member.doc}" if member.doc else "")
    return member.value + (f"  {member.doc}" if member.doc else "")


class Intellisense:
    def __init__(self, vocab: Vocabulary) -> None:
        self.vocab = vocab

    # completion

    def complete(self, text: str, offset: int) -> Completion | None:
        if in_comment(text, offset):
            return None
        quote = in_string(text, offset)
        if quote is not None:
            return self._in_string(text, offset, quote)
        code = mask(text[:offset])
        member = BASE_BEFORE.search(code)
        if member:
            return self._members(text, offset, member.group(1), member.group(2))
        word = WORD_END.search(code)
        prefix = word.group(0) if word else ""
        start = offset - len(prefix)
        call = enclosing_call(text, offset)
        if call and call.table_start is not None and call.name in self.vocab.commands:
            return self._table(text, offset, start, prefix, call)
        line_start = text.rfind("\n", 0, start) + 1
        if re.fullmatch(r"\s*function\s+", text[line_start:start]):
            return self._hooks(start, prefix)
        return self._general(text, offset, start, prefix)

    @staticmethod
    def _filter(items: list[Item], prefix: str) -> list[Item]:
        lower = prefix.lower()
        return [item for item in items if item.label.lower().startswith(lower)]

    def _done(self, start: int, items: list[Item], prefix: str, auto: int, context: str) -> Completion | None:
        items = self._filter(items, prefix)
        if not items:
            return None
        return Completion(start, prefix, items, auto, context)

    def _members(self, text: str, offset: int, base: str, prefix: str) -> Completion | None:
        vocab = self.vocab
        start = offset - len(prefix)
        items: list[Item] = []
        if base in vocab.enums:
            enum = vocab.enums[base]
            items = [Item(name, name, f"= {value}", "enum_member") for name, value in enum.entries.items()]
        elif base in vocab.imported_libraries(text):
            library = vocab.libraries[base]
            items = [Item(m.name, m.name, _member_detail(m), "member") for m in library.members]
        elif base in vocab.std_tables:
            items = [Item(name, name, "", "std") for name in vocab.std_tables[base]]
        return self._done(start, items, prefix, 0, "member")

    def _in_string(self, text: str, offset: int, quote: int) -> Completion | None:
        vocab = self.vocab
        prefix = text[quote + 1 : offset]
        code_before = mask(text[:quote])
        if IMPORT_BEFORE.search(code_before):
            items = [Item(name, name, "", "module") for name in vocab.import_names()]
            return self._done(quote + 1, items, prefix, 0, "string")
        call = enclosing_call(text, quote)
        if call and call.key and call.name in vocab.commands:
            value = vocab.commands[call.name].field(call.key)
            if value and value.values:
                items = [Item(name, name, note, "value") for name, note in value.values]
                return self._done(quote + 1, items, prefix, 0, "string")
        return None

    def _table(self, text: str, offset: int, start: int, prefix: str, call: CallContext) -> Completion | None:
        command = self.vocab.commands[call.name]
        if call.key is not None:
            value = command.field(call.key)
            if value is None:
                return None
            if value.enum:
                enum = self.vocab.enums.get(value.enum)
                items = [Item(value.enum, value.enum, enum.doc if enum else "", "enum")]
            elif value.kind == "flag":
                items = [Item("true", "true", "", "keyword"), Item("false", "false", "", "keyword")]
            else:
                return None
            return self._done(start, items, prefix, 0, "table-value")
        used = self._used_keys(text, offset, call)
        assigned = re.match(r"\s*=(?!=)", text[offset:])
        items = [
            Item(f.name, f.name if assigned else f"{f.name} = ", f"{f.describe()}. {f.doc}", "field")
            for f in command.fields
            if f.name not in used
        ]
        return self._done(start, items, prefix, 1, "table-key")

    @staticmethod
    def _used_keys(text: str, offset: int, call: CallContext) -> set[str]:
        """The keys already in the table, not counting the entry the cursor is in."""
        code = mask(text)
        assert call.table_start is not None
        depth, begin, keys = 0, call.table_start + 1, set()
        end = len(code)
        for i in range(call.table_start + 1, len(code)):
            c = code[i]
            if c in "({[":
                depth += 1
            elif c in ")}]":
                if depth == 0:
                    end = i
                    break
                depth -= 1
            elif c in ",;" and depth == 0:
                if not begin <= offset <= i:
                    keys.update(re.findall(r"^\s*([A-Za-z_]\w*)\s*=(?!=)", code[begin:i]))
                begin = i + 1
        if not begin <= offset <= end:
            keys.update(re.findall(r"^\s*([A-Za-z_]\w*)\s*=(?!=)", code[begin:end]))
        return keys

    def _hooks(self, start: int, prefix: str) -> Completion | None:
        items = [Item(h.name, h.signature(), h.doc, "hook") for h in self.vocab.hooks.values()]
        return self._done(start, items, prefix, 0, "hook")

    def _general(self, text: str, offset: int, start: int, prefix: str) -> Completion | None:
        vocab = self.vocab
        items: list[Item] = [Item(k, k, "", "keyword") for k in sorted(luastd.KEYWORDS)]
        items += [Item(c.name, c.name, c.signature(), "command") for c in vocab.commands.values()]
        items += [Item(e.name, e.name, e.doc, "enum") for e in vocab.enums.values()]
        items += [Item(g, g, lib.summary, "library") for g, lib in vocab.imported_libraries(text).items()]
        items += [Item(n, n, RUNTIME_DOCS.get(n, ""), "global") for n in vocab.base]
        items += [Item(n, n, "", "std") for n in vocab.std_tables]
        seen = {item.label for item in items}
        for word in sorted(set(re.findall(r"[A-Za-z_]\w*", mask(text)))):
            if word not in seen and word != prefix:
                items.append(Item(word, word, "", "word"))
        items.sort(key=lambda item: (KIND_RANK.get(item.kind, 9), item.label.lower()))
        unique: dict[str, Item] = {}
        for item in items:
            unique.setdefault(item.label, item)  # the most specific kind of a name wins
        return self._done(start, list(unique.values()), prefix, 2, "general")

    # description

    def describe(self, text: str, offset: int) -> str:
        code = mask(text)
        left = WORD_END.search(code[:offset])
        right = re.match(r"\w*", code[offset:])
        word = (left.group(0) if left else "") + (right.group(0) if right else "")
        if left is None and not (right and right.group(0)):
            word = ""
        start = offset - (len(left.group(0)) if left else 0)
        if word and not word[0].isdigit():
            found = self._describe_word(text, code, word, start)
            if found:
                return found
        return self._describe_call(text, offset)

    def _describe_word(self, text: str, code: str, word: str, start: int) -> str:
        vocab = self.vocab
        owner = re.search(r"([A-Za-z_]\w*)\s*[.:]\s*$", code[:start])
        if owner:
            return self._describe_member(owner.group(1), word)
        if word in vocab.commands:
            command = vocab.commands[word]
            return f"{command.signature()}: {command.doc}"
        if word in vocab.hooks:
            hook = vocab.hooks[word]
            return f"{hook.signature()}: {hook.doc}"
        if word in vocab.enums:
            return f"{word}: {vocab.enums[word].doc}"
        if word in vocab.libraries:
            lib = vocab.libraries[word]
            return f"{word} (import \"{lib.import_name}\"): {lib.summary}"
        if word in vocab.runtime_globals:
            return RUNTIME_DOCS.get(word, "")
        call = enclosing_call(text, start)
        if call and call.table_start is not None and call.name in vocab.commands:
            value = vocab.commands[call.name].field(word)
            if value and re.match(r"\s*=(?!=)", text[start + len(word) :]):
                return self._describe_value(call.name, value)
        return ""

    def _describe_member(self, owner: str, word: str) -> str:
        vocab = self.vocab
        if owner in vocab.enums:
            enum = vocab.enums[owner]
            return f"{owner}.{word} = {enum.entries[word]}" if word in enum.entries else ""
        lib: Library | None = vocab.libraries.get(owner)
        member = lib.member(word) if lib else None
        if member is None:
            return ""
        if member.kind == "function":
            sep = ":" if member.method else "."
            head = f"{owner}{sep}{word}({', '.join(member.params or [])})"
        else:
            head = f"{owner}.{word} = {member.value}"
        return f"{head}: {member.doc}" if member.doc else head

    @staticmethod
    def _describe_value(command: str, value: Value, position: str = "") -> str:
        text = f"{command}.{value.name}{position}: {value.describe()}"
        if value.doc:
            text += f". {value.doc}"
        if value.values:
            text += f" ({len(value.values)} names, e.g. {', '.join(n for n, _ in value.values[:3])})"
        return text

    def _describe_call(self, text: str, offset: int) -> str:
        call = enclosing_call(text, offset)
        if call is None or call.name not in self.vocab.commands:
            return ""
        command = self.vocab.commands[call.name]
        head = f"{command.signature()}: {command.doc}"
        if call.table_start is None and call.argument < len(command.args):
            arg = command.args[call.argument]
            return f"{command.signature()}   {arg.name}: {arg.describe()}. {arg.doc}"
        if call.table_start is not None and call.key:
            value = command.field(call.key)
            if value:
                return self._describe_value(command.name, value)
        return head
