"""Checks of how a script uses the plugin's API: what the compiler cannot see and the plugin finds out only at run time.

Errors are what stops the script or leaves a name undefined; warnings are what the plugin ignores or adjusts (with a
line in its log). The checks work on tokens, not on a syntax tree, and are meant to stay quiet when unsure: a name
that the script declares or assigns anywhere is never reported, whatever the scope.
"""

from __future__ import annotations

import difflib
import math
import re
from collections.abc import Sequence

from lua_editor.api.model import Value
from lua_editor.check.luatokens import Tok, tokenize
from lua_editor.check.problems import Problem
from lua_editor.editor import luastd
from lua_editor.editor.vocabulary import Vocabulary

SOURCE = "api"
ALL_STD = frozenset({"os", "io", "package", "debug", "coroutine", "utf8", "math", "string", "table"})
IMPORT_NAME = re.compile(r"[A-Za-z0-9_-]{1,40}")
HOOK_NAME = re.compile(r"On[A-Z]\w*")
LEGACY = {"unpack": "table.unpack", "math.pow": "the ^ operator", "table.getn": "the # operator"}


def matching(tokens: Sequence[Tok], i: int) -> int | None:
    """The index of the token that closes the bracket at tokens[i]."""
    depth = 0
    for j in range(i, len(tokens)):
        t = tokens[j]
        if t.kind == "op" and t.text in "({[":
            depth += 1
        elif t.kind == "op" and t.text in ")}]":
            depth -= 1
            if depth == 0:
                return j
    return None


def split_arguments(tokens: Sequence[Tok], start: int, stop: int) -> list[tuple[int, int]]:
    """The index ranges [a, b) of the comma separated entries between start and stop."""
    ranges: list[tuple[int, int]] = []
    depth, begin = 0, start
    for j in range(start, stop):
        t = tokens[j]
        if t.kind == "op" and t.text in "({[":
            depth += 1
        elif t.kind == "op" and t.text in ")}]":
            depth -= 1
        elif t.kind == "op" and t.text in ",;" and depth == 0:
            ranges.append((begin, j))
            begin = j + 1
    if begin < stop:
        ranges.append((begin, stop))
    return ranges


def declared_names(tokens: Sequence[Tok]) -> set[str]:
    """Every name the script declares or assigns anywhere: locals, functions, parameters, loop variables, assignments
    and table keys. Where they are visible is not tracked, which can only hide a problem, never invent one."""
    names: set[str] = set()
    n = len(tokens)
    for i, t in enumerate(tokens):
        if t.kind == "name" and t.text == "local":
            j = i + 1
            if j < n and tokens[j].text == "function":
                if j + 1 < n and tokens[j + 1].kind == "name":
                    names.add(tokens[j + 1].text)
                continue
            while j < n and tokens[j].kind == "name":
                names.add(tokens[j].text)
                j += 1
                if j + 2 < n and tokens[j].text == "<" and tokens[j + 2].text == ">":
                    j += 3
                if j < n and tokens[j].text == ",":
                    j += 1
                else:
                    break
        elif t.kind == "name" and t.text == "function":
            j = i + 1
            if j < n and tokens[j].kind == "name":
                if j + 1 < n and tokens[j + 1].text in (".", ":"):
                    k = j + 1
                    method = False
                    while k + 1 < n and tokens[k].text in (".", ":") and tokens[k + 1].kind == "name":
                        method = tokens[k].text == ":"
                        k += 2
                    if method:
                        names.add("self")
                    j = k
                else:
                    names.add(tokens[j].text)
                    j += 1
            if j < n and tokens[j].text == "(":
                close = matching(tokens, j) or n
                names.update(x.text for x in tokens[j + 1 : close] if x.kind == "name")
        elif t.kind == "name" and t.text == "for":
            j = i + 1
            while j < n and tokens[j].text not in ("in", "=", "do"):
                if tokens[j].kind == "name":
                    names.add(tokens[j].text)
                j += 1
        elif t.kind == "op" and t.text == "=":
            j = i - 1
            while j >= 0 and tokens[j].kind == "name" and not (j > 0 and tokens[j - 1].text in (".", ":")):
                names.add(tokens[j].text)
                if j > 0 and tokens[j - 1].text == ",":
                    j -= 2
                else:
                    break
    return names


def suggest(word: str, candidates: Sequence[str]) -> str:
    close = difflib.get_close_matches(word, list(candidates), n=1, cutoff=0.6)
    return f"; did you mean {close[0]}?" if close else ""


class _Checker:
    def __init__(self, text: str, vocab: Vocabulary, script_names: set[str] | None, own: str | None) -> None:
        assert vocab.api is not None
        self.text, self.vocab, self.script_names, self.own = text, vocab, script_names, own
        self.api = vocab.api
        self.tokens = tokenize(text)
        self.problems: list[Problem] = []
        sandbox = self.api.sandbox
        removed = set(sandbox.get("removedGlobals", [])) - set(vocab.runtime_globals)
        self.sandbox_libs = sandbox.get("libraries", [])
        self.unavailable = removed | (ALL_STD - set(self.sandbox_libs)) | {"require"}
        self.imported = vocab.imported_libraries(text)
        self.declared = declared_names(self.tokens)
        self.known = (
            set(luastd.BASE)
            | set(vocab.runtime_globals)
            | set(vocab.std_tables)
            | set(vocab.commands)
            | set(vocab.enums)
            | set(self.imported)
        )
        self.text_limit = self.api.limits.get("textMaxLength", 31)

    # reporting

    def report(self, a: int, b: int, message: str, severity: str) -> None:
        first, last = self.tokens[a], self.tokens[b - 1]
        length = last.end - first.start if last.line == first.line else len(first.text)
        self.problems.append(Problem(first.line, first.column, length, message, severity, SOURCE))

    def error(self, a: int, b: int, message: str) -> None:
        self.report(a, b, message, "error")

    def warn(self, a: int, b: int, message: str) -> None:
        self.report(a, b, message, "warning")

    # the walk

    def run(self) -> list[Problem]:
        tokens = self.tokens
        for i, t in enumerate(tokens):
            if t.kind != "name" or t.text in luastd.KEYWORDS:
                continue
            prev = tokens[i - 1] if i else None
            if prev and prev.kind == "op" and prev.text in (".", ":"):
                continue
            if prev and prev.text == "function":
                self.definition(i, local=i >= 2 and tokens[i - 2].text == "local")
                continue
            self.use(i)
        self.problems.sort(key=lambda p: (p.line, p.column or 0))
        return self.problems

    def use(self, i: int) -> None:
        tokens, vocab, name = self.tokens, self.vocab, self.tokens[i].text
        after = tokens[i + 1].text if i + 1 < len(tokens) else ""
        if name in self.declared:
            return
        if name in self.unavailable:
            libs = ", ".join(self.sandbox_libs)
            self.error(i, i + 1, f"{name} is not available in the {self.api.title} sandbox (it has {libs})")
        elif name in vocab.libraries and name not in self.imported:
            lib = vocab.libraries[name]
            self.error(i, i + 1, f'{name} comes from import "{lib.import_name}", which this script does not do')
        elif name == "import":
            self.check_import(i)
        elif name in vocab.commands and after in ("(", "{"):
            self.check_command(i)
        elif after == "." and i + 2 < len(tokens) and tokens[i + 2].kind == "name":
            if name in vocab.enums or name in self.imported or name in vocab.std_tables:
                self.check_member(i)
        elif after == "(" and name not in self.known:
            hint = f"; use {LEGACY[name]}" if name in LEGACY else suggest(name, sorted(self.known | self.declared))
            self.error(i, i + 1, f"unknown function {name}{hint}")

    # members: OscWaveform.Square, Scales.quantise, math.floor

    def check_member(self, i: int) -> None:
        owner, member = self.tokens[i].text, self.tokens[i + 2].text
        vocab = self.vocab
        if owner in vocab.enums:
            entries = vocab.enums[owner].entries
            if member not in entries:
                self.error(i, i + 3, f"{owner} has no member {member}{suggest(member, list(entries))}")
        elif owner in self.imported:
            lib = vocab.libraries[owner]
            if lib.member(member) is None and not self.assigns(owner, member):
                names = [m.name for m in lib.members]
                self.error(i, i + 3, f"{owner} has no member {member}{suggest(member, names)}")
        elif owner in vocab.std_tables:
            if member in self.api.sandbox.get("removedFields", {}).get(owner, []):
                self.error(i, i + 3, f"{owner}.{member} is removed from the {self.api.title} sandbox")
            elif member not in vocab.std_tables[owner]:
                key = f"{owner}.{member}"
                hint = f"; use {LEGACY[key]}" if key in LEGACY else suggest(member, vocab.std_tables[owner])
                self.error(i, i + 3, f"{key} does not exist{hint}")

    def assigns(self, owner: str, member: str) -> bool:
        """True if the script itself sets owner.member (a library extended in place)."""
        t = self.tokens
        return any(
            t[j].text == owner and t[j + 1].text == "." and t[j + 2].text == member and t[j + 3].text == "="
            for j in range(len(t) - 3)
        )

    # hooks

    def definition(self, i: int, local: bool) -> None:
        name, hooks = self.tokens[i].text, self.vocab.hooks
        if not hooks:
            return
        if name in hooks and local:
            self.warn(i, i + 1, f"{name} is local, so the plugin never calls it; remove local")
        elif name not in hooks and not local and HOOK_NAME.fullmatch(name):
            self.warn(i, i + 1, f"{name} is not a hook of {self.api.title}{suggest(name, list(hooks))}")

    # import "name"

    def check_import(self, i: int) -> None:
        tokens = self.tokens
        j = i + 1
        if j < len(tokens) and tokens[j].text == "(":
            j += 1
        if j >= len(tokens) or tokens[j].kind != "string":
            return
        name = str(tokens[j].value)
        if not IMPORT_NAME.fullmatch(name):
            self.error(j, j + 1, "an import name has 1 to 40 letters, digits, - or _")
            return
        if self.script_names is None or name in self.vocab.noop_imports or name == self.own:
            return
        known = self.script_names | {lib.import_name for lib in self.vocab.libraries.values()}
        if name not in known:
            self.error(j, j + 1, f'import "{name}": there is no {name}.lua here{suggest(name, sorted(known))}')

    # calls of the plugin's commands

    def check_command(self, i: int) -> None:
        tokens = self.tokens
        command = self.vocab.commands[tokens[i].text]
        open_at = i + 1
        close = matching(tokens, open_at)
        if close is None:
            return
        if tokens[open_at].text == "{":
            arguments = [(open_at, close + 1)]
        else:
            arguments = split_arguments(tokens, open_at + 1, close)
        positional, count = len(command.args), len(arguments)
        most = positional + (1 if command.fields else 0)
        if count < positional:
            self.error(i, i + 1, f"{command.name} expects {positional} argument(s) and gets {count}")
        elif count > most:
            self.error(i, i + 1, f"{command.name} takes at most {most} argument(s) and gets {count}")
        for value, (a, b) in zip(command.args, arguments):
            self.check_value(a, b, value, f"{command.name}: argument {value.name}")
        if command.fields and count == positional + 1:
            a, b = arguments[positional]
            if tokens[a].text == "{" and matching(tokens, a) == b - 1:
                self.check_table(command, a, b - 1)
            else:
                found = self.literal(a, b)
                if found is not None and found[0] != "enum":
                    self.error(a, b, f"{command.name}: the last argument must be a table of fields")

    def check_table(self, command, open_at: int, close: int) -> None:
        tokens = self.tokens
        for a, b in split_arguments(tokens, open_at + 1, close):
            if b - a >= 3 and tokens[a].kind == "name" and tokens[a + 1].text == "=":
                key = tokens[a].text
                value = command.field(key)
                if value is None:
                    names = [f.name for f in command.fields]
                    self.warn(a, a + 1, f"{command.name} has no field {key}; the plugin ignores it{suggest(key, names)}")
                else:
                    self.check_value(a + 2, b, value, f"{command.name}.{key}")
            elif not (tokens[a].text == "[" and a + 1 < b):
                self.warn(a, b, f"{command.name}: an entry without a name is ignored")

    def literal(self, a: int, b: int) -> tuple[str, object] | None:
        t = self.tokens
        if b - a >= 2 and t[a].text == "{" and matching(t, a) == b - 1:
            return ("table", None)
        if b - a == 1:
            if t[a].kind == "number":
                return ("number", t[a].value)
            if t[a].kind == "string":
                return ("string", t[a].value)
            if t[a].text in ("true", "false"):
                return ("boolean", t[a].text == "true")
        elif b - a == 2 and t[a].text == "-" and t[a + 1].kind == "number" and t[a + 1].value is not None:
            return ("number", -float(t[a + 1].value))  # type: ignore[arg-type]
        elif b - a == 3 and t[a].text in self.vocab.enums and t[a].text not in self.declared and t[a + 1].text == ".":
            entries = self.vocab.enums[t[a].text].entries
            if t[a + 2].text in entries:
                return ("enum", (t[a].text, entries[t[a + 2].text]))
        return None

    def check_value(self, a: int, b: int, spec: Value, label: str) -> None:
        found = self.literal(a, b)
        if found is None or a >= b:
            return
        kind, value = found
        if spec.kind in ("number", "integer"):
            if kind in ("string", "boolean", "table"):
                self.error(a, b, f"{label} must be a number, not a {'string' if kind == 'string' else kind}")
                return
            if kind == "enum":
                enum, value = value  # type: ignore[misc]
                if spec.enum and enum != spec.enum:
                    self.warn(a, b, f"{label} wants a {spec.enum} constant, this is a {enum} one")
            number = float(value)  # type: ignore[arg-type]
            low, high = spec.min if spec.min is not None else -math.inf, spec.max if spec.max is not None else math.inf
            if spec.kind == "integer" and number != math.floor(number):
                self.warn(a, b, f"{label} = {number:g} is not a whole number; the plugin ignores the call")
            elif number < low or number > high:
                effect = "the plugin ignores the call" if spec.kind == "integer" else "it is limited to that range"
                self.warn(a, b, f"{label} = {number:g} is outside {low:g} to {high:g}; {effect}")
        elif spec.kind == "flag":
            if kind != "boolean":
                self.error(a, b, f"{label} must be true or false")
        elif spec.kind == "text":
            if kind != "string":
                self.error(a, b, f"{label} must be a string")
                return
            text = str(value)
            if len(text.encode("utf-8")) > self.text_limit:
                self.warn(a, b, f"{label} is longer than {self.text_limit} characters; the plugin ignores the call")
            elif spec.values and text not in {name for name, _ in spec.values}:
                names = [name for name, _ in spec.values]
                self.warn(a, b, f"{label}: {text!r} is not a known name; the plugin ignores the call{suggest(text, names)}")


def check_api(
    text: str, vocab: Vocabulary, script_names: set[str] | None = None, own: str | None = None
) -> list[Problem]:
    """The problems of a script that has compiled. script_names: the scripts that import can reach (None: not known
    yet, imports are not checked); own: this script's import name."""
    if vocab.api is None:
        return []
    return _Checker(text, vocab, script_names, own).run()


def check_size(size_bytes: int, vocab: Vocabulary, first_line: str = "") -> list[Problem]:
    limit = vocab.api.limits.get("scriptMaxBytes") if vocab.api else None
    if limit is None or size_bytes <= limit:
        return []
    message = f"the script is {size_bytes / 1024:.0f} KiB; the plugin loads at most {limit / 1024:.0f} KiB"
    return [Problem(1, None, len(first_line), message, "error", SOURCE)]
